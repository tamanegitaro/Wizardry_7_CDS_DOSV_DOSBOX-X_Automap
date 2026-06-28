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

// Minimal includes — no OpenGL headers needed here
#include "automap.h"
#include "dos_inc.h"
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
#ifndef WIN32
#  include <sys/stat.h>
#endif
#ifdef WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif
extern PhysPt am_loadaddress;

// Forward declarations from am_main.cpp
void AM_DrawRect(int x, int y, int w, int h, int colorIndex);
void AM_DrawW6Sprite(int x, int y, int w, int h, float u0, float v0, float u1,
                     float v1, bool dark = false);
void AM_DrawW7Sprite(int x, int y, int w, int h, float u0, float v0, float u1,
                     float v1, bool dark = false,
                     SDL_RendererFlip flip = SDL_FLIP_NONE);
void SetAutomapWindowTitle(const char* title);
SDL_Renderer* AM_GetRenderer();

//#define SHOWALL
//#define DEVMODE

bool amw7_show_tooltips = true;
bool amw7_hide_in_dark_zones = true;
bool amw7_use_spellPower = false;
bool amw7_rus = false;
static bool s_dataseg_scanned   = false;
extern int am_height;
extern int am_width;
#define SCREENH am_height
#define SCREENW am_width

#define W7_SQUARE_SIZE 22
#define W7_QUADRANT_COUNT 16
#define W7_LEVEL_COUNT 32

bool amw7_show_enemylist = false;
bool amw7_show_spellwindow = false;
bool amw7_show_usewindow = false;
bool amw7_show_unlockwindow = false;
bool amw7_show_disarmwindow = false;
bool amw7_show_lootwindow = false;
bool amw7_show_buywindow = false;
bool amw7_show_sellwindow = false;
bool amw7_show_givewindow = false;
bool amw7_show_savewindow = false;
bool amw7_show_configwindow = false;
bool amw7_show_addcharacterwindow = false;
bool amw7_show_securitywindow = false;
bool amw7_show_loadwindow = false;
bool amw7_show_importwindow = false;
bool amw7_intro = false;
bool amw7_isSaving = false;

bool amw7_isLoading = false;
bool amw7_mouseIsLocked = false;
uint32_t amw7_dataseg_addr;
uint16_t W7GameStateOnOverlayLoad = 0xFF0F;
uint16_t prevW7GameStateOnOverlayLoad = 0xF0FF;
uint16_t oW7GameStateOnOverlayLoad = 0xFF0F;
uint8_t amw7_visdata[W7_LEVEL_COUNT][W7_QUADRANT_COUNT][8][8];
uint8_t amw7_spellPower[W7_LEVEL_COUNT][W7_QUADRANT_COUNT][8][8];
uint8_t amw7_spellPowerLO[W7_LEVEL_COUNT][W7_QUADRANT_COUNT][8][8];
int8_t amw7_solid_tiles[W7_LEVEL_COUNT][W7_QUADRANT_COUNT][8][8];//DON'T SAVE THIS

uint16_t amw7_current_level = 0xFFFF; // Party position: map index
uint16_t amw7_current_quadrant = 0; // Party position: quadrant
uint16_t amw7_current_qX = 0;
uint16_t amw7_current_qY = 0;
uint16_t amw7_current_dir = 0;

uint16_t oamw7_current_level = 0xFFFF;
uint16_t oamw7_current_quadrant = 0;
uint16_t oamw7_current_qX = 0;
uint16_t oamw7_current_qY = 0;
uint16_t oamw7_current_dir = 0;

uint16_t amw7_level = 0; // for rendering / notes

//link target:
uint16_t amw7_jLevel = 0xFFFF;
uint8_t amw7_jX = 0;
uint8_t amw7_jY = 0;
/* Wizardry 7 Cache */
uint8_t amw7_cache_hwalls[W7_LEVEL_COUNT][W7_QUADRANT_COUNT*64*4/8];
uint8_t amw7_cache_vwalls[W7_LEVEL_COUNT][W7_QUADRANT_COUNT*64*4/8];
uint8_t amw7_cache_qsx[W7_LEVEL_COUNT][W7_QUADRANT_COUNT];
uint8_t amw7_cache_qsy[W7_LEVEL_COUNT][W7_QUADRANT_COUNT];
uint8_t amw7_cache_features[W7_LEVEL_COUNT][W7_QUADRANT_COUNT*64*6/8];
uint8_t amw7_cache_features_dirs[W7_LEVEL_COUNT][W7_QUADRANT_COUNT*64*2/8];
uint8_t amw7_cache_floor0[W7_LEVEL_COUNT][W7_QUADRANT_COUNT * 64 / 8];
uint8_t amw7_cache_floor1[W7_LEVEL_COUNT][W7_QUADRANT_COUNT * 64 / 8];
uint8_t amw7_cache_floor2[W7_LEVEL_COUNT][W7_QUADRANT_COUNT * 64 / 8];
uint8_t amw7_cache_visited_cells[W7_LEVEL_COUNT][W7_QUADRANT_COUNT * 64 / 8];
bool amw7_map_data_dirty = false;
bool amw7_mapIsLoading = false;

uint32_t wiz7_palette[16];

struct AMW7Note {
	uint16_t quadrant;
	uint16_t qX;
	uint16_t qY;
	uint32_t color;
	std::wstring str;
};

std::vector<AMW7Note> amw7_notes[W7_LEVEL_COUNT];

static char s_w7NtsPath[512] = {};


static void W7_EnsureLocalConfigDir(const char* basePath) {
	if (!basePath || basePath[0] == 0) return;
#ifdef WIN32
	CreateDirectoryA(basePath, NULL); // OK if it already exists.
#else
	mkdir(basePath, 0755); // OK if it already exists.
#endif
}

static void W7_BuildNtsPath(char* out, size_t outSize, const char* basePath) {
	if (!out || outSize == 0) return;
	out[0] = 0;
	if (!basePath || basePath[0] == 0) basePath = "./Config";
	W7_EnsureLocalConfigDir(basePath);
	size_t n = strlen(basePath);
	if (n > 0 && basePath[n - 1] != '/' && basePath[n - 1] != '\\') {
		snprintf(out, outSize, "%s%cAUTOMAP7.NTS", basePath, '/');
	} else {
		snprintf(out, outSize, "%sAUTOMAP7.NTS", basePath);
	}
}

void W7_SetSavePath(const char* basePath) {
	W7_BuildNtsPath(s_w7NtsPath, sizeof(s_w7NtsPath), basePath);
}

static const uint32_t kW7NativeNotesMaxPerLevel = 4096;
static const uint32_t kW7NativeNoteMaxChars = 4096;

static bool W7_ReadBytes(FILE* f, void* out, size_t n) {
	return n == 0 || fread(out, 1, n, f) == n;
}

static long W7_GetFileSize(FILE* f) {
	if (!f) return -1;
	long cur = ftell(f);
	if (cur < 0) cur = 0;
	if (fseek(f, 0, SEEK_END) != 0) return -1;
	long size = ftell(f);
	fseek(f, cur, SEEK_SET);
	return size;
}

static bool W7_CanReadBytes(FILE* f, long fileSize, size_t n) {
	if (n == 0) return true;
	if (!f || fileSize < 0) return false;
	long pos = ftell(f);
	if (pos < 0 || pos > fileSize) return false;
	return n <= (size_t)(fileSize - pos);
}

static bool W7_ReadBytesChecked(FILE* f, void* out, size_t n, long fileSize) {
	if (!W7_CanReadBytes(f, fileSize, n)) return false;
	return W7_ReadBytes(f, out, n);
}

static bool W7_WriteBytes(FILE* f, const void* data, size_t n) {
	return n == 0 || fwrite(data, 1, n, f) == n;
}

static bool W7_WriteU16LE(FILE* f, uint16_t v) {
	unsigned char b[2] = { (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF) };
	return fwrite(b, 1, 2, f) == 2;
}

static bool W7_ReadU16LE(FILE* f, uint16_t* v) {
	unsigned char b[2] = {};
	if (!W7_ReadBytes(f, b, 2)) return false;
	*v = (uint16_t)(b[0] | (b[1] << 8));
	return true;
}

static bool W7_WriteWStringUTF16LELen(FILE* f, const std::wstring& s, uint32_t len) {
	if (len > s.length()) len = (uint32_t)s.length();
	for (uint32_t i = 0; i < len; ++i) {
		if (!W7_WriteU16LE(f, (uint16_t)s[i])) return false;
	}
	return true;
}

static bool W7_ReadWStringUTF16LEChecked(FILE* f, uint32_t len, std::wstring& out, long fileSize) {
	if (len > kW7NativeNoteMaxChars) return false;
	if (!W7_CanReadBytes(f, fileSize, (size_t)len * 2)) return false;
	out.clear();
	out.reserve(len);
	for (uint32_t i = 0; i < len; ++i) {
		uint16_t ch = 0;
		if (!W7_ReadU16LE(f, &ch)) return false;
		out.push_back((wchar_t)ch);
	}
	return true;
}

static void W7_AssignLoadedNotes(std::vector<AMW7Note> loaded[W7_LEVEL_COUNT]) {
	for (int i = 0; i < W7_LEVEL_COUNT; ++i) {
		amw7_notes[i] = loaded[i];
	}
}

static bool W7_LoadNativeNotesFile(FILE* f, std::vector<AMW7Note> loaded[W7_LEVEL_COUNT], long fileSize) {
	for (int i = 0; i < W7_LEVEL_COUNT; i++) {
		uint32_t n = 0;
		if (!W7_ReadBytesChecked(f, &n, 4, fileSize)) return false;
		if (n > kW7NativeNotesMaxPerLevel) return false;
		if (n > 0 && !W7_CanReadBytes(f, fileSize, (size_t)n * 14)) return false;
		for (uint32_t j = 0; j < n; j++) {
			AMW7Note note;
			if (!W7_ReadBytesChecked(f, &note.quadrant, 2, fileSize)) return false;
			if (!W7_ReadBytesChecked(f, &note.qX, 2, fileSize)) return false;
			if (!W7_ReadBytesChecked(f, &note.qY, 2, fileSize)) return false;
			if (!W7_ReadBytesChecked(f, &note.color, 4, fileSize)) return false;
			uint32_t len = 0;
			if (!W7_ReadBytesChecked(f, &len, 4, fileSize)) return false;
			if (!W7_ReadWStringUTF16LEChecked(f, len, note.str, fileSize)) return false;
			loaded[i].push_back(note);
		}
	}
	return true;
}

static void W7_BuildSiblingPath(const char* path, const char* suffix, char* out, size_t outSize) {
	if (!out || outSize == 0) return;
	out[0] = 0;
	if (!path || !suffix) return;
	snprintf(out, outSize, "%s%s", path, suffix);
}

static bool W7_CommitTempFile(const char* tmpPath, const char* finalPath) {
	if (!tmpPath || !finalPath) return false;
#ifdef WIN32
	DeleteFileA(finalPath);
	return MoveFileA(tmpPath, finalPath) != 0;
#else
	remove(finalPath);
	return rename(tmpPath, finalPath) == 0;
#endif
}

static void W7_QuarantineBadNativeNotes(const char* path) {
	if (!path || path[0] == 0) return;
	char badPath[640] = {};
	W7_BuildSiblingPath(path, ".bad", badPath, sizeof(badPath));
	if (badPath[0] == 0) return;
#ifdef WIN32
	DeleteFileA(badPath);
	MoveFileA(path, badPath);
#else
	remove(badPath);
	rename(path, badPath);
#endif
}

void W7_NativeSaveNotes() {

	char tmpPath[640] = {};
	W7_BuildSiblingPath(s_w7NtsPath, ".tmp", tmpPath, sizeof(tmpPath));

	FILE* f = fopen(tmpPath, "wb");

	bool ok = true;
	for (int i = 0; ok && i < W7_LEVEL_COUNT; i++) {
		uint32_t n = (uint32_t)amw7_notes[i].size();
		if (n > kW7NativeNotesMaxPerLevel) n = kW7NativeNotesMaxPerLevel;
		ok = ok && W7_WriteBytes(f, &n, 4);
		for (int j = 0; ok && j < (int)n; j++) {
			ok = ok && W7_WriteBytes(f, &amw7_notes[i][j].quadrant, 2);
			ok = ok && W7_WriteBytes(f, &amw7_notes[i][j].qX, 2);
			ok = ok && W7_WriteBytes(f, &amw7_notes[i][j].qY, 2);
			ok = ok && W7_WriteBytes(f, &amw7_notes[i][j].color, 4);
			uint32_t len = (uint32_t)amw7_notes[i][j].str.length();
			if (len > kW7NativeNoteMaxChars) len = kW7NativeNoteMaxChars;
			ok = ok && W7_WriteBytes(f, &len, 4);
			if (ok && len > 0) ok = W7_WriteWStringUTF16LELen(f, amw7_notes[i][j].str, len);
		}
	}

	if (fclose(f) != 0) ok = false;
	bool committed = false;
	if (ok) committed = W7_CommitTempFile(tmpPath, s_w7NtsPath);
	if (!ok || !committed) {
		remove(tmpPath);
	} else {
	}
}

static bool W7_LoadNativeNotesFromPath(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) return false;

	long fileSize = W7_GetFileSize(f);
	if (fileSize < 0 || fileSize > 8L * 1024L * 1024L) {
		fclose(f);
		W7_QuarantineBadNativeNotes(path);
		return false;
	}

	std::vector<AMW7Note> loaded[W7_LEVEL_COUNT];
	bool ok = W7_LoadNativeNotesFile(f, loaded, fileSize);
	fclose(f);

	if (ok) {
		W7_AssignLoadedNotes(loaded);
	} else {
		W7_QuarantineBadNativeNotes(path);
	}
	return ok;
}

void W7_NativeLoadNotes() {
	W7_LoadNativeNotesFromPath(s_w7NtsPath);
}

