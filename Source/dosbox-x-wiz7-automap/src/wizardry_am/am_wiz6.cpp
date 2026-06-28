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

#include "dos_inc.h"
#include "automap.h"
#include "dosbox.h"
#include "mem.h"
#include "cpu.h"
#include "regs.h"
#include "paging.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#ifdef WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

// Forward declarations from am_main.cpp
void AM_DrawRect(int x, int y, int w, int h, int colorIndex);
void AM_DrawW6Sprite(int x, int y, int w, int h, float u0, float v0, float u1,
                     float v1, bool dark = false,
                     SDL_RendererFlip flip = SDL_FLIP_NONE);
void AM_DrawW7Sprite(int x, int y, int w, int h, float u0, float v0, float u1,
                     float v1, bool dark = false);
void SetAutomapWindowTitle(const char* title);
SDL_Renderer* AM_GetRenderer();

bool amw6_show_tooltips = true;
bool amw6_hide_in_dark_zones = true;
extern int am_height;
extern int am_width;
#define SCREENH am_height
#define SCREENW am_width

//each w6 map have 12 quadrants
#define W6_QUADRANT_COUNT 12
#define W6_SQUARE_SIZE 22 
#define W6_LEVEL_COUNT 16

uint32_t amw6_dataseg_addr;
uint8_t amw6_visdata[W6_LEVEL_COUNT][W6_QUADRANT_COUNT][8][8];
uint16_t amw6_level = 0;
uint16_t amw6_jLevel = 0xFFFF;
uint8_t amw6_jX = 0;
uint8_t amw6_jY = 0;
/* wizardry 6 cache */
uint8_t amw6_cache_qsx[W6_LEVEL_COUNT][W6_QUADRANT_COUNT];
uint8_t amw6_cache_qsy[W6_LEVEL_COUNT][W6_QUADRANT_COUNT];
uint8_t amw6_cache_hwalls[W6_LEVEL_COUNT][W6_QUADRANT_COUNT*64*2/8];
uint8_t amw6_cache_vwalls[W6_LEVEL_COUNT][W6_QUADRANT_COUNT*64*2/8];
uint8_t amw6_cache_features[W6_LEVEL_COUNT][W6_QUADRANT_COUNT*64*4/8];
uint8_t amw6_cache_features_dirs[W6_LEVEL_COUNT][W6_QUADRANT_COUNT*64*2/8];
uint8_t amw6_cache_floor[W6_LEVEL_COUNT][W6_QUADRANT_COUNT*64/8];
uint8_t amw6_cache_roof[W6_LEVEL_COUNT][W6_QUADRANT_COUNT*64/8];

uint32_t wiz6_palette[16];

struct AMW6Note {
	uint16_t quadrant;
	uint16_t qX;
	uint16_t qY;
	uint32_t color;
	std::wstring str;
};

std::vector<AMW6Note> amw6_notes[W6_LEVEL_COUNT];

const char *amw6MapNames[W6_LEVEL_COUNT] = {
	"Castle Entrance and Towers",
	"Castle Upper Level, Tower Stairs and Bell Tower",
	"Castle Basement and Hazard Area",
	"Mountain Area and Amazulu Burial Chamber",
	"Amazulu Pyramid",
	"Mines Area",
	"Mountain Alpes and Castle Lower Level",
	"Pyramid Basement",
	"River Styx",
	"Hall of the Dead and Tomb of the Damned",
	"Swamp Area",
	"Temple of Ramm",
	"Forest Area",
	"Lower Level of the Temple of Ramm",
	"Map 14",
	"Map 15",
};

/////////////////////////////////////////////////
// Tiles
/////////////////////////////////////////////////



void W6_DrawWaterTile(int x, int y, int w, int h, bool dark = false){
	AM_DrawW6Sprite(x,y,w,h,0,0,7.0f/256.0f,7.0f/16.0f,dark);
}

void W6_DrawDarkTile(int x, int y, int w, int h, bool dark = false){
	AM_DrawW6Sprite(x,y,w,h,16.0f/256.0f,0,23.0f/256.0f,7.0f/16.0f,dark);
}

void W6_DrawLightTile(int x, int y, int w, int h, bool dark = false){
	AM_DrawW6Sprite(x,y,w,h,32.0f/256.0f,0,39.0f/256.0f,7.0f/16.0f,dark);
}

void W6_DrawHPassage(int x, int y, int w, int h, bool dark = false){
	AM_DrawW6Sprite(x,y,w,h,48.0f/256.0f,0,57.0f/256.0f,3.0f/16.0f,dark);
}

void W6_DrawVPassage(int x, int y, int w, int h, bool dark = false){
	AM_DrawW6Sprite(x,y,w,h,71.0f/256.0f,0,74.0f/256.0f,9.0f/16.0f,dark);
}

void W6_DrawHWall(int x, int y, int w, int h, bool dark = false){
	AM_DrawW6Sprite(x,y,w,h,80.0f/256.0f,0,89.0f/256.0f,3.0f/16.0f,dark);
}

void W6_DrawVWall(int x, int y, int w, int h, bool dark = false){
	AM_DrawW6Sprite(x,y,w,h,103.0f/256.0f,0,106.0f/256.0f,9.0f/16.0f,dark);
}

void W6_DrawHDoor(int x, int y, int w, int h, bool dark = false){
	AM_DrawW6Sprite(x,y,w,h,112.0f/256.0f,0,121.0f/256.0f,3.0f/16.0f,dark);
}

void W6_DrawVDoor(int x, int y, int w, int h, bool dark = false){
	AM_DrawW6Sprite(x,y,w,h,135.0f/256.0f,0,138.0f/256.0f,9.0f/16.0f,dark);
}

void W6_DrawHPortcullis(int x, int y, int w, int h, bool dark = false){
	AM_DrawW6Sprite(x,y,w,h,60.0f/256.0f,0,69.0f/256.0f,3.0f/16.0f,dark);
}

void W6_DrawVPortcullis(int x, int y, int w, int h, bool dark = false){
	AM_DrawW6Sprite(x,y,w,h,42.0f/256.0f,0,45.0f/256.0f,9.0f/16.0f,dark);
}

void W6_DrawStairsUp(int x, int y, int w, int h, bool dark = false){
	AM_DrawW6Sprite(x,y,w,h,142.0f/256.0f,13.0f/16.0f,155.0f/256.0f,0,dark);
}

void W6_DrawFountainUp(int x, int y, int w, int h, bool dark = false){
	AM_DrawW6Sprite(x,y,w,h,189.0f/256.0f,15.0f/16.0f,205.0f/256.0f,0,dark);
}

void W6_DrawFountainDown(int x, int y, int w, int h, bool dark = false){
	AM_DrawW6Sprite(x,y,w,h,189.0f/256.0f,0,105.0f/256.0f,15.0f/16.0f,dark);
}

void W6_DrawStairsDown(int x, int y, int w, int h, bool dark = false){
	AM_DrawW6Sprite(x,y,w,h,142.0f/256.0f,0,155.0f/256.0f,13.0f/16.0f,dark);
}

void W6_DrawCursorUp(int x, int y, int w, int h){
	AM_DrawW6Sprite(x,y,w,h,174.0f/256.0f,0,185.0f/256.0f,14.0f/16.0f,false);
}

void W6_DrawCursorDown(int x, int y, int w, int h){
	AM_DrawW6Sprite(x,y,w,h,174.0f/256.0f,0,185.0f/256.0f,14.0f/16.0f,false,SDL_FLIP_VERTICAL);
}

void W6_DrawCursorRight(int x, int y, int w, int h){
	AM_DrawW6Sprite(x,y,w,h,157.0f/256.0f,2.0f/16.0f,171.0f/256.0f,13.0f/16.0f,false);
}

void W6_DrawCursorLeft(int x, int y, int w, int h){
	AM_DrawW6Sprite(x,y,w,h,157.0f/256.0f,2.0f/16.0f,171.0f/256.0f,13.0f/16.0f,false,SDL_FLIP_HORIZONTAL);
}


//////////////////////////////////////////////////////////////////////////////////////////////
///  Save / Load
//////////////////////////////////////////////////////////////////////////////////////////////

void W6_NewGame(){
	memset(wiz6_palette, 0, sizeof(wiz6_palette));
	memset(amw6_visdata, 0, sizeof(amw6_visdata));
	memset(amw6_cache_qsx, 0, sizeof(amw6_cache_qsx));
	memset(amw6_cache_qsy, 0, sizeof(amw6_cache_qsy));
	memset(amw6_cache_hwalls, 0, sizeof(amw6_cache_hwalls));
	memset(amw6_cache_vwalls, 0, sizeof(amw6_cache_vwalls));
	memset(amw6_cache_features, 0, sizeof(amw6_cache_features));
	memset(amw6_cache_features_dirs, 0, sizeof(amw6_cache_features_dirs));
	memset(amw6_cache_floor, 0, sizeof(amw6_cache_floor));
	memset(amw6_cache_roof, 0, sizeof(amw6_cache_roof));
	memset(amw6_visdata, 0, sizeof(amw6_visdata));
	for (int i = 0; i < W6_LEVEL_COUNT; i++){
		amw6_notes[i].clear();
	}
}

void W6_ChangeExt(char* outBuf, const char* fullName, const char* newExt) {
	int l = strlen(fullName);
	if (l < 1) { strcpy(outBuf, newExt); return; }
	int i = -1;
	for (i = l - 1; i >= 0; i--)
		if (fullName[i] == '\\' || fullName[i] == '/') break;
	int sl = strlen(&fullName[i+1]);
	int j = 0;
	for (j = 0; j < sl; j++)
		if (fullName[i+1+j] == '.') break;
	strncpy(outBuf, fullName, i+1+j);
	strcpy(&outBuf[i+1+j], newExt);
}

