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

## Where the pen is pointing

Drawing the frame from the document has one cost: the cursor is on the desktop, and the desktop is
not in the picture. Neither the system pointer nor the pen cursor Xournal++ hands to GTK can reach
the file. Left at that, a viewer sees ink appear with no idea where the pen was between strokes,
and pointing at something already written -- half of what happens in a lecture -- shows nothing at
all.

So the frame draws its own marker at the pen: a ring in the current pen's color, with a darker edge
beneath it so a light pen stays visible over a light page, and a dot at the center at the width the
pen would actually draw. A ring rather than a filled disc, because it has to be findable over a
page already covered in ink of that same color without hiding the thing it is pointing at. The
eraser gets a neutral gray, having no color of its own.

It is on by default and turned off under **Preferences > Recording**. Its size is given in pixels
of the finished video, the same way the caption safe area is, and converted to a fraction of the
page -- so 24 px means 24 px whatever resolution is recorded and whatever size the projector window
happens to be.

Where the pen is comes from `InputContext`, which every pen, eraser and mouse event passes through
on its way to a handler. Three details are worth knowing:

- **Touch does not count as pointing.** A finger scrolling the page is not indicating anything, and
  a marker that jumped to every scroll gesture would be noise.
- **The position is kept in widget coordinates**, not page ones, and converted against the scroll
  position of the moment. Scrolling under a pen that has not moved therefore reports the new part
  of the page it now sits over, which is what actually happened.
- **It is forgotten on leave and on the stylus leaving range.** A marker frozen where the pen was
  ten minutes ago is worse than no marker, and reaching for a coffee should not leave one behind.

Unlike the caption guide, this is drawn into the recording -- and, being part of the shared frame,
into the projector as well. That is deliberate: the projector is how you check what is being
recorded, so it has to show what the recording shows. It does mean the projector has to repaint
faster than a page nobody is touching needs. `XournalView` tells it the pointer moved, once per
motion event; the projector's own clock still decides when to repaint, so the rate stays capped
however fast the tablet reports.

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

## What happens to the microphone

A bare microphone into a recording sounds like a bare microphone. Three stages sit between the
capture and the encoder, the same three a streaming setup always puts there, in the same order:

| Stage | Default | What it is for |
| --- | --- | --- |
| Compressor | 20:1 above −18 dB, 6 ms attack, 60 ms release, +6 dB out | Holds a voice at one level for an hour without anyone riding a fader |
| Equalizer | −0.6 dB mid, +3.6 dB high | Out of the muddy middle, up where consonants live |
| Noise suppression | RNNoise | Fan, hum, keyboard, room |

They are named and scaled the way OBS names and scales them -- dB for levels and gains,
milliseconds for times, a plain number for the ratio -- so a value copied from one to the other
means the same thing in both. ffmpeg applies them, inside the process that is already encoding, so
they cost nothing worth measuring next to the video.

Only the recording is processed. The separate `.ogg`, if you write one, keeps the untouched
capture, so a stroke played back years from now still sounds like the room did.

Two details are worth knowing before changing anything:

- **The equalizer's bands meet at 880 Hz and 5 kHz**, and those crossovers are not configurable --
  a band gain means nothing to anyone unless the band is the one they are used to. The low and high
  controls are shelves and the middle one is a broad peak spanning what is left; that is not
  identical to a three-band equalizer built from one crossover pair, but it is the same three
  controls doing the same three things to the same three parts of the spectrum.
- **RNNoise needs a trained model, and cannot work without one.** `sh.rnnn` ships in the
  application's resources and is found automatically; `micRnnoiseModel` in `settings.xml` points at
  a different one. With no model at all the chain drops to the spectral denoiser rather than
  recording with no suppression, and the preferences page says so rather than leaving it to be
  discovered afterwards.

Stages that would do nothing are left out of the command line rather than written as no-ops, so
the command shown in the preferences says what is actually being done to the sound. RNNoise runs at
48 kHz, so ffmpeg resamples on the way in and the finished audio track is 48 kHz whatever the
capture rate was.

## What a frame costs

A frame is emitted sixty times a second, and drawing a page of real lecture notes at 1080p takes
**37 ms** -- more than twice a whole core's worth of work per second of recording, taken from the
UI thread, which is the thread collecting the pen input. Left that way the application crawls
exactly while someone is writing, and the recording looks fine afterwards, because the frames were
all produced: the cost lands on the person at the keyboard, not on the file.