const char *amw7MapNames[W7_LEVEL_COUNT] = {
	"West of New City, Orchid Field, Road to Dane Tower and Nyctalinth",
	"Road to Nyctalinth",
	"Road connecting Nyctalinth, Rattkin Ruins and Orkogre Forest",
	"Orkogre Forest",
	"East of New City, Munkharama Bridge, Road to Munkharama and Orkogre Forest",
	"Road from Munkharama to Ukpyr",
	"Road from Ukpyr to Mountains",
	"Mountain Steps",
	"Eryn River",
	"Sea of Sorrows Center",
	"Sea of Sorrows West",
	"Sea of Sorrows East",
	"Sea of Sorrows South",
	"Myrmideon Forest and Lesser Wilds",
	"New City",
	"Dane Tower",
	"Nyctalinth",
	"Beginners Dungeon and Orkogre Castle",
	"Rattkin Ruins",
	"Munkharama and Land of Dreams",
	"Sky City",
	"Ukpyr",
	"Witch Cave, Giants Cave and Sphinx Cave",
	"Munkharama Underground Temple",
	"Crypt (Hall of the Dead and Chamber of Gorrors)",
	"Brombadeg Watery Cave",
	"Astral Dominae Tomb (Teleporter Levels and the Tomb)",
	"High Mountains",
	"Nyctalinth Dungeon",
	"Funhouse",
	"Old City, Abbey Cellar, Mountain Exit from Funhouse, Dane Tower Top Levels (5,6,7 and 8)",
	"New City (T'Rang building, Umpani Building, Security Staion and Mysterious Courtyard)",
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Offsets in data segment:
//    0x5B6E: word     -  tileset Index
//    0x8D12: word     -  max 240 used with q qx qy
//    0x82EE: word     -  view dir
//	  0x8F24: word	   -  old view dir
//    0x830C: word     -  current map/game level number
//    0x8F3E: word     -  quadrant Index
//    0x8F40: word     -  qX
//    0x8F42: word     -  qY
//	  0x8F44: word	   -  absolute x position
//    0x8F46: word	   -  absolute y position
//    0xE12: pointer to current map data
//			  + 0x5A0 : size 0xF0       byte Array[240]
//			  + 0x690 : size 0xF0       byte Array[240]  ??qX??
//			  + 0x780 : size 0xF0       byte Array[240]  ??qY??
//			  + 0x870 : size 0xF0       byte Array[240]  ??quadrantIndex??
//			  + 0x960 : size 0xF0       byte Array[240]
//            + 0xB40 : size 0x10       byte Array[W7_QUADRANT_COUNT]
//    0xE14: pointer to current map data
//            + 0x90 : size 0x200       4 bit Array[W7_QUADRANT_COUNT * 64] horizontal walls
//            + 0x290 : size 0x200      4 bit Array[W7_QUADRANT_COUNT * 64] vertical walls
//            + 0x490 : size 0x10       byte Array[W7_QUADRANT_COUNT] quadrant start Y
//            + 0x4A0 : size 0x10       byte Array[W7_QUADRANT_COUNT] quadrant start X
//            + 0x4B0 : size 0x300      6 bit Array[W7_QUADRANT_COUNT * 64] "feature" map
//            + 0x7B0 : size 0x100      2 bit Array[W7_QUADRANT_COUNT * 64] "feature direction" map
//            + 0x8B0 : size 0x80       1 bit Array[W7_QUADRANT_COUNT * 64]       layer 0 [0=no tile;1=tile]
//            + 0x930 : size 0x80       1 bit Array[W7_QUADRANT_COUNT * 64]		  layer 1 [0=no tile;1=tile]
//            + 0x9B0 : size 0x80       1 bit Array[W7_QUADRANT_COUNT * 64]       layer 2 [0=no tile;1=tile]
//    0x8AFE: 1 bit Array[W7_QUADRANT_COUNT * 64]  - current map data - visited cells
//    0x8B7E: 1 bit Array[W7_QUADRANT_COUNT * 64]
//    0x8D14+(level*0x10): 1 bit Array[W7_QUADRANT_COUNT * 64]  - map data - ??? - used by Locate Object spell
///////////////////////////////////////////////////////////////////////////////////////////////

uint16_t GetBitArrayElement(uint8_t *pArray, uint16_t index, uint16_t bitsPerElement);

//////////////////////////////////////////////////////////////////////////////////////////////////////

void W7_DrawCursorUp(int x, int y, int w, int h){
	// Up arrow sprite
	AM_DrawW7Sprite(x,y,w,h,174.0f/256.0f,0,185.0f/256.0f,14.0f/32.0f,false);
}

void W7_DrawCursorDown(int x, int y, int w, int h){
	// Down = Up flipped vertically
	AM_DrawW7Sprite(x,y,w,h,174.0f/256.0f,0,185.0f/256.0f,14.0f/32.0f,false,SDL_FLIP_VERTICAL);
}

void W7_DrawCursorRight(int x, int y, int w, int h){
	AM_DrawW7Sprite(x,y,w,h,157.0f/256.0f,2.0f/32.0f,171.0f/256.0f,13.0f/32.0f,false);
}

void W7_DrawCursorLeft(int x, int y, int w, int h){
	// Left = Right flipped horizontally
	AM_DrawW7Sprite(x,y,w,h,157.0f/256.0f,2.0f/32.0f,171.0f/256.0f,13.0f/32.0f,false,SDL_FLIP_HORIZONTAL);
}

typedef void (*DrawTileFunc)(int x, int y, int w, int h, int index, bool dark);


void W7_DrawFloorTile(int x, int y, int w, int h, int index, bool dark){
	AM_DrawW7Sprite(x,y,w,h,(index*8.0f)/256.0f,0,(index*8.0f+7.0f)/256.0f,7.0f/32.0f,dark);
}

void W7_DrawHWallTile(int x, int y, int w, int h, int index, bool dark){
	AM_DrawW7Sprite(x,y,w,h,(index*10.0f)/256.0f,8.0f/32.0f,(index*10.0f+9.0f)/256.0f,10.0f/32.0f,dark);
}

void W7_DrawVWallTile(int x, int y, int w, int h, int index, bool dark){
	AM_DrawW7Sprite(x,y,w,h,(index*4.0f)/256.0f,12.0f/32.0f,(index*4.0f+2.0f)/256.0f,21.0f/32.0f,dark);
}

void W7_DrawPitOrLadder(int x, int y, int w, int h, int index, bool dark){
	AM_DrawW7Sprite(x,y,w,h,(16.0f+index*8.0f)/256.0f,24.0f/32.0f,(index*8.0f+21.0f)/256.0f,29.0f/32.0f,dark);
}

void W7_DrawStairs(int x, int y, int w, int h, int index, bool dark){
	AM_DrawW7Sprite(x,y,w,h,(index*8.0f)/256.0f,30.0f/32.0f,(index*8.0f+7.0f)/256.0f,24.0f/32.0f,dark);
}

void W7_DrawFountain(int x, int y, int w, int h, bool dark){
	AM_DrawW7Sprite(x,y,w,h,142.0f/256.0f,21.0f/32.0f,152.0f/256.0f,12.0f/32.0f,dark);
}

//Tileset index for each map(see in VMAZE.OVR: cs:0x5431 sub_5431)
int map2tileset[32] = {
	2,//0
	2,
	2,
	2,
	2,
	2,
	2,//6
	4,
	6,
	6,
	6,
	6,
	6,
	2,//13
	3,//14
	1,
	3,//16
	1,
	3,//18
	3,
	3,
	3,//21
	5,//22
	1,
	1,
	5,//25
	1,
	4,//special
	5,//28
	1,
	1,
	3,
};

//////////////////////////////////////////////////////////////////////////////
// Tilesets
//////////////////////////////////////////////////////////////////////////////
// Num  Type[DS:5B6C]   Index=TexName;..
//  1       0           0=MAZEDATA.VGA;5=WATRDATA.VGA;
//  2		1			1=TREEDATA.VGA;4=OTWNDATA.VGA;6=TILEDATA.VGA;
//  3	    2           2=TOWNDATA.VGA;4=OTWNDATA.VGA;5=WATRDATA.VGA;6=TILEDATA.VGA;
//  4		1		    1=TREEDATA.VGA;4=OCAVDATA.VGA;
//  5		2			2=CAVEDATA.VGA;4=OCAVDATA.VGA;5=WATRDATA.VGA;
//  6		1           1=TREEDATA.VGA;4=OTWNDATA.VGA;5=WATRDATA.VGA;

//Precipice(map 27 - blocks visibility like "darkness" )/Partial darkness(map 7 - unused)
#define AMW7_PDARK -2
#define AMW7_DARK -2
#define AMW7_ROOF -1
#define AMW7_ORCHID -1
#define AMW7_NO_TILE -1
#define AMW7_DUNGEON_TILE 0
#define AMW7_CAVE_TILE 0
#define AMW7_WATER_TILE 1
#define AMW7_SLABS_TILE 2
#define AMW7_GRASS_TILE 3
#define AMW7_ROAD_TILE 4
#define AMW7_FOG_TILE 5
#define AMW7_DUNGEON_SOLID_TILE 5
#define AMW7_CITY_SOLID_TILE 6
#define AMW7_FOREST_SOLID_TILE 7
#define AMW7_CAVE_SOLID_TILE 8
#define AMW7_BAD_TILE 9
#define AMW7_BAD_TILE2 10
#define AMW7_DARK_TILE 11

#define AMW7_DUNGEON_SOLID 0
#define AMW7_DUNGEON_PASSAGE 1
#define AMW7_DUNGEON_PORTCULLIS 2
#define AMW7_CITY_SOLID 3
#define AMW7_CITY_PASSAGE 4
#define AMW7_CITY_DOOR 5
#define AMW7_FOREST_SOLID 6
#define AMW7_CAVE_SOLID 7
#define AMW7_BAD_WALL 8
#define AMW7_BAD_WALL2 9
#define AMW7_CITY_WINDOW 10
#define AMW7_CAVE_PASSAGE 11

struct W7TileSet {
	int defTile;
	int altTile;
	int addTile0;
	int addTile1;

	int defWall;
	int passage;
	int solid0;
	int solid1;

	int solidTile0;
	int solidTile1;

	int noSolidTileCoeff;
	int solidTile1Coeff;
};

W7TileSet w7tilesets[7] = {
	{AMW7_BAD_TILE,		AMW7_BAD_TILE,		AMW7_BAD_TILE,		AMW7_BAD_TILE,		AMW7_BAD_WALL,			AMW7_BAD_WALL,			AMW7_BAD_WALL,		AMW7_BAD_WALL,		AMW7_BAD_TILE,				AMW7_BAD_TILE,			0,	3},//Invalid
	{AMW7_DUNGEON_TILE,	AMW7_WATER_TILE,	AMW7_DARK,			AMW7_BAD_TILE,		AMW7_DUNGEON_SOLID,		AMW7_DUNGEON_PASSAGE,	AMW7_DUNGEON_SOLID,	AMW7_BAD_WALL,		AMW7_DUNGEON_SOLID_TILE,	AMW7_BAD_TILE,			0,	3},
	{AMW7_ROAD_TILE,	AMW7_GRASS_TILE,	AMW7_SLABS_TILE,	AMW7_ORCHID,		AMW7_FOREST_SOLID,		AMW7_BAD_WALL,			AMW7_CITY_SOLID,	AMW7_FOREST_SOLID,	AMW7_CITY_SOLID_TILE,		AMW7_FOREST_SOLID_TILE,	0,	3},
	{AMW7_GRASS_TILE,	AMW7_SLABS_TILE,	AMW7_ROOF,			AMW7_WATER_TILE,	AMW7_CITY_SOLID,		AMW7_CITY_PASSAGE,		AMW7_CITY_SOLID,	AMW7_BAD_WALL,		AMW7_CITY_SOLID_TILE,		AMW7_BAD_TILE,			0,	3},
	{AMW7_CAVE_TILE,	AMW7_GRASS_TILE,	AMW7_PDARK,			AMW7_BAD_TILE,		AMW7_FOREST_SOLID,		AMW7_BAD_WALL,			AMW7_CAVE_SOLID,	AMW7_FOREST_SOLID,	AMW7_CAVE_SOLID_TILE,		AMW7_FOREST_SOLID_TILE, 0,	1},
	{AMW7_CAVE_TILE,	AMW7_WATER_TILE,	AMW7_ROOF,			AMW7_FOG_TILE,		AMW7_CAVE_SOLID,		AMW7_BAD_WALL,			AMW7_CAVE_SOLID,	AMW7_BAD_WALL,		AMW7_CAVE_SOLID_TILE,		AMW7_BAD_TILE,			0,	3},
	{AMW7_ROAD_TILE,	AMW7_GRASS_TILE,	AMW7_WATER_TILE,	AMW7_FOG_TILE,		AMW7_FOREST_SOLID,		AMW7_BAD_WALL,			AMW7_CITY_SOLID,	AMW7_FOREST_SOLID,	AMW7_CITY_SOLID_TILE,		AMW7_FOREST_SOLID_TILE,	0,	3},
};

W7TileSet *GetTS(uint16_t level){
	if ((level<0) || (level>31))
		return &w7tilesets[0];
	return &w7tilesets[map2tileset[level]];
}

W7TileSet *GetTS(){
	return GetTS(amw7_level);
}

bool W7C_PointInQuadrant(uint16_t x, uint16_t y, uint16_t quadrant){
	uint8_t qsx,qsy;
	qsx = amw7_cache_qsx[amw7_level][quadrant];
	qsy = amw7_cache_qsy[amw7_level][quadrant];
	if ((x>=qsx) && (y>=qsy) && (x<=qsx+7) && (y<=qsy+7))
		return true;
	return false;
}

bool W7C_PointAtLeftSideOfQuadrant(uint16_t x, uint16_t y, uint16_t quadrant){
	uint8_t qsx,qsy;
	qsx = amw7_cache_qsx[amw7_level][quadrant];
	qsy = amw7_cache_qsy[amw7_level][quadrant];
	if ((x==qsx) && (y>=qsy) && (y<=qsy+7))
		return true;
	return false;
}

bool W7C_PointInAnyQuadrant(uint16_t x, uint16_t y){
	for (int q = 0; q < W7_QUADRANT_COUNT; q++){
		if (W7C_PointInQuadrant(x, y, q))
			return true;
	}
	return false;
}

bool W7C_AbsToQuadrant(int x, int y, uint16_t &quadrant, uint16_t &qx, uint16_t &qy){
	for (int q = 0; q < W7_QUADRANT_COUNT; q++){
		if (W7C_PointInQuadrant(x, y, q)){
			uint8_t qsx,qsy;
			qsx = amw7_cache_qsx[amw7_level][q];
			qsy = amw7_cache_qsy[amw7_level][q];

			quadrant = q;
			qx = x - qsx;
			qy = y - qsy;

			return true;
		}
	}
	return false;
}

bool W7C_PointAtLeftSideOfAnyQuadrant(uint16_t x, uint16_t y){
	for (int q = 0; q < W7_QUADRANT_COUNT; q++){
		if (W7C_PointAtLeftSideOfQuadrant(x, y, q))
			return true;
	}
	return false;
}

bool W7VIS_IsTransparent(int val){
	if ((val==0) || (val==1) || (val==5))
		return true;
	return false;
}

bool W7C_TopIsVisible(uint16_t x, uint16_t y){
	uint16_t tq,tqx,tqy;
	if (!W7C_AbsToQuadrant(x,y,tq,tqx,tqy)) return false;

	return W7VIS_IsTransparent(GetBitArrayElement(amw7_cache_hwalls[amw7_current_level],tq*64 + tqy*8 + tqx,4));
}

bool W7C_BottomIsVisible(uint16_t x, uint16_t y){
	return W7C_TopIsVisible(x,y-1);
}

bool W7C_RightIsVisible(uint16_t x, uint16_t y){
	uint16_t tq,tqx,tqy;
	if (!W7C_AbsToQuadrant(x,y,tq,tqx,tqy)) return false;

	return W7VIS_IsTransparent(GetBitArrayElement(amw7_cache_vwalls[amw7_current_level],tq*64 + tqy*8 + tqx,4));
}

bool W7C_LeftIsVisible(uint16_t x, uint16_t y){
	return W7C_RightIsVisible(x-1,y);
}

bool W7C_IsDarkZone(uint16_t quadrant, uint16_t qx, uint16_t qy){
	if (GetTS()->addTile0==AMW7_DARK){
		if (GetBitArrayElement(amw7_cache_floor0[amw7_current_level],quadrant*64 + qy*8 + qx,1)!=0)
			return amw7_hide_in_dark_zones;
	}
	return false;
}

bool W7C_IsDarkZone(uint16_t level, uint16_t quadrant, uint16_t qx, uint16_t qy){
	if (GetTS(level)->addTile0==AMW7_DARK){
		if (GetBitArrayElement(amw7_cache_floor0[level],quadrant*64 + qy*8 + qx,1)!=0)
			return true;
	}
	return false;
}

int W7_FindNote(uint16_t quadrant, uint16_t qx, uint16_t qy){
	for (int i = 0; i < (int)amw7_notes[amw7_level].size(); i++){
		if ((amw7_notes[amw7_level][i].quadrant==quadrant) &&
			(amw7_notes[amw7_level][i].qX==qx) &&
			(amw7_notes[amw7_level][i].qY==qy))
			return i;
	}
	return -1;
}

int W7_GetVis(uint16_t level, uint16_t quadrant, uint16_t qx, uint16_t qy){
	if (amw7_use_spellPower){
		if (amw7_spellPower[level][quadrant][qx][qy]<=4)
			return GetBitArrayElement(amw7_cache_visited_cells[level],quadrant*64 + qy*8 + qx,1);
		else
			return 1;
	}
	return amw7_visdata[level][quadrant][qx][qy];
}

void W7_DrawQuadrantFirstPass(int x, int y, uint16_t quadrant){
	//all tiles & fake water are some at all spell power levels
	int defTile = GetTS()->defTile;
	int altTile = GetTS()->altTile;
	int addTile0 = GetTS()->addTile0;
	int addTile1 = GetTS()->addTile1;
	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			if (W7C_IsDarkZone(amw7_level, quadrant, j, i)) continue;
			int v = W7_GetVis(amw7_level, quadrant, j, i);
			if (v==0) continue;
			uint16_t q = GetBitArrayElement(amw7_cache_floor2[amw7_level],quadrant*64 + i*8 + j,1);
			uint16_t qx =  W7_SQUARE_SIZE*j;
			uint16_t qy = (W7_SQUARE_SIZE*7) - W7_SQUARE_SIZE*i;
			if (q!=0)
				W7_DrawFloorTile(x + qx+1,y + qy+1,W7_SQUARE_SIZE-1,W7_SQUARE_SIZE-1,altTile,v!=1);//grass
			else
				W7_DrawFloorTile(x + qx+1,y + qy+1,W7_SQUARE_SIZE-1,W7_SQUARE_SIZE-1,defTile,v!=1);//Default tile: Road
		}
	}
	if (addTile0>=0)
	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			if (W7C_IsDarkZone(amw7_level, quadrant, j, i)) continue;
			int v = W7_GetVis(amw7_level, quadrant, j, i);
			if (v==0) continue;
			uint16_t q = GetBitArrayElement(amw7_cache_floor0[amw7_level],quadrant*64 + i*8 + j,1);
			uint16_t qx =  W7_SQUARE_SIZE*j;
			uint16_t qy = (W7_SQUARE_SIZE*7) - W7_SQUARE_SIZE*i;
			if (q!=0)
				W7_DrawFloorTile(x + qx+1,y + qy+1,W7_SQUARE_SIZE-1,W7_SQUARE_SIZE-1,addTile0,v!=1);//Slabs, etc
		}
	}
	if (addTile1>=0)
	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			if (W7C_IsDarkZone(amw7_level, quadrant, j, i)) continue;
			int v = W7_GetVis(amw7_level, quadrant, j, i);
			if (v==0) continue;
			uint16_t q = GetBitArrayElement(amw7_cache_floor1[amw7_level],quadrant*64 + i*8 + j,1);
			uint16_t qx =  W7_SQUARE_SIZE*j;
			uint16_t qy = (W7_SQUARE_SIZE*7) - W7_SQUARE_SIZE*i;
			if (q!=0)
				W7_DrawFloorTile(x + qx+1,y + qy+1,W7_SQUARE_SIZE-1,W7_SQUARE_SIZE-1,addTile1,v!=1);//Slabs, etc
		}
	}
	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			if (W7C_IsDarkZone(amw7_level, quadrant, j, i)) continue;
			int v = W7_GetVis(amw7_level, quadrant, j, i);
			if (v==0) continue;
			uint16_t q = GetBitArrayElement(&amw7_cache_features[amw7_level][0],quadrant*64 + i*8 + j,6);
			(void)GetBitArrayElement(&amw7_cache_features_dirs[amw7_level][0],quadrant*64 + i*8 + j,2);
			uint16_t qx =  W7_SQUARE_SIZE*j;
			uint16_t qy = (W7_SQUARE_SIZE*7) - W7_SQUARE_SIZE*i;
			switch (q){
			//Fake Water
			case 13: W7_DrawFloorTile(x + qx+1,y + qy+1,W7_SQUARE_SIZE-1,W7_SQUARE_SIZE-1,AMW7_WATER_TILE,v!=1);
				break;
			};
		}
	}
	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			if (W7C_IsDarkZone(amw7_level, quadrant, j, i)) continue;
			int v = W7_GetVis(amw7_level, quadrant, j, i);
			if (v==0) continue;
			int8_t st = amw7_solid_tiles[amw7_level][quadrant][j][i];
			if (st==AMW7_NO_TILE) continue;
			uint16_t qx =  W7_SQUARE_SIZE*j;
			uint16_t qy = (W7_SQUARE_SIZE*7) - W7_SQUARE_SIZE*i;

			W7_DrawFloorTile(x + qx+1,y + qy+1,W7_SQUARE_SIZE-1,W7_SQUARE_SIZE-1,st,v!=1);
		}
	}
}