bool amw6_saveLock = false;
bool amw6_loadLock = false;
bool amw6_isSaving = false;
bool amw6_isLoading = false;
uint32_t amw6_lastLoadTime = 0;
static char s_w6VisPath[512] = {};  // native host path for visdata autosave
static char s_w6NtsPath[512] = {};  // native host path for notes autosave

void W6_SetSavePath(const char* basePath) {
	snprintf(s_w6VisPath, sizeof(s_w6VisPath), "%sAUTOMAP6.VIS", basePath);
	snprintf(s_w6NtsPath, sizeof(s_w6NtsPath), "%sAUTOMAP6.NTS", basePath);
}

void W6_NativeSaveVis() {
	if (s_w6VisPath[0] == 0) return;
	FILE* f = fopen(s_w6VisPath, "wb");
	if (!f) return;
	fwrite(&amw6_visdata[0][0][0][0], 1, sizeof(amw6_visdata), f);
	fclose(f);
}

void W6_NativeLoadVis() {
	if (s_w6VisPath[0] == 0) return;
	FILE* f = fopen(s_w6VisPath, "rb");
	if (!f) return;
	fread(&amw6_visdata[0][0][0][0], 1, sizeof(amw6_visdata), f);
	fclose(f);
}



void W6_NativeSaveNotes() {
	if (s_w6NtsPath[0] == 0) return;
	FILE* f = fopen(s_w6NtsPath, "wb");
	if (!f) return;
	for (int i = 0; i < W6_LEVEL_COUNT; i++) {
		uint32_t n = (uint32_t)amw6_notes[i].size();
		fwrite(&n, 4, 1, f);
		for (int j = 0; j < (int)n; j++) {
			fwrite(&amw6_notes[i][j].quadrant, 2, 1, f);
			fwrite(&amw6_notes[i][j].qX, 2, 1, f);
			fwrite(&amw6_notes[i][j].qY, 2, 1, f);
			fwrite(&amw6_notes[i][j].color, 4, 1, f);
			uint32_t len = (uint32_t)amw6_notes[i][j].str.length();
			fwrite(&len, 4, 1, f);
			if (len > 0) fwrite(amw6_notes[i][j].str.c_str(), 1, len, f);
		}
	}
	fclose(f);
}

void W6_NativeLoadNotes() {
	if (s_w6NtsPath[0] == 0) return;
	FILE* f = fopen(s_w6NtsPath, "rb");
	if (!f) return;
	for (int i = 0; i < W6_LEVEL_COUNT; i++) {
		amw6_notes[i].clear();
		uint32_t n = 0;
		if (fread(&n, 4, 1, f) != 1) break;
		for (uint32_t j = 0; j < n; j++) {
			AMW6Note note;
			fread(&note.quadrant, 2, 1, f);
			fread(&note.qX, 2, 1, f);
			fread(&note.qY, 2, 1, f);
			fread(&note.color, 4, 1, f);
			uint32_t len = 0;
			fread(&len, 4, 1, f);
			note.str.resize(len);
			if (len > 0) fread(&note.str[0], 1, len, f);
			amw6_notes[i].push_back(note);
		}
	}
	fclose(f);
}

void W6_Load(const char* fullName){
	if (!amw6_isLoading) return;
	if (amw6_loadLock) return;
	amw6_loadLock = true;
	W6_NewGame();
	char fileName[DOS_PATHLENGTH+DOS_NAMELENGTH_ASCII];

	memset(fileName,0,sizeof(fileName));
	W6_ChangeExt(fileName,fullName,".PAL");
	memset(wiz6_palette, 0, sizeof(wiz6_palette));
	if (DOS_FileExists(fileName)){
		uint16_t handle = 0;
		if (DOS_OpenFile(fileName,OPEN_READ,&handle)){
			uint16_t toread = sizeof(wiz6_palette);
			DOS_ReadFile(handle,(uint8_t*)&wiz6_palette[0],&toread);
			DOS_CloseFile(handle);
		}
	}

	memset(fileName,0,sizeof(fileName));
	W6_ChangeExt(fileName,fullName,".VIS");
	memset(amw6_visdata, 0, sizeof(amw6_visdata));
	if (DOS_FileExists(fileName)){
		uint16_t handle = 0;
		if (DOS_OpenFile(fileName,OPEN_READ,&handle)){
			uint16_t toread = sizeof(amw6_visdata);
			DOS_ReadFile(handle,&amw6_visdata[0][0][0][0],&toread);
			DOS_CloseFile(handle);
		}
	}

	memset(fileName,0,sizeof(fileName));
	W6_ChangeExt(fileName,fullName,".NTS");
	for (int i = 0; i < W6_LEVEL_COUNT; i++) amw6_notes[i].clear();
	if (DOS_FileExists(fileName)){
		uint16_t handle = 0;
		if (DOS_OpenFile(fileName,OPEN_READ,&handle)){
			uint32_t dwbuf = 0;
			for (int i = 0; i < W6_LEVEL_COUNT; i++){
				uint16_t toread = 4;
				uint32_t ncount = 0;
				DOS_ReadFile(handle,(uint8_t*)&ncount,&toread);
				for (int j = 0; j < (int)ncount; j++){
					AMW6Note note;
					toread = 2; DOS_ReadFile(handle,(uint8_t*)&note.quadrant,&toread);
					toread = 2; DOS_ReadFile(handle,(uint8_t*)&note.qX,&toread);
					toread = 2; DOS_ReadFile(handle,(uint8_t*)&note.qY,&toread);
					toread = 4; DOS_ReadFile(handle,(uint8_t*)&note.color,&toread);
					toread = 4; dwbuf = 0;
					DOS_ReadFile(handle,(uint8_t*)&dwbuf,&toread);
					toread = (dwbuf + 1) * 2;
					note.str.resize(dwbuf);
					DOS_ReadFile(handle,(uint8_t*)&note.str[0],&toread);
					amw6_notes[i].push_back(note);
				}
			}
			DOS_CloseFile(handle);
		}
	}

	memset(fileName,0,sizeof(fileName));
	W6_ChangeExt(fileName,fullName,".CAC");
	if (DOS_FileExists(fileName)){
		uint16_t handle = 0;
		if (DOS_OpenFile(fileName,OPEN_READ,&handle)){
			uint16_t toread;
			toread = sizeof(amw6_cache_qsx);   DOS_ReadFile(handle,&amw6_cache_qsx[0][0],&toread);
			toread = sizeof(amw6_cache_qsy);   DOS_ReadFile(handle,&amw6_cache_qsy[0][0],&toread);
			toread = sizeof(amw6_cache_hwalls); DOS_ReadFile(handle,&amw6_cache_hwalls[0][0],&toread);
			toread = sizeof(amw6_cache_vwalls); DOS_ReadFile(handle,&amw6_cache_vwalls[0][0],&toread);
			toread = sizeof(amw6_cache_features); DOS_ReadFile(handle,&amw6_cache_features[0][0],&toread);
			toread = sizeof(amw6_cache_features_dirs); DOS_ReadFile(handle,&amw6_cache_features_dirs[0][0],&toread);
			toread = sizeof(amw6_cache_floor); DOS_ReadFile(handle,&amw6_cache_floor[0][0],&toread);
			toread = sizeof(amw6_cache_roof);  DOS_ReadFile(handle,&amw6_cache_roof[0][0],&toread);
			DOS_CloseFile(handle);
		}
	}
	// Restore exploration data from native VIS file (overrides the memset in W6_NewGame)
	W6_NativeLoadVis();
	W6_NativeLoadNotes();
	amw6_loadLock = false;
}