So a page is drawn once and the picture kept. The kept picture holds the page's settled content;
ink still under the pen is an overlay and is drawn afresh on every frame, which is what makes
writing appear as it is written. `Control::getCanvasRevision()` says when the settled content has
moved -- a stroke finished, an undo, a background swapped, a layer hidden. On the same page a
frame falls to **0.55 ms**, which is 3% of a core rather than 226%.

Renewing the picture is itself kept off the UI thread, because the renewals were the last thing
still stalling it: a page re-render at every stroke's end, plus one per consumer every quarter
second, each worth tens of milliseconds, felt exactly like intermittent lag -- and twice as much
of it with the projector open, which is how it was first noticed. Three routes now renew the
picture, in order of cheapness:

- **A finished stroke is drawn straight into the kept picture**, the same way the main view draws
  it into its own buffer, before the overlay it came from is deleted. That order is what keeps the
  newest stroke from flickering out of the projector and the recording for even a frame. It costs
  one stroke.
- **Everything else goes through a scheduler worker** -- the same pool the main view renders on.
  An undo, an eraser pass, a background change bump the revision; the next frame notices, kicks a
  background render, and keeps blitting the old picture until the new one lands a few frames
  later. A wrong-for-40-ms picture is invisible; a 40 ms stall under the pen is not.
- **Only a page flip or a resize renders synchronously**, because those need a genuinely different
  picture, and holding the previous page on show would be a lie the projector's audience sees.

Any such scheme has to answer what happens to a change nobody reports, because a recording that
silently stops following the page is a worse failure than a slow one. Every route a change is
known to take says so, including the ones that reach the page without ever passing through
`RepaintHandler` -- an undo takes exactly that route, which is why the projector used to show one
only after the next stroke shook it loose. And whatever anybody reports, a picture more than a
quarter of a second old is renewed regardless -- in the background, so the safety net costs
nothing on the thread that matters.

The recorder also skips frames that cannot differ from the last one: same kept picture, same page,
no overlays on either side means the pixels are identical, so nothing is packed and the writer
just re-sends its copy on schedule, which is what it does between frames anyway. Most of a lecture
is a still page being talked about, and a still page now costs approximately nothing.

The projector repaints on a clock of its own for the same reason, at thirty frames a second rather
than once per motion event: a tablet sends motion far faster than anyone can see, and each one used
to mean another full page.

## The scrollbar during a lecture

Presentation mode now shows no scrollbars at all. Free scrolling is already suppressed there --
changing pages is the only way the view moves -- so a scrollbar cannot do its job in presentation
mode; what it turned out to do instead is blink. GTK fades its overlay scrollbar indicator in on
**every pointer motion over the window** -- there is no proximity test, and pens are not excluded
-- and the timer that hides it again runs out mid-stroke during sustained writing, so the
indicator flickered in and out over the page the whole time the pen was down.

Hiding the scrollbar widgets is not enough on its own, because the indicator is separate machinery
that runs whether or not the bars are visible. What silences it is turning **overlay scrolling**
off. Setting the scrollbar *policy* to NEVER also silences it and must not be used: a
`GtkScrolledWindow` with policy NEVER stops clipping and hands its child the child's full natural
height, which here meant a 2780-pixel canvas inside a 948-pixel window -- the page pinned to the
top, the rest of the window empty, and nothing scrollable. Outside presentation mode the
scrollbars behave exactly as configured in the preferences.

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

## Where the projector opens

Small, and then wherever you last left it. It starts as a 480×270 corner tile -- 16:9, so it
matches the recording it previews from the first frame -- and from then on its own remembered size
and position win. Position and size are written down whenever it is closed, including when the
application quits with it still open.

Both are stored relative to the origin of a monitor identified by *description* rather than by
index, exactly as the main window is: indices are reassigned whenever a display is plugged in, so
an index restores the projector onto the wrong panel precisely when a second display is involved.
If the remembered monitor is not connected, the projector opens at the default placement rather
than off-screen, and a remembered position is clamped onto the work area so a window saved from a
larger display still comes back reachable by its title bar.