int w7wallRemap[16] = {
	0,
	3,//sand
	2,
	6,//grass
	4,
	5,
	6,
	7,
	8,
	9,
	10,
	11,
	12,
	13,
	14,
	15,
};

DrawTileFunc drawwallfunc[2] = {
	W7_DrawHWallTile,
	W7_DrawVWallTile,
};

void W7_DrawQuadrantSecondPassEx(int x, int y, uint16_t quadrant, int xDir, int yDir, uint8_t *amw7_cache_dwalls){
	uint8_t cqsx,cqsy;
	cqsx = amw7_cache_qsx[amw7_level][quadrant];
	cqsy = amw7_cache_qsy[amw7_level][quadrant];

	int defWall = GetTS()->defWall;
	int passage = GetTS()->passage;
	int solid0 = GetTS()->solid0;
	int solid1 = GetTS()->solid1;
	int solidTile1 = GetTS()->solidTile1;

	x--;
	y--;
	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			uint8_t sp = 0;
			uint16_t tq,tqx,tqy;
			bool v = false;

			if (amw7_use_spellPower){
				sp = amw7_spellPower[amw7_level][quadrant][j][i];
				if (W7C_AbsToQuadrant(cqsx + j + yDir,cqsy + i + xDir,tq,tqx,tqy)){
					sp = (amw7_spellPower[amw7_level][tq][tqx][tqy]>sp) ? amw7_spellPower[amw7_level][tq][tqx][tqy] : sp;
				}
			}

			//Common vis
			bool d = (W7_GetVis(amw7_level,quadrant,j,i)!=1);
			if (W7C_AbsToQuadrant(cqsx + j + yDir,cqsy + i + xDir,tq,tqx,tqy)){
				v = (W7_GetVis(amw7_level,tq,tqx,tqy)!=0);
				d = ((W7_GetVis(amw7_level,tq,tqx,tqy)!=1) && d);
			}
			if ((!v) && W7_GetVis(amw7_level,quadrant,j,i)==0) continue;
			if (amw7_use_spellPower){
				if (sp<=1) continue;
			}

			uint16_t q = GetBitArrayElement(amw7_cache_dwalls,quadrant*64 + i*8 + j,4);
			uint16_t qx =  W7_SQUARE_SIZE*yDir + W7_SQUARE_SIZE*j;
			uint16_t qy = (W7_SQUARE_SIZE*7) - W7_SQUARE_SIZE*i;

			//q:
			//     0  - no wall
			//     1  - passage 0
			//     2  - solid wall 0
			//     3  - door
			//     4  - solid wall 1
			//     5  - cave passage
			//     6  - portcullis
			//     7  - window
			if (q!=0){

				switch (q){
					case 1://passage
						q = (amw7_use_spellPower && sp<=2) ? solid0 : passage;
						break;
					case 2://solid 0
						q = solid0;
						break;
					case 3://door
						q = (amw7_use_spellPower && sp<=2) ? AMW7_CITY_SOLID : AMW7_CITY_DOOR;
						break;
					case 4://solid 1
						q = solid1;
						break;
					case 5://Cave passage
						q = (amw7_use_spellPower && sp<=2) ? AMW7_CAVE_SOLID : AMW7_CAVE_PASSAGE;
						break;
					case 6://Dungeon portcullis
						q = (amw7_use_spellPower && sp<=2) ? AMW7_DUNGEON_SOLID : AMW7_DUNGEON_PORTCULLIS;
						break;
					case 7://City window
						q = (amw7_use_spellPower && sp<=2) ? AMW7_CITY_SOLID : AMW7_CITY_WINDOW;
						break;
				};

				drawwallfunc[yDir](x + qx,y + qy,(W7_SQUARE_SIZE+2)*xDir + 3*yDir,3*xDir + (W7_SQUARE_SIZE+2)*yDir,q,d);
			}
		}
	}

	for (int i = 0; i < 8; i++){
		bool d = (W7_GetVis(amw7_level,quadrant,i*xDir,i*yDir)!=1);
		uint8_t sp = 0;
		if (amw7_use_spellPower){
			sp = amw7_spellPower[amw7_level][quadrant][i*xDir][i*yDir];
			if (sp<=1) continue;
		}
		if (W7_GetVis(amw7_level,quadrant,i*xDir,i*yDir)==0)
			continue;
		if (((!W7C_PointInAnyQuadrant(cqsx+i,cqsy-1)) && (xDir)) || ((!W7C_PointAtLeftSideOfAnyQuadrant(cqsx-8,cqsy+i)) && (yDir))){
			uint16_t qx =(W7_SQUARE_SIZE*i)*xDir;
			uint16_t qy =(W7_SQUARE_SIZE*8)*xDir + ((W7_SQUARE_SIZE*7) - W7_SQUARE_SIZE*i)*yDir;

			int8_t st = amw7_solid_tiles[amw7_level][quadrant][i*xDir][i*yDir];

			bool water = (((GetTS()->altTile==AMW7_WATER_TILE) && (GetBitArrayElement(amw7_cache_floor2[amw7_level],quadrant*64 + i*yDir*8 + i*xDir,1)!=0)) ||
						  ((GetTS()->addTile0==AMW7_WATER_TILE) && (GetBitArrayElement(amw7_cache_floor0[amw7_level],quadrant*64 + i*yDir*8 + i*xDir,1)!=0)) ||
						  ((GetTS()->addTile1==AMW7_WATER_TILE) && (GetBitArrayElement(amw7_cache_floor1[amw7_level],quadrant*64 + i*yDir*8 + i*xDir,1)!=0)) ||
						  (GetBitArrayElement(&amw7_cache_features[amw7_level][0],quadrant*64 + i*yDir*8 + i*xDir,6)==13 /* Fake Water */ ));
			if (!water || solid0==AMW7_DUNGEON_SOLID || solid0==AMW7_CAVE_SOLID)
				drawwallfunc[yDir](x + qx,y + qy,(W7_SQUARE_SIZE+2)*xDir + 3*yDir,3*xDir + (W7_SQUARE_SIZE+2)*yDir,(st==solidTile1 || st==AMW7_NO_TILE) ? defWall : solid0,d);
		}
	}
}

