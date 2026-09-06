# Bucharest Lite — Developer Guide: Codec Plugins

This guide covers the Phase 1 codec plugin layer (PLG-1..PLG-9): the loadable
plugin ABI, how the FFmpeg-backed plugins are structured, the audio encode
constraints that trip people up, and how the plugin test harness works.

## Overview

A codec plugin is a `dlopen`-loadable shared module that exports a single
entry point `bl_get_codec_plugin()` returning a `BlCodecPlugin*`
(`include/bl_plugins/codec_plugin.h`, ABI v2). Bucharest Lite ships nine
FFmpeg-backed codecs as modules:

| Plugin | Codec | encoder | decoder | module output |
|--------|-------|---------|---------|---------------|
| PLG-1 | h264 | libx264 | h264 | `plugins/video/blh264.so` |
| PLG-2 | vp9 | libvpx-vp9 | vp9 | `plugins/video/blvp9.so` |
| PLG-3 | av1 | libsvtav1 | libdav1d | `plugins/video/blav1.so` |
| PLG-4 | theora | libtheora | theora | `plugins/video/bltheora.so` |
| PLG-5 | mpeg4 | mpeg4 | mpeg4 | `plugins/video/blmpeg4.so` |
| PLG-6 | aac | aac | aac | `plugins/audio/blaac.so` |
| PLG-7 | flac | flac | flac | `plugins/audio/blflac.so` |
| PLG-8 | vorbis | libvorbis | vorbis | `plugins/audio/blvorbis.so` |
| PLG-9 | opus | libopus | opus | `plugins/audio/blopus.so` |

## ABI contract (what every plugin must honor)

- **Video interchange format**: BGRA32 packed (bottom-up not assumed; the
  wrapper converts). Audio interchange: float32 planar (each channel is a
  plane of `sample_count` floats).
- **Ownership**: decoded/encoded buffers are produced through
  `cfg->host->alloc(size, userdata)` and freed by the host via
  `cfg->host->free(ptr, userdata)`. The plugin never frees host memory itself.
- **flush()** carries no metadata: trailing decoder flush frames and encoder
  EOF frames are count-only (no geometry/keyframe guarantees).
- **decode()** is one-packet-per-call. A server is not allowed to return
  `BL_DECODE_NEED_MORE_INPUT` to a DecoderBridge caller; use
  `thread_count = 1` and hand packets one at a time.
- **Encoder availability** is reported at `init()`; if the encoder cannot be
  opened the plugin still returns `BL_OK` from `init()` and `encode()` fails
  with `BL_ERR_ENCODE_FAILED` so callers can fall back.

## FFmpeg wrapper (`src/plugins/ffmpeg/ffmpeg_plugin_common.c`)

All nine plugins are thin descriptors on top of one shared static library.
Each descriptor sets a `BlFfmpegProfile`:

```c
static const BlFfmpegProfile kProfile = {
    /*decode_id*/ AV_CODEC_ID_H264,
    /*decode_name*/ "h264",
    /*encode_id*/ AV_CODEC_ID_H264,
    /*encode_name*/ "libx264",
    /*encode_fallback_name*/ NULL,
    /*enc_pix_fmt*/ AV_PIX_FMT_YUV420P,      /* video */
    /*enc_sample_fmt*/ AV_SAMPLE_FMT_S16,     /* audio */
    /*encode_options*/ kOptions,
    /*caps_flags*/ 0u,                        /* or BL_FLAG_LOSSLESS */
    /*file_extensions*/ kExtensions,
    /*is_video*/ 1,
};
```

The wrapper:

1. Opens the decoder (`decode_name`) and encoder (`encode_name`, falling
   back to `encode_fallback_name`) against the `decode_id`/`encode_id`.
2. Open is **best-effort**: codecs that need container extradata
   (theora/vorbis/opus with raw packets) fail `avcodec_open2` at `init()`;
   the wrapper tolerates that, leaving the decoder NULL until extradata is
   attached via `cfg->extradata`/`cfg->extradata_size`. `decode()` returns
   `BL_ERR_DECODE_FAILED` until then.
3. Decode: per-call frame → swscale to BGRA32 (video) or swresample to FLTP
   (audio) → `host->alloc`.
4. Encode: converts the interchange frame to the encoder's native format and
   forwards to `avcodec_send_frame`.

