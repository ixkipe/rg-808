> This file contains features to be implemented,
> and concerns only core logic changes, so everything listed here
> applies to both the RG-808 and the RG-SMP.

- [x] introduce 2 new step volume types: ghost (just barely audible) and weak (quieter than regular)
  - [x] 4 volume types total: ghost, weak, regular, accented
  - [x] select by holding down A on a step in the sequencer for 0.5 sec, modal window pops up with 4 volume types, select w/ D-pad
- [x] when selecting an empty pattern in the pattern library - must create a clean pattern and switch to it
- [x] synchronize the sequencer's playhead with the swing
- [x] POSSIBLE BUG: check the "save pattern" functionality both in the main menu and in the pattern library menu. "save pattern" in the main menu must save the current state ONLY to the currently selected pattern, while the "save here" option in the pattern library must ONLY save to the pattern the cursor is currently at.
  - verified: main-menu save writes only the active slot; SAVE HERE writes the cursor slot and switches to it (save-as). Regression test added.