void W6_Save(const char* fullName){
	if (!amw6_isSaving) return;
	if (amw6_saveLock) return;
	amw6_saveLock = true;

	char fileName[DOS_PATHLENGTH+DOS_NAMELENGTH_ASCII];

	memset(fileName,0,sizeof(fileName));
	W6_ChangeExt(fileName,fullName,".PAL");
	uint16_t handle = 0;
	if (DOS_CreateFile(fileName,0,&handle)){
		uint16_t towrite = sizeof(wiz6_palette);
		DOS_WriteFile(handle,(uint8_t*)&wiz6_palette[0],&towrite);
		DOS_FlushFile(handle); DOS_CloseFile(handle);
	}

	memset(fileName,0,sizeof(fileName));
	W6_ChangeExt(fileName,fullName,".VIS");
	handle = 0;
	if (DOS_CreateFile(fileName,0,&handle)){
		uint16_t towrite = sizeof(amw6_visdata);
		DOS_WriteFile(handle,&amw6_visdata[0][0][0][0],&towrite);
		DOS_FlushFile(handle); DOS_CloseFile(handle);
	}

	memset(fileName,0,sizeof(fileName));
	W6_ChangeExt(fileName,fullName,".NTS");
	handle = 0;
	if (DOS_CreateFile(fileName,0,&handle)){
		uint16_t towrite;
		for (int i = 0; i < W6_LEVEL_COUNT; i++){
			towrite = 4;
			uint32_t dwbuf = amw6_notes[i].size();
			DOS_WriteFile(handle,(uint8_t*)&dwbuf,&towrite);
			for (int j = 0; j < (int)amw6_notes[i].size(); j++){
				towrite = 2; DOS_WriteFile(handle,(uint8_t*)&amw6_notes[i][j].quadrant,&towrite);
				towrite = 2; DOS_WriteFile(handle,(uint8_t*)&amw6_notes[i][j].qX,&towrite);
				towrite = 2; DOS_WriteFile(handle,(uint8_t*)&amw6_notes[i][j].qY,&towrite);
				towrite = 4; DOS_WriteFile(handle,(uint8_t*)&amw6_notes[i][j].color,&towrite);
				towrite = 4; dwbuf = amw6_notes[i][j].str.length();
				DOS_WriteFile(handle,(uint8_t*)&dwbuf,&towrite);
				towrite = (dwbuf + 1) * 2;
				DOS_WriteFile(handle,(uint8_t*)&amw6_notes[i][j].str[0],&towrite);
			}
		}
		DOS_FlushFile(handle); DOS_CloseFile(handle);
	}

	memset(fileName,0,sizeof(fileName));
	W6_ChangeExt(fileName,fullName,".CAC");
	handle = 0;
	if (DOS_CreateFile(fileName,0,&handle)){
		uint16_t towrite;
		towrite = sizeof(amw6_cache_qsx);   DOS_WriteFile(handle,&amw6_cache_qsx[0][0],&towrite);
		towrite = sizeof(amw6_cache_qsy);   DOS_WriteFile(handle,&amw6_cache_qsy[0][0],&towrite);
		towrite = sizeof(amw6_cache_hwalls); DOS_WriteFile(handle,&amw6_cache_hwalls[0][0],&towrite);
		towrite = sizeof(amw6_cache_vwalls); DOS_WriteFile(handle,&amw6_cache_vwalls[0][0],&towrite);
		towrite = sizeof(amw6_cache_features); DOS_WriteFile(handle,&amw6_cache_features[0][0],&towrite);
		towrite = sizeof(amw6_cache_features_dirs); DOS_WriteFile(handle,&amw6_cache_features_dirs[0][0],&towrite);
		towrite = sizeof(amw6_cache_floor); DOS_WriteFile(handle,&amw6_cache_floor[0][0],&towrite);
		towrite = sizeof(amw6_cache_roof);  DOS_WriteFile(handle,&amw6_cache_roof[0][0],&towrite);
		DOS_FlushFile(handle); DOS_CloseFile(handle);
	}
	amw6_saveLock = false;
}




////////////////////////////////////////////////////////////////////////////////////////////////////
////////        DIRECT                                                      
////////////////////////////////////////////////////////////////////////////////////////////////////

uint16_t W6_GetBitArrayElement(uint16_t arrayOfs, uint16_t index, uint16_t bitsPerElement){
	uint16_t ret;
	uint16_t elemOfs = index * bitsPerElement;
	uint16_t elemShift = elemOfs % 8;
	uint16_t elemMask = 0xFFFF;
	elemMask >>= 16 - bitsPerElement;
	elemOfs /= 8;
	mem_readw_checked(amw6_dataseg_addr + arrayOfs + elemOfs, &ret);
	ret >>= elemShift;
	ret &= elemMask;
	return ret;
}

bool W6_TestBitArray(uint16_t arrayOfs, uint16_t index){
	uint16_t ret;
	uint16_t elemOfs = index;
	uint16_t elemShift = elemOfs % 8;
	uint16_t elemMask = 1;
	elemMask <<= elemShift;
	elemOfs /= 8;
	mem_readw_checked(amw6_dataseg_addr + arrayOfs + elemOfs, &ret);
	return ((ret & elemMask)!=0);
}

bool W6_PointInQuadrant(uint16_t x, uint16_t y, uint16_t quadrant){
	uint16_t mapdataOfs;
	mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);

	uint8_t qsx,qsy;
	mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1E0, &qsx);
	mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1EC, &qsy);
	if ((x>=qsx) && (y>=qsy) && (x<=qsx+7) && (y<=qsy+7))
		return true;
	return false;
}

bool W6_PointAtLeftSideOfQuadrant(uint16_t x, uint16_t y, uint16_t quadrant){
	uint16_t mapdataOfs;
	mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);

	uint8_t qsx,qsy;
	mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1E0, &qsx);
	mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1EC, &qsy);
	if ((x==qsx) && (y>=qsy) && (y<=qsy+7))
		return true;
	return false;
}

bool W6_PointAtBottomSideOfQuadrant(uint16_t x, uint16_t y, uint16_t quadrant){
	uint16_t mapdataOfs;
	mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);

	uint8_t qsx,qsy;
	mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1E0, &qsx);
	mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1EC, &qsy);
	if ((x>=qsx) && (x<=qsx+7) && (y==qsy+7))
		return true;
	return false;
}

bool W6_PointInAnyQuadrant(uint16_t x, uint16_t y){
	for (int q = 0; q < W6_QUADRANT_COUNT; q++){
		if (W6_PointInQuadrant(x, y, q))
			return true;
	}
	return false;
}

bool W6_PointAtLeftSideOfAnyQuadrant(uint16_t x, uint16_t y){
	for (int q = 0; q < W6_QUADRANT_COUNT; q++){
		if (W6_PointAtLeftSideOfQuadrant(x, y, q))
			return true;
	}
	return false;
}

bool W6_PointAtBottomSideOfAnyQuadrant(uint16_t x, uint16_t y){
	for (int q = 0; q < W6_QUADRANT_COUNT; q++){
		if (W6_PointAtBottomSideOfQuadrant(x, y, q))
			return true;
	}
	return false;
}

bool W6_AbsToQuadrant(int x, int y, uint16_t &quadrant, uint16_t &qx, uint16_t &qy){
	for (int q = 0; q < W6_QUADRANT_COUNT; q++){
		if (W6_PointInQuadrant(x, y, q)){
			uint16_t mapdataOfs;
			mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);

			uint8_t qsx,qsy;
			mem_readb_checked(amw6_dataseg_addr + mapdataOfs + q + 0x1E0, &qsx);
			mem_readb_checked(amw6_dataseg_addr + mapdataOfs + q + 0x1EC, &qsy);


			quadrant = q;
			qx = x - qsx;
			qy = y - qsy;

			return true;
		}
	}
	return false;
}

bool W6_TopIsVisible(uint16_t x, uint16_t y){
	uint16_t tq,tqx,tqy;
	if (!W6_AbsToQuadrant(x,y,tq,tqx,tqy)) return false;

	uint16_t mapdataOfs;
	mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);
	mapdataOfs += 0x60;//horizontal walls map
	uint16_t q = W6_GetBitArrayElement(mapdataOfs,tq*64 + tqy*8 + tqx, 2);
	return (q<2);
}

bool W6_BottomIsVisible(uint16_t x, uint16_t y){
	return W6_TopIsVisible(x,y-1);
}

bool W6_RightIsVisible(uint16_t x, uint16_t y){
	uint16_t tq,tqx,tqy;
	if (!W6_AbsToQuadrant(x,y,tq,tqx,tqy)) return false;

	uint16_t mapdataOfs;
	mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);
	mapdataOfs += 0x120;//vertical walls map
	uint16_t q = W6_GetBitArrayElement(mapdataOfs,tq*64 + tqy*8 + tqx, 2);
	return (q<2);
}

