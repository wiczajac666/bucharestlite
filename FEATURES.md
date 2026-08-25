# Bucharest Lite — Open-Source NLE
## Feature Research & Specification

### Open-Source NLE Landscape — Feature Research

### Editors Analyzed

| Editor | Language/Framework | License |
|---|---|---|
| **Kdenlive** | Qt + MLT framework | GPL |
| **Shotcut** | Qt + FFmpeg | GPL |
| **OpenShot** | Python + PyQt + libopenshot | GPL |
| **Pitivi** | GTK + GStreamer | LGPL |
| **Cinelerra-GG** | C++ native | GPL |
| **DaVinci Resolve** | C++ (closed-source, free tier) | Proprietary |
| **OpenReel** | Web-based (browser) | MIT |

### Complete Feature List (Aggregated)

**Core Editing:**
- Multi-track timeline (unlimited video + audio tracks)
- Drag-and-drop media import
- Timeline trimming, splitting, ripple editing
- 3-point editing (in/out points)
- Snapping / magnetic alignment
- Zoom & pan timeline
- Frame-accurate editing (step through frames)
- Speed/time remapping (slow-mo, fast-forward, reverse)
- Keyframeable clip properties (scale, rotation, position, opacity)
- Nested compositions / subclips
- Proxy editing (low-res workflow for high-res media)
- Project auto-save / backup

**Transitions & Effects:**
- Video transitions (fade, wipe, dissolve, slide, zoom, etc.)
- Real-time effect previews
- Keyframeable effects (color, blur, sharpen, distortion)
- Video filters (brightness, contrast, gamma, hue, saturation, greyscale, chroma key)
- Audio filters (EQ, compression, reverb, noise reduction, volume)
- Built-in effect libraries (frei0r, FFmpeg filters, LADSPA)

**Compositing:**
- Layer-based compositing (alpha blending)
- Chroma key (green/blue screen)
- Picture-in-picture
- Masking / roto-scoping
- Overlay / watermark support

**Titles & Text:**
- Built-in title editor (fonts, colors, alignment, spacing)
- Title templates (scrolling credits, lower thirds, callouts)
- SVG vector titles
- Animated titles (scroll, typewriter, 3D via Blender)
- Subtitle support (SRT, ASS, WebVTT)
- Auto-subtitle generation (Whisper/VOSK)

**Audio:**
- Multi-track audio mixing
- Waveform display
- Audio/video clip splitting
- Per-channel audio control
- Audio level meters / scopes
- Audio syncing tools

**Color Grading:**
- Color correction (brightness, contrast, saturation, gamma)
- Scopes: histogram, waveform, vectorscope, RGB parade
- Color wheels / curves
- LUT support (3D LUT import)
- Advanced: node-based grading (Cinelerra-GG)

**Media & Codec Support:**
- FFmpeg-based format support (hundreds of codecs)
- Image sequence import
- Audio format support (MP3, WAV, AAC, FLAC, OGG, etc.)
- Batch encoding / transcoding
- Proxy generation
- Format detection and metadata display

**Export & Rendering:**
- Custom export profiles
- Batch rendering
- Render presets (YouTube, Vimeo, DVD, Blu-ray, mobile)
- Quality measurement (PSNR, SSIM)
- Stream / encode to IP

**UI & Workflow:**
- Customizable interface / layouts
- Keyboard shortcuts (remappable)
- Multi-monitor support
- Dark/light themes
- Undo/redo (unlimited)
- Search / filter media bins
- Collections / bins / folders
- Project templates

**Advanced:**
- Multi-cam editing
- Motion tracking (Cinelerra-GG)
- Video stabilization (Cinelerra-GG)
- 8K timeline support (Cinelerra-GG)
- Online resource integration (Pexels, Pixabay, Freesound)
- Plugin architecture (frei0r, OpenFX, LADSPA)
- Portable / standalone builds

### Modular Codec Plugin List

**Video codecs:**
- H.264 (AVC)
- VP9
- AV1
- Theora
- MPEG-4 Part 2

**Audio codecs:**
- AAC
- FLAC
- Vorbis
- Opus

Each codec is a loadable plugin — the core ships with a codec registry and plugin loader. New codecs are added by dropping in a plugin binary + metadata.

---

### Essential Features for Bucharest Lite

These are the **must-have** features — the baseline for any functional NLE:

1. **Multi-track timeline** (unlimited video + audio tracks with snap/align)
2. **Timeline editing** — trim, split, ripple, drag-and-drop, zoom/pan
3. **3-point editing** (in/out point workflow)
4. **Frame-accurate playback** (step through frames)
5. **Speed/time remapping** (slow-mo, fast, reverse)
6. **Keyframeable clip properties** (scale, rotate, position, opacity, volume)
7. **Transitions** — crossfade, wipe, dissolve (with adjustable duration)
8. **Video effects** — color correction, brightness/contrast/gamma/hue, blur/sharpen
9. **Chroma key** — green/blue screen compositing
10. **Layer-based compositing** — alpha blending, overlays, watermarks
11. **Built-in title editor** — text with fonts, colors, basic animation
12. **Subtitle support** — SRT/ASS import, burn-in
13. **Multi-track audio** — waveform display, mixing, per-clip volume
14. **Scopes** — audio meters, histogram, waveform
15. **Modular codec support** — plugin-based encoder/decoder loading
16. **Proxy editing** — auto-generated low-res for smooth editing
17. **Project auto-save** — crash recovery
18. **Unlimited undo/redo**
19. **Custom export** — format selection, quality presets, batch render
20. **Cross-platform** — Linux, Windows, macOS