On macOS the projector also refuses to be a tab. The system merges a newly opened window into the
frontmost window's tab bar, and the factory setting for when it does so is "in full screen" -- so an
application restored into presentation mode swallows the projector the instant it opens, and what
should have been a second floating view is a tab that merely hides the canvas. `NSWindow`'s tabbing
mode is set to *disallowed* before the window is ever ordered in, which is why it is realized in the
constructor rather than on first show: a window that has already joined a tab group does not leave
it because the mode changed afterwards.

Three things in that path were wrong and are worth not reintroducing:

- **Save and restore must use the same rectangle.** Measuring against `gdk_monitor_get_geometry`
  and restoring against `gdk_monitor_get_workarea` differs by the height of the menu bar on macOS,
  and by whatever panels are present elsewhere, so the window walks by that much on every reopen.
- **`gtk_window_get_position` is documented as returning what `gtk_window_move` needs to be given
  to leave a window where it is, and the quartz backend does not honour it** -- it moves the
  content area and reports the frame. `moveTo` therefore asks, reads back where the window actually
  went, and corrects by the difference, which is zero on a backend that got it right.
- **A maximized or full-screen window's size must not be written down.** It is the size of the
  screen, and restoring it would mean the projector could never be small again: every close would
  write the screen size back over the size it should reopen at.

## The caption safe area

Burnt-in subtitles are added downstream and cover the bottom of the finished frame. The projector
shades that strip so nothing worth reading gets written into it.

The height is configured in **lines of the finished video**, not as a percentage and not in window
pixels, because that is how a subtitling requirement is normally written down -- "keep the bottom
150 px clear" at 1080p. The projector converts it to a fraction of the recording's frame height and
applies that to the page rectangle, so the guide means the same thing whatever size the window
happens to be.

The guide is a plain translucent fill with no edge line: everything under the tint is covered, and
the first clear row is the first safe one. An edge line was tried and removed -- it invited the
question of whether the line's own rows were inside or outside the covered strip.

The shading is drawn by `ProjectorWindow` and by nothing else. It cannot reach the recording, which
is drawn separately and never sees anything the projector does.

## The frame rate indicator

The projector puts the frame rate in its top-right corner and the record button repeats it beside
the clock, in the spirit of OBS's status bar. It is switched on by default and turned off under
**Preferences > Video Recording > Projector window**.

The reading says which rate it is showing, because two different ones matter at different times:

- **`REC 59.8 fps`** while a video is being recorded -- the recording's own rate. Frames are drawn
  on the user interface thread, so this falling below the configured rate says that thread is not
  keeping up, and every frame it misses is a frame the encoder repeats. That is the whole reason
  for the feature: a lecture that stuttered is worth knowing about while it is still being given.
- **`29.9 fps`** with nothing being recorded -- the projector's redraw clock, which runs at 30 Hz.

What is measured on the projector's side is the clock, not the paints. The window only repaints
when the page has changed, so counting paints would read a few frames a second on a page nobody is
writing on -- correct, and indistinguishable from a fault. The clock ticks at a fixed rate whatever
the page is doing, and falls behind exactly when the machine is too busy, which is the thing worth
warning about.

Both numbers come from `xoj::canvas::FrameRateMeter`, which counts ticks in a sliding one-second
window. A lifetime average would barely move for a stall that lasted a minute, and a window makes
the reading fall away on its own when ticks stop instead of freezing at the last healthy value.
Nothing is reported for the first half second of measuring: a rate divided out of one or two
samples is noise -- the first reading of a 30 Hz clock came out as a million -- and the indicator
holds its previous text rather than showing a figure it cannot stand behind.

White above nine tenths of the rate being aimed for, amber below that, red below six tenths.

**None of it reaches the recording.** The readout is painted by `ProjectorWindow` after the page,
onto the projector's own picture; `VideoRecorder` draws its frames separately and calls nothing in
that file. The indicator asks for its own repaints only when the text it would show has actually
changed, which on a healthy machine is a few times a minute rather than thirty times a second.

## The buttons

Recording is started and stopped from the toolbar's audio group, and both buttons there mean what
they say during a recording:

- **Record** turns red and counts, `0:07`, `12:40`, `1:07:24` past an hour, with the frame rate
  beside it while a video is being written. A toggle's pressed-in
  look is easy to miss across a wide toolbar, and nothing else in the window says how far into a
  take you are -- which are exactly the two mistakes a recording invites: talking for ten minutes
  to a recorder that was never started, and leaving one running long after the lecture ended. The
  counter reads the recording's own start time on every tick rather than counting ticks, so a
  missed timeout shows as a skipped second instead of accumulating into a wrong duration.
