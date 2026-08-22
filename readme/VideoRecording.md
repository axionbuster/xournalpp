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

So one shared renderer draws a marker at the pen on the live canvas and in the frame, in the current
pen's color. The eraser is the deliberate exception on the live canvas: when allowed by the Eraser
Visibility setting, its rectangular outline shows the area that will actually be erased. A frame
cannot capture that native cursor, so the projector and recording use a neutral gray marker for the
eraser instead. Three marker shapes are offered. All are drawn at the same size, all have a thin
dark edge so they have a boundary against a white page, and all mark the exact tip; what they trade
is how much of the page underneath survives:

| Shape | What it does |
| --- | --- |
| **Disk** (default) | A translucent fill -- the shape a screen recorder's cursor highlight conventionally takes, Camtasia's default being a circular pool of color. The writing stays readable straight through it. |
| **Ring** | An outline only. Hides nothing whatever, but is the easiest of the three to lose against a page already covered in ink of the same color. |
| **Dot** | A solid fill. The most visible, and the only one that actually covers what it is over. |

Disk and ring also carry a small solid dot at the center, at the width the pen would draw, so the
marker says exactly where the ink would land rather than merely the neighborhood. A solid dot is
already its own tip mark and gets none.

It is on by default and turned off under **Preferences > Video Recording > Recording**, where the
shape is chosen too. Its diameter is given in pixels of the finished video, the same way the
caption safe area is: a recording at the configured height draws it at exactly the number asked
for, and the live canvas and a projector window of any size draw it in the same proportion. Any
value is accepted, fractions included -- two pixels on a 4K recording and half the page height are
both things somebody has a reason to want, and nothing here knows better than the person watching
the result. **A diameter of 0 draws nothing**, which is the convenient way to switch the marker off
while the number is already under the cursor; the checkbox does the same thing and remembers the
size.

On the live canvas, ordinary pointing and drawing use the marker alone. A retained 1x1 transparent
GDK cursor keeps the platform pointer out of the way; this matters on macOS, where asking Quartz for
a large custom cursor can clip it or fall back to the system arrow. Cursors that communicate an
interaction -- the eraser's size outline when enabled, selection resize and rotate handles, the
text caret, pan and vertical-space modes -- stay visible. The result keeps those affordances without
letting the native arrow randomly appear over ordinary ink.

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
recorded, so it has to show what the recording shows. The live canvas redraws only the small regions
at the old and new marker positions. `XournalView` also tells the projector the pointer moved, once
per motion event, and advances the recorder's live-frame generation. Each consumer's own clock
still decides when to repaint, so the rate stays capped however fast the tablet reports.

## How the two streams get into one file

One ffmpeg process, two pipes.

Video goes to its standard input as raw BGRA -- what a Cairo `ARGB32` surface already is in memory,
so no conversion happens on our side. A writer thread emits frames on its own clock, at whatever
rate the configured frame rate asks for, repeating the last frame when nothing has changed. It
emits exactly as many frames as wall-clock time says it should: raw video carries no timestamps, so
the length of the video track is purely a matter of how many frames were sent, and getting that
count right is what keeps the picture level with the sound over a long recording.

The UI-side render clock runs at GLib's default-idle priority. Pen input, normal interface work and
GTK's own redraws therefore go first; if they keep the UI thread busy, the writer repeats its latest
frame rather than making the pen wait behind an eight-megabyte frame copy.

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
ink still under the pen is an overlay and is drawn afresh whenever live pixels change, which is what
makes writing appear as it is written. A cheap activity snapshot combines repaint/pointer
generation, `Control::getCanvasRevision()`, frame-cache generation and the current page. The first
counter covers active ink, selections and laser/geometry overlays; the revision covers settled
content -- a stroke finished, an undo, a background swapped, a layer hidden. On the same page a
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
quarter of a second old is checked regardless. That bounded watchdog catches unreported live and
settled changes; full-page renewal still runs in the background, so the safety net costs little on
the thread that matters.