bool W6_LeftIsVisible(uint16_t x, uint16_t y){
	return W6_RightIsVisible(x-1,y);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////        CACHE                                                      
////////////////////////////////////////////////////////////////////////////////////////////////////

uint16_t W6C_GetBitArrayElement(uint8_t *pArray, uint16_t index, uint16_t bitsPerElement){
	uint16_t ret;
	uint16_t elemOfs = index * bitsPerElement;
	uint16_t elemShift = elemOfs % 8;
	uint16_t elemMask = 0xFFFF;
	elemMask >>= 16 - bitsPerElement;
	elemOfs /= 8;
	ret = *((uint16_t*)(pArray + elemOfs));
	ret >>= elemShift;
	ret &= elemMask;
	return ret;
}

bool W6C_TestBitArray(uint8_t *pArray, uint16_t index){
	uint16_t ret;
	uint16_t elemOfs = index;
	uint16_t elemShift = elemOfs % 8;
	uint16_t elemMask = 1;
	elemMask <<= elemShift;
	elemOfs /= 8;
	ret = *((uint16_t*)(pArray + elemOfs));
	return ((ret & elemMask)!=0);
}

bool W6C_PointInQuadrant(uint16_t x, uint16_t y, uint16_t quadrant){
	uint8_t qsx,qsy;
	qsx = amw6_cache_qsx[amw6_level][quadrant];
	qsy = amw6_cache_qsy[amw6_level][quadrant];
	if ((x>=qsx) && (y>=qsy) && (x<=qsx+7) && (y<=qsy+7))
		return true;
	return false;
}

bool W6C_PointAtLeftSideOfQuadrant(uint16_t x, uint16_t y, uint16_t quadrant){
	uint8_t qsx,qsy;
	qsx = amw6_cache_qsx[amw6_level][quadrant];
	qsy = amw6_cache_qsy[amw6_level][quadrant];
	if ((x==qsx) && (y>=qsy) && (y<=qsy+7))
		return true;
	return false;
}

bool W6C_PointAtBottomSideOfQuadrant(uint16_t x, uint16_t y, uint16_t quadrant){
	uint8_t qsx,qsy;
	qsx = amw6_cache_qsx[amw6_level][quadrant];
	qsy = amw6_cache_qsy[amw6_level][quadrant];
	if ((x>=qsx) && (x<=qsx+7) && (y==qsy+7))
		return true;
	return false;
}

bool W6C_PointInAnyQuadrant(uint16_t x, uint16_t y){
	for (int q = 0; q < W6_QUADRANT_COUNT; q++){
		if (W6C_PointInQuadrant(x, y, q))
			return true;
	}
	return false;
}

bool W6C_PointAtLeftSideOfAnyQuadrant(uint16_t x, uint16_t y){
	for (int q = 0; q < W6_QUADRANT_COUNT; q++){
		if (W6C_PointAtLeftSideOfQuadrant(x, y, q))
			return true;
	}
	return false;
}

bool W6C_PointAtBottomSideOfAnyQuadrant(uint16_t x, uint16_t y){
	for (int q = 0; q < W6_QUADRANT_COUNT; q++){
		if (W6C_PointAtBottomSideOfQuadrant(x, y, q))
			return true;
	}
	return false;
}

bool W6C_AbsToQuadrant(int x, int y, uint16_t &quadrant, uint16_t &qx, uint16_t &qy){
	for (int q = 0; q < W6_QUADRANT_COUNT; q++){
		if (W6C_PointInQuadrant(x, y, q)){
			uint8_t qsx,qsy;
			qsx = amw6_cache_qsx[amw6_level][q];
			qsy = amw6_cache_qsy[amw6_level][q];


			quadrant = q;
			qx = x - qsx;
			qy = y - qsy;

			return true;
		}
	}
	return false;
}

bool W6C_TopIsVisible(uint16_t x, uint16_t y){
	uint16_t tq,tqx,tqy;
	if (!W6C_AbsToQuadrant(x,y,tq,tqx,tqy)) return false;

	uint16_t q = W6C_GetBitArrayElement(amw6_cache_hwalls[amw6_level],tq*64 + tqy*8 + tqx, 2);
	return (q<2);
}

bool W6C_BottomIsVisible(uint16_t x, uint16_t y){
	return W6C_TopIsVisible(x,y-1);
}

bool W6C_RightIsVisible(uint16_t x, uint16_t y){
	uint16_t tq,tqx,tqy;
	if (!W6C_AbsToQuadrant(x,y,tq,tqx,tqy)) return false;


	uint16_t q = W6C_GetBitArrayElement(amw6_cache_vwalls[amw6_level],tq*64 + tqy*8 + tqx, 2);
	return (q<2);
}

bool W6C_LeftIsVisible(uint16_t x, uint16_t y){
	return W6C_RightIsVisible(x-1,y);
}

bool W6C_IsDarkZone(uint16_t quadrant, uint16_t qx, uint16_t qy){
	uint16_t f = W6C_GetBitArrayElement(&amw6_cache_features[amw6_level][0],quadrant*64 + qy*8 + qx,4);
	uint16_t q = W6C_GetBitArrayElement(amw6_cache_floor[amw6_level],quadrant*64 + qy*8 + qx,1);
	bool isDarkZone =  (!((q==0) && (f!=14 /*Pit*/))) && ((amw6_level==12) || (amw6_level==5));
	return isDarkZone && amw6_hide_in_dark_zones;
}

int W6_FindNote(uint16_t quadrant, uint16_t qx, uint16_t qy){
	for (int i = 0; i < (int)amw6_notes[amw6_level].size(); i++){
		if ((amw6_notes[amw6_level][i].quadrant==quadrant) && 
			(amw6_notes[amw6_level][i].qX==qx) &&
			(amw6_notes[amw6_level][i].qY==qy))
			return i;
	}
	return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////
/// Rendering
/////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////
//	amw6_dataseg_addr + 0x4F9C = Current Quadrant
//	amw6_dataseg_addr + 0x4FA0 = Current Quadrant X
//	amw6_dataseg_addr + 0x4F9E = Current Quadrant Y
//	amw6_dataseg_addr + 0x363C = Current Level Index
//  amw6_dataseg_addr + 0x4EE8 = quadIndex0 * W6_QUADRANT_COUNT + quadIndex1
//////////////////////////////////////////////////////
//  amw6_dataseg_addr + 0x4FA8    =	QMAP DATA OFFSET
//  qmapdataOfs + 0x0 =   ?? 16 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]
//  qmapdataOfs + 0x120 = ?? 16 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]
//  qmapdataOfs + 0x240 = ?? 16 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]    //Bit Packed: quadrantIndex, qX, qY
//  qmapdataOfs + 0x360 = ?? 8 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]
//  qmapdataOfs + 0x3F0 = ?? 8 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]
//  qmapdataOfs + 0x480 = ?? 8 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]
//  qmapdataOfs + 0x510 = ?? 8 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]
//  qmapdataOfs + 0x5A0 = ?? 8 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]
//  qmapdataOfs + 0x630 = ?? 8 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]
//  qmapdataOfs + 0x6C0 = ?? 8 bit array[W6_QUADRANT_COUNT]
//////////////////////////////////////////////////////
//  amw6_dataseg_addr + 0x4FAA    = MAP DATA OFFSET
//  mapdataOfs + 0x60  = horizontal walls map
//  mapdataOfs + 0x120 = vertical walls map
//  mapdataOfs + 0x1E0 = quadrant start X
//  mapdataOfs + 0x1EC = quadrant start Y
//  mapdataOfs + 0x1F8 = "feature" map
//  mapdataOfs + 0x378 = "feature direction" map
//  mapdataOfs + 0x43A = floor map
//  mapdataOfs + 0x49A = roof map
//  mapdataOfs + 0x4FA = ?? 3 bit array
//  mapdataOfs + 0x512 = ?? 3 bit array
//  mapdataOfs + 0x52A = ?? 4 bit array[W6_QUADRANT_COUNT]
//  mapdataOfs + 0x536 = ?? 8 bit array[W6_QUADRANT_COUNT]
//////////////////////////////////////////////////////

void W6_DrawQuadrantFirstPass(int x, int y, uint16_t quadrant){

	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			if (amw6_visdata[amw6_level][quadrant][j][i]==0) continue;
			uint16_t f = W6C_GetBitArrayElement(&amw6_cache_features[amw6_level][0],quadrant*64 + i*8 + j,4);
			uint16_t q = W6C_GetBitArrayElement(amw6_cache_floor[amw6_level],quadrant*64 + i*8 + j,1);
			uint16_t qx =  W6_SQUARE_SIZE*j;
			uint16_t qy = (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*i;
			if ((q==0) && (f!=14 /*Pit*/))
				W6_DrawDarkTile(x + qx+1,y + qy+1,W6_SQUARE_SIZE-1,W6_SQUARE_SIZE-1,(amw6_visdata[amw6_level][quadrant][j][i]!=1));
		}
	}
	//water map = floor map & roof map
	if ((amw6_level==8) || (amw6_level==10) || (amw6_level==12))
	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			if (amw6_visdata[amw6_level][quadrant][j][i]==0) continue;
			uint16_t r = W6C_GetBitArrayElement(amw6_cache_roof[amw6_level],quadrant*64 + i*8 + j,1);
			uint16_t q = W6C_GetBitArrayElement(amw6_cache_floor[amw6_level],quadrant*64 + i*8 + j,1);
			uint16_t qx =  W6_SQUARE_SIZE*j;
			uint16_t qy = (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*i;
			if ((q!=0) && ((r!=0) || (amw6_level==8)))
				W6_DrawWaterTile(x + qx+1,y + qy+1,W6_SQUARE_SIZE-1,W6_SQUARE_SIZE-1,(amw6_visdata[amw6_level][quadrant][j][i]!=1));
		}
	}
}

