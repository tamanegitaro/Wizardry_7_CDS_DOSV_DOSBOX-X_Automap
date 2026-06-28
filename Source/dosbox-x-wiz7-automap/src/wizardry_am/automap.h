/*
 *  Wizardry 6 & 7 Automap Mod
 *  Original: Copyright (C) 2014 KoriTama
 *  Refactor: Copyright (C) 2025 DungeonCrawl-Classics.com
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  ── Refactor Details -─────────────────────────────────────────────────────
 *  The original mod used OpenGL 1.x immediate mode (glBegin/glEnd).
 *  This port replaces that entirely with SDL_Renderer (SDL2 accelerated 2D).
 *  This port also uses DOXBox Staging 0.84.0-alpha (a550b) for rendering.
 *  -- Fixes core functional issues to restore functionality on latest DOSBox.
 *  -- Fixes to exploration progress (fog of war) in Wiz6 so it works and saves
 * correctly.
 *  -- Added a right-click option to purge saved exploration progress in Wiz6.
 *  -- No Wiz7 exploration purge added as it is handled by the game itself.
 *  -- Added a right-click option to delete all Notes in Wiz6 and Wiz7.
 *
 */

#pragma once

#include <cstdint>
#include <cstddef>

void AutoMapOnMainWindowMouseMotion(int x, int y);
int32_t AutoMapGetVersionDelta();

// ── Forward declarations from DOSBox Staging ──────────────────────────────────
class Section;

// ── Lifecycle ─────────────────────────────────────────────────────────────────

// Called from dosbox.cpp during config section registration
void AutoMapInit(Section* section);

// Called from sdlmain.cpp after the main SDL window is created.
// Creates the secondary automap SDL window and SDL_Renderer.
void AutoMapCreate();

// Called from sdlmain.cpp once per frame to redraw the automap window.
void AutoMapUpdate();

// Called from sdlmain.cpp to route SDL events that belong to our window.
// Returns true if the event was consumed.
bool AutoMapHandleEvent(const union SDL_Event& event);

// Called from sdlmain.cpp when the main window is first created (Windows only —
// sets up the main-window tooltip used for room-name hints on the game view).
void AutoMapOnMainWindowCreate(void* hwnd_win32);

// ── DOS hooks (called from dos_execute.cpp / dos_files.cpp) ──────────────────

// Called when an EXE is loaded.  Detects whether it is Wiz6/7 and records
// the data-segment base address for subsequent memory reads.
void AutoMapDetectGame(char* name, uint16_t loadseg, uint16_t headersize);

// Called after AutoMapDetectGame confirmed a supported game; installs the
// software INT 3 breakpoints into the emulated program's address space.
void AutoMapSetupBreakpoints();

// Called whenever the DOS kernel opens a file (read or write).
void AutoMapOnOpenDOSFile(const char* fileName, uint8_t flags, uint16_t entry);

// Called whenever the DOS kernel creates (writes) a new file.
void AutoMapOnCreateDOSFile(const char* fileName);

// Called from dos_execute.cpp when the program terminates; clears all state.
void AutomapTerminate();

// ── CPU hook (called from cpu.cpp INT 3 handler) ──────────────────────────────

// Checks whether the faulting address matches one of our registered breakpoints.
// If so, fires the callback and returns true (caller must NOT pass to normal
// INT 3 / debugger handling).
bool AutomapBreakpoint();

// Called from the custom loop installed by AutomapBreakpoint to resume normal
// execution.  Returns true once and resets the flag.
bool AutoMapExitLoop();

// ── SDL window ID (read by sdlmain.cpp event loop) ───────────────────────────

// Returns the SDL window ID of the automap window so sdlmain.cpp can route
// events to AutoMapHandleEvent().  Returns 0 if the window does not exist.
uint32_t AutoMapGetWindowID();
