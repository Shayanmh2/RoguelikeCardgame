#pragma once

// The shape of a cutscene's timeline, shared by the generated IntroTable.h
// and EndingTable.h (tools/make_intro_scene.py, tools/make_ending_scene.py).

// One step: which frame of the sheet, for how long, a sound to play as it
// starts, and for the ending, which lit knight to draw over it (light * 2 +
// pose, -1 for none).
struct CutsceneStep { short frame; short ms; const char* sfx; short knight; };

// A shot plays `intro` steps once from `first`, then loops the `loop` after them.
struct CutsceneShot { short first; short intro; short loop; };