void W6_DrawQuadrantSecondPass(int x, int y, uint16_t quadrant){
	uint8_t cqsx = amw6_cache_qsx[amw6_level][quadrant];
	uint8_t cqsy = amw6_cache_qsy[amw6_level][quadrant];

	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			uint16_t tq,tqx,tqy;
			bool v = false;
			bool d = (amw6_visdata[amw6_level][quadrant][j][i]!=1);
			if (W6C_AbsToQuadrant(cqsx + j + 1,cqsy + i,tq,tqx,tqy)){
				v = (amw6_visdata[amw6_level][tq][tqx][tqy]!=0);
				d = ((amw6_visdata[amw6_level][tq][tqx][tqy]!=1) && d);
			}
			if ((!v) && amw6_visdata[amw6_level][quadrant][j][i]==0) continue;
			uint16_t q = W6C_GetBitArrayElement(amw6_cache_vwalls[amw6_level],quadrant*64 + i*8 + j,2);
			//q:
			//   0 - empty (no wall)
			//   1 - passage 
			//   2 - solid wall
			//   3 - horizontal wood door
			uint16_t qx =  W6_SQUARE_SIZE + W6_SQUARE_SIZE*j;
			uint16_t qy = (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*i;
			switch (q){
			case 0://skip
				break;
			case 1:	W6_DrawVPassage(x + qx,y + qy,3,W6_SQUARE_SIZE+2,d);
				break;
			case 2:
				{
					if ((W6C_GetBitArrayElement(amw6_cache_features[amw6_level],quadrant*64 + i*8 + j,4)==7) && (W6C_GetBitArrayElement(amw6_cache_features_dirs[amw6_level],quadrant*64 + i*8 + j,2)==1))
						W6_DrawVPortcullis(x + qx, y + qy,3,W6_SQUARE_SIZE+2,d);
					else
						W6_DrawVWall(x + qx, y + qy,3,W6_SQUARE_SIZE+2,d);
				}
				break;
			case 3: W6_DrawVDoor(x + qx, y + qy,3,W6_SQUARE_SIZE+2,d);
				break;
			};
		}
	}

	for (int i = 0; i < 8; i++){
		if (amw6_visdata[amw6_level][quadrant][0][i]!=0)
		if (!W6C_PointAtLeftSideOfAnyQuadrant(cqsx-8,cqsy+i)){
			uint16_t qy = (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*i;
			W6_DrawVWall(x, y + qy,3,W6_SQUARE_SIZE+2,(amw6_visdata[amw6_level][quadrant][0][i]!=1));
		}

		if (amw6_visdata[amw6_level][quadrant][i][0]!=0)
		if (!W6C_PointInAnyQuadrant(cqsx+i,cqsy-1)){
			uint16_t qx = W6_SQUARE_SIZE*i;
			W6_DrawHWall(x + qx, y + W6_SQUARE_SIZE*8,W6_SQUARE_SIZE+2,3,(amw6_visdata[amw6_level][quadrant][i][0]!=1));
		}
	}

	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			uint16_t tq,tqx,tqy;
			bool v = false;
			bool d = (amw6_visdata[amw6_level][quadrant][j][i]!=1);
			if (W6C_AbsToQuadrant(cqsx + j,cqsy + i + 1,tq,tqx,tqy)){
				v = (amw6_visdata[amw6_level][tq][tqx][tqy]!=0);
				d = ((amw6_visdata[amw6_level][tq][tqx][tqy]!=1) && d);
			}
			if ((!v) && (amw6_visdata[amw6_level][quadrant][j][i]==0)) continue;
			uint16_t q = W6C_GetBitArrayElement(amw6_cache_hwalls[amw6_level],quadrant*64 + i*8 + j,2);
			//q:
			//   0 - empty (no wall)
			//   1 - passage
			//   2 - solid wall
			//   3 - horizontal wood door
			uint16_t qx =  W6_SQUARE_SIZE*j;
			uint16_t qy = (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*i;
			switch (q){
			case 0://skip
				break;
			case 1:	W6_DrawHPassage(x + qx,y + qy,W6_SQUARE_SIZE+2,3,d);
				break;
			case 2: 
				{
					if ((W6C_GetBitArrayElement(amw6_cache_features[amw6_level],quadrant*64 + i*8 + j,4)==7) && (W6C_GetBitArrayElement(amw6_cache_features_dirs[amw6_level],quadrant*64 + i*8 + j,2)==0))
						W6_DrawHPortcullis(x + qx, y + qy,W6_SQUARE_SIZE+2,3,d);
					else
						W6_DrawHWall(x + qx, y + qy,W6_SQUARE_SIZE+2,3,d);
				}
				break;
			case 3: W6_DrawHDoor(x + qx, y + qy,W6_SQUARE_SIZE+2,3,d);
				break;
			};
		}
	}

}

void W6_DrawQuadrantThirdPass(int x, int y, uint16_t quadrant){
	//stairs
	static const int direction[4][2] = {
		{0,-W6_SQUARE_SIZE},
		{W6_SQUARE_SIZE,0},
		{0,W6_SQUARE_SIZE},
		{-W6_SQUARE_SIZE,0},
	};
	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			if (amw6_visdata[amw6_level][quadrant][j][i]==0) continue;
			uint16_t q = W6C_GetBitArrayElement(amw6_cache_features[amw6_level],quadrant*64 + i*8 + j,4);
			uint16_t d = W6C_GetBitArrayElement(amw6_cache_features_dirs[amw6_level],quadrant*64 + i*8 + j,2);
			uint16_t qx =  W6_SQUARE_SIZE*j;
			uint16_t qy = (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*i;
			switch (q){
			case 1: W6_DrawStairsUp(x + qx+3 + direction[d][0], y + qy+3 + direction[d][1],W6_SQUARE_SIZE-3,W6_SQUARE_SIZE-3,(amw6_visdata[amw6_level][quadrant][j][i]!=1)); break;
			case 2: W6_DrawStairsDown(x + qx+3 + direction[d][0], y + qy+3 + direction[d][1],W6_SQUARE_SIZE-3,W6_SQUARE_SIZE-3,(amw6_visdata[amw6_level][quadrant][j][i]!=1)); break;
			};
		}
	}

	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			if (amw6_visdata[amw6_level][quadrant][j][i]==0) continue;
			uint16_t q = W6C_GetBitArrayElement(&amw6_cache_features[amw6_level][0],quadrant*64 + i*8 + j,4);
			(void)W6C_GetBitArrayElement(&amw6_cache_features_dirs[amw6_level][0],quadrant*64 + i*8 + j,2);
			uint16_t qx =  W6_SQUARE_SIZE*j;
			uint16_t qy = (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*i;
			switch (q){
			//Fountain
			case 4: W6_DrawFountainUp(x + qx+3, y + qy+3,W6_SQUARE_SIZE-5,W6_SQUARE_SIZE-5,(amw6_visdata[amw6_level][quadrant][j][i]!=1)); break;
			//Pit Down
			//case 14: AM_DrawRect(x + qx, y + qy,W6_SQUARE_SIZE,W6_SQUARE_SIZE,0); break;
			};
		}
	}
}

void W6_DrawQuadrantFourthPass(int x, int y, uint16_t quadrant, uint16_t qX, uint16_t qY, bool showMark){
	
	for (int i = 0; i < (int)amw6_notes[amw6_level].size(); i++){
		if (amw6_notes[amw6_level][i].quadrant==quadrant){
			uint16_t qx = x + W6_SQUARE_SIZE*amw6_notes[amw6_level][i].qX;
			uint16_t qy = y + (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*amw6_notes[amw6_level][i].qY;
			float w = W6_SQUARE_SIZE-6;
			float h = W6_SQUARE_SIZE-7;

						{ /* note border */
				uint32_t cl = amw6_notes[amw6_level][i].color;
				extern SDL_Renderer* AM_GetRenderer();
				SDL_Renderer* ren = AM_GetRenderer();
				if (ren) {
					SDL_SetRenderDrawColor(ren,
					                       cl & 0xFF,
					                       (cl >> 8) & 0xFF,
					                       (cl >> 16) & 0xFF,
					                       255);
					SDL_Rect border = {(int)(qx + 4),
					                   (int)(qy + 4),
					                   (int)w,
					                   (int)h};
					SDL_RenderDrawRect(ren, &border);
				}
			}
		}
	}

	uint16_t amw6level = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x363C, &amw6level);

	if ((showMark) && (amw6level==amw6_level) && (!W6C_IsDarkZone(quadrant,qX,qY))){
		uint16_t mdir = 0;
		mem_readw_checked(amw6_dataseg_addr + 0x4F9A, &mdir);
		uint16_t qx = W6_SQUARE_SIZE*qX;
		uint16_t qy = (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*qY;
		uint16_t tsize = W6_SQUARE_SIZE;
		switch (mdir){
			case 0: W6_DrawCursorUp(x+qx,y+qy,tsize,tsize); break;
			case 1: W6_DrawCursorRight(x+qx,y+qy,tsize,tsize); break;
			case 2: W6_DrawCursorDown(x+qx,y+qy,tsize,tsize); break;
			case 3: W6_DrawCursorLeft(x+qx,y+qy,tsize,tsize); break;
			default: W6_DrawCursorDown(x+qx,y+qy,tsize,tsize); break;
		};
//		AM_DrawRect(x+qx+(W6_SQUARE_SIZE/4),y+qy+(W6_SQUARE_SIZE/4),tsize,tsize,-1);
	}
}

int map_draw_x = 0;
int map_draw_y = 0;
int map_scroll_x = 0;
int map_scroll_y = 0;
bool amW6inGame = false;

void W6_Update(int XSize, int YSize){
	(void)XSize; (void)YSize;
	uint16_t quadrant = 0;
	uint16_t qx = 0;
	uint16_t qy = 0;
	uint16_t amw6level = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x4F9C, &quadrant);
	mem_readw_checked(amw6_dataseg_addr + 0x4FA0, &qx);
	mem_readw_checked(amw6_dataseg_addr + 0x4F9E, &qy);
	mem_readw_checked(amw6_dataseg_addr + 0x363C, &amw6level);
	//----------------Detect in-game cond---------------------
	uint16_t W6GameState = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x363A, &W6GameState);
	amW6inGame = (W6GameState == 5) || ((W6GameState>=10) && (W6GameState<=14)) || (W6GameState==19) || (W6GameState==20) || (W6GameState==22) || (W6GameState==24);
	if (amW6inGame){
		if (amw6level>=16){
			amW6inGame = false;
		} 
	}

	// Blank window when not in game
	if (!amW6inGame) {
		SetAutomapWindowTitle("Wizardry 6 - Automap Mod");
		return;  // BeginFrame already cleared to black, EndFrame will present
	}

	// Periodically save visdata and notes every ~5 seconds
	static int s_saveTimer = 0;
	if (++s_saveTimer >= 300) {
		s_saveTimer = 0;
		W6_NativeSaveVis();
		W6_NativeSaveNotes();
	}

	//-----------------------------------------------------------
	uint16_t mdir = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x4F9A, &mdir);
	if (amW6inGame){
		static uint16_t oquadrant = 0;
		static uint16_t oqx = 0;
		static uint16_t oqy = 0;
		static uint16_t oamw6level = 0;
		static uint16_t omdir = 0;
		if ((oquadrant != quadrant) ||
			(oqx != qx) ||
			(oqy != qy) ||
			(oamw6level != amw6level) ||
			(omdir != mdir)){
				amw6_level = amw6level;
				//Reset scroll
				map_scroll_x = 0;
				map_scroll_y = 0;
				//update cache
				uint16_t mapdataOfs;
				mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);

				// Guard: don't cache if mapdataOfs not yet initialized
				if (mapdataOfs < 0x100) return;

				MEM_BlockRead(amw6_dataseg_addr + mapdataOfs + 0x1E0, &amw6_cache_qsx[amw6level][0], sizeof(amw6_cache_qsx)/W6_LEVEL_COUNT);
				MEM_BlockRead(amw6_dataseg_addr + mapdataOfs + 0x1EC, &amw6_cache_qsy[amw6level][0], sizeof(amw6_cache_qsy)/W6_LEVEL_COUNT);
				MEM_BlockRead(amw6_dataseg_addr + mapdataOfs + 0x60, &amw6_cache_hwalls[amw6level][0], sizeof(amw6_cache_hwalls)/W6_LEVEL_COUNT);
				MEM_BlockRead(amw6_dataseg_addr + mapdataOfs + 0x120, &amw6_cache_vwalls[amw6level][0], sizeof(amw6_cache_vwalls)/W6_LEVEL_COUNT);
				MEM_BlockRead(amw6_dataseg_addr + mapdataOfs + 0x1F8, &amw6_cache_features[amw6level][0], sizeof(amw6_cache_features)/W6_LEVEL_COUNT);
				MEM_BlockRead(amw6_dataseg_addr + mapdataOfs + 0x378, &amw6_cache_features_dirs[amw6level][0], sizeof(amw6_cache_features_dirs)/W6_LEVEL_COUNT);
				MEM_BlockRead(amw6_dataseg_addr + mapdataOfs + 0x43A, &amw6_cache_floor[amw6level][0], sizeof(amw6_cache_floor)/W6_LEVEL_COUNT);
				MEM_BlockRead(amw6_dataseg_addr + mapdataOfs + 0x49A, &amw6_cache_roof[amw6level][0], sizeof(amw6_cache_roof)/W6_LEVEL_COUNT);
		}

		oquadrant = quadrant;
		oqx = qx;
		oqy = qy;
		oamw6level = amw6level;
		omdir = mdir;
	}
	if (amW6inGame)
	if (amw6level==amw6_level) 
	if ((quadrant<W6_QUADRANT_COUNT) && (qx<8) && (qy<8)){
		

		if (!W6C_IsDarkZone(quadrant,qx,qy)) {
			if (amw6_visdata[amw6_level][quadrant][qx][qy] == 0) {
					}
			amw6_visdata[amw6_level][quadrant][qx][qy] = 1;
		}

		uint16_t mapdataOfs;
		mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);

		uint8_t qsx,qsy;
		mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1E0, &qsx);
		mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1EC, &qsy);

		uint16_t hvq,hvqx,hvqy;
		if ((W6_AbsToQuadrant(qsx+qx,  qsy+qy-1,hvq,hvqx,hvqy)) && (amw6_visdata[amw6_level][hvq][hvqx][hvqy]==0)){
			if (W6_BottomIsVisible(qsx+qx,  qsy+qy))
				if (!W6C_IsDarkZone(hvq,hvqx,hvqy))
					amw6_visdata[amw6_level][hvq][hvqx][hvqy] = 2;
		}
		if ((W6_AbsToQuadrant(qsx+qx-1,qsy+qy,hvq,hvqx,hvqy)) && (amw6_visdata[amw6_level][hvq][hvqx][hvqy]==0)){
			if (W6_LeftIsVisible(qsx+qx,  qsy+qy))
				if (!W6C_IsDarkZone(hvq,hvqx,hvqy))
					amw6_visdata[amw6_level][hvq][hvqx][hvqy] = 2;
		}
		if ((W6_AbsToQuadrant(qsx+qx+1,qsy+qy,hvq,hvqx,hvqy)) && (amw6_visdata[amw6_level][hvq][hvqx][hvqy]==0)){
			if (W6_RightIsVisible(qsx+qx,  qsy+qy))
				if (!W6C_IsDarkZone(hvq,hvqx,hvqy))
					amw6_visdata[amw6_level][hvq][hvqx][hvqy] = 2;
		}
		if ((W6_AbsToQuadrant(qsx+qx,  qsy+qy+1,hvq,hvqx,hvqy)) && (amw6_visdata[amw6_level][hvq][hvqx][hvqy]==0)){
			if (W6_TopIsVisible(qsx+qx,  qsy+qy))
				if (!W6C_IsDarkZone(hvq,hvqx,hvqy))
					amw6_visdata[amw6_level][hvq][hvqx][hvqy] = 2;	
		}
	}

	SetAutomapWindowTitle("Wizardry 6 - Automap Mod");


					// SDL_Renderer: clear handled by am_main.cpp BeginFrame()
