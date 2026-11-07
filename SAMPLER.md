# RG-SMP
- 11 tracks
- sample browser (instead of RD-808's sample kit)
  - show .wav files in assets/wav folder, can select for a particular track
  - can also have subdirectories, so make recursive traversal possible (if in a subdirectory, display the ".." option (go to parent directory) as the first option)
  - error handling: if a sample was used in a pattern but is now deleted - simply do not play it, maybe show yellow exclamation mark in the track's header
- color palette must resemble that of KORG volca sample 2 (white/black/grey/red)
- everything else stays the same

## Implementation status
- 11 fixed tracks `01`-`11`; sample paths are stored per pattern (`SMP` lines).
- Recursive browser under `assets/wav`, `..` first inside a subdirectory,
  directory-first sorting, A assign/audition, X audition, B back, leaf level
  remembered for the session.
- Samples are loaded only for the active pattern/song; one sample max 10 s;
  song bank max 128 files / 64 MB (refused with a message if exceeded).
- Missing files are detected at startup/pattern load; silent track + yellow `!`.
- Volca-style black/dark-grey background, white/grey tracks, red accents,
  yellow warnings. Track labels show no sample filename; the browser does.
- Each track keeps at most two live voices.
- The 808 pattern/kit format is not imported; new patterns use `RGSMP1`.
- First launch installs a factory 909 groove named `DEFAULT` into slot 001
  (130 BPM, written once; deleting patterns later does not restore it).