void W7_DrawQuadrantSecondPass(int x, int y, uint16_t quadrant){
	W7_DrawQuadrantSecondPassEx(x,y,quadrant,0,1,&amw7_cache_vwalls[amw7_level][0]);
	W7_DrawQuadrantSecondPassEx(x,y,quadrant,1,0,&amw7_cache_hwalls[amw7_level][0]);
}

void W7_DrawQuadrantThirdPass(int x, int y, uint16_t quadrant){
	//stairs
	static const int direction[4][2] = {
		{0,-W7_SQUARE_SIZE},
		{W7_SQUARE_SIZE,0},
		{0,W7_SQUARE_SIZE},
		{-W7_SQUARE_SIZE,0},
	};

	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			if (W7C_IsDarkZone(amw7_level, quadrant, j, i)) continue;
			uint8_t sp = 0;
			bool dark = (W7_GetVis(amw7_level,quadrant,j,i)!=1);
			if (amw7_use_spellPower){
				sp = amw7_spellPower[amw7_level][quadrant][j][i];
				if (sp<=3) continue;
			}
			if (W7_GetVis(amw7_level,quadrant,j,i)==0) continue;
			uint16_t q = GetBitArrayElement(&amw7_cache_features[amw7_level][0],quadrant*64 + i*8 + j,6);
			uint16_t d = GetBitArrayElement(&amw7_cache_features_dirs[amw7_level][0],quadrant*64 + i*8 + j,2);
			uint16_t qx =  W7_SQUARE_SIZE*j;
			uint16_t qy = (W7_SQUARE_SIZE*7) - W7_SQUARE_SIZE*i;
			switch (q){
			//Stairs Up
			case 1: W7_DrawStairs(x + qx+3 + direction[d][0], y + qy+3 + direction[d][1],W7_SQUARE_SIZE-5,W7_SQUARE_SIZE-5,0,dark);
				break;
			//Stairs Down
			case 2: W7_DrawStairs(x + qx+3 + direction[d][0], y + qy+3 + direction[d][1],W7_SQUARE_SIZE-5,W7_SQUARE_SIZE-5,1,dark);
				break;
			//Sconce
			case 3:
				break;
			//Fountain
			case 4:
				W7_DrawFountain(x + qx+3, y + qy+3,W7_SQUARE_SIZE-5,W7_SQUARE_SIZE-5,dark);
				break;
			//Secret Button
			case 5:
				break;
			//Hole
			case 6:
				break;
			//Chest
			case 7:
				break;
			//Ladder Down
			case 8:
				W7_DrawPitOrLadder(x + qx+3, y + qy+3,W7_SQUARE_SIZE-5,W7_SQUARE_SIZE-5,3,dark);
				break;
			//Ladder Up
			case 9:
				W7_DrawPitOrLadder(x + qx+3, y + qy+3,W7_SQUARE_SIZE-5,W7_SQUARE_SIZE-5,1,dark);
				break;
			//Lever Left/Right
			case 10://invisible on map in-game
				break;
			//Lever Up/Down
			case 11://invisible on map in-game
				break;
			//Statue
			case 12://invisible on map in-game
				break;
			//Fake Water
			case 13: //Render in first pass as water tile
				break;
			//Pit Down
			case 14:
				W7_DrawPitOrLadder(x + qx+3, y + qy+3,W7_SQUARE_SIZE-5,W7_SQUARE_SIZE-5,2,dark);
				break;
			//Pit Up
			case 15:
				W7_DrawPitOrLadder(x + qx+3, y + qy+3,W7_SQUARE_SIZE-5,W7_SQUARE_SIZE-5,0,dark);
				break;
			//????????????
			case 16:
				break;
			//Pit Up AND Down
			case 17:
				W7_DrawPitOrLadder(x + qx+3, y + qy+3,W7_SQUARE_SIZE-5,W7_SQUARE_SIZE-5,4,dark);
				break;
			//>=18           nothing?
			};
		}
	}
}

uint8_t LocateObject(uint16_t quadrant, uint16_t qX, uint16_t qY, uint8_t loIndex){
	uint16_t lodataOfs;
	mem_readw_checked((amw7_dataseg_addr + 0x0E2E), &lodataOfs);
	uint8_t bitArray[16];
	MEM_BlockRead((amw7_dataseg_addr + 0x8E1A) + amw7_current_level*16, &bitArray[0], sizeof(bitArray));
	for (uint8_t i = loIndex; i < 240; i++){
		uint8_t quadrantIndex;
		mem_readb_checked(amw7_dataseg_addr + lodataOfs + 0x870 + i, &quadrantIndex);
		if (quadrantIndex!=quadrant) return 0xFF;
		uint8_t iqX;
		mem_readb_checked(amw7_dataseg_addr + lodataOfs + 0x690 + i, &iqX);
		if (iqX==qX){
			uint8_t iqY;
			mem_readb_checked(amw7_dataseg_addr + lodataOfs + 0x780 + i, &iqY);
			if (iqY==qY){
				union {
					uint8_t udata;
					int8_t sdata;
				};
				mem_readb_checked(amw7_dataseg_addr + lodataOfs + 0x5A0 + i, &udata);
				if (sdata>0){
					uint8_t unkIndex;
					mem_readb_checked(amw7_dataseg_addr + lodataOfs + 0x960 + i, &unkIndex);

					if (GetBitArrayElement(bitArray,unkIndex,1)==0)
						return i;
				}
			}
		}
	}
	return 0xFF;
}

void W7_DrawQuadrantFourthPass(int x, int y, uint16_t quadrant, uint16_t qX, uint16_t qY, bool showMark){
	///////////////////////////////////////////Locate Object/////////////////////////////////////////////////
	if (amw7_level==amw7_current_level){
		uint16_t lodataOfs;
		mem_readw_checked((amw7_dataseg_addr + 0x0E2E), &lodataOfs);
		uint8_t sloIndex;
		mem_readb_checked(amw7_dataseg_addr + lodataOfs + 0xB40 + quadrant, &sloIndex);
		for (int i = 0; i < 8; i++){
			for (int j = 0; j < 8; j++){
				if (W7C_IsDarkZone(amw7_level, quadrant, j, i)) continue;
				if (W7_GetVis(amw7_level,quadrant,j,i)==0) continue;
				if (amw7_use_spellPower && amw7_spellPowerLO[amw7_level][quadrant][j][i]==0) continue;
				bool dark = (W7_GetVis(amw7_level,quadrant,j,i)!=1);
				uint8_t loIndex = sloIndex;
				while ((loIndex=LocateObject(quadrant,j,i,loIndex))!=0xFF){
					uint8_t objType = 0;
					mem_readb_checked(amw7_dataseg_addr + lodataOfs + 0x5A0 + loIndex, &objType);
					if ((objType==3/*chest*/) || (amw7_use_spellPower && objType==8/*hidden thing*/)){
						int qx = x + W7_SQUARE_SIZE*j;
						int qy = y + (W7_SQUARE_SIZE*7) - W7_SQUARE_SIZE*i;

						{ /* note dot */ uint32_t cl = (dark) ? 0x880880u : 0xFF0FF0u;
                 AM_DrawRect(qx+W7_SQUARE_SIZE/2-1, qy+W7_SQUARE_SIZE/2, 3, 3,
                     (int)((cl&0xFF)|(((cl>>8)&0xFF)<<8)|(((cl>>16)&0xFF)<<16))); }
					}

					loIndex++;
				}
			}
		}
	}
	/////////////////////////////////////////////////////////////////////////////////////////////////////////
	for (int i = 0; i < (int)amw7_notes[amw7_level].size(); i++){
		if (amw7_notes[amw7_level][i].quadrant==quadrant){
			int qx = x + W7_SQUARE_SIZE*amw7_notes[amw7_level][i].qX;
			int qy = y + (W7_SQUARE_SIZE*7) - W7_SQUARE_SIZE*amw7_notes[amw7_level][i].qY;
			float w = W7_SQUARE_SIZE-6;
			float h = W7_SQUARE_SIZE-7;

			{ /* note border */
  uint32_t cl = amw7_notes[amw7_level][i].color;
  int nx=qx+4, ny=qy+4, nw=(int)(w), nh=(int)(h);
  extern SDL_Renderer* AM_GetRenderer();
  SDL_Renderer* ren = AM_GetRenderer();
  if (ren) {
    SDL_SetRenderDrawColor(ren, cl&0xFF, (cl>>8)&0xFF, (cl>>16)&0xFF, 255);
    SDL_Rect border={nx,ny,nw,nh};
    SDL_RenderDrawRect(ren,&border);
  }
}
		}
	}

	if ((showMark) && (amw7_current_level==amw7_level) && (!W7C_IsDarkZone(quadrant,qX,qY))){
		int qx = W7_SQUARE_SIZE*qX;
		int qy = (W7_SQUARE_SIZE*7) - W7_SQUARE_SIZE*qY;
		int tsize = W7_SQUARE_SIZE;
		switch (amw7_current_dir){
			case 0: W7_DrawCursorUp(x+qx,y+qy,tsize,tsize); break;
			case 1: W7_DrawCursorRight(x+qx,y+qy,tsize,tsize); break;
			case 2: W7_DrawCursorDown(x+qx,y+qy,tsize,tsize); break;
			case 3: W7_DrawCursorLeft(x+qx,y+qy,tsize,tsize); break;
			default: W7_DrawCursorUp(x+qx,y+qy,tsize,tsize); break;
		};
	}
}

extern int map_draw_x;
extern int map_draw_y;
extern int map_scroll_x;
extern int map_scroll_y;
bool amW7inGame = true;

bool W7VIS_IsWall(int val){
	if ((val==0) || (val==1) || (val==3) || (val==5) || (val==6))
		return false;
	//2 4 7
	return true;
}

bool W7_IsInGame(){
	return ((amw7_current_level>=0) && (amw7_current_level<=31) && (W7GameStateOnOverlayLoad>4) && (W7GameStateOnOverlayLoad!=7) && (W7GameStateOnOverlayLoad!=8) && (W7GameStateOnOverlayLoad!=16) && !((prevW7GameStateOnOverlayLoad==4) && (W7GameStateOnOverlayLoad==17)/*called from main menu*/));
}

void W7_UpdateCache(uint16_t uArray); // forward declaration
void W7_CopyVisData(uint16_t level);  // forward declaration
void W7_ApplyMappingSkill();          // forward declaration
void W7_UpdateVisData();              // forward declaration

bool amw7_need_update_once = false;

bool W7_NeedUpdate(){
	if (amw7_need_update_once){
		amw7_need_update_once = false;
		return true;
	}
	amW7inGame = W7_IsInGame();
	if (amW7inGame){
		if (amw7_map_data_dirty){
			return true;
		}
	}
	return false;
}

void W7_DetectSolidTiles(uint16_t level){
	uint16_t blvl = amw7_level;
	amw7_level = level;
	int solidTile0 = GetTS()->solidTile0;
	int solidTile1 = GetTS()->solidTile1;
	int noSolidTileCoeff = GetTS()->noSolidTileCoeff;
	int solidTile1Coeff  = GetTS()->solidTile1Coeff;
	for (int q = 0; q < W7_QUADRANT_COUNT; q++){
		uint8_t cqsx,cqsy;
		cqsx = amw7_cache_qsx[amw7_level][q];
		cqsy = amw7_cache_qsy[amw7_level][q];
		for (int i = 0; i < 8; i++){
			for (int j = 0; j < 8; j++){
				bool lw = false;
				bool rw = false;
				bool tw = false;
				bool bw = false;
				uint16_t l = 0;
				uint16_t r = GetBitArrayElement(&amw7_cache_vwalls[amw7_level][0],q*64 + i*8 + j,4);
				uint16_t t = GetBitArrayElement(&amw7_cache_hwalls[amw7_level][0],q*64 + i*8 + j,4);
				uint16_t b = 0;

				rw = W7VIS_IsWall(r);
				tw = W7VIS_IsWall(t);
				if (j==0){
					if (!W7C_PointAtLeftSideOfAnyQuadrant(cqsx-8,cqsy+i)){
						lw = true;
						l = r;
					} else {
						uint16_t nq,nqx,nqy;
						if (W7C_AbsToQuadrant(cqsx-1,cqsy+i,nq,nqx,nqy)){
							l = GetBitArrayElement(&amw7_cache_vwalls[amw7_level][0],nq*64 + nqy*8 + nqx,4);
							lw = W7VIS_IsWall(l);
						}
					}
				} else {
					l = GetBitArrayElement(&amw7_cache_vwalls[amw7_level][0],q*64 + i*8 + j-1,4);
					lw = W7VIS_IsWall(l);
				}
				if (i==0){
					if (!W7C_PointInAnyQuadrant(cqsx+j,cqsy-1)){
						bw = true;
						b = t;
					} else {
						uint16_t nq,nqx,nqy;
						if (W7C_AbsToQuadrant(cqsx+j,cqsy-1,nq,nqx,nqy)){
							b = GetBitArrayElement(&amw7_cache_hwalls[amw7_level][0],nq*64 + nqy*8 + nqx,4);
							bw = W7VIS_IsWall(b);
						}
					}
				} else {
					b = GetBitArrayElement(&amw7_cache_hwalls[amw7_level][0],q*64 + (i-1)*8 + j,4);
					bw = W7VIS_IsWall(b);
				}
				if (lw && rw && tw && bw){
					int s1c = 0;
					if (r==4) s1c++;
					if (l==4) s1c++;
					if (b==4) s1c++;
					if (t==4) s1c++;
					if (s1c<=noSolidTileCoeff)
						amw7_solid_tiles[amw7_level][q][j][i] = solidTile0;
					else
					if (s1c>=solidTile1Coeff)
						amw7_solid_tiles[amw7_level][q][j][i] = solidTile1;
					else
						amw7_solid_tiles[amw7_level][q][j][i] = AMW7_NO_TILE;
				} else
					amw7_solid_tiles[amw7_level][q][j][i] = AMW7_NO_TILE;
		//		amw7_solid_tiles[amw7_level][q][j][i] = (lw && rw && tw && bw) ? (((r==4) && (l==4) /*&& (b==4)*/ && (t==4)) ? solidTile1 : solidTile0) : AMW7_NO_TILE;
			}
		}
	}
	amw7_level = blvl;
}