//		glEnable(GL_BLEND); 
			//		glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

					//AM_DrawRect(5,5,25,25);


					
//					AM_DrawW6Sprite(5,50,256,16,0,0,1,1);
				
	uint16_t mapdataOfs;
	mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);

	uint8_t tlcqx,tlcqy;
	mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1E0, &tlcqx);
	mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1EC, &tlcqy);

	map_draw_x = -(((tlcqx+qx)*W6_SQUARE_SIZE) - ((SCREENW/2)-(W6_SQUARE_SIZE/2)));
	map_draw_y = -(((255+7)*W6_SQUARE_SIZE - (tlcqy+qy)*W6_SQUARE_SIZE) - ((SCREENH/2)-(W6_SQUARE_SIZE/2)));
	
	if (amW6inGame){
		if (!W6C_IsDarkZone(quadrant,qx,qy)){
			for (int q = 0; q < W6_QUADRANT_COUNT; q++){
				uint8_t tlqx,tlqy;
				tlqx = amw6_cache_qsx[amw6_level][q];
				tlqy = amw6_cache_qsy[amw6_level][q];
				W6_DrawQuadrantFirstPass(map_draw_x + map_scroll_x + tlqx*W6_SQUARE_SIZE, map_draw_y + map_scroll_y+ 255*W6_SQUARE_SIZE - tlqy*W6_SQUARE_SIZE, q);
			}

			for (int q = 0; q < W6_QUADRANT_COUNT; q++){
				uint8_t tlqx,tlqy;
				tlqx = amw6_cache_qsx[amw6_level][q];
				tlqy = amw6_cache_qsy[amw6_level][q];
				W6_DrawQuadrantSecondPass(map_draw_x + map_scroll_x + tlqx*W6_SQUARE_SIZE, map_draw_y + map_scroll_y + 255*W6_SQUARE_SIZE - tlqy*W6_SQUARE_SIZE, q);
			}

			for (int q = 0; q < W6_QUADRANT_COUNT; q++){
				uint8_t tlqx,tlqy;
				tlqx = amw6_cache_qsx[amw6_level][q];
				tlqy = amw6_cache_qsy[amw6_level][q];
				W6_DrawQuadrantThirdPass(map_draw_x + map_scroll_x + tlqx*W6_SQUARE_SIZE, map_draw_y + map_scroll_y + 255*W6_SQUARE_SIZE - tlqy*W6_SQUARE_SIZE, q);
			}

			for (int q = 0; q < W6_QUADRANT_COUNT; q++){
				uint8_t tlqx,tlqy;
				tlqx = amw6_cache_qsx[amw6_level][q];
				tlqy = amw6_cache_qsy[amw6_level][q];
				W6_DrawQuadrantFourthPass(map_draw_x + map_scroll_x + tlqx*W6_SQUARE_SIZE, map_draw_y + map_scroll_y + 255*W6_SQUARE_SIZE - tlqy*W6_SQUARE_SIZE, q, qx, qy, (quadrant==q));
			}

			uint16_t amw6level = 0;
			mem_readw_checked(amw6_dataseg_addr + 0x363C, &amw6level);
			if (amw6_jLevel==amw6_level){
				uint16_t qx = map_draw_x + map_scroll_x + amw6_jX*W6_SQUARE_SIZE;
				uint16_t qy = map_draw_y + map_scroll_y+ (255+7)*W6_SQUARE_SIZE - amw6_jY*W6_SQUARE_SIZE;
				float w = W6_SQUARE_SIZE-3;
				float h = W6_SQUARE_SIZE-3;

			{ /* highlight border */
					extern SDL_Renderer* AM_GetRenderer();
					SDL_Renderer* ren = AM_GetRenderer();
					if (ren) {
						SDL_SetRenderDrawColor(
						        ren, 250, 159, 31, 255); // pumpkin orange
						SDL_Rect border = {(int)(qx + 3),
						                   (int)(qy + 3),
						                   (int)w,
						                   (int)h};
						SDL_RenderDrawRect(ren, &border);
					}
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// User Input
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool MousePositionToQuadrantPosition(int mx, int my, uint16_t &quadrant, uint16_t &qx, uint16_t &qy){
	int selAbsX = ((-map_draw_x-map_scroll_x) + mx) / W6_SQUARE_SIZE;
	int selAbsY = ((-map_draw_y-map_scroll_y) + my) / W6_SQUARE_SIZE;
	selAbsY -= 7;
	selAbsY = 255 - selAbsY; 
	if (selAbsY < 0)
		selAbsY = 0;

	return W6C_AbsToQuadrant(selAbsX,selAbsY,quadrant,qx,qy);
}

void TooltipForMainWindow_Show(bool show);
void TooltipForMainWindow_SetText(wchar_t *text);

void W6_OnMouseMotionInMainWindow(RECT rc, int newX, int newY){
	if (!amw6_show_tooltips){
		TooltipForMainWindow_Show(false);
		return;
	}
	uint16_t W6GameState = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x363A, &W6GameState);
	uint16_t TE = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x503E, &TE);

	uint16_t amw6level = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x363C, &amw6level);
	//----------------Detect in-game cond---------------------
	amW6inGame = (W6GameState == 5) || ((W6GameState>=10) && (W6GameState<=14)) || (W6GameState==19) || (W6GameState==20) || (W6GameState==22) || (W6GameState==24);
	if (amW6inGame){
		if (amw6level>=16){
			amW6inGame = false;
		} 
	}
	
	if (!amW6inGame || (W6GameState==5 && TE!=0 && TE!=1)){
		TooltipForMainWindow_Show(false);
		return;
	}
#define PY (78.0f / 420.0f)
#define PWIDTH (145.0f / 640.0f)
#define PHEIGHT (67.0f / 420.0f)
	int psx = rc.right - (int)(rc.right * PWIDTH);
	int psy = (int)(rc.bottom * PY);
	int pw = (int)(rc.right * PWIDTH);
	int ph = (int)(rc.bottom * PHEIGHT);

	static const char *magicCats[6] = {
		"Fire",
		"Water",
		"Air",
		"Earth",
		"Mental",
		"Magic",
	};
	static char *neffects[10] = {
		"Silenced",
		"Frightened",
		"Falls asleep",
		"Paralyzed",
		"Blinded",
		"Poisoned",
		"Diseased",
		"Veggified!",
		"Nauseous",
		"Cursed",
	};
	int num = -1;
	int nEnemy = -1;
	if ((newY>psy) && (newY<psy+ph*3)){
		num = ((newY-psy) / ph);
		if (num>2) num = 2;
		if (newX<pw){
			num *= 2;
		} else
		if (newX>psx){
			num = num * 2 + 1;
		} else
			num = -1;
	}

	uint16_t chaCount = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x43CE, &chaCount);

	static wchar_t char_tip[1024*10];
	char_tip[0] = 0;
	if ((num>=0) && (num<chaCount)){
		//num++;
		char name[8];
		uint16_t cofs = 0x43E8 + num*0x1B0;
		MEM_BlockRead(amw6_dataseg_addr + cofs, &name[0], 8);
		uint16_t chp,mhp;
		mem_readw_checked(amw6_dataseg_addr + cofs + 0x18,&chp);//Current HP
		mem_readw_checked(amw6_dataseg_addr + cofs + 0x1A,&mhp);//Maximum HP
		name[7] = 0;
		swprintf(char_tip,L"Name: %s\r\nHP: %d/%d",&name[0],chp,mhp);
		bool fc = true;
		for (int i = 0; i < 6; i++){
			uint16_t cmp,mmp;
			mem_readw_checked(amw6_dataseg_addr + cofs + 0x28 + i*4,&cmp);//Current MP
			mem_readw_checked(amw6_dataseg_addr + cofs + 0x28 + i*4 + 2,&mmp);//Maximum MP
			if (mmp>0){
				if (fc){
					swprintf(&char_tip[wcslen(char_tip)],L"\r\nMana points:");
					fc = false;
				}
				swprintf(&char_tip[wcslen(char_tip)],L"\r\n  %s %d/%d",&magicCats[i][0],cmp,mmp);
			}
		}

		fc = true;
		for (int i = 0; i < 10; i++){
			uint8_t d;
			mem_readb_checked(amw6_dataseg_addr + cofs + 0x122 + i,&d);
			if (d>0){
				if (fc){
					swprintf(&char_tip[wcslen(char_tip)],L"\r\nNegative effects:");
					fc = false;
				}
				swprintf(&char_tip[wcslen(char_tip)],L"\r\n  %s(%d rounds)",&neffects[i][0],d);
			}
		}

		uint16_t SubScreenState = 0;
		mem_readw_checked(amw6_dataseg_addr + 0x3598, &SubScreenState);

		if ((W6GameState>=11) && (W6GameState<=14)) {

		uint8_t d;
		uint16_t pGroup;
		mem_readw_checked(amw6_dataseg_addr + 0x43A8, &pGroup);
		mem_readb_checked(amw6_dataseg_addr + pGroup + 0x2C*num + 0x29,&d);
		if ((d & 0x80)!=0){
			if (fc){
				swprintf(&char_tip[wcslen(char_tip)],L"\r\nNegative effects:");
				fc = false;
			}
			swprintf(&char_tip[wcslen(char_tip)],L"\r\n  Slowed");
		}

		mem_readb_checked(amw6_dataseg_addr + pGroup + 0x2C*num + 0x28,&d);
		if ((d & 0x80)!=0){
			if (fc){
				swprintf(&char_tip[wcslen(char_tip)],L"\r\nNegative effects:");
				fc = false;
			}
			swprintf(&char_tip[wcslen(char_tip)],L"\r\n  Weakened");
		}

		//Positive effects
		fc = true;
		mem_readb_checked(amw6_dataseg_addr + pGroup + 0x2C*num + 0x29,&d);
		if (((d & 0x80)==0) && (d!=0)){
			if (fc){
				swprintf(&char_tip[wcslen(char_tip)],L"\r\nPositive effects:");
				fc = false;
			}
			swprintf(&char_tip[wcslen(char_tip)],L"\r\n  Hasted");
		}

		}
	} else {
#define EWIDTH (320.0f / 640.0f)
#define EY (316.0f / 420.0f)
#define EHEIGHT ((420.0f-305.0f-22.0f) / 420.0f)
		int esx = rc.right - (int)(rc.right * EWIDTH);
		int esy = (int)(rc.bottom * EY);
		int ew = (int)(rc.right * EWIDTH);
		int eh = (int)(rc.bottom * EHEIGHT);

		uint16_t SpellScreenState = 0;
		mem_readw_checked(amw6_dataseg_addr + 0x5016, &SpellScreenState);

		uint16_t SubScreenState = 0;
		mem_readw_checked(amw6_dataseg_addr + 0x3598, &SubScreenState);

		if ((W6GameState==12) && (SubScreenState!=5398/*use*/) && (SubScreenState!=4865/*spell*/) && (SubScreenState!=5391/*spell*/))     ///(SpellScreenState!=0) && (SpellScreenState!=1))
		if ((newX>esx) && (newX<esx+ew))
		if ((newY>esy) && (newY<esy+eh)){
			nEnemy = ((newY-esy) / (eh/5));
			if (nEnemy>4) nEnemy = 4;
			uint16_t gtAddr = 0x43B8 + nEnemy*2;//Template
			uint16_t pTemplate = 0;
			uint16_t gAddr = 0x43AA + nEnemy*2;
			uint16_t pGroup = 0;
			uint8_t eCount = 0;
			mem_readw_checked(amw6_dataseg_addr + gtAddr, &pTemplate);
			mem_readw_checked(amw6_dataseg_addr + gAddr, &pGroup);
			mem_readb_checked(amw6_dataseg_addr + pGroup + 0x19D,&eCount);

			uint8_t dn = 0;
			mem_readb_checked(amw6_dataseg_addr + pGroup + 0x19C, &dn);//0x19C: 0=known monster; 1=unknown monster     ???

			/* Groub Member Array [gAddr]
			       0x0       word        Level
				   0x2       word        Current HP
				   0x4       word        Maximum HP

				   0xE                              ?? Silenced ??

				   0x19D     byte		 Count of members
				   0x19E     byte        ?? Count of ready to fight members ??
			*/

			if (eCount>0){
				int nSubEnemy = (newX-esx) / (ew / eCount);
				uint16_t curHP = 0, maxHP = 0, curStamina = 0, maxStamina = 0;
				char ename[16];
				MEM_BlockRead(amw6_dataseg_addr + pTemplate + 32*dn, &ename[0], 16);
				mem_readw_checked(amw6_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x2,&curHP);
				mem_readw_checked(amw6_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x4,&maxHP);
				mem_readw_checked(amw6_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x6,&curStamina);
				mem_readw_checked(amw6_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x8,&maxStamina);

				char_tip[0] = 0;
				if (eCount>1){
					swprintf(&char_tip[wcslen(char_tip)],L"%s[%d/%d]",&ename[0],nSubEnemy+1,eCount);
				} else {
					swprintf(&char_tip[wcslen(char_tip)],L"%s",&ename[0]);
				}
				swprintf(&char_tip[wcslen(char_tip)],L"\r\nHP: %d/%d",curHP,maxHP);

				bool fc = true;
				for (int i = 0; i < 10; i++){
					uint8_t d;
					mem_readb_checked(amw6_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0xE + i,&d);
					if (d>0){
						if (fc){
							swprintf(&char_tip[wcslen(char_tip)],L"\r\nNegative effects:");
							fc = false;
						}
						swprintf(&char_tip[wcslen(char_tip)],L"\r\n  %s(%d rounds)",&neffects[i][0],d);
					}
				}
				uint8_t d;
				mem_readb_checked(amw6_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x29,&d);
				if ((d & 0x80)!=0){
					if (fc){
						swprintf(&char_tip[wcslen(char_tip)],L"\r\nNegative effects:");
						fc = false;
					}
					swprintf(&char_tip[wcslen(char_tip)],L"\r\n  Slowed");
				}

				mem_readb_checked(amw6_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x28,&d);
				if ((d & 0x80)!=0){
					if (fc){
						swprintf(&char_tip[wcslen(char_tip)],L"\r\nNegative effects:");
						fc = false;
					}
					swprintf(&char_tip[wcslen(char_tip)],L"\r\n  Weakened");
				}
			} else
				nEnemy = -1;
		}
	}

	static wchar_t char_tip_backup[1024*10];
	bool tipUpdated = (wcscmp(&char_tip[0],&char_tip_backup[0])!=0);
	if (tipUpdated){
		wcscpy(&char_tip_backup[0],&char_tip[0]);
	}
	if ((((num>=0) && (num<chaCount)) || (nEnemy>=0)) && (tipUpdated))
		TooltipForMainWindow_SetText(&char_tip_backup[0]);
	TooltipForMainWindow_Show(((num>=0) && (num<chaCount)) || (nEnemy>=0));
}

