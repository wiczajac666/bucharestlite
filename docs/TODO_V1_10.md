# TODO — v1.10.0 Feature Push

Working list for the five items agreed after v1.9.15. One item at a time;
each item ends with a green `ctest` run and a commit.

## 1. Linux audio backend  ← in progress
- [ ] Refactor `IDeviceOutput` into `bl_audio/device_output.hpp`
- [ ] Fix planar/interleaved confusion in `AudioEngine` ring buffer
- [ ] Move WinMM (waveOut) sink into its own file + interleave correctly
- [ ] Add ALSA sink (`libasound.so.2` via `dlopen`, no build dependency)
- [ ] Honour `BL_AUDIO_DEVICE` (defaults to `default`) for tests/headless
- [ ] Fall back to `NullDeviceOutput` when no backend is usable
- [ ] Unit tests: planar ring path, interleave helper, null-device ALSA write
- [ ] `ctest --preset linux-release` green + commit

## 2. Audio playback polish
- [ ] Replace per-chunk demuxer open/seek in `PreviewPanel` with persistent readers
- [ ] Resample any source rate to 48 kHz (linear, preview-grade)
- [ ] Downmix/upmix to stereo, apply clip gain and track mute/solo
- [ ] Re-seek readers when the playhead jumps (> ~100 ms drift)
- [ ] Remove the busy-wait sleep when the device blocks on write
- [ ] Unit tests for the reader (seek, resample, channel mapping) + UI smoke test
- [ ] `ctest` green + commit

## 3. 3-point editing UI
- [ ] Source-monitor panel with mark-in / mark-out
- [ ] Insert / overwrite edit from source marks onto the timeline
- [ ] Keyboard actions + View menu entries
- [ ] Model-level tests already exist: extend with UI-level coverage
- [ ] `ctest` green + commit

## 4. Masking / roto
- [ ] Mask model (polygon/bezier) + serialization
- [ ] Mask rasterisation in the compositor path
- [ ] Inspector mask editor (add/move/delete points, feather)
- [ ] Preview + export both honour masks
- [ ] Tests (model round-trip, rasteriser, export smoke)
- [ ] `ctest` green + commit

## 5. GPU compositor
- [ ] Decide backend (OpenGL 3.3 core, Qt `QOpenGLWidget` surface)
- [ ] Extract the CPU compositor into a backend interface
- [ ] Implement shader-based layer blend + effects subset
- [ ] CPU fallback when no GL context is available
- [ ] Benchmark against CPU path; keep both selectable
- [ ] Tests (offscreen GL where available, CPU fallback everywhere)
- [ ] `ctest` green + commit

## Cross-cutting
- [ ] Update CHANGELOG / README / user-guide per item
- [ ] Bump to 1.10.0 once all five are in
- [ ] Rebuild deb / rpm / Windows installer at the end