- **Stop** ends a recording, not only a playback. It sits next to the record button, so during a
  recording that is plainly what a user reaching for it means; playback and recording never overlap,
  so there is nothing to choose between. It is insensitive when there is nothing to stop, and it now
  appears in a build with no audio support at all, because there is still a video to stop.

## Where things live

| File | Role |
| --- | --- |
| `src/core/control/VideoRecorder.{h,cpp}` | ffmpeg process, command line, frame pump, `AudioFilterConfig` |
| `src/core/audio/PipedAudioSource.{h,cpp}` | the microphone, as raw samples on a pipe |
| `src/core/gui/toolbarMenubar/RecordButton.{h,cpp}` | the red, counting record button |
| `resources/rnnoise/sh.rnnn` | the RNNoise model, shipped in the bundle |
| `src/core/gui/CanvasFrame.{h,cpp}` | the one function that draws "the page, alone", the pen marker, the frame cache and the frame rate meter |
| `src/core/gui/XournalView.{h,cpp}` | where the pen was last seen, fed from `InputContext` |
| `src/core/gui/ProjectorWindow.{h,cpp}` | the projector window, the caption guide and the frame rate indicator |
| `src/core/gui/dialog/RecordingSettingsPanel.{h,cpp}` | the preferences page |
| `src/core/control/Control.cpp` | `startRecording` / `stopRecording`, projector lifetime |

## Testing

`XOPP_NO_RECOVERY=1` suppresses the "Xournal++ crashed last time" prompt at startup. An automated
run launches the application repeatedly and kills it rather than quitting, so every launch after
the first otherwise opens onto a dialog waiting for a human. The recovery file itself is left
untouched -- a real launch still offers it, which is the only kind that should be answering.

`XOPP_NO_FOCUS=1` stops a test instance from taking the keyboard. It refuses focus at the window
level and, on macOS, demotes the process to an accessory application, which cannot become the
active one at all -- so whatever is being typed at the moment a test window opens keeps going where
it was going. Use it on every automated launch.

The one thing it cannot do is keep the screen. Presentation mode is macOS full screen, and a
full-screen window takes over its display by definition, so a test that has to be in presentation
mode will be seen. Leave `presentationMode` off in the scratch configuration unless the behaviour
under test is presentation mode itself.

Closing the projector never touches a recording in progress, and reopening it is always safe: the
recording is drawn from the document, not from that window.

## A dialog must never be full screen

Opening the preferences over a full-screen window used to leave the application a black rectangle
that answered nothing, as soon as the dialog was dismissed.

macOS offers a window opened over a full-screen one the whole space, and an ordinary resizable
GtkWindow qualifies -- so the dialog came up full screen in its own right, measured 1920x1080 with
the full-screen bit set. Closing it made AppKit run the exit-full-screen transition, and that
transition re-frames a window GTK has already started destroying: a segmentation fault inside
`-[GdkQuartzView setFrame:]`. The application did not vanish, because the crash handler catches the
signal and writes an emergency save, so what was left on screen was the window it no longer had a
main loop for.

`xoj::util::gtk::setFullScreenAuxiliary` marks a popup as an auxiliary occupant of the space
instead. It is called from `PopupWindowWrapper::show`, which every popup in the application goes
through, so the fix is in one place. The dialog is now a floating 1056x740 window over the page --
what a preferences window should look like anyway -- and there is no transition left to run when it
closes.

Presentation mode is full screen, and this fork restores presentation mode at startup, so this was
reachable from the first thing a lecture does.

This is not a fork-only problem. Upstream has it filed twice and open, from both ends of the same
mechanism -- [#6412](https://github.com/xournalpp/xournalpp/issues/6412), "Preferences Crash on Exit
in Fullscreen", and [#6631](https://github.com/xournalpp/xournalpp/issues/6631), which reports the
black unresponsive window rather than the crash under it. Neither names a cause and neither
references the other. Inkscape reports the same GTK-quartz behaviour without the crash, in
[inbox#7277](https://gitlab.com/inkscape/inbox/-/issues/7277): dialogs come up full screen when the
main window is. The fix here is small and self-contained, so it is worth offering upstream.
