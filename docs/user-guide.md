# Bucharest Lite — User Guide

## Export Workflow

Bucharest Lite uses a composition-based export pipeline where what you see in the preview is what you export. The export pipeline reuses the same `Compositor::renderFrame()` code path as the interactive preview engine.

### Export Dialog

Open the export dialog via `File → Export...` or press `Ctrl+E`. The dialog collects the output target and format options, then renders the whole composition into the chosen file in the background while you keep using the app.

#### Export Settings Fields

| Field | Description |
|---|---|
| **Output Path** | File path for the exported video. Use the `Browse...` button. Supported extensions: `.mp4`, `.webm`. |
| **Container** | `MP4 (.mp4)` or `WebM (.webm)`. MP4 pairs with H.264/MPEG-4; WebM pairs with VP9/AV1/Theora. The dialog keeps container and codec in sync. |
| **Video Codec** | Installed codec plugins: H.264 (libx264), VP9, AV1 (SVT-AV1 or libaom), MPEG-4, Theora. |
| **Quality (CRF)** | Constant-rate factor from 0 to 51, default 23. Lower = higher quality and larger file; 0 uses the encoder's default rate control. |
| **Resolution** | `Full`, `Half`, `Quarter`, or `Custom` width/height. Scaling applies relative to the project sequence size. |

Audio is not exported in this version; the dialog marks this so exported clips stay silent.

### Batch Export

Multiple export jobs can be queued via `File → Batch Export...`. Each job runs with its own output path and format settings; the Render button processes the queue one job at a time and updates each row's status live.

#### Batch Queue Operations

- **Add Job...**: opens the export dialog; the chosen settings become a queued job.
- **Remove**: remove the selected job from the queue (a job currently rendering cannot be removed).
- **Run**: render all pending/failed jobs in order.
- **Cancel** (shown while rendering): stops the current job; the remaining queue is kept for later.
- **Persist**: the queue is saved to disk on every change (`export_queue.json` in the app data directory), so interrupted jobs show up again the next time you open Batch Export.

A job that fails stays in the queue marked `Failed`; fix the underlying problem and press Run again to retry only the unfinished jobs. Closing the dialog does not stop a queued run until the current job's output file is closed.

### Export Progress

During export, a progress window shows:
- Current frame being rendered/encoded
- Estimated time remaining
- Speed (frames/second)
- Cancel button to abort the current export

### Troubleshooting

| Issue | Solution |
|---|---|
| Export fails with "missing codec" | Install the required codec plugin or select a different codec |
| Export fails with "unsupported container" | Use `.mp4` or `.webm`; other containers are not built in this version |
| Green frames or color shift | Ensure project color space matches the export target (BT.709 vs BT.2020) |
| Very large output files | Lower the quality (increase CRF) or select a lower resolution |

### Export Limitations (v1)

- The whole timeline is exported; work-range and per-clip ranges are not yet available in the UI
- Audio tracks are not exported yet (no mixdown pipeline)
- Containers limited to MP4 and WebM — Matroska is deferred because the codec plugin buffers carry no SPS/PPS extradata the mkv muxer requires
- Nested compositions are not supported — flat sequences only
- Multi-cam source clips are not supported for export
- Hardware acceleration (NVDEC/VAAPI/VideoToolbox) is not available in v1