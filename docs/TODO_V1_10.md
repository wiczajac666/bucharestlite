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

## 3. 3-point editing UI
- [ ] Source-monitor panel with mark-in / mark-out
- [ ] Insert / overwrite edit from source marks onto the timeline
- [ ] Keyboard actions + View menu entries
- [ ] Model-level tests already exist: extend with UI-level coverage
- [x] `ctest` green + commit

## 4. Masking / roto
- [ ] Mask model (polygon/bezier) + serialization
- [ ] Mask rasterisation in the compositor path
- [ ] Inspector mask editor (add/move/delete points, feather)
- [ ] Preview + export both honour masks
- [ ] Tests (model round-trip, rasteriser, export smoke)
- [x] `ctest` green + commit

## 5. GPU compositor
- [ ] Decide backend (OpenGL 3.3 core, Qt `QOpenGLWidget` surface)
- [ ] Extract the CPU compositor into a backend interface
- [ ] Implement shader-based layer blend + effects subset
- [ ] CPU fallback when no GL context is available
- [ ] Benchmark against CPU path; keep both selectable
- [ ] Tests (offscreen GL where available, CPU fallback everywhere)
- [x] `ctest` green + commit

## Cross-cutting
- [ ] Update CHANGELOG / README / user-guide per item
- [ ] Bump to 1.10.0 once all five are in
- [ ] Rebuild deb / rpm / Windows installer at the end
