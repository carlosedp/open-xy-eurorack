# Testing Cirrus on an OP-XY

Checks to run on a device with a firmware that includes Cirrus, from top to bottom, in one project:
each section leaves the project ready for the next. To run only some sections, set up what their first
line says. Host tests (`python ../open-xy/sdk.py test --machine machines/cirrus`) don't replace these: they
can't hear the sound or see the screen.

Report a failure with the section, the line and what you saw. A photo of the screen helps. Note the SDK
commit and firmware each full pass used, at the end of this file.

AI agents working on the machine's code keep this file up to date as they go: each change that adds,
removes or alters something a person would check on the device updates its lines here.

## Setup

- Build a firmware with Cirrus ([building firmware](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md#3-add-your-machine))
  and install it.
- Create a new project, **plain**, without Cirrus: give track 1 a stock sound of its own and save it.
  Later steps load it as a project that never had the machine.
- Create another new project, **qa**, for the pass: don't use a project you care about.
- Track 1: Cirrus (Shift + M1 on track 1, then choose it). Load a stereo sample a few seconds long with
  the stock sample browser. Put one note on step 1 lasting a whole bar, so it sounds all the time while
  the sequencer plays.
- Press play, and leave it playing unless a step says otherwise.

## Firmware basics

- [ ] The OP-XY boots, and stock engines, effects, the sequencer and project save/load work
- [ ] Cirrus is in the engine list of tracks 1-8 (Shift + M1 there), under its name, and in no other
      track's list

## Its screen and controls

- [ ] Before a sample is loaded, the screen asks for one
- [ ] With a sample, its waveform fills the screen under the two-row header; the page the encoders edit
      is large and bright, the other small and grey
- [ ] A press of any encoder switches between Position, Size, Density, Texture and Pitch, Speed, Spread,
      Jitter; each keeps its own value, and the header shows which page is active
- [ ] Each control through its whole range, both directions, reading 0-99: the sound follows, with no
      clicks, jumps or silence at the ends
  - [ ] Position moves where the grains start, and the faint scan line with it
  - [ ] Size lengthens and shortens the grains
  - [ ] Density: regular grains up to 46, only the note's own grain from 47 to 52, random grains from 53
  - [ ] Texture softens the grains' edges
  - [ ] Pitch transposes the grains, lingering at -24, -12, -7, 0, +7, +12 and +24
  - [ ] Speed: the scan stands still at 50, moves forward above and back below
  - [ ] Spread pans the grains, and the screen shows them above (left) and below (right)
  - [ ] Jitter detunes each grain at random, up to a semitone at 99
- [ ] Shift + a turn of the encoders isn't a fine turn here: see [Shift and the stock sampler](#shift-and-the-stock-sampler)
- [ ] Fast turns: the value keeps up, no flicker or slowdown
- [ ] The screen while playing and while stopped, and after leaving it and coming back: nothing stale,
      missing or drawn twice
- [ ] Each held note, up to four, has its own playhead that pulses as the note starts; grains glow along
      it as Density schedules them
- [ ] Every key across the keyboard's octaves plays the grains at its pitch, middle C at the sample's own;
      releasing ends the note; no note gets stuck, also after long holds
- [ ] Chords and fast repeated notes: every note sounds and ends, and the notes of a chord scatter apart
- [ ] The octave buttons and the track's transpose move the pitch as on the stock sampler
- [ ] A mono sample plays in mono, a stereo one in stereo

## Shift and the stock sampler

- [ ] While Shift is held, the header's edited row shows Start, End, Window and Gain: the start, the
      end and the window's length as 0-99 of the sample, and the gain from -30 to +20, 0 for a new sample
- [ ] Shift + encoder 1 moves the sample's start, and Shift + encoder 2 its end: the grains stay inside,
      and the waveform marks follow
- [ ] Shift + encoder 3 slides the window between them, keeping its length, both ways, and stops at the
      sample's start and end
- [ ] Shift + encoder 4 changes the gain heard, its caption follows, and the waveform grows taller as
      the gain rises and flatter as it falls; a newly loaded sample shows at its usual height
- [ ] A trim shorter than 128 frames plays nothing, and widening it brings the sound back
- [ ] Releasing Shift: the header shows the machine's controls again, and turns edit them

## Envelope, filter and LFO pages

- [ ] M2 (envelope): a released note's playhead rings on, dimmer, for about the Release: short at its
      shortest, up to 30 s at its longest; the sound follows the envelope as on the stock sampler
- [ ] M3 (filter) changes the sound as on the stock sampler
- [ ] On M2, M3 and M4, the encoders and Shift + the encoders edit those pages as on the stock sampler,
      never Cirrus' controls or the trim

## Parameter locks

- [ ] Lock each control on a step (hold the step, turn the knob): it plays on that step, and the screen
      shows the lock while the step is held
- [ ] Several locks on one step, and locks on neighbouring steps: each plays as written
- [ ] Clear the locks: the control plays its own value again

## LFOs

- [ ] The track's LFO (M4) lists Cirrus' controls by their names, and moving one of them is heard,
      stopped and playing
- [ ] Rate, depth and shape behave as on a stock engine; turning the LFO off stops the movement

Then turn the LFO off.

## Patterns, scenes and songs

- [ ] Give the track a second pattern with different values: each pattern keeps its own
- [ ] Copy a pattern: the copy holds the same values, and changing one leaves the other alone
- [ ] Switch between the patterns while playing and while stopped: the sound and the screen follow at once
- [ ] Make scenes 1 and 2 with the two patterns, and a song of scene 1 then scene 2, looping: each scene
      plays its own values from its first bar, on every pass

Then play scene 1.

## Presets

- [ ] Shift + track key lists presets: Cloud, Stretch, Stutter, Octave Down, Frozen and Rewind are listed,
      and choosing one sets its values
- [ ] Save the track's sound as a preset, change it, load the preset: the saved values come back
- [ ] A stock preset loaded on a track holding Cirrus (and the reverse) gives what it says, with no stray
      values
- [ ] A new Cirrus sound has no attack, full sustain and a long release

## Samples

- [ ] Load a different sample with the browser: the screen and the sound move to it, and the trim covers
      the whole new sample
- [ ] Record a sample with the stock recorder: Cirrus plays it, and the screen shows it
- [ ] The stock sample browser and recorder keep their own pages while Cirrus is on the track

## Mute

- [ ] Mute the track (hold aux and press the track's key): it's silent, with no click or tail left behind;
      unmute: it plays as before
- [ ] The same from the mixer
- [ ] Muted in scene 2 only: switching between scenes 1 and 2 mutes and unmutes it

Unmute it everywhere.

## Saving and loading

- [ ] Save **qa**, power off and on, load it: Cirrus is there with its sample, its trim, its gain and
      every value as you left them, on each pattern
- [ ] Change a value, wait a few seconds and power off without saving: the OP-XY starts normally, and
      **qa** loads as last saved
- [ ] Load **plain**: track 1 has its own sound, nothing of **qa** shows. Load **qa** again: all back
- [ ] While playing, load **plain** and then **qa** again: nothing freezes, no stray values
- [ ] Create a new project: put Cirrus on a track; it starts at its defaults and asks for a sample
- [ ] On a firmware without Cirrus, open **qa**: it loads, and the rest of the project plays

## CPU

- [ ] Follow [hardware profiling](https://github.com/etcetc000/open-xy/blob/staging/docs/profiling.md):
      a profiling build from the committed source, a run collected, and the readout kept with the commit
      it measured

## In the builder

Once the catalog entry is merged and released (the beta site from `staging`, open-xy.com from `main`; see
[submitting a machine](https://github.com/etcetc000/open-xy/blob/staging/docs/catalog.md#submit-a-machine)):

- [ ] The machine list shows Cirrus as Community, with its name, maintainer and description
- [ ] Filtering by its kind (sample engines) lists it, and filtering by the other kinds doesn't
- [ ] Searching its name, and each of its keywords, finds it; its details panel lists its keywords, and
      clicking one lists it among that keyword's machines
- [ ] Choosing it builds a firmware that includes it, and that firmware passes
      [Firmware basics](#firmware-basics)

## Known limits

Not failures, but worth confirming they haven't changed.

- The screen runs apart from the audio: its grains are the controls' grains, not the voices' own
- Cirrus has no control for direction or the loop markers; a region reversed on the stock sampler plays
  each grain backward
- Clouds' feedback, diffuser and reverb aren't here: send the track to Stratus for them

## Passes

| Date | Machine commit | SDK commit | Firmware | Result |
| --- | --- | --- | --- | --- |