The recorder skips before drawing when that complete activity snapshot is unchanged, even if a
stationary pointer or selection is present. At 60 Hz the watchdog reduces an unchanged second from
60 full 1080p draws to four safety draws. It also skips packing after a safety draw proves identical:
same kept picture, same page and no overlays means the writer can just re-send its copy on schedule.
Most of a lecture is a still page being talked about, and a still page now costs approximately
nothing.

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
that runs whether or not the bars are visible. What silences it is scrollbar policy **EXTERNAL**,
the policy for a scrollbar someone else owns: no bar and no indicator on that axis, while the
canvas stays clipped to the viewport and the adjustments keep working. Two policies that also
silence it must not be used. NEVER stops clipping and hands the child its full natural height --
here a 2780-pixel canvas inside a 948-pixel window, the page pinned to the top and nothing
scrollable. Turning **overlay scrolling** off keeps the geometry but once hid a real bug: page
visibility is recomputed from the scroll adjustments, and until `Layout::adjustmentReconfigured`
it was recomputed only when the scroll *value* changed -- which presentation mode never changes --
so the visibility computed against the zero-sized viewport of early startup stuck, the live
stroke's mask came out empty, and ink appeared only on pen lift. Outside presentation mode the
scrollbars behave exactly as configured in the preferences.

Both pipes are handed to ffmpeg by GLib's own descriptor mapping, not by a child-setup function
with `G_SPAWN_LEAVE_DESCRIPTORS_OPEN`. That flag also leaves the *writing* ends open inside the
child, so closing our copies at the end of a recording never produces an end of file and ffmpeg
waits forever for input that cannot arrive.

The defaults are 1920×1080 at 60 fps and 160 kbit/s AAC, in a fragmented `.mov` -- fragmented so a
recording lost to a crash or a flat battery still plays up to the point it stopped. What encodes
the picture, and how many bits it is allowed, are worked out rather than configured; the next two
sections are about that.

## Which encoder, and who decides

`videoRecordingVideoCodec` is `auto` by default, which is not an encoder name but an instruction to
go and find one. Encoders are tried in this order, and the first that works is used:

| | |
| --- | --- |
| `hevc_videotoolbox` | Apple's media engine |
| `h264_videotoolbox` | the same engine, on Macs that do not offer HEVC |
| `hevc_nvenc`, `h264_nvenc` | NVIDIA |
| `libx265`, `libx264` | the processor, if nothing else answered |

Two things about that list are deliberate.

**"Works" is established by encoding, not by asking.** `ffmpeg -encoders` lists what the binary was
built with, which is a different question from what this machine can do: a Mac with no media engine
lists both VideoToolbox encoders, and a machine with no card in it lists both NVENC ones. Each
candidate is therefore given two frames to encode, with the same arguments a recording would use --
the same pixel format going in, the same quality option, the same upload or conversion in front of
it -- because those are what a working encoder has to accept. A probe that tested something simpler
would pass encoders our own command line then breaks on. The whole thing takes about a third of a
second and is remembered per ffmpeg binary, so the preferences page can rebuild its live preview on
every keystroke without re-probing.

**VAAPI and QSV are missing on purpose.** Both want a device opened and a surface format negotiated
before they will take a frame, and none of that plumbing is here. Listing them would mean sometimes
choosing an encoder that fails at the moment a lecture starts.

The preferences page says which one was chosen and whether it is hardware, because it is the one
setting on that page nobody picked.

## Quality, not bitrate

A bitrate is the wrong instrument for a page of handwriting. Most of a lecture is a still page being
talked about, which needs almost no bits at all; a bitrate spends its budget anyway, and then a fast
scroll arrives and the same budget is all there is. `videoRecordingQuality` asks for a picture
instead, on VideoToolbox's 0-100 scale, and a still page then costs approximately nothing. Setting
it to 0 goes back to `videoRecordingVideoBitrate`, for anyone who has to hit a fixed size.

The software encoders do not have that scale -- theirs is `-crf`, which runs 51 to 0 in the other
direction -- so they are handed the crf that corresponds to the same number. One control means one
picture whichever encoder was chosen. Measured on the same 32-second recording, `h264_videotoolbox`
and `hevc_videotoolbox` at quality 80 scored VMAF 97.40 and 97.30 against the same reference, which
is what makes a single control honest.

