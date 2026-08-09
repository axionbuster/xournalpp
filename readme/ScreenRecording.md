# Screen recording and the projector window

This fork replaces the audio-only recorder with an audio **and** video capture, and adds a
projector window: a second window showing the current page on its own, with no toolbars around
it, meant to be put on another display or pointed at by a capture.

## How the capture is made

Nothing is encoded in-process. `ScreenRecorder` spawns `ffmpeg` as a child and drives it over a
pipe:

- **macOS** — one `avfoundation` input carrying both picture and sound. A single input is what
  keeps them in step: AVFoundation timestamps both off the same clock, whereas two inputs drift
  apart by however long the screen grabber took to start.
- **Windows** — `gdigrab` for the desktop, `dshow` for the microphone.
- **Linux** — `x11grab` for the display, `pulse` for the microphone.

Stopping writes `q` to ffmpeg's stdin — its own request to finish cleanly, flush the encoders and
write the container index — and escalates to `SIGINT` and then `SIGKILL` only if that is ignored.
`stop()` blocks until the child is gone, deliberately: the file is not playable until ffmpeg has
finished writing, and returning earlier would leave the user looking at a corrupt recording.

The defaults mirror an OBS "simple output" profile: 1920×1080 at 60 fps, 6000 kbit/s video,
160 kbit/s AAC at 48 kHz, hardware H.264 (`h264_videotoolbox` on Apple silicon), in a fragmented
`.mov`. Fragmented so a recording lost to a crash or a flat battery still plays up to the point
it stopped.

The exact command line is shown, live, in **Preferences → Screen Recording**, built from the
values currently in the dialog rather than the saved ones. `ScreenRecorderConfig` exists for that
reason: it is the whole input to `buildCommandLine`, and it can be filled either from `Settings`
or from unsaved widgets.

## macOS: the privacy keys are load-bearing

macOS attributes a child process's privacy requests to the application bundle that spawned it.
When a process asks for the microphone or the camera and the responsible bundle's `Info.plist`
carries no matching usage description, macOS does not decline the request — **it aborts the
process**. ffmpeg dies on `SIGABRT` the instant it opens the audio device, having written nothing
to stderr to say why, so the last thing in the log is whatever harmless warning preceded it.

`mac-setup/Info.plist` therefore has to carry `NSMicrophoneUsageDescription` and
`NSCameraUsageDescription`. They are not optional and their absence does not degrade gracefully.

Screen capture itself is granted separately, through **System Settings → Privacy & Security →
Screen & System Audio Recording**, and is keyed to the bundle's code signature — so a rebuilt,
ad-hoc-signed bundle may have to be permitted again.

`ScreenRecorder::onChildExited` recognises a signalled death and says so, because the stderr tail
cannot.

## The caption safe area

Burnt-in subtitles are added downstream, and they cover the bottom of the finished frame. The
projector shades that strip so nothing worth reading gets written into it.

The height is configured in **lines of the finished video**, not as a percentage and not in
window pixels, because that is how a subtitling requirement is normally written down — "keep the
bottom 150 px clear" at 1080p. The projector converts it to a fraction of the recording's frame
height and applies that to the page rectangle, so the guide means the same thing whatever size
the window happens to be. The default, 150 lines of 1080, comes from that convention.

The shading is drawn by `ProjectorWindow` and by nothing else. It is not in the recording: the
encoder is fed the screen grabber's own frames, which never see anything the projector draws. The
one way it can reach a recording is the obvious one — a capture wide enough to include the
projector window itself.

## Where things live

| File | Role |
| --- | --- |
| `src/core/control/ScreenRecorder.{h,cpp}` | ffmpeg process, command line, device enumeration |
| `src/core/gui/ProjectorWindow.{h,cpp}` | the projector window and its painting |
| `src/core/gui/dialog/RecordingSettingsPanel.{h,cpp}` | the preferences page |
| `src/core/control/Control.cpp` | `startRecording` / `stopRecording`, projector lifetime |
| `src/core/gui/RepaintHandler.cpp` | forwards main-view repaints to the projector |

Closing the projector never touches a recording in progress, and reopening it is always safe: the
capture is a separate process that does not know the window exists.
