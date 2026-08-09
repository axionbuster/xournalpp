# Video recording and the projector window

This fork turns the audio-only recorder into a video recorder, and adds a projector window: a
second window showing the current page on its own, with no toolbars around it, meant to be put on
another display or watched while recording.

## What is recorded

The canvas, and only the canvas. Each frame is drawn from the document model at the output
resolution and handed to ffmpeg over a pipe. Nothing is captured off the screen, and that one
decision settles most of the questions people ask about this feature:

- **The picture cannot contain anything but the page.** No toolbars, no scrollbars, no menu bar, no
  notification that arrived mid-lecture, no second monitor.
- **It is sharp at the output resolution** regardless of the window size, the zoom level, or whether
  the display is HiDPI. Zooming in while recording changes nothing in the file.
- **No screen-recording permission is involved**, on any platform. The microphone is the only thing
  the operating system is asked about.

The page is centred and letterboxed on the projector's background colour, so the projector window
is an exact preview of the recording -- apart from the caption guide, which is drawn only there.

## How the two streams get into one file

One ffmpeg process, two pipes.

Video goes to its standard input as raw BGRA -- what a Cairo `ARGB32` surface already is in memory,
so no conversion happens on our side. A writer thread emits frames on its own clock, at whatever
rate the configured frame rate asks for, repeating the last frame when nothing has changed. It
emits exactly as many frames as wall-clock time says it should: raw video carries no timestamps, so
the length of the video track is purely a matter of how many frames were sent, and getting that
count right is what keeps the picture level with the sound over a long recording.

Sound comes from the same PortAudio capture the audio recorder has always used, written as 32-bit
floats to a second pipe on descriptor 3. The device, sample rate and gain are the ones under
**Preferences → Audio Recording**; there is no second device list.

Frames are drawn on a timer, unconditionally, rather than when something signals that the canvas
changed. Redrawing only on a signal is tempting -- most of a lecture is a still page -- but a
change can reach the page by routes that do not send one (an undo, a background change, a layer
being hidden), and a recording that silently stops following the page is worse than one that costs
a few percent of a core. Drawing a frame measures around 1.5 ms for a plain page at 1080p.

Both pipes are handed to ffmpeg by GLib's own descriptor mapping, not by a child-setup function
with `G_SPAWN_LEAVE_DESCRIPTORS_OPEN`. That flag also leaves the *writing* ends open inside the
child, so closing our copies at the end of a recording never produces an end of file and ffmpeg
waits forever for input that cannot arrive.

The defaults mirror an OBS "simple output" profile: 1920×1080 at 60 fps, 6000 kbit/s video,
160 kbit/s AAC, hardware H.264 (`h264_videotoolbox` on Apple silicon), in a fragmented `.mov` --
fragmented so a recording lost to a crash or a flat battery still plays up to the point it stopped.

The exact command line is shown, live, in **Preferences → Video Recording**, built from the values
currently in the dialog rather than the saved ones. `VideoRecorderConfig` exists for that reason: it
is the whole input to `buildCommandLine`, and it can be filled either from `Settings` or from
unsaved widgets.

## macOS: the microphone key is load-bearing

macOS attributes a child process's privacy requests to the application bundle that spawned it. When
a process asks for the microphone and the responsible bundle's `Info.plist` carries no usage
description, macOS does not decline the request -- **it aborts the process**. ffmpeg used to die on
`SIGABRT` the instant it opened an audio device, having written nothing to stderr to say why.

`mac-setup/Info.plist` therefore has to carry `NSMicrophoneUsageDescription`. It is not optional and
its absence does not degrade gracefully. Note that testing from a terminal cannot catch this: the
terminal is then the responsible process, and it has its own microphone permission.

## The separate audio file

Xournal++'s older feature -- replaying the audio that was being recorded while a given stroke was
drawn -- needs a sound file of its own for strokes to point at. That is a second file beside every
recording, for something you may not be using, so it is **off by default** and lives behind "Also
write a separate audio file" in the preferences.

## The caption safe area

Burnt-in subtitles are added downstream and cover the bottom of the finished frame. The projector
shades that strip so nothing worth reading gets written into it.

The height is configured in **lines of the finished video**, not as a percentage and not in window
pixels, because that is how a subtitling requirement is normally written down -- "keep the bottom
150 px clear" at 1080p. The projector converts it to a fraction of the recording's frame height and
applies that to the page rectangle, so the guide means the same thing whatever size the window
happens to be.

The shading is drawn by `ProjectorWindow` and by nothing else. It cannot reach the recording, which
is drawn separately and never sees anything the projector does.

## Where things live

| File | Role |
| --- | --- |
| `src/core/control/VideoRecorder.{h,cpp}` | ffmpeg process, command line, frame pump |
| `src/core/audio/PipedAudioSource.{h,cpp}` | the microphone, as raw samples on a pipe |
| `src/core/gui/CanvasFrame.{h,cpp}` | the one function that draws "the page, alone" |
| `src/core/gui/ProjectorWindow.{h,cpp}` | the projector window and the caption guide |
| `src/core/gui/dialog/RecordingSettingsPanel.{h,cpp}` | the preferences page |
| `src/core/control/Control.cpp` | `startRecording` / `stopRecording`, projector lifetime |

Closing the projector never touches a recording in progress, and reopening it is always safe: the
recording is drawn from the document, not from that window.
