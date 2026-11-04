# Testing Stratus on an OP-XY

Checks to run on a device with a firmware that includes Stratus, from top to bottom, in one project:
each section leaves the project ready for the next. To run only some sections, set up what their first
line says. Host tests (`python ../open-xy/sdk.py test --machine machines/stratus`) don't replace these:
they can't hear the sound or see the screen.

Report a failure with the section, the line and what you saw. A photo of the screen helps. Note the SDK
commit and firmware each full pass used, at the end of this file.

AI agents working on the machine's code keep this file up to date as they go: each change that adds,
removes or alters something a person would check on the device updates its lines here.

## Setup

- Build a firmware with Stratus ([building firmware](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md#3-add-your-machine))
  and install it.
- Create a new project, **plain**, without Stratus: give track 1 a stock sound of its own and save it.
  Later steps load it as a project that never had the machine.
- Create another new project, **qa**, for the pass: don't use a project you care about.
- Track 1: a stock synth, with one note on step 1 lasting a whole bar, so it sounds all the time while
  the sequencer plays. Track 15 (a send): Stratus (Shift + track 15's key, then choose it). Turn track 1's
  send to track 15 up.
- Press play, and leave it playing unless a step says otherwise.

## Firmware basics

- [ ] The OP-XY boots, and stock engines, effects, the sequencer and project save/load work
- [ ] Stratus is in the effect list of tracks 15 and 16 (Shift + their track keys), under its name, and
      in no other track's list

## Its page and controls

- [ ] The send page shows the two-row header, the page the encoders edit large and bright and the other
      small and grey, over a second of the recording as a tape with a blinking record head at its right
- [ ] A press of any encoder switches between Position, Size, Pitch, Density and Feedback, Texture, Dry,
      Reverb; each keeps its own value, and the header shows which page is active
- [ ] Each control through its whole range, both directions, reading 0-99: the sound follows, with no
      clicks, jumps or silence at the ends
  - [ ] Position: at 99 the grains play the most recent sound, at 0 a second ago; the notch on the tape
        moves toward the record head as it turns up
  - [ ] Size lengthens and shortens the grains, 32 ms to 0.5 s
  - [ ] Pitch transposes the grains, lingering at -24, -12, -7, 0, +7, +12 and +24
  - [ ] Density: regular grains up to 46, none from 47 to 52, random grains from 53
  - [ ] Feedback feeds the return back into the recording, and dies away when turned down, without
        running away
  - [ ] Texture softens the grains' edges up to 75, then the diffuser blurs them toward 99
  - [ ] Dry: at 99 track 1's own sound stays as the send rises; at 0 the send is track 1's dry/wet, fully
        wet at the top
  - [ ] Reverb adds Clouds' reverb to the return, and its tail rings out
- [ ] Shift + encoder 1 turns Freeze on and off: while frozen, recording stops, the grains keep playing
      what the tape holds, and the tape's bars stop and light up; the edited row shows Freeze while Shift
      is held
- [ ] Shift + encoders 2-4 turn nothing
- [ ] Fast turns: the value keeps up, no flicker or slowdown
- [ ] The page while playing and while stopped, and after leaving it and coming back: nothing stale,
      missing or drawn twice
- [ ] With track 1's send at 0 the effect is silent; raising it brings the effect in smoothly
- [ ] Stop the sequencer while a note rings: the effect's tail ends naturally, with no click
- [ ] The same effect on track 16, at the same time as on track 15: each keeps its own values, and each
      page shows its own tape

## Parameter locks

- [ ] Lock each control on a step (hold the step, turn the knob): it plays on that step, and the page
      shows the lock while the step is held
- [ ] Several locks on one step, and locks on neighbouring steps: each plays as written
- [ ] Clear the locks: the control plays its own value again

## Patterns, scenes and songs

- [ ] Give the track a second pattern with different values: each pattern keeps its own
- [ ] Copy a pattern: the copy holds the same values, and changing one leaves the other alone
- [ ] Switch between the patterns while playing and while stopped: the sound and the page follow at once
- [ ] Make scenes 1 and 2 with the two patterns, and a song of scene 1 then scene 2, looping: each scene
      plays its own values from its first bar, on every pass

Then play scene 1.

## Mute

- [ ] Mute the track (hold aux and press the track's key): it's silent, with no click or tail left behind;
      unmute: it plays as before
- [ ] The same from the mixer
- [ ] Muted in scene 2 only: switching between scenes 1 and 2 mutes and unmutes it

Unmute it everywhere.

## Saving and loading

- [ ] Save **qa**, power off and on, load it: Stratus is there with every value, and Freeze, as you left
      them, on each pattern
- [ ] Change a value, wait a few seconds and power off without saving: the OP-XY starts normally, and
      **qa** loads as last saved
- [ ] Load **plain**: track 1 has its own sound, nothing of **qa** shows. Load **qa** again: all back,
      and no sound from before the load is heard from the recording
- [ ] While playing, load **plain** and then **qa** again: nothing freezes, no stray values
- [ ] Create a new project: put Stratus on a send; it starts at its defaults
- [ ] On a firmware without Stratus, open **qa**: it loads, and the rest of the project plays

## CPU

- [ ] Follow [hardware profiling](https://github.com/etcetc000/open-xy/blob/staging/docs/profiling.md):
      a profiling build from the committed source, a run collected, and the readout kept with the commit
      it measured

## In the builder

Once the catalog entry is merged and released (the beta site from `staging`, open-xy.com from `main`; see
[submitting a machine](https://github.com/etcetc000/open-xy/blob/staging/docs/catalog.md#submit-a-machine)):

- [ ] The machine list shows Stratus as Community, with its name, maintainer and description
- [ ] Filtering by its kind (effects) lists it, and filtering by the other kinds doesn't
- [ ] Searching its name, and each of its keywords, finds it; its details panel lists its keywords, and
      clicking one lists it among that keyword's machines
- [ ] Choosing it builds a firmware that includes it, and that firmware passes
      [Firmware basics](#firmware-basics)

## Known limits

Not failures, but worth confirming they haven't changed.

- The page can't hear the send: the tape's bars and grains are drawn from the controls, not Clouds' own
- With the shift row, Shift no longer turns the other controls in fine steps
- The return is all wet: each track's dry sound stays in the stock mixer, set by Dry through the send
- On a firmware from an SDK release without [open-xy#389](https://github.com/etcetc000/open-xy/pull/389),
  the page doesn't follow an LFO on its controls; the sound does

## Passes

| Date | Machine commit | SDK commit | Firmware | Result |
| --- | --- | --- | --- | --- |