Three named settings sit in front of the number, and are what the preferences page shows first:

| Preset | Quality | Keyframes |
| --- | --- | --- |
| Smaller files | 65 | every 4 s |
| Balanced (default) | 80 | every 2 s |
| Sharper picture | 92 | every 2 s |

The keyframe interval belongs with them rather than beside them. ffmpeg's own default is twelve
frames, which at 60 fps is five keyframes a second, and a keyframe is a whole 1080p page every time.
On a page nobody is writing on that is essentially the entire file. Lengthening it and changing
nothing else took a real 32-second recording from 9.4 MB to 2.2 MB. What it costs is what a
truncated file loses: fragments start at keyframes, so a recording cut short by a crash loses at
most one interval off its tail.

Together with HEVC and the quality control, that same recording finishes at **2.19 MB against
9.65 MB**, scoring VMAF 96.7 against the original -- 4.4 times smaller, for a picture that does not
differ to look at.

## What the conversion used to cost

The frame pump writes BGRA, which is what a Cairo `ARGB32` surface already is in memory. Handing
that to an encoder that wants YUV means a conversion, and doing it in software -- `-vf
format=yuv420p`, converting 1920×1080 sixty times a second -- was costing more processor time than
the encoding it was feeding. VideoToolbox will do the same conversion on the media engine, so it
does, and which route the frames take depends on what the chosen encoder will accept:

- **`hevc_videotoolbox` lists `bgra` among its input formats**, so the frames go in untouched and no
  filter is inserted at all.
- **`h264_videotoolbox` does not**, so `-init_hw_device videotoolbox` plus `hwupload` wraps them in
  VideoToolbox surfaces and the conversion happens on the far side of that.
- **Anything else is a software encoder** and still gets `format=yuv420p`, because there is nowhere
  else for the work to go.

Measured over 300 frames at 1080p, the software conversion took 2.76 s of processor time and the
hardware routes took 0.94 s and 1.03 s. On the same clip, `libx265` needed 20.48 s and `libx264`
5.65 s to do the encoding the media engine did in well under one.

That the media engine is genuinely doing it can be checked without instrumentation: ffmpeg's
`allow_sw` option defaults to *false* for the VideoToolbox encoders, so an encode that succeeds at
all is a hardware encode. Forcing `-require_sw 1` on the same clip took 19.7 s of wall time against
1.8 s. There is no separate encoder process on Apple silicon -- the work goes to the media engine
through the kernel driver rather than to a `VTEncoderXPCService` -- so a process-level measurement
of ffmpeg is a fair one, and during a 1080p60 encode ffmpeg does not reach the top four processes
by processor use.

On macOS the sound takes the same route: `aac_at` is AudioToolbox's AAC encoder, Apple's own, and
used about a third of the processor time of ffmpeg's built-in `aac` on the same audio.

## HEVC has to be tagged `hvc1`

QuickTime plays HEVC in `.mov` and `.mp4` only under the `hvc1` sample entry. ffmpeg writes `hev1`
by default, and the difference is not cosmetic: a `hev1` file opens to a window that never paints,
and QuickLook hangs on it outright rather than failing. `-tag:v hvc1` is therefore set whenever the
container can carry it. Everything that plays `hev1` also plays `hvc1`, so there is nothing to
weigh up and it is not conditional on the platform.

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

- **`REC 59.8 fps`** while a video is being recorded -- how often the UI thread services the
  recording clock. An unchanged opportunity may skip drawing entirely without lowering the number;
  the number falls when higher-priority input/UI work delays the clock, and every delayed frame is
  one the encoder repeats. That is the whole reason for the feature: a lecture that stuttered is
  worth knowing about while it is still being given.
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
| `src/core/gui/dialog/RecordingSettingsPanel.{h,cpp}` | the preferences page, the quality presets |
| `test/unit_tests/control/VideoRecorderCommandLineTest.cpp` | what the command line must contain |
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