void W7_CopyVisData(uint16_t level){
	uint16_t blevel = amw7_level;
	amw7_level = level;
	for (int quadrant = 0; quadrant < W7_QUADRANT_COUNT; quadrant++){
		uint8_t qsx = amw7_cache_qsx[amw7_level][quadrant];
		uint8_t qsy = amw7_cache_qsy[amw7_level][quadrant];

		for (int qY = 0; qY < 8; qY++){
			for (int qX = 0; qX < 8; qX++){
				if (W7C_IsDarkZone(quadrant,qX,qY)) continue;
				uint16_t v = GetBitArrayElement(amw7_cache_visited_cells[level],quadrant*64 + qY*8 + qX,1);
				if ((v!=1) || (amw7_visdata[amw7_level][quadrant][qX][qY]==1)) continue;
				amw7_visdata[amw7_level][quadrant][qX][qY] = 1;

				uint16_t hvq,hvqx,hvqy;
				if ((W7C_AbsToQuadrant(qsx+qX,  qsy+qY-1,hvq,hvqx,hvqy)) && (amw7_visdata[amw7_level][hvq][hvqx][hvqy]==0)){
					if (W7C_BottomIsVisible(qsx+qX,  qsy+qY))
						if (!W7C_IsDarkZone(hvq,hvqx,hvqy))
							amw7_visdata[amw7_level][hvq][hvqx][hvqy] = 2;
				}
				if ((W7C_AbsToQuadrant(qsx+qX-1,qsy+qY,hvq,hvqx,hvqy)) && (amw7_visdata[amw7_level][hvq][hvqx][hvqy]==0)){
					if (W7C_LeftIsVisible(qsx+qX,  qsy+qY))
						if (!W7C_IsDarkZone(hvq,hvqx,hvqy))
							amw7_visdata[amw7_level][hvq][hvqx][hvqy] = 2;
				}
				if ((W7C_AbsToQuadrant(qsx+qX+1,qsy+qY,hvq,hvqx,hvqy)) && (amw7_visdata[amw7_level][hvq][hvqx][hvqy]==0)){
					if (W7C_RightIsVisible(qsx+qX,  qsy+qY))
						if (!W7C_IsDarkZone(hvq,hvqx,hvqy))
							amw7_visdata[amw7_level][hvq][hvqx][hvqy] = 2;
				}
				if ((W7C_AbsToQuadrant(qsx+qX,  qsy+qY+1,hvq,hvqx,hvqy)) && (amw7_visdata[amw7_level][hvq][hvqx][hvqy]==0)){
					if (W7C_TopIsVisible(qsx+qX,  qsy+qY))
						if (!W7C_IsDarkZone(hvq,hvqx,hvqy))
							amw7_visdata[amw7_level][hvq][hvqx][hvqy] = 2;
				}
			}
		}
	}
	amw7_level = blevel;
}

void W7_UpdateCache(uint16_t uArray){
	if (amw7_current_level>31) {
		return;
	}

	uint16_t mapdataOfs;
	mem_readw_checked((amw7_dataseg_addr + 0x0E30), &mapdataOfs);

	if ((uArray==0) || (uArray==mapdataOfs + 0x90))  MEM_BlockRead(amw7_dataseg_addr + mapdataOfs + 0x90, &amw7_cache_hwalls[amw7_current_level][0], sizeof(amw7_cache_hwalls)/W7_LEVEL_COUNT);
	if ((uArray==0) || (uArray==mapdataOfs + 0x290)) MEM_BlockRead(amw7_dataseg_addr + mapdataOfs + 0x290, &amw7_cache_vwalls[amw7_current_level][0], sizeof(amw7_cache_vwalls)/W7_LEVEL_COUNT);
	// Original DOSBox 0.74 Automap order.
	// Despite later comments saying otherwise, the working old mod reads
	// mapdata+0x490 as quadrant start X and mapdata+0x4A0 as quadrant start Y.
	if ((uArray==0) || (uArray==mapdataOfs + 0x490)) MEM_BlockRead(amw7_dataseg_addr + mapdataOfs + 0x490, &amw7_cache_qsx[amw7_current_level][0], sizeof(amw7_cache_qsx)/W7_LEVEL_COUNT);
	if ((uArray==0) || (uArray==mapdataOfs + 0x4A0)) MEM_BlockRead(amw7_dataseg_addr + mapdataOfs + 0x4A0, &amw7_cache_qsy[amw7_current_level][0], sizeof(amw7_cache_qsy)/W7_LEVEL_COUNT);

	if ((uArray==0) || (uArray==mapdataOfs + 0x4B0)) MEM_BlockRead(amw7_dataseg_addr + mapdataOfs + 0x4B0, &amw7_cache_features[amw7_current_level][0], sizeof(amw7_cache_features)/W7_LEVEL_COUNT);
	if ((uArray==0) || (uArray==mapdataOfs + 0x7B0)) MEM_BlockRead(amw7_dataseg_addr + mapdataOfs + 0x7B0, &amw7_cache_features_dirs[amw7_current_level][0], sizeof(amw7_cache_features_dirs)/W7_LEVEL_COUNT);
	if ((uArray==0) || (uArray==mapdataOfs + 0x8B0)) MEM_BlockRead(amw7_dataseg_addr + mapdataOfs + 0x8B0, &amw7_cache_floor0[amw7_current_level][0], sizeof(amw7_cache_floor0)/W7_LEVEL_COUNT);
	if ((uArray==0) || (uArray==mapdataOfs + 0x930)) MEM_BlockRead(amw7_dataseg_addr + mapdataOfs + 0x930, &amw7_cache_floor1[amw7_current_level][0], sizeof(amw7_cache_floor1)/W7_LEVEL_COUNT);
	if ((uArray==0) || (uArray==mapdataOfs + 0x9B0)) MEM_BlockRead(amw7_dataseg_addr + mapdataOfs + 0x9B0, &amw7_cache_floor2[amw7_current_level][0], sizeof(amw7_cache_floor2)/W7_LEVEL_COUNT);
	if ((uArray==0) || (uArray==0x8C04))
		MEM_BlockRead((amw7_dataseg_addr + 0x8C04),
		              &amw7_cache_visited_cells[amw7_current_level][0],
		              sizeof(amw7_cache_visited_cells)/W7_LEVEL_COUNT);

	if ((uArray==0) || (uArray==mapdataOfs + 0x90) || (uArray==mapdataOfs + 0x290)){
		W7_DetectSolidTiles(amw7_current_level);
	}
	if ((uArray==0) || (uArray==0x8C04)){
		W7_CopyVisData(amw7_current_level);
	}


#ifdef SHOWALL
	for (int q = 0; q < W7_QUADRANT_COUNT; q++){
		for (int i = 0; i < 8; i++)
			for (int j = 0; j < 8; j++)
				amw7_spellPower[amw7_current_level][q][i][j] = 7;
	}
#endif
}

void W7_Mapping(int spellPower, bool useSkill, bool isLocateObjectActive){
	if (W7C_IsDarkZone(amw7_current_quadrant,amw7_current_qX,amw7_current_qY)) return;

	int EffectSizeInSquares = (useSkill) ? 15 : ((spellPower<=4) ? spellPower*2+3 : spellPower*2-7);
	int HalfOfEffectSizeInSquares = EffectSizeInSquares / 2;

	uint8_t qsx,qsy;
	qsx = amw7_cache_qsx[amw7_current_level][amw7_current_quadrant];
	qsy = amw7_cache_qsy[amw7_current_level][amw7_current_quadrant];

	uint16_t saved_level = amw7_level;
	amw7_level = amw7_current_level;
	if (isLocateObjectActive)
		spellPower = 1;
	for (int y = 0; y < EffectSizeInSquares; y++){
		for (int x = 0; x < EffectSizeInSquares; x++){
			int absX = qsx + amw7_current_qX - HalfOfEffectSizeInSquares + x;
			int absY = qsy + amw7_current_qY - HalfOfEffectSizeInSquares + y;

			uint16_t cquadrant;
			uint16_t cqX;
			uint16_t cqY;
			if (W7C_AbsToQuadrant(absX,absY,cquadrant,cqX,cqY)){
				//if (W7C_IsDarkZone(cquadrant,cqX,cqY)) continue;
				if (isLocateObjectActive)
				if (spellPower>amw7_spellPowerLO[amw7_current_level][cquadrant][cqX][cqY])
					amw7_spellPowerLO[amw7_current_level][cquadrant][cqX][cqY] = spellPower;
				if (spellPower>amw7_spellPower[amw7_current_level][cquadrant][cqX][cqY])
					amw7_spellPower[amw7_current_level][cquadrant][cqX][cqY] = spellPower;
			}
		}
	}
	amw7_level = saved_level;
}

