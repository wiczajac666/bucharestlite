# TODO — v1.10.0 Feature Push

Working list for the five items agreed after v1.9.15. One item at a time;
each item ends with a green `ctest` run and a commit.

## 1. Linux audio backend  ✅ done (f4c0e75)
- [x] Refactor `IDeviceOutput` into `bl_audio/device_output.hpp`
- [x] Fix planar/interleaved confusion in `AudioEngine` ring buffer
- [x] Move WinMM (waveOut) sink into its own file + interleave correctly
- [x] Add ALSA sink (`libasound.so.2` via `dlopen`, no build dependency)
- [x] Honour `BL_AUDIO_DEVICE` (defaults to `default`) for tests/headless
- [x] Fall back to `NullDeviceOutput` when no backend is usable
- [x] Unit tests: planar ring path, interleave helper, null-device ALSA write
- [x] `ctest --preset linux-release` green + commit

## 2. Audio playback polish  ✅ done
- [x] Replace per-chunk demuxer open/seek in `PreviewPanel` with persistent readers
- [x] Resample any source rate to 48 kHz (linear, preview-grade)
- [x] Downmix/upmix to stereo, apply clip gain and track mute/solo
- [x] Re-seek readers when the playhead jumps (> ~100 ms drift)
- [x] Remove the busy-wait sleep when the device blocks on write
- [x] Unit tests for the reader (seek, resample, channel mapping, EOS)
- [x] `ctest` green + commit

## 3. 3-point editing UI  ✅ done
- [x] Source-monitor panel with mark-in / mark-out
- [x] Insert / overwrite edit from source marks onto the timeline
- [x] Keyboard actions + View menu entries (Ctrl+8, Alt+I/O, `,`/`.`)
- [x] UI tests for the source monitor and insert/overwrite primitives
- [x] `ctest` green + commit

## 4. Masking / roto  ✅ done
- [x] Mask model (polygon + smooth Catmull-Rom curve) + serialization
- [x] Mask rasterisation in the compositor path (feather, invert, opacity)
- [x] Inspector mask editor (add/move/delete points, feather)
- [x] Preview + export both honour masks (shared compositor)
- [x] Tests (model round-trip, rasteriser, compositor/render path)
- [x] `ctest` green + commit

## 5. GPU compositor  ✅ done
- [x] Extract the CPU compositor into an `ICompositorBackend` interface
- [x] Cache processed clip layers and hand the whole stack to one backend call
- [x] OpenGL 3.3 core offscreen backend (Qt context, GLSL ping-pong blend)
- [x] CPU fallback when no GL context is available (headless hosts)
- [x] Keep both selectable: CPU default, `BL_COMPOSITOR=gpu|auto` opts in
- [x] Benchmark against the CPU path; effects/masks stay CPU-side
- [x] Tests (offscreen GL where available, CPU fallback everywhere)
- [x] `ctest` green + commit

## Cross-cutting
- [ ] Update CHANGELOG / README / user-guide per item
- [ ] Bump to 1.10.0 once all five are in
- [ ] Rebuild deb / rpm / Windows installer at the end