void W6_OnAutomapDrag(int deltaX, int deltaY){
	if (!amW6inGame) return;

	uint16_t quadrant = 0;
	uint16_t qx = 0;
	uint16_t qy = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x4F9C, &quadrant);
	mem_readw_checked(amw6_dataseg_addr + 0x4FA0, &qx);
	mem_readw_checked(amw6_dataseg_addr + 0x4F9E, &qy);

	if (W6C_IsDarkZone(quadrant,qx,qy)){
		return;
	}

	map_scroll_x -= deltaX;
	map_scroll_y -= deltaY;
	if (map_scroll_x<W6_SQUARE_SIZE*(-256))
		map_scroll_x = W6_SQUARE_SIZE*(-256);
	if (map_scroll_x>W6_SQUARE_SIZE*256)
		map_scroll_x = W6_SQUARE_SIZE*256;
	if (map_scroll_y<W6_SQUARE_SIZE*(-256))
		map_scroll_y = W6_SQUARE_SIZE*(-256);
	if (map_scroll_y>W6_SQUARE_SIZE*256)
		map_scroll_y = W6_SQUARE_SIZE*256;
	AutoMapUpdate();
}

void TooltipForAutomapWindow_Show(bool show);
void TooltipForAutomapWindow_SetText(wchar_t *text);