void W7_Update(int XSize, int YSize){
	(void)XSize; (void)YSize;
	uint16_t W7GameStateNext = 0;
	mem_readw_checked((amw7_dataseg_addr + 0x9164), &W7GameStateNext);
	uint16_t W7GameState = 0;
	mem_readw_checked((amw7_dataseg_addr + 0x8410), &W7GameState);
	uint16_t tilesetIndex = 0;
	mem_readw_checked((amw7_dataseg_addr + 0x5C74), &tilesetIndex);
	amW7inGame = W7_IsInGame();

	// If not in game (e.g. main menu, save/quit), show blank window
	if (!amW7inGame) {
		SetAutomapWindowTitle("Wizardry 7 - Automap Mod");
		return;  // BeginFrame already cleared to black, EndFrame will present
	}

	if (amW7inGame){
		if (!amw7_mapIsLoading)
		if (amw7_map_data_dirty){
				amw7_map_data_dirty = false;
#ifdef SHOWALL
				for (int q = 0; q < W7_QUADRANT_COUNT; q++){
						uint8_t cqsx,cqsy;
						cqsx = amw7_cache_qsx[amw7_level][q];
						cqsy = amw7_cache_qsy[amw7_level][q];
						for (int i = 0; i < 8; i++){
							for (int j = 0; j < 8; j++){
								bool lw = false;
								bool rw = false;
								bool tw = false;
								bool bw = false;

								if (j==0){
									if (!W7C_PointAtLeftSideOfAnyQuadrant(cqsx-8,cqsy+i)){
										lw = true;
									} else {
										uint16_t nq,nqx,nqy;
										if (W7C_AbsToQuadrant(cqsx-1,cqsy+i,nq,nqx,nqy)){
											lw = W7VIS_IsWall( GetBitArrayElement(&amw7_cache_vwalls[amw7_level][0],nq*64 + nqy*8 + nqx,4));//ok
										}
									}
								} else {
									 lw = W7VIS_IsWall(GetBitArrayElement(&amw7_cache_vwalls[amw7_level][0],q*64 + i*8 + j-1,4));
								}
								rw = W7VIS_IsWall( GetBitArrayElement(&amw7_cache_vwalls[amw7_level][0],q*64 + i*8 + j,4));
								tw = W7VIS_IsWall( GetBitArrayElement(&amw7_cache_hwalls[amw7_level][0],q*64 + i*8 + j,4));
								if (i==0){
									if (!W7C_PointInAnyQuadrant(cqsx+j,cqsy-1)){
										bw = true;
									} else {
										uint16_t nq,nqx,nqy;
										if (W7C_AbsToQuadrant(cqsx+j,cqsy-1,nq,nqx,nqy)){
											bw = W7VIS_IsWall(GetBitArrayElement(&amw7_cache_hwalls[amw7_level][0],nq*64 + nqy*8 + nqx,4));
										}
									}
								} else {
									 bw = W7VIS_IsWall(GetBitArrayElement(&amw7_cache_hwalls[amw7_level][0],q*64 + (i-1)*8 + j,4));
								}


								amw7_visdata[amw7_level][q][j][i] = (lw && rw && tw && bw) ? 0 : 1;
							}
						}
				}
#endif
		}
	}

	SetAutomapWindowTitle("Wizardry 7 - Automap Mod");

	// SDL_Renderer: clear & projection handled by am_main.cpp BeginFrame()
if (amW7inGame){
		uint8_t tlcqx = amw7_cache_qsx[amw7_level][amw7_current_quadrant];
		uint8_t tlcqy = amw7_cache_qsy[amw7_level][amw7_current_quadrant];

		// Player's absolute position in map squares
		int playerAbsX = tlcqx + amw7_current_qX;
		int playerAbsY = tlcqy + amw7_current_qY;

		// Drawing code places quadrant at:
		//   screen_x = map_draw_x + tlqx*W7_SQUARE_SIZE
		//   screen_y = map_draw_y + 255*W7_SQUARE_SIZE - tlqy*W7_SQUARE_SIZE
		// So player pixel position is:
		//   px = map_draw_x + playerAbsX * W7_SQUARE_SIZE
		//   py = map_draw_y + 255*W7_SQUARE_SIZE - playerAbsY*W7_SQUARE_SIZE
		// We want px = SCREENW/2, py = SCREENH/2:
		map_draw_x = SCREENW/2 - playerAbsX * W7_SQUARE_SIZE;
		map_draw_y = SCREENH/2 - (255 - playerAbsY) * W7_SQUARE_SIZE;

		bool darkZone = W7C_IsDarkZone(amw7_current_quadrant,amw7_current_qX,amw7_current_qY);
		if (!darkZone){
			for (int q = 0; q < W7_QUADRANT_COUNT; q++){
				uint8_t tlqx,tlqy;
				tlqx = amw7_cache_qsx[amw7_level][q];
				tlqy = amw7_cache_qsy[amw7_level][q];
				int sx = map_draw_x + map_scroll_x + tlqx*W7_SQUARE_SIZE;
				int sy = map_draw_y + map_scroll_y + 255*W7_SQUARE_SIZE - tlqy*W7_SQUARE_SIZE;
				W7_DrawQuadrantFirstPass(sx, sy, q);
			}

			for (int q = 0; q < W7_QUADRANT_COUNT; q++){
				uint8_t tlqx,tlqy;
				tlqx = amw7_cache_qsx[amw7_level][q];
				tlqy = amw7_cache_qsy[amw7_level][q];
				W7_DrawQuadrantSecondPass(map_draw_x + map_scroll_x + tlqx*W7_SQUARE_SIZE, map_draw_y + map_scroll_y + 255*W7_SQUARE_SIZE - tlqy*W7_SQUARE_SIZE, q);
			}

			for (int q = 0; q < W7_QUADRANT_COUNT; q++){
				uint8_t tlqx,tlqy;
				tlqx = amw7_cache_qsx[amw7_level][q];
				tlqy = amw7_cache_qsy[amw7_level][q];
				W7_DrawQuadrantThirdPass(map_draw_x + map_scroll_x + tlqx*W7_SQUARE_SIZE, map_draw_y + map_scroll_y + 255*W7_SQUARE_SIZE - tlqy*W7_SQUARE_SIZE, q);
			}

			for (int q = 0; q < W7_QUADRANT_COUNT; q++){
				uint8_t tlqx,tlqy;
				tlqx = amw7_cache_qsx[amw7_level][q];
				tlqy = amw7_cache_qsy[amw7_level][q];
				W7_DrawQuadrantFourthPass(map_draw_x + map_scroll_x + tlqx*W7_SQUARE_SIZE, map_draw_y + map_scroll_y + 255*W7_SQUARE_SIZE - tlqy*W7_SQUARE_SIZE, q, amw7_current_qX, amw7_current_qY, ((amw7_current_quadrant==q) && (amw7_current_level==amw7_level)) );
			}

			if (amw7_jLevel==amw7_level){
				int qx = map_draw_x + map_scroll_x + amw7_jX*W7_SQUARE_SIZE;
				int qy = map_draw_y + map_scroll_y+ (255+7)*W7_SQUARE_SIZE - amw7_jY*W7_SQUARE_SIZE;
				float w = W7_SQUARE_SIZE-3;
				float h = W7_SQUARE_SIZE-3;

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

////////////////////////////////////////////////////////////////////////////////////////
// User Input
////////////////////////////////////////////////////////////////////////////////////////

bool W7_MousePositionToQuadrantPosition(int mx, int my, uint16_t &quadrant, uint16_t &qx, uint16_t &qy){
	int selAbsX = ((-map_draw_x-map_scroll_x) + mx) / W7_SQUARE_SIZE;
	int selAbsY = ((-map_draw_y-map_scroll_y) + my) / W7_SQUARE_SIZE;
	selAbsY -= 7;
	selAbsY = 255 - selAbsY;
	if (selAbsY < 0)
		selAbsY = 0;

	return W7C_AbsToQuadrant(selAbsX,selAbsY,quadrant,qx,qy);
}

void W7_OnAutomapDrag(int deltaX, int deltaY){
	if (!amW7inGame) return;

	map_scroll_x -= deltaX;
	map_scroll_y -= deltaY;
	if (map_scroll_x<W7_SQUARE_SIZE*(-256))
		map_scroll_x = W7_SQUARE_SIZE*(-256);
	if (map_scroll_x>W7_SQUARE_SIZE*256)
		map_scroll_x = W7_SQUARE_SIZE*256;
	if (map_scroll_y<W7_SQUARE_SIZE*(-256))
		map_scroll_y = W7_SQUARE_SIZE*(-256);
	if (map_scroll_y>W7_SQUARE_SIZE*256)
		map_scroll_y = W7_SQUARE_SIZE*256;
	amw7_need_update_once = true;
	AutoMapUpdate();
}

void TooltipForMainWindow_Show(bool show);
void TooltipForMainWindow_SetText(wchar_t *text);

char rus[128] = {
        0,          1,          2,          3,          4,          5,
        6,          7,          8,          9,          10,         11,
        12,         13,         14,         15,         16,         17,
        18,         19,         20,         21,         22,         'X',
        (char)0xB7, 25,         'x',        27,         (char)0xC2, (char)0xAB,
        (char)0xBB, (char)0xB0, ' ',        '!',        (char)0xB9, '#',
        '$',        '%',        '&',        '\'',       '(',        ')',
        '*',        '+',        ',',        '-',        '.',        '/',
        '0',        '1',        '2',        '3',        '4',        '5',
        '6',        '7',        '8',        '9',        ':',        ';',
        (char)0xF0, (char)0xF1, (char)0xF2, '?',        '@',        (char)0xC0,
        (char)0xC1, (char)0xC2, (char)0xC3, (char)0xC4, (char)0xC5, (char)0xC6,
        (char)0xC7, (char)0xC8, (char)0xC9, (char)0xCA, (char)0xCB, (char)0xCC,
        (char)0xCD, (char)0xCE, (char)0xCF, (char)0xD0, (char)0xD1, (char)0xD2,
        (char)0xD3, (char)0xD4, (char)0xD5, (char)0xD6, (char)0xD7, (char)0xD8,
        (char)0xD9, '[',        '\\',       ']',        '^',        '_',
        (char)0xE0, (char)0xE1, (char)0xE2, (char)0xE3, (char)0xE4, (char)0xE5,
        (char)0xE6, (char)0xE7, (char)0xE8, (char)0xE9, (char)0xEA, (char)0xEB,
        (char)0xEC, (char)0xED, (char)0xEE, (char)0xEF, (char)0xF0, (char)0xF1,
        (char)0xF2, (char)0xF3, (char)0xF4, (char)0xF5, (char)0xF6, (char)0xF7,
        (char)0xF8, (char)0xF9, (char)0xFA, '{',        '|',        '}',
        '~',        127};

void W7_Translate(wchar_t *outBuf, char *inBuf){
	if (amw7_rus){
		char *in = inBuf;
		while (*in){
			if (*in<128)
				*in = rus[*in];
			in++;
		}
		const uint32_t codepage = 866;
		//Ansi to wide char
		int bufLen;
	    if ((bufLen=MultiByteToWideChar( codepage, 0, inBuf, strlen(inBuf), NULL, 0 ))!=0){
	        if (MultiByteToWideChar( codepage, 0, inBuf, strlen(inBuf), (WCHAR*)outBuf, bufLen ) ){
			    return;
			}
		}
	}
	while (*inBuf){
		*outBuf = *inBuf;
		outBuf++;
		inBuf++;
	}
	*outBuf = *inBuf;
}

void W7_OnMouseMotionInMainWindow(RECT rc, int newX, int newY){
	if (!amw7_show_tooltips){
		TooltipForMainWindow_Show(false);
		return;
	}
	uint16_t W7GameState = 0;
	mem_readw_checked((amw7_dataseg_addr + 0x8410), &W7GameState);

	bool mouseInPopupWindow = false;
#define CONFIGWIN_X (35.0f / 640.0f)
#define CONFIGWIN_Y (84.0f / 400.0f)
#define CONFIGWIN_WIDTH (570.0f / 640.0f)
#define CONFIGWIN_HEIGHT (300.0f / 400.0f)
	int cwsx = (int)(rc.right * CONFIGWIN_X);
	int cwsy = (int)(rc.bottom * CONFIGWIN_Y);
	int cww = (int)(rc.right * CONFIGWIN_WIDTH);
	int cwh = (int)(rc.bottom * CONFIGWIN_HEIGHT);
	if (amw7_show_configwindow && (newY>cwsy) && (newX>cwsx) && (newX<cwsx+cww) && (newY<cwsy+cwh))
		mouseInPopupWindow = true;

#define SPELLWIN_X (98.0f / 640.0f)
#define SPELLWIN_Y (228.0f / 400.0f)
#define SPELLWIN_WIDTH (444.0f / 640.0f)
	int swsx = (int)(rc.right * SPELLWIN_X);
	int swsy = (int)(rc.bottom * SPELLWIN_Y);
	int sww = (int)(rc.right * SPELLWIN_WIDTH);
	if (amw7_show_spellwindow && (newY>swsy) && (newX>swsx) && (newX<swsx+sww))
		mouseInPopupWindow = true;
#define USEWIN_X (108.0f / 640.0f)
#define USEWIN_Y (284.0f / 400.0f)
#define USEWIN_WIDTH (424.0f / 640.0f)
	int uwsx = (int)(rc.right * USEWIN_X);
	int uwsy = (int)(rc.bottom * USEWIN_Y);
	int uww = (int)(rc.right * USEWIN_WIDTH);
	if (amw7_show_usewindow && (newY>uwsy) && (newX>uwsx) && (newX<uwsx+uww))
		mouseInPopupWindow = true;
	if (amw7_show_lootwindow && (newY>uwsy) && (newX>uwsx) && (newX<uwsx+uww))
		mouseInPopupWindow = true;
	if (amw7_show_sellwindow && (newY>uwsy) && (newX>uwsx) && (newX<uwsx+uww))
		mouseInPopupWindow = true;
	if (amw7_show_givewindow && (newY>uwsy) && (newX>uwsx) && (newX<uwsx+uww))
		mouseInPopupWindow = true;
	if (amw7_show_savewindow && (newY>uwsy) && (newX>uwsx) && (newX<uwsx+uww))
		mouseInPopupWindow = true;
	if (amw7_show_addcharacterwindow && (newY>uwsy) && (newX>uwsx) && (newX<uwsx+uww))
		mouseInPopupWindow = true;
	if (amw7_show_securitywindow && (newY>uwsy) && (newX>uwsx) && (newX<uwsx+uww))
		mouseInPopupWindow = true;
	if (amw7_show_loadwindow && (newY>uwsy) && (newX>uwsx) && (newX<uwsx+uww))
		mouseInPopupWindow = true;
	if (amw7_show_importwindow && (newY>uwsy) && (newX>uwsx) && (newX<uwsx+uww))
		mouseInPopupWindow = true;
#define UNLOCKWIN_X (108.0f / 640.0f)
#define UNLOCKWIN_Y (284.0f / 400.0f)
#define UNLOCKWIN_WIDTH (424.0f / 640.0f)
	int ulwsx = (int)(rc.right * UNLOCKWIN_X);
	int ulwsy = (int)(rc.bottom * UNLOCKWIN_Y);
	int ulww = (int)(rc.right * UNLOCKWIN_WIDTH);
	if ((W7GameStateOnOverlayLoad==26) && (amw7_show_unlockwindow) && (newY>ulwsy) && (newX>ulwsx) && (newX<ulwsx+ulww))//explore mode
		mouseInPopupWindow = true;
	if ((W7GameStateOnOverlayLoad==21) && (amw7_show_disarmwindow) && (newY>ulwsy) && (newX>ulwsx) && (newX<ulwsx+ulww))//explore mode
		mouseInPopupWindow = true;
#define BUYWIN_X (68.0f / 640.0f)
#define BUYWIN_Y (284.0f / 400.0f)
#define BUYWIN_WIDTH (504.0f / 640.0f)
	int bwsx = (int)(rc.right * BUYWIN_X);
	int bwsy = (int)(rc.bottom * BUYWIN_Y);
	int bww = (int)(rc.right * BUYWIN_WIDTH);
	if ((W7GameStateOnOverlayLoad==18) && (amw7_show_buywindow) && (newY>bwsy) && (newX>bwsx) && (newX<bwsx+bww))//explore mode
		mouseInPopupWindow = true;

	if (amw7_intro || amw7_mouseIsLocked || (!amW7inGame && (W7GameStateOnOverlayLoad!=4 /* Main Menu / Intro */) && (W7GameStateOnOverlayLoad!=7 /* Save / Quit */)) || (W7GameStateOnOverlayLoad==17 /* VPCVW.OVR Character Screen */) || (W7GameStateOnOverlayLoad==22 /* VPCLV.OVR Level Up Screen */) || mouseInPopupWindow) {
		TooltipForMainWindow_Show(false);
		return;
	}
#define PY (32.0f / 400.0f)
#define PWIDTH (142.0f / 640.0f)
#define PHEIGHT (92.0f / 400.0f)
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
	mem_readw_checked((amw7_dataseg_addr + 0x6ACE), &chaCount);

	static wchar_t char_tip[1024*10];
	char_tip[0] = 0;
	if ((num>=0) && (num<chaCount)){
		//num++;
		char name[8];
		uint16_t cofs = 0x5CFA + num*0x248;
		MEM_BlockRead(amw7_dataseg_addr + cofs, &name[0], 8);
		uint16_t chp,mhp;
		mem_readw_checked(amw7_dataseg_addr + cofs + 0x18,&chp);//Current HP
		mem_readw_checked(amw7_dataseg_addr + cofs + 0x1A,&mhp);//Maximum HP
		name[7] = 0;
		wchar_t wname[16];
		memset(wname,0,sizeof(wname));
		W7_Translate(wname,name);
		swprintf(char_tip,L"Name: %ls\r\nHP: %d/%d",&wname[0],chp,mhp);
		if ((W7GameState>=11) && (W7GameState<=14)) {
			uint8_t d;
			uint16_t pGroup;
			mem_readw_checked((amw7_dataseg_addr + 0x8588), &pGroup);
			mem_readb_checked(amw7_dataseg_addr + pGroup + 0x2C*num + 0x29,&d);
			swprintf(&char_tip[wcslen(char_tip)],L"\r\nInitiative: %d",(int8_t)d);
		}
		bool fc = true;
		for (int i = 0; i < 6; i++){
			uint16_t cmp,mmp;
			mem_readw_checked(amw7_dataseg_addr + cofs + 0x28 + i*4,&cmp);//Current MP
			mem_readw_checked(amw7_dataseg_addr + cofs + 0x28 + i*4 + 2,&mmp);//Maximum MP
			if (mmp>0){
				if (fc){
					swprintf(&char_tip[wcslen(char_tip)],L"\r\nMana points:");
					fc = false;
				}
				swprintf(&char_tip[wcslen(char_tip)],L"\r\n  %s %d/%d",&magicCats[i][0],cmp,mmp);
			}
		}
		//Offsets for characters:
		//  0xC - Exp
		//  0x10  - MKS
		//	0x14  - Money
		//  0x18  - Current HP
		//  0x1A  - Maximum HP
		//  0x1C  - Current Stamina
		//  0x1E  - Maximum Stamina
		//	0x20  - Current CC
		//  0x22  - Maximum CC
		//  0x24  - Level
		//  0x26  - Live
		//  0x28-0x3F  - MPs
		//  0x40  - Items(0x5C34,0x5C3B)
		//  0x166-0x17F  - Negative effects(bytes)
		//  0x170-0x177  - Attributes(bytes)
		//  0x178-0x199  - Skills(bytes)
		fc = true;
		for (int i = 0; i < 10; i++){
			uint8_t d;
			mem_readb_checked(amw7_dataseg_addr + cofs + 0x166 + i,&d);
			if (d>0){
				if (fc){
					swprintf(&char_tip[wcslen(char_tip)],L"\r\nNegative effects:");
					fc = false;
				}
				swprintf(&char_tip[wcslen(char_tip)],L"\r\n  %s(%d rounds)",&neffects[i][0],d);
			}
		}

		if ((W7GameStateOnOverlayLoad>=11) && (W7GameStateOnOverlayLoad<=14)) {
			uint8_t d;
			uint16_t pGroup;
			mem_readw_checked((amw7_dataseg_addr + 0x8588), &pGroup);
			mem_readb_checked(amw7_dataseg_addr + pGroup + 0x2C*num + 0x28,&d);
			if ((d & 0x80)!=0){
				if (fc){
					swprintf(&char_tip[wcslen(char_tip)],L"\r\nNegative effects:");
					fc = false;
				}
				swprintf(&char_tip[wcslen(char_tip)],L"\r\n  Weakened");
			}
		}
	} else {
#define EX (197.0f / 640.0f)
#define EWIDTH (248.0f / 640.0f)
#define EY (327.0f / 400.0f)
#define EHEIGHT ((60.0f) / 400.0f)
		int esx = (int)(rc.right * EX);
		int esy = (int)(rc.bottom * EY);
		int ew = (int)(rc.right * EWIDTH);
		int eh = (int)(rc.bottom * EHEIGHT);

		if (amw7_show_enemylist && (W7GameStateOnOverlayLoad==12))
		if ((newX>esx) && (newX<esx+ew))
		if ((newY>esy) && (newY<esy+eh)){
			nEnemy = ((newY-esy) / (eh/5));
			if (nEnemy>4) nEnemy = 4;
			uint16_t gtAddr = 0x8598 + nEnemy*2;//Template
			uint16_t pTemplate = 0;
			uint16_t gAddr = 0x858A + nEnemy*2;
			uint16_t pGroup = 0;
			uint8_t eCount = 0;
			mem_readw_checked(amw7_dataseg_addr + gtAddr, &pTemplate);
			mem_readw_checked(amw7_dataseg_addr + gAddr, &pGroup);
			mem_readb_checked(amw7_dataseg_addr + pGroup + 0x1A1,&eCount);

			uint8_t dn = 0;
			mem_readb_checked(amw7_dataseg_addr + pGroup + 0x1A0, &dn);//0x1A0: 0=known monster; 1=unknown monster

			/* Groub Member Array [gAddr]
			       0x0       word        Level
				   0x2       word        Current HP
				   0x4       word        Maximum HP

				   0xE       byte[10]    negative effects
				   0x18		 ?

				   0x29		 byte        Current Initiative

				   0x18C     byteArray[6]
				   0x192     byteArray[6]
				   0x198     byteArray[6]

				   0x1A1     byte		 Count of members
				   0x1A2     byte        ?? Count of ready to fight members ??
			*/

			if (eCount>0){
				int nSubEnemy = (newX-esx) / (ew / eCount);
				uint16_t curHP = 0, maxHP = 0, curStamina = 0, maxStamina = 0;
				char ename[16];
				MEM_BlockRead(amw7_dataseg_addr + pTemplate + 32*dn, &ename[0], 16);
				mem_readw_checked(amw7_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x2,&curHP);
				mem_readw_checked(amw7_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x4,&maxHP);
				mem_readw_checked(amw7_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x6,&curStamina);
				mem_readw_checked(amw7_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x8,&maxStamina);
				wchar_t wename[32];
				memset(wename,0,sizeof(wename));
				W7_Translate(wename,ename);
				char_tip[0] = 0;
				if (eCount>1){
					swprintf(&char_tip[wcslen(char_tip)],L"%ls[%d/%d]",&wename[0],nSubEnemy+1,eCount);
				} else {
					swprintf(&char_tip[wcslen(char_tip)],L"%ls",&wename[0]);
				}
				swprintf(&char_tip[wcslen(char_tip)],L"\r\nHP: %d/%d",curHP,maxHP);

				uint8_t d;
				mem_readb_checked(amw7_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x29,&d);
				swprintf(&char_tip[wcslen(char_tip)],L"\r\nInitiative: %d",(int8_t)d);

				bool fc = true;
				for (int i = 0; i < 10; i++){
					mem_readb_checked(amw7_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0xE + i,&d);
					if (d>0){
						if (fc){
							swprintf(&char_tip[wcslen(char_tip)],L"\r\nNegative effects:");
							fc = false;
						}
						swprintf(&char_tip[wcslen(char_tip)],L"\r\n  %s(%d rounds)",&neffects[i][0],d);
					}
				}
				mem_readb_checked(amw7_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x28,&d);
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

void TooltipForAutomapWindow_Show(bool show);
void TooltipForAutomapWindow_SetText(wchar_t *text);

void W7_OnMouseMotionInAutomapWindow(int newX, int newY, bool alt){
	if (!amW7inGame){
		TooltipForAutomapWindow_Show(false);
		return;
	}

	if (W7C_IsDarkZone(amw7_current_quadrant,amw7_current_qX,amw7_current_qY)){
		TooltipForAutomapWindow_Show(false);
		return;
	}

	if (!alt){
		uint16_t squadrant = 0;
		uint16_t sqx = 0;
		uint16_t sqy = 0;
		bool qexists = W7_MousePositionToQuadrantPosition(newX,newY,squadrant,sqx,sqy);
		bool selc = (qexists && (amw7_level==amw7_current_level) && (squadrant==amw7_current_quadrant) && (sqx==amw7_current_qX) && (sqy==amw7_current_qY));
		int n = (qexists) ? W7_FindNote(squadrant,sqx,sqy) : -1;
        // Update the text.
        wchar_t *text = (selc) ? (wchar_t*)L"Current Position" : (wchar_t*)L"Invisible";// coords;
		if (n>=0)
			text = &amw7_notes[amw7_level][n].str[0];
		TooltipForAutomapWindow_SetText(text);
		TooltipForAutomapWindow_Show(selc || (n>=0));
	}

	if (alt){
		uint16_t squadrant = 0;
		uint16_t sqx = 0;
		uint16_t sqy = 0;
		if (W7_MousePositionToQuadrantPosition(newX,newY,squadrant,sqx,sqy)){
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

void W7_OnMouseButtonInAutomapWindow(SDL_MouseButtonEvent * button, bool alt, bool ctrl){
	if (!amW7inGame) return;

	if (W7C_IsDarkZone(amw7_current_quadrant,amw7_current_qX,amw7_current_qY)){
		TooltipForAutomapWindow_Show(false);
		return;
	}

	if (button->button==SDL_BUTTON_MIDDLE){
		amw7_level = amw7_current_level;
		map_scroll_x = 0;
		map_scroll_y = 0;
		amw7_need_update_once = true;
		AutoMapUpdate();
	}
	uint16_t squadrant = 0;
	uint16_t sqx = 0;
	uint16_t sqy = 0;
	if ((alt) && (!ctrl))
	if (button->clicks!=0)
	if (W7_MousePositionToQuadrantPosition(button->x, button->y, squadrant, sqx, sqy)){
		if ((button->button==SDL_BUTTON_LEFT)){
			char buf[100];
			sprintf(buf,"{%d:%d:%d:%d}",amw7_level,squadrant,sqx,sqy);
			SDL_SetClipboardText(buf);
		}
	}

	if ((!alt) && (ctrl))
	if (button->clicks!=0)
	if (W7_MousePositionToQuadrantPosition(button->x, button->y, squadrant, sqx, sqy)){
		if ((button->button==SDL_BUTTON_LEFT)){
			int n = W7_FindNote(squadrant,sqx,sqy);
			if (n>=0){
				const wchar_t *str = amw7_notes[amw7_level][n].str.c_str();
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
						if ((jLevel>=0) && (jLevel<W7_LEVEL_COUNT) && (jQuadrant>=0) && (jQuadrant<W7_QUADRANT_COUNT) && (jqX>=0) && (jqX<8) && (jqY>=0) && (jqY<8)){
							amw7_level = jLevel;
							W7_DetectSolidTiles(jLevel);
							uint8_t tlcqx,tlcqy;
							tlcqx = amw7_cache_qsx[amw7_level][jQuadrant];
							tlcqy = amw7_cache_qsy[amw7_level][jQuadrant];
							amw7_jLevel = amw7_level;
							amw7_jX = tlcqx + jqX;
							amw7_jY = tlcqy + jqY;

							int JDX = ( -(((tlcqx+jqX)*W7_SQUARE_SIZE) - ((SCREENW/2)-(W7_SQUARE_SIZE/2))) );
							int JDY = ( -(((255+8)*W7_SQUARE_SIZE - (tlcqy+jqY)*W7_SQUARE_SIZE) - ((SCREENH/2)-(W7_SQUARE_SIZE/2))) );
							map_scroll_x = JDX - map_draw_x;
							map_scroll_y = JDY - map_draw_y;

							amw7_need_update_once = true;
							AutoMapUpdate();
						}
					}
				}
			}
		}
	}

	if ((!alt) && (!ctrl))
	if (button->clicks!=0)
	if (W7_MousePositionToQuadrantPosition(button->x, button->y, squadrant, sqx, sqy)){
		int n = W7_FindNote(squadrant,sqx,sqy);
		if ((button->button==SDL_BUTTON_LEFT)){
				TooltipForAutomapWindow_Show(false);

				std::wstring ibuf;
				ibuf.resize(1024);
				ibuf[0] = 0;
				if (n>=0){
					wcscpy(&ibuf[0],amw7_notes[amw7_level][n].str.c_str());
				}
				if (InputBox(L"Enter comment",L"if you leave it blank, it will remove marker from map",&ibuf[0],1023)){
					if (n>=0){
						if (wcslen(&ibuf[0])>0)
							amw7_notes[amw7_level][n].str = std::wstring(&ibuf[0]);
						else {
							amw7_notes[amw7_level].erase(amw7_notes[amw7_level].begin() + n);
						}
					} else
					if (wcslen(&ibuf[0])>0)
					{
						AMW7Note note;
						note.quadrant = squadrant;
						note.qX = sqx;
						note.qY = sqy;
						note.str = &ibuf[0];
						note.color = 0xFF0000FF;
						amw7_notes[amw7_level].push_back(note);
					}
					W7_NativeSaveNotes();
					amw7_need_update_once = true;
					AutoMapUpdate();
				}
		} else
		if ((button->button==SDL_BUTTON_RIGHT) && (n>=0)){
				TooltipForAutomapWindow_Show(false);

				if (ChooseColorDialog(amw7_notes[amw7_level][n].color,wiz7_palette)){
					amw7_need_update_once = true;
					AutoMapUpdate();
				}
		} else
		if ((button->button==SDL_BUTTON_RIGHT) && (n<0)){
			int r = MessageBoxA(NULL,
				"Clear all map notes for Wizardry 7?\n\nThis cannot be undone.",
				"Automap - Clear Notes",
				MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
			if (r == IDYES) {
				for (int i = 0; i < W7_LEVEL_COUNT; i++) {
					amw7_notes[i].clear();
					memset(&amw7_cache_visited_cells[i][0], 0, sizeof(amw7_cache_visited_cells)/W7_LEVEL_COUNT);
				}
				W7_NativeSaveNotes();
				amw7_need_update_once = true;
				AutoMapUpdate();
			}
		}
	}
}

void W7_OnOverlayLoad()
{
	if (!s_dataseg_scanned) {
		s_dataseg_scanned = true;
		for (uint32_t offset = 0; offset < 0x80000; offset++) {
			uint8_t c = 0;
			mem_readb_checked(am_loadaddress + offset, &c);
			if (c != 'I') {
				continue;
			}
			char buf[16] = {};
			for (int i = 0; i < 15; i++) {
				uint8_t b = 0;
				mem_readb_checked(am_loadaddress + offset + i, &b);
				buf[i] = b;
			}
			if (strncmp(buf, "INSERT SAVEGAME", 15) == 0) {
				amw7_dataseg_addr = am_loadaddress + offset - 0xB74;
				break;
			}
		}
	}
	prevW7GameStateOnOverlayLoad = W7GameStateOnOverlayLoad;
	mem_readw_checked((amw7_dataseg_addr + 0x8410), &W7GameStateOnOverlayLoad);

	if (oW7GameStateOnOverlayLoad != W7GameStateOnOverlayLoad) {
		oW7GameStateOnOverlayLoad = W7GameStateOnOverlayLoad;
		amw7_need_update_once = true;
	}
#ifdef DEVMODE
	if ((W7GameStateOnOverlayLoad==3) || (W7GameStateOnOverlayLoad==9) || (W7GameStateOnOverlayLoad==25) || (W7GameStateOnOverlayLoad==29) || (W7GameStateOnOverlayLoad==30)){
		char buf[100];
		sprintf(buf,"Unknown W7GameStateOnOverlayLoad=%d",W7GameStateOnOverlayLoad);
		MessageBoxA(0,buf,"Unknown Game State",0);
	}
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////
// Save/Load
////////////////////////////////////////////////////////////////////////////////////////////////

void W7_NewGame(){

	amw7_current_level = 0xFFFF; // Party position: map index
	amw7_current_quadrant = 0; // Party position: quadrant
	amw7_current_qX = 0;
	amw7_current_qY = 0;
	amw7_current_dir = 0;

	oamw7_current_level = 0xFFFF;
	oamw7_current_quadrant = 0;
	oamw7_current_qX = 0;
	oamw7_current_qY = 0;
	oamw7_current_dir = 0;

	memset(wiz7_palette, 0, sizeof(wiz7_palette));
	memset(amw7_visdata, 0, sizeof(amw7_visdata));
	memset(amw7_spellPower, 0, sizeof(amw7_spellPower));
	memset(amw7_spellPowerLO, 0, sizeof(amw7_spellPowerLO));

//link target:
	amw7_jLevel = 0xFFFF;
	amw7_jX = 0;
	amw7_jY = 0;
/* Wizardry 7 Cache */
	memset(amw7_cache_hwalls, 0, sizeof(amw7_cache_hwalls));
	memset(amw7_cache_vwalls, 0, sizeof(amw7_cache_vwalls));
	memset(amw7_cache_qsx, 0, sizeof(amw7_cache_qsx));
	memset(amw7_cache_qsy, 0, sizeof(amw7_cache_qsy));
	memset(amw7_cache_features, 0, sizeof(amw7_cache_features));
	memset(amw7_cache_features_dirs, 0, sizeof(amw7_cache_features_dirs));
	memset(amw7_cache_floor0, 0, sizeof(amw7_cache_floor0));
	memset(amw7_cache_floor1, 0, sizeof(amw7_cache_floor1));
	memset(amw7_cache_floor2, 0, sizeof(amw7_cache_floor2));
	memset(amw7_cache_visited_cells, 0, sizeof(amw7_cache_visited_cells));
	amw7_map_data_dirty = false;
	amw7_mapIsLoading = false;

	for (int i = 0; i < W7_LEVEL_COUNT; i++){
		amw7_notes[i].clear();
	}
}

bool amw7_saveLock = false;

void W7_Save(const char *fullName){
	(void)fullName;
	if (!amw7_isSaving) return;
	if (amw7_saveLock) return;
	amw7_saveLock = true;
	W7_NativeSaveNotes();
	amw7_saveLock = false;
}

bool amw7_loadLock = false;

void W7_Load(const char *){
	if (!amw7_isLoading) return;
	if (amw7_loadLock) return;
	amw7_loadLock = true;

	// Old Automap behavior: reset Automap state only from the DOS-open based
	// W7_Load path while amw7_isLoading is true.  Do not synthesize this from
	// the OpenSaveFileStart breakpoint; that changed BP callback semantics.
	W7_NewGame();
	W7_NativeLoadNotes();

	amw7_loadLock = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////
// Breakpoints/Interrupts
////////////////////////////////////////////////////////////////////////////////////////////////

void W7BP_OnShowEnemyList(){
	amw7_show_enemylist = true;
}

void W7BP_OnHideEnemyList(){
	amw7_show_enemylist = false;
}

void W7BP_OnShowSpellWindow(){
	amw7_show_spellwindow = true;
}

void W7BP_OnHideSpellWindow(){
	amw7_show_spellwindow = false;
}

void W7BP_OnShowUseWindow(){
	amw7_show_usewindow = true;
}

void W7BP_OnHideUseWindow(){
	amw7_show_usewindow = false;
}

void W7BP_OnHideUnlockWindow(){
	amw7_show_unlockwindow = false;
}

void W7BP_OnShowUnlockWindow(){
	amw7_show_unlockwindow = true;
}

void W7BP_OnShowDisarmWindow(){
	amw7_show_disarmwindow = true;
}

void W7BP_OnHideDisarmWindow(){
	amw7_show_disarmwindow = false;
}

void W7BP_OnShowLootWindow(){
	amw7_show_lootwindow = true;
}

void W7BP_OnHideLootWindow(){
	amw7_show_lootwindow = false;
}

void W7BP_OnShowBuyWindow(){
	amw7_show_buywindow = true;
}

void W7BP_OnHideBuyWindow(){
	amw7_show_buywindow = false;
}

void W7BP_OnShowSellWindow(){
	amw7_show_sellwindow = true;
}

void W7BP_OnHideSellWindow(){
	amw7_show_sellwindow = false;
}

void W7BP_OnShowGiveWindow(){
	amw7_show_givewindow = true;
}

void W7BP_OnHideGiveWindow(){
	amw7_show_givewindow = false;
}

void W7BP_OnShowSaveWindow(){
	amw7_show_savewindow = true;
}

void W7BP_OnHideSaveWindow(){
	amw7_show_savewindow = false;
}

void W7BP_OnShowConfigWindow(){
	amw7_show_configwindow = true;
}

void W7BP_OnHideConfigWindow(){
	amw7_show_configwindow = false;
}

void W7BP_OnShowAddCharacterWindow(){
	amw7_show_addcharacterwindow = true;
}

void W7BP_OnHideAddCharacterWindow(){
	amw7_show_addcharacterwindow = false;
}

void W7BP_OnShowSecurityWindow(){
	amw7_show_securitywindow = true;
}

void W7BP_OnHideSecurityWindow(){
	amw7_show_securitywindow = false;
}

void W7BP_OnShowLoadWindow(){
	amw7_show_loadwindow = true;
}

void W7BP_OnHideLoadWindow(){
	amw7_show_loadwindow = false;
}

void W7BP_OnShowImportWindow(){
	amw7_show_importwindow = true;
}

void W7BP_OnHideImportWindow(){
	amw7_show_importwindow = false;
}

void W7BP_OnIntroStart(){
	amw7_intro = true;
}

void W7BP_OnIntroEnd(){
	amw7_intro = false;
}

void W7BP_OnCreateSaveFileStart(){
	amw7_isSaving = true;
}

void W7BP_OnCreateSaveFileEnd(){
	amw7_isSaving = false;
}

void W7BP_OnOpenSaveFileStart(){
	amw7_isLoading = true;
}

void W7BP_OnOpenSaveFileEnd(){
	amw7_isLoading = false;
}

void AMW7BP_OnMouseLock(){
	amw7_mouseIsLocked = true;
}

void AMW7BP_OnMouseUnlock(){
	amw7_mouseIsLocked = false;
}

void AMW7BP_OnBitArrayModification(){
	uint16_t pArray = 0;
	uint16_t mapdataOfs = 0;
	mem_readw_checked(SegPhys(ss)+reg_bp+4,&pArray);
	mem_readw_checked((amw7_dataseg_addr + 0x0E30), &mapdataOfs);
	if ((pArray==mapdataOfs+0x90) || //h. walls
		(pArray==mapdataOfs+0x290) || //v. walls
		(pArray==mapdataOfs+0x4B0) || //features
		(pArray==mapdataOfs+0x7B0) || //dirs of features
		(pArray==mapdataOfs+0x8B0) || //floor0
		(pArray==mapdataOfs+0x930) || //floor1
		(pArray==mapdataOfs+0x9B0) || // floor2
		(pArray==0x8C04)){
		W7_UpdateCache(pArray);
		amw7_map_data_dirty = true;
	}
}

void W7BP_OnMapLoadStart(){
	amw7_mapIsLoading = true;
}

void W7BP_OnMapLoadEnd(){
	amw7_mapIsLoading = false;
	oamw7_current_level = amw7_current_level;
	mem_readw_checked((amw7_dataseg_addr + 0x8412), &amw7_current_level);
	amw7_map_data_dirty = true;
	W7_UpdateCache(0);

	//Reset scroll
	amw7_level = amw7_current_level;
	map_scroll_x = 0;
	map_scroll_y = 0;
}

bool W7_PChasJourneyMapKit(uint16_t pcIndex){
	uint16_t cofs = 0x6AD0 + pcIndex * 0x248;
	for (int i = 0; i < 20; i++){//Inv+Swag
		uint16_t itemID = 0;
		mem_readw_checked(amw7_dataseg_addr + cofs + 0x1A4 + i*2, &itemID);
		if (itemID==411) return true;
	}
	return false;
}

void W7_ApplyMappingSkill(){
	uint8_t maxMappingSkill = 0;
	uint16_t chaCount = 0;
	mem_readw_checked((amw7_dataseg_addr + 0x6ACE), &chaCount);
	for (uint16_t c = 0; c < chaCount; c++){
		if (!W7_PChasJourneyMapKit(c)) continue;

		uint8_t skill = 0;
		mem_readb_checked((amw7_dataseg_addr + 0x5E8C) + c*0x248, &skill);
		if (skill > maxMappingSkill)
			maxMappingSkill = skill;
	}
	if (maxMappingSkill==0) return;
	uint16_t spellPower = 1;
	uint8_t skillLevel = 10;
	for (int i = 0; i < 3; i++){
		if (maxMappingSkill>=skillLevel)
			spellPower++;
		skillLevel = (skillLevel*2) + 10;
	}
	W7_Mapping(spellPower, true, false);
}

void W7_UpdateVisData(){
	if (!W7C_IsDarkZone(amw7_current_level, amw7_current_quadrant, amw7_current_qX,amw7_current_qY))
		amw7_visdata[amw7_current_level][amw7_current_quadrant][amw7_current_qX][amw7_current_qY] = 1;

	uint8_t qsx = amw7_cache_qsx[amw7_current_level][amw7_current_quadrant];
	uint8_t qsy = amw7_cache_qsy[amw7_current_level][amw7_current_quadrant];

	uint16_t hvq,hvqx,hvqy;
	if ((W7C_AbsToQuadrant(qsx+amw7_current_qX,  qsy+amw7_current_qY-1,hvq,hvqx,hvqy)) && (amw7_visdata[amw7_level][hvq][hvqx][hvqy]==0)){
		if (W7C_BottomIsVisible(qsx+amw7_current_qX,  qsy+amw7_current_qY))
			if (!W7C_IsDarkZone(hvq,hvqx,hvqy))
				amw7_visdata[amw7_level][hvq][hvqx][hvqy] = 2;
	}
	if ((W7C_AbsToQuadrant(qsx+amw7_current_qX-1,qsy+amw7_current_qY,hvq,hvqx,hvqy)) && (amw7_visdata[amw7_level][hvq][hvqx][hvqy]==0)){
		if (W7C_LeftIsVisible(qsx+amw7_current_qX,  qsy+amw7_current_qY))
			if (!W7C_IsDarkZone(hvq,hvqx,hvqy))
				amw7_visdata[amw7_level][hvq][hvqx][hvqy] = 2;
	}
	if ((W7C_AbsToQuadrant(qsx+amw7_current_qX+1,qsy+amw7_current_qY,hvq,hvqx,hvqy)) && (amw7_visdata[amw7_level][hvq][hvqx][hvqy]==0)){
		if (W7C_RightIsVisible(qsx+amw7_current_qX,  qsy+amw7_current_qY))
			if (!W7C_IsDarkZone(hvq,hvqx,hvqy))
				amw7_visdata[amw7_level][hvq][hvqx][hvqy] = 2;
	}
	if ((W7C_AbsToQuadrant(qsx+amw7_current_qX,  qsy+amw7_current_qY+1,hvq,hvqx,hvqy)) && (amw7_visdata[amw7_level][hvq][hvqx][hvqy]==0)){
		if (W7C_TopIsVisible(qsx+amw7_current_qX,  qsy+amw7_current_qY))
			if (!W7C_IsDarkZone(hvq,hvqx,hvqy))
				amw7_visdata[amw7_level][hvq][hvqx][hvqy] = 2;
	}
}

void W7BP_OnMapRedraw(){
	mem_readw_checked((amw7_dataseg_addr + 0x9048), &amw7_current_qX);
	mem_readw_checked((amw7_dataseg_addr + 0x9046), &amw7_current_qY);
	mem_readw_checked((amw7_dataseg_addr + 0x9044), &amw7_current_quadrant);
	mem_readw_checked((amw7_dataseg_addr + 0x83F4), &amw7_current_dir);
	const bool positionChanged =
		(oamw7_current_quadrant != amw7_current_quadrant) ||
		(oamw7_current_qX != amw7_current_qX) ||
		(oamw7_current_qY != amw7_current_qY) ||
		(oamw7_current_dir != amw7_current_dir);

	if (positionChanged){

		oamw7_current_quadrant = amw7_current_quadrant;
		oamw7_current_qX = amw7_current_qX;
		oamw7_current_qY = amw7_current_qY;
		oamw7_current_dir = amw7_current_dir;

		//Reset scroll
		amw7_level = amw7_current_level;
		map_scroll_x = 0;
		map_scroll_y = 0;

		W7_ApplyMappingSkill();
		W7_UpdateVisData();

		amw7_map_data_dirty = true;
	}
}

void W7BP_OnCastSpell(){
	uint16_t playerIndex = 0;
	uint16_t spellID = 0;
	uint16_t spellPower = 0;
	uint16_t unknown = 0;
	mem_readw_checked(SegPhys(ss)+reg_bp+4,&playerIndex);
	mem_readw_checked(SegPhys(ss)+reg_bp+6,&spellID);
	mem_readw_checked(SegPhys(ss)+reg_bp+8,&spellPower);
	mem_readw_checked(SegPhys(ss)+reg_bp+0xA,&unknown);

	if (spellID==72){//Wizard's Eye      (unknown==0) ?
		W7_Mapping(spellPower,false,false);
		amw7_need_update_once = true;
	} else
	if (spellID==75){//Locate Object
		W7_Mapping(spellPower,false,true);
		amw7_need_update_once = true;
	} else
	if (spellID==99){//Map      -  Kit      (SpellPower==0)      (unknown==0) ?

	}
}
