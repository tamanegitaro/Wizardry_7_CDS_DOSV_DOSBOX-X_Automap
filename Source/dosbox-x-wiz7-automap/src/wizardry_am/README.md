# Wizardry67_AutomapMod
This is a "clone" of public source code, refactored for modern dosbox so it will work.

Wizardry 6 & 7 Automap Mod

Original: Copyright (C) 2014 KoriTama

Refactor: Copyright (C) 2025 DungeonCrawl-Classics.com

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

── Refactor Details ──────────────────────────────────────────────────────────────────

The original mod used OpenGL 1.x immediate mode (glBegin/glEnd).

This port replaces that entirely with SDL_Renderer (SDL2 accelerated 2D).

This port also uses DOXBox Staging 0.84.0-alpha (a550b) for rendering.

-- Fixes core functional issues to restore functionality on latest DOSBox.

-- Fixes to exploration progress (fog of war) in Wiz6 so it works and saves correctly.

-- Added a right-click option to purge saved exploration progress in Wiz6.

-- No Wiz7 exploration purge added as it is handled by the game itself.

-- Added a right-click option to delete all Notes in Wiz6 and Wiz7.


── Installation Steps ────────────────────────────────────────────────────────────────

1. Install Wizardry 6 Steam or GOG
2. Install Wizardry 7 DOS from Steam or GOG  (not Gold)
3. Navigate to the install directory you chose for the game
4. Open the DOSBox folder
5. Rename existing dosbox.exe to original_dosbox.exe
6. Copy the file contents of the zip to the DOSBox folder
7. Launch the game through existing shortcuts that Steam/GOG created
8. Alt+Enter to get out of full-screen so you can see the Map window.

DungeonCrawl-Classics is not affiliated with the original mod author but if you have questions
about the 2025 refactor, reach out to questions.dcc@gmail.com