void W6_OnMouseMotionInAutomapWindow(int newX, int newY, bool alt){
	if (!amW6inGame){
		TooltipForAutomapWindow_Show(false);
		return;
	}

	uint16_t quadrant = 0;
	uint16_t qx = 0;
	uint16_t qy = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x4F9C, &quadrant);
	mem_readw_checked(amw6_dataseg_addr + 0x4FA0, &qx);
	mem_readw_checked(amw6_dataseg_addr + 0x4F9E, &qy);

	if (W6C_IsDarkZone(quadrant,qx,qy)){
		TooltipForAutomapWindow_Show(false);
		return;
	}

	uint16_t amw6level = 0; 
	mem_readw_checked(amw6_dataseg_addr + 0x363C, &amw6level);
	if (!alt){
		uint16_t squadrant = 0;
		uint16_t sqx = 0;
		uint16_t sqy = 0;
		bool qexists = MousePositionToQuadrantPosition(newX,newY,squadrant,sqx,sqy);
		bool selc = (qexists && (amw6_level==amw6level) && (squadrant==quadrant) && (sqx==qx) && (sqy==qy));
		int n = (qexists) ? W6_FindNote(squadrant,sqx,sqy) : -1;
        // Update the text.
        wchar_t *text = (selc) ? (wchar_t*)L"Current Position" : (wchar_t*)L"Invisible";// coords;
		if (n>=0)
			text = &amw6_notes[amw6_level][n].str[0];
		TooltipForAutomapWindow_SetText(text);
		TooltipForAutomapWindow_Show(selc || (n>=0));
	}

	if (alt){
		uint16_t squadrant = 0;
		uint16_t sqx = 0;
		uint16_t sqy = 0;
		if (MousePositionToQuadrantPosition(newX,newY,squadrant,sqx,sqy)){
			static wchar_t buf[100];
			swprintf(buf,L"Quadrant: %d qX: %d qY: %d",squadrant,sqx,sqy);
			TooltipForAutomapWindow_SetText(&buf[0]);
			TooltipForAutomapWindow_Show(true);
		} else
			TooltipForAutomapWindow_Show(false);
	}
}

bool InputBox(const wchar_t *title, const wchar_t *hint, wchar_t *buf, int bufSize);
bool ChooseColorDialog(uint32_t &color, uint32_t *palette);

void W6_OnMouseButtonInAutomapWindow(SDL_MouseButtonEvent * button, bool alt, bool ctrl){
	if (!amW6inGame) return;

	uint16_t quadrant = 0;
	uint16_t qx = 0;
	uint16_t qy = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x4F9C, &quadrant);
	mem_readw_checked(amw6_dataseg_addr + 0x4FA0, &qx);
	mem_readw_checked(amw6_dataseg_addr + 0x4F9E, &qy);

	if (W6C_IsDarkZone(quadrant,qx,qy)){
		TooltipForAutomapWindow_Show(false);
		return;
	}

	if (button->button==SDL_BUTTON_MIDDLE){
		uint16_t amw6level = 0; 
		mem_readw_checked(amw6_dataseg_addr + 0x363C, &amw6level);
		amw6_level = amw6level;
		map_scroll_x = 0;
		map_scroll_y = 0;
		AutoMapUpdate();
	}
	uint16_t squadrant = 0;
	uint16_t sqx = 0;
	uint16_t sqy = 0;
	if ((alt) && (!ctrl))
	if (button->clicks!=0)
	if (MousePositionToQuadrantPosition(button->x, button->y, squadrant, sqx, sqy)){
		if ((button->button==SDL_BUTTON_LEFT)){
			char buf[100];
			sprintf(buf,"{%d:%d:%d:%d}",amw6_level,squadrant,sqx,sqy);
			SDL_SetClipboardText(buf);
		}
	}

	if ((!alt) && (ctrl))
	if (button->clicks!=0)
	if (MousePositionToQuadrantPosition(button->x, button->y, squadrant, sqx, sqy)){
		if ((button->button==SDL_BUTTON_LEFT)){
			int n = W6_FindNote(squadrant,sqx,sqy);
			if (n>=0){
				const wchar_t *str = amw6_notes[amw6_level][n].str.c_str();
				int start = -1;
				int end = -1;
				for (int i = 0; i < wcslen(str); i++)
					if (str[i]==L'{'){
						start = i;
						break;
					}

				for (int i = 0; i < wcslen(str); i++)
					if (str[i]==L'}'){
						end = i;
						break;
					}

				if ((start>=0) && (end>start)){
					wchar_t buf[100];
					buf[end-start+1]=0;
					wcsncpy(&buf[0], &str[start], end-start+1);
					int jLevel = 0;
					int jQuadrant = 0;
					int jqX = 0;
					int jqY = 0;
					if (swscanf(&buf[0],L"{%d:%d:%d:%d}",&jLevel,&jQuadrant,&jqX,&jqY)==4){
						if ((jLevel>=0) && (jLevel<W6_LEVEL_COUNT) && (jQuadrant>=0) && (jQuadrant<W6_QUADRANT_COUNT) && (jqX>=0) && (jqX<8) && (jqY>=0) && (jqY<8)){
							amw6_level = jLevel;

							uint8_t tlcqx,tlcqy;
							tlcqx = amw6_cache_qsx[amw6_level][jQuadrant];
							tlcqy = amw6_cache_qsy[amw6_level][jQuadrant];
							amw6_jLevel = amw6_level;
							amw6_jX = tlcqx + jqX;
							amw6_jY = tlcqy + jqY;

							int JDX = ( -(((tlcqx+jqX)*W6_SQUARE_SIZE) - ((SCREENW/2)-(W6_SQUARE_SIZE/2))) );
							int JDY = ( -(((255+8)*W6_SQUARE_SIZE - (tlcqy+jqY)*W6_SQUARE_SIZE) - ((SCREENH/2)-(W6_SQUARE_SIZE/2))) );
							map_scroll_x = JDX - map_draw_x;
							map_scroll_y = JDY - map_draw_y;

							AutoMapUpdate();
						}
					}
				}
			}
		}
	}

	if ((!alt) && (!ctrl))
	if (button->clicks!=0)
	if (MousePositionToQuadrantPosition(button->x, button->y, squadrant, sqx, sqy)){
		int n = W6_FindNote(squadrant,sqx,sqy);
		if ((button->button==SDL_BUTTON_LEFT)){
				TooltipForAutomapWindow_Show(false);

				std::wstring ibuf;
				ibuf.resize(1024);
				ibuf[0] = 0;
				if (n>=0){
					wcscpy(&ibuf[0],amw6_notes[amw6_level][n].str.c_str());
				}
				if (InputBox(L"Enter comment",L"if you leave it blank, it will remove marker from map",&ibuf[0],1023)){
					if (n>=0){
						if (wcslen(&ibuf[0])>0)
							amw6_notes[amw6_level][n].str = std::wstring(&ibuf[0]);
						else {
							amw6_notes[amw6_level].erase(amw6_notes[amw6_level].begin() + n);
						}
					} else 
					if (wcslen(&ibuf[0])>0)
					{
						AMW6Note note;
						note.quadrant = squadrant;
						note.qX = sqx;
						note.qY = sqy;
						note.str = &ibuf[0];
						note.color = 0xFF0000FF;
						amw6_notes[amw6_level].push_back(note);
					}
					AutoMapUpdate();
				}
		} else
		if ((button->button==SDL_BUTTON_RIGHT) && (n>=0)){
				TooltipForAutomapWindow_Show(false);

				if (ChooseColorDialog(amw6_notes[amw6_level][n].color,wiz6_palette)){
					AutoMapUpdate();
				}
		} else
		if ((button->button==SDL_BUTTON_RIGHT) && (n<0)){
			int r = MessageBoxA(NULL,
				"Clear all exploration progress and map notes for Wizardry 6?\n\nThis cannot be undone.",
				"Automap - Clear All",
				MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
			if (r == IDYES) {
				memset(amw6_visdata, 0, sizeof(amw6_visdata));
				for (int i = 0; i < W6_LEVEL_COUNT; i++) amw6_notes[i].clear();
				W6_NativeSaveVis();
				W6_NativeSaveNotes();
				AutoMapUpdate();
			}
		}
	}
}