### The audio encode feed (read this)

`feed_one_audio_slice()` + `push_pending_samples()` implement a pending
sample batch. Incoming audio is appended to a planar FIFO and consumed in
**full `frame_size` granules** only. Why:

- `libvorbis` rejects any frame larger than its `frame_size` (64 at 48 kHz);
  the generic `avcodec_send_frame` check fails such frames outright.
- `libopus` **rejects mixed-size frames**: it internally rewrites the audio
  from `frame->data[0]` into a reusable sample buffer and produces a packet
  per `send_frame`; sending 960 then 64 then 960 triggers `EINVAL` on the
  second 960.
- Sending too much input in one burst makes `avcodec_send_frame` return
  `EAGAIN` when the encoder's internal buffering fills up.

Therefore `encode()` never hard-fails on `EAGAIN`: it stops, keeps the
remainder pending, and the next `encode()` (or `flush()`) continues. At
`flush()`, the sub-`frame_size` remainder is pushed as the **final, small
frame** before the EOF marker (`avcodec_send_frame(NULL)`).

In practice for the shipped plugins:

- aac/flac/vorbis: each 1024-sample chunk maps to exact granules, no
  remainder.
- opus (frame_size 960): a 1024-sample chunk sends one full granule and the
  64-sample remainder stays pending until the next call; the flush tail is
  the final small frame.

### Per-codec notes

- `av1`: the native `av1` decoder in some FFmpeg builds (e.g. Debian) is
  hardware-gated ("Your platform doesn't support hardware accelerated AV1
  decoding"); `av1_plugin.c` uses `libdav1d` for decode. `libsvtav1` also
  requires both dimensions >= 64, so round-trip tests use 128x64.
- `opus`: encoder sample format must be `AV_SAMPLE_FMT_S16` or
  `AV_SAMPLE_FMT_FLT` (FLTP is rejected); set a `frame_size` option (960 =
  20 ms @ 48 kHz) so granule accounting is deterministic.
- `flac`: declared `BL_FLAG_LOSSLESS`; a `frame_size` option pins framing so
  input decodes back to the exact s16 quantization of the source.
- `vp9`: `cpu-used` is clamped to the valid 0..9 range.

## Building and staging

CMake (`src/plugins/CMakeLists.txt`) builds every module with
`POSITION_INDEPENDENT_CODE` (the shared `bl_ffmpeg_plugin_common` static lib
needs PIC to link into MODULEs) and writes them to:

```
<build>/plugins/video/bl<name>.so
<build>/plugins/audio/bl<name>.so
```

Tests re-stage those into `<build>/tests/unit/plugins/…` via the
`bl_core_tests_plugins` ALL target. That copy is stamp-ordered against the
real MODULE targets (`h264 vp9 av1 … opus`) — depend on a custom aggregate
target instead and stale binaries will silently leak into the test dir.

## Testing

`tests/unit/test_codec_plugins.cpp` exercises every plugin through the real
dlopen ABI:

- `CodecPlugins/VideoRoundTripTest` — h264/vp9/av1/theora/mpeg4 raw encode ->
  decode geometry checks.
- `CodecPlugins/AudioRoundTripTest` — aac sample-count round trip.
- `CodecPlugins/AudioLosslessRoundTripTest` — flac decodes to the exact s16
  quantization of the input (eps = 1/32767 + 1e-5 per sample).
- `CodecPlugins/ExtradataCodecTest` — theora/vorbis/opus decoded from
  container fixtures (`tests/media/test_video_theora.ogv`,
  `test_audio_vorbis.ogg`, `test_audio_opus.opus`); extradata and params come
  from a libavformat probe (`probeContainer()`), packets via `Demuxer`.
- `CodecPluginTest.StandaloneEncodersProducePayload` — encode-only
  emissions for theora/vorbis/opus.
- `CodecPluginTest.*` — plugin registry loading, Annex-B fixture through
  DecoderBridge.

Run the sweep the same way the project gates CI:

```
cmake --preset linux-debug   && cmake --build --preset linux-debug   && ctest --preset linux-debug
cmake --preset linux-release && cmake --build --preset linux-release && ctest --preset linux-release
cmake --preset linux-tsan    && cmake --build --preset linux-tsan    && ctest --preset linux-tsan
```