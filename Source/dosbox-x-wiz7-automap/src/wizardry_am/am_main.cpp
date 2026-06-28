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
 *  -- Fixes to exploration progress (fog of war) in Wiz6 so it works and saves correctly.
 *  -- Added a right-click option to purge saved exploration progress in Wiz6.
 *  -- No Wiz7 exploration purge added as it is handled by the game itself.
 *  -- Added a right-click option to delete all Notes in Wiz6 and Wiz7.
 *
 */

#include "automap.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <vector>
#include <algorithm>
#if defined(_WIN32)
# include <strings.h>
#endif


// DOSBox Staging public headers
#include "dosbox.h"
#include "mem.h"
#include "cpu.h"
#include "regs.h"
#include "callback.h"
#include "setup.h"
#include "control.h"
#include "support.h"
#include "pic.h"

// SDL2
#include <SDL.h>

#ifdef WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <SDL_syswm.h>
#  include <windows.h>
#  include <commctrl.h>
#  include <commdlg.h>
#endif

#include "InputBox.h"


// ── Game identification ───────────────────────────────────────────────────────

enum class AutoMapGame { None = 0, Wizardry6, Wizardry7 };
static AutoMapGame s_amGame = AutoMapGame::None;

static const char* s_amGameName[] = { "undetected", "Wizardry 6", "Wizardry 7" };

// ── Config / window state ─────────────────────────────────────────────────────

bool am_show_automap     = false;
int  am_width            = 512;
int  am_height           = 512;

extern bool amw6_show_tooltips;
extern bool amw6_hide_in_dark_zones;
extern bool amw7_show_tooltips;
extern bool amw7_hide_in_dark_zones;
extern bool amw7_use_spellPower;
extern bool amw7_rus;

// ── SDL_Renderer window & resources ──────────────────────────────────────────

static SDL_Window*   s_amWindow   = nullptr;
static SDL_Renderer* s_amRenderer = nullptr;
static uint32_t      s_amWindowID = 0;

// Sprite-atlas textures (one per game)
static SDL_Texture*  s_texW6      = nullptr;   // 256×16 RGBA atlas (Wiz6 tiles)
static SDL_Texture*  s_texW7      = nullptr;   // 256×32 RGBA atlas (Wiz7 tiles)

extern unsigned long  tiles32[];        // from am_wiz6_res.cpp
extern int            tiles32_count;
extern unsigned long  w7tiles32[];      // from am_wiz7_res.cpp
extern int            w7tiles32_count;


// ── Color palette for solid-color drawing ────────────────────────────────────
// Mirrors the original glColor3f table in DrawRectGL.

static const SDL_Color s_palette[] = {
    {   0,   0,   0, 255 }, // -1  black         (background / no feature)
    { 255,   0,   0, 255 }, //  0  red            (no feature)
    {   0, 255,   0, 255 }, //  1  green          (stairs up)
    { 255, 255,   0, 255 }, //  2  yellow         (stairs down)
    {   0,   0, 255, 255 }, //  3  blue           (sconce)
    { 255,   0, 255, 255 }, //  4  magenta        (fountain)
    {   0, 255, 255, 255 }, //  5  cyan
    {  64,  64,  64, 255 }, //  6  dark grey
    { 191, 191, 191, 255 }, //  7  light grey
    { 153, 102,  31, 255 }, //  8  brown
    { 250, 159,  31, 255 }, //  9  pumpkin orange (niche)
    { 250,  10, 179, 255 }, // 10  hot pink
    { 153, 102, 179, 255 }, // 11  purple
    { 255, 255, 255, 255 }, // 12+ white (default)
};
static constexpr int PALETTE_SIZE = (int)(sizeof(s_palette) / sizeof(s_palette[0]));

static SDL_Color IndexToColor(int idx) {
    // idx -1 maps to index 0 (black)
    int i = idx + 1;
    if (i < 0 || i >= PALETTE_SIZE) i = PALETTE_SIZE - 1;
    return s_palette[i];
}

// ── Drawing primitives ────────────────────────────────────────────────────────
// These replace every glBegin/glEnd call in the original am_main.cpp.

void AM_DrawRect(int x, int y, int w, int h, int colorIndex)
{
    if (!s_amRenderer) return;
    SDL_Color c = IndexToColor(colorIndex);
    SDL_SetRenderDrawColor(s_amRenderer, c.r, c.g, c.b, c.a);
    SDL_Rect r = { x, y, w, h };
    SDL_RenderFillRect(s_amRenderer, &r);
}

// Draw a sub-region of a sprite atlas at (x,y) with size (w,h).
// u0,v0,u1,v1 are normalised UV coordinates in [0,1].
// The atlas pixel dimensions must be passed so we can convert to SDL_Rect.
// 'dark' dims the quad to 50% brightness.

void AM_DrawSprite(int x, int y, int w, int h,
                   float u0, float v0, float u1, float v1,
                   SDL_Texture* tex,
                   int atlasW, int atlasH,
                   bool dark = false,
                   SDL_RendererFlip flip = SDL_FLIP_NONE)
{
    if (!s_amRenderer || !tex) return;

    int sx = (int)(u0 * atlasW);
    int sy = (int)(v0 * atlasH);
    int sw = (int)((u1 - u0) * atlasW);
    int sh = (int)((v1 - v0) * atlasH);
    if (sw <= 0 || sh <= 0) return;

    SDL_SetTextureColorMod(tex,
        dark ? 128 : 255,
        dark ? 128 : 255,
        dark ? 128 : 255);

    SDL_Rect src  = { sx, sy, sw, sh };
    SDL_Rect dest = {  x,  y,  w,  h };
    SDL_RenderCopyEx(s_amRenderer, tex, &src, &dest, 0, nullptr, flip);

    SDL_SetTextureColorMod(tex, 255, 255, 255);
}

// Wiz6 atlas is 256×16 px.  Wiz7 atlas is 256×32 px.
// Expose named wrappers so am_wiz6.cpp / am_wiz7.cpp don't need to know the
// atlas dimensions.

void AM_DrawW6Sprite(int x, int y, int w, int h,
                     float u0, float v0, float u1, float v1,
                     bool dark = false,
                     SDL_RendererFlip flip = SDL_FLIP_NONE)
{
    AM_DrawSprite(x, y, w, h, u0, v0, u1, v1, s_texW6, 256, 16, dark, flip);
}

void AM_DrawW7Sprite(int x, int y, int w, int h,
                     float u0, float v0, float u1, float v1,
                     bool dark = false,
                     SDL_RendererFlip flip = SDL_FLIP_NONE)
{
    AM_DrawSprite(x, y, w, h, u0, v0, u1, v1, s_texW7, 256, 32, dark, flip);
}

// ── Texture upload ────────────────────────────────────────────────────────────
// The resource files store 32-bit RGBA pixels in arrays of unsigned long.
// We need them as SDL_PIXELFORMAT_RGBA8888 (or ABGR depending on endianness).
// The original code ORed 0xFF000000 to set alpha, matching OpenGL GL_RGBA.

static SDL_Texture* UploadAtlas(SDL_Renderer* ren,
                                unsigned long* pixels, int count,
                                int atlasW, int atlasH)
{
    // Make a local copy so we don't modify the read-only source array
    std::vector<uint32_t> buf(count);
    for (int i = 0; i < count; ++i) {
        uint32_t p = (uint32_t)pixels[i];
        if (p == 0) { buf[i] = 0; continue; }
        // Data is 0x00BBGGRR, set alpha to FF → swap R and B for ARGB8888 (0xFFRRGGBB)
        uint8_t b = (p >> 16) & 0xFF;
        uint8_t g = (p >>  8) & 0xFF;
        uint8_t r = (p      ) & 0xFF;
        buf[i] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }

    SDL_Texture* tex = SDL_CreateTexture(ren,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STATIC,
        atlasW, atlasH);
    if (!tex) {
        return nullptr;
    }

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(tex, nullptr, buf.data(), atlasW * 4);
    return tex;
}

// ── Renderer helpers ──────────────────────────────────────────────────────────

static void BeginFrame()
{
    if (!s_amRenderer) return;
    SDL_SetRenderDrawColor(s_amRenderer, 0, 0, 0, 255);
    SDL_RenderClear(s_amRenderer);
    // Logical size lets all drawing code use fixed coordinates regardless of
    // how the user has resized the automap window.
    SDL_RenderSetLogicalSize(s_amRenderer, am_width, am_height);
}

static void EndFrame()
{
    if (!s_amRenderer) return;
    SDL_RenderPresent(s_amRenderer);
}

// ── Window title helper ───────────────────────────────────────────────────────

void SetAutomapWindowTitle(const char* title)
{
    static char oldTitle[1024] = {};
    if (s_amWindow && strcmp(oldTitle, title) != 0) {
        strncpy(oldTitle, title, sizeof(oldTitle) - 1);
        SDL_SetWindowTitle(s_amWindow, title);
    }
}

// ── Win32 tooltip plumbing (Windows only) ────────────────────────────────────
// Unchanged from the original except that HWND is obtained via SDL_GetWindowWMInfo
// which still works in SDL2.

#ifdef WIN32
static TOOLINFOW s_toolItem  = {};
static HWND      s_hwndAM    = nullptr;   // automap HWND
static HWND      s_hwndTT    = nullptr;   // automap tooltip

static TOOLINFOW s_toolItemM = {};
static HWND      s_hwndMain  = nullptr;   // main game HWND
static HWND      s_hwndTTM   = nullptr;   // main-window tooltip

static HWND CreateTrackingTooltip(HWND parent, LPCWSTR text, TOOLINFOW& ti, bool transparent)
{
    InitCommonControls();
    HWND hwndTT = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP | WS_CHILD | WS_VISIBLE | SS_NOTIFY,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        parent, nullptr, GetModuleHandle(nullptr), nullptr);
    if (!hwndTT) return nullptr;

    SetWindowPos(hwndTT, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    memset(&ti, 0, sizeof(ti));
    ti.cbSize   = TTTOOLINFOW_V2_SIZE;
    ti.uFlags   = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE
                | (transparent ? TTF_TRANSPARENT : 0u);
    ti.hwnd     = parent;
    ti.hinst    = GetModuleHandle(nullptr);
    ti.lpszText = const_cast<LPWSTR>(text);
    ti.uId      = (UINT_PTR)parent;
    GetClientRect(parent, &ti.rect);

    SendMessageW(hwndTT, TTM_ADDTOOLW,        0, (LPARAM)&ti);
    SendMessageW(hwndTT, TTM_SETMAXTIPWIDTH,  0, 400);
    return hwndTT;
}
#endif // WIN32

// ── Public tooltip API (called by am_wiz6/am_wiz7) ───────────────────────────

void TooltipForAutomapWindow_Show(bool show)
{
#ifdef WIN32
    if (!s_hwndTT) return;
    SendMessageW(s_hwndTT, TTM_TRACKACTIVATE,
        (WPARAM)(show ? TRUE : FALSE), (LPARAM)&s_toolItem);
#endif
}

void TooltipForAutomapWindow_SetText(wchar_t* text)
{
#ifdef WIN32
    if (!s_hwndTT) return;
    s_toolItem.lpszText = const_cast<LPWSTR>(text);
    SendMessageW(s_hwndTT, TTM_SETTOOLINFOW, 0, (LPARAM)&s_toolItem);
#endif
}

static BOOL s_showTipBackup = -1;

void TooltipForMainWindow_Show(bool show)
{
#ifdef WIN32
    if (!s_hwndTTM) return;
    BOOL SHOW = show ? TRUE : FALSE;
    if (s_showTipBackup != SHOW) {
        s_showTipBackup = SHOW;
        SendMessageW(s_hwndTTM, TTM_TRACKACTIVATE, (WPARAM)SHOW, (LPARAM)&s_toolItemM);
    }
#endif
}

void TooltipForMainWindow_SetText(wchar_t* text)
{
#ifdef WIN32
    if (!s_hwndTTM) return;
    s_toolItemM.lpszText = const_cast<LPWSTR>(text);
    SendMessageW(s_hwndTTM, TTM_SETTOOLINFOW, 0, (LPARAM)&s_toolItemM);
#endif
}

// ── Callback / loop plumbing ──────────────────────────────────────────────────

static Bitu  s_automapCallbackHandle = 0;
static bool  s_automapInitDone = false;
static bool AutoMapReasonCanAllocateCallback(const char* reason)
{
    // DOSBox-X can call AutoMapCreate() very early, while CALLBACK_Init has not
    // finished yet. Defer CALLBACK_Allocate() until DOS execution / breakpoint
    // handling, where the callback subsystem is ready.
    if (!reason) return true;
    if (std::strcmp(reason, "AutoMapCreate") == 0) return false;
    if (std::strcmp(reason, "AutoMapInit") == 0) return false;
    return true;
}

static void AutoMapEnsureInitialized(const char* reason);
Bitu AutoMapGetCallbackHandle()
{
    AutoMapEnsureInitialized("AutoMapGetCallbackHandle");
    return s_automapCallbackHandle;
}
static bool  s_amExitLoop = false;

static Bitu AutomapLoopResume()
{
    // Re-enable all breakpoints and hand control back to the normal CPU loop.
    extern void AM_ActivateBreakpoints(PhysPt excludeAddr, bool activate);
    AM_ActivateBreakpoints(SegPhys(cs) + reg_eip, true);
    DOSBOX_SetNormalLoop();
    return 0;
}

static Bitu AutoMapCallbackHandler()
{
    s_amExitLoop = true;
    DOSBOX_SetLoop(&AutomapLoopResume);
    return 0;
}

void W6_NativeSaveVis();  // forward declaration
void W6_NativeLoadVis();  // forward declaration
void W6_NativeSaveNotes(); // forward declaration
void W6_NativeLoadNotes(); // forward declaration
void W6_SetSavePath(const char* basePath);  // forward declaration
void W7_SetSavePath(const char* basePath);  // forward declaration
void W7_NativeLoadNotes();  // forward declaration
void W7_NativeSaveNotes();  // forward declaration

bool AutoMapExitLoop()
{
    if (s_amExitLoop) {
        s_amExitLoop = false;
        // Save W6 visdata and notes on exit
        if (s_amGame == AutoMapGame::Wizardry6) { W6_NativeSaveVis(); W6_NativeSaveNotes(); }
        // Save W7 notes on exit
        if (s_amGame == AutoMapGame::Wizardry7) W7_NativeSaveNotes();
        return true;
    }
    return false;
}

// ── Breakpoint engine ─────────────────────────────────────────────────────────
// Identical logic to the original — writes 0xCC into emulated memory.

typedef void (*AM_BreakpointCallback)();

struct AM_BP {
    bool                  active;
    bool                  isInOverlay;
    PhysPt                location;
    uint8_t               oldData;
    AM_BreakpointCallback callback;
};

static std::vector<AM_BP> s_bps;



static PhysPt AM_PhysMakeProt(uint16_t selector, uint32_t offset)
{
    Descriptor desc;
    if (cpu.gdt.GetDescriptor(selector, desc)) return desc.GetBase() + offset;
    return 0;
}

static PhysPt AM_GetAddress(uint16_t seg, uint32_t offset)
{
    if (seg == SegValue(cs)) return SegPhys(cs) + offset;
    if (cpu.pmode && !(reg_flags & FLAG_VM)) {
        Descriptor desc;
        if (cpu.gdt.GetDescriptor(seg, desc))
            return AM_PhysMakeProt(seg, offset);
    }
    return (seg << 4) + offset;
}


static PhysPt AM_PhysFromRealLinear(uint32_t off)
{
    // Breakpoint definitions are already real-mode *linear physical*
    // addresses: am_loadaddress is PhysMake(loadseg, 0), and every DS/OVR
    // breakpoint offset is added to that physical base.  Therefore this value
    // must be used as-is for mem_readb()/mem_writeb().
    //
    // Do NOT pass this through RealSeg()/RealOff(): DOSBox RealPt encodes
    // seg:off as (seg << 16) | off, so RealSeg(0x12B9B) would become 0x0001
    // and PhysMake(0x0001, 0x2B9B) would incorrectly wrap a high OVR address
    // down to around 0x2Bxx.  VMAZE MapRedraw lives above the first 64 KiB
    // boundary, so that bug prevents its OVR BP from ever being installed.
    return static_cast<PhysPt>(off);
}

int AM_AddBreakpoint(uint32_t off, bool isInOverlay = false,
                     AM_BreakpointCallback cb = nullptr)
{
    AM_BP bp;
    bp.active      = false;
    bp.isInOverlay = isInOverlay;
    bp.location    = AM_PhysFromRealLinear(off);
    bp.oldData     = mem_readb(bp.location);
    bp.callback    = cb;
    s_bps.push_back(bp);
    const int idx = (int)s_bps.size() - 1;
    return idx;
}

bool AM_DeleteBreakpoint(PhysPt where)
{
    auto it = std::find_if(s_bps.begin(), s_bps.end(),
        [where](const AM_BP& b){ return b.location == where; });
    if (it == s_bps.end()) {
        return false;
    }
    s_bps.erase(it);
    return true;
}

void AM_ActivateBreakpoint(int index, bool active)
{
    AM_BP& bp = s_bps[index];
    if (active) {
        const uint8_t data = mem_readb(bp.location);
        if (data != 0xCC) {
            bp.oldData = data;
            mem_writeb(bp.location, 0xCC);
        }
    } else {
        const uint8_t cur = mem_readb(bp.location);
        if (cur == 0xCC) {
            mem_writeb(bp.location, bp.oldData);
        }
    }
    bp.active = active;
}

void AM_ActivateBreakpoints(PhysPt excludeAddr, bool activate)
{
    for (int i = 0; i < (int)s_bps.size(); ++i) {
        if (activate && s_bps[i].location == excludeAddr) continue;
        AM_ActivateBreakpoint(i, activate);
    }
}

static bool s_overlayLoaded              = false;
static bool s_ignoreOverlayCycleOnce     = true;

void AM_ActivateOverlayBreakpoints(PhysPt excludeAddr, bool activate)
{
    for (int i = 0; i < (int)s_bps.size(); ++i) {
        if (s_bps[i].isInOverlay && !s_overlayLoaded) continue;
        if (activate && s_bps[i].location == excludeAddr) continue;
        AM_ActivateBreakpoint(i, activate);
    }
}


static void AM_DeleteOverlayBreakpoints()
{
	for (auto it = s_bps.begin(); it != s_bps.end(); ) {
		if (!it->isInOverlay) {
			++it;
			continue;
		}
		if (it->active && mem_readb(it->location) == 0xCC) {
			mem_writeb(it->location, it->oldData);
		}
		it = s_bps.erase(it);
	}
}

static int AutomapFindBreakpointAt(PhysPt addr)
{
    for (int i = 0; i < (int)s_bps.size(); ++i) {
        if (s_bps[i].active && s_bps[i].location == addr)
            return i;
    }
    return -1;
}

static int AutomapCheckBreakpoint(PhysPt addr)
{
    return AutomapFindBreakpointAt(addr);
}

bool AutomapBreakpoint()
{
    AutoMapEnsureInitialized("AutomapBreakpoint");
    const uint16_t cs_val = SegValue(cs);
    const uint32_t eip_at_entry = reg_eip;
    const PhysPt where = AM_GetAddress(cs_val, eip_at_entry);

    // Clean INT3 handling policy:
    //   * hit address is the address reported by the DOSBox-X INT3 hook
    //   * no stale swallow
    //   * no EIP-1 fallback
    //   * no previous-BP heuristic
    // If a valid BP is not registered at this address, this is not our INT3.
    int i = AutomapCheckBreakpoint(where);
    if (i < 0) {
        return false;
    }

    const PhysPt hit_where = s_bps[i].location;

    // Restore the opcode at the BP that is about to execute, run Automap
    // callback, then re-arm every other BP.  The paired/null BP or next event
    // will re-arm hit_where later, matching the old Automap seesaw model.
    AM_ActivateBreakpoint(i, false);
    if (s_bps[i].callback) {
        s_bps[i].callback();
    }
    AM_ActivateBreakpoints(hit_where, true);

    return true;
}

// ── Forward declarations for per-game modules ─────────────────────────────────

void W6_Update(int xSize, int ySize);
void W7_Update(int xSize, int ySize);
bool W7_NeedUpdate();
void W6_NewGame();
void W7_NewGame();
void W6_Load(const char* fullName);
void W7_Load(const char*);
void W6_Save(const char* fullName);
void W7_Save(const char* fullName);
extern bool amw6_isSaving;
extern bool amw6_isLoading;
void W6_SetSavePath(const char* basePath);
void W6_NativeLoadVis();
void W6_NativeSaveVis();
void W6_NativeSaveNotes();
void W6_NativeLoadNotes();
void W7_OnOverlayLoad();
void W6_OnMouseMotionInMainWindow(RECT rc, int newX, int newY);
void W7_OnMouseMotionInMainWindow(RECT rc, int newX, int newY);
void W6_OnMouseMotionInAutomapWindow(int newX, int newY, bool alt);
void W7_OnMouseMotionInAutomapWindow(int newX, int newY, bool alt);
void W6_OnAutomapDrag(int dx, int dy);
void W7_OnAutomapDrag(int dx, int dy);
void W6_OnMouseButtonInAutomapWindow(SDL_MouseButtonEvent* btn, bool alt, bool ctrl);
void W7_OnMouseButtonInAutomapWindow(SDL_MouseButtonEvent* btn, bool alt, bool ctrl);

extern uint32_t amw6_dataseg_addr;
extern uint32_t amw7_dataseg_addr;
PhysPt am_loadaddress = 0;

// Overlay breakpoint callbacks (defined in am_wiz7.cpp)
void W7BP_OnMapLoadStart();   void W7BP_OnMapLoadEnd();
void W7BP_OnMapRedraw();
void W7BP_OnShowEnemyList();  void W7BP_OnHideEnemyList();
void W7BP_OnShowSpellWindow();void W7BP_OnHideSpellWindow();
void W7BP_OnShowUseWindow();  void W7BP_OnHideUseWindow();
void W7BP_OnShowUnlockWindow();void W7BP_OnHideUnlockWindow();
void W7BP_OnShowDisarmWindow();void W7BP_OnHideDisarmWindow();
void W7BP_OnShowLootWindow(); void W7BP_OnHideLootWindow();
void W7BP_OnShowBuyWindow();  void W7BP_OnHideBuyWindow();
void W7BP_OnShowSellWindow(); void W7BP_OnHideSellWindow();
void W7BP_OnShowGiveWindow(); void W7BP_OnHideGiveWindow();
void W7BP_OnShowSaveWindow(); void W7BP_OnHideSaveWindow();
void W7BP_OnShowConfigWindow();void W7BP_OnHideConfigWindow();
void W7BP_OnShowAddCharacterWindow(); void W7BP_OnHideAddCharacterWindow();
void W7BP_OnShowSecurityWindow(); void W7BP_OnHideSecurityWindow();
void W7BP_OnShowLoadWindow(); void W7BP_OnHideLoadWindow();
void W7BP_OnShowImportWindow();void W7BP_OnHideImportWindow();
void W7BP_OnIntroStart();     void W7BP_OnIntroEnd();
void W7BP_OnCreateSaveFileStart(); void W7BP_OnCreateSaveFileEnd();
void W7BP_OnOpenSaveFileStart();   void W7BP_OnOpenSaveFileEnd();
void W7BP_OnCastSpell();
void AMW7BP_OnMouseLock();    void AMW7BP_OnMouseUnlock();
void AMW7BP_OnBitArrayModification();
void AMBP_OnOverlayLoad();    void AMBP_OnOverlayUnload();
extern uint16_t W7GameStateOnOverlayLoad;


static void AutoMapEnsureInitialized(const char* reason)
{
    Section* section = nullptr;
    if (control)
        section = control->GetSection("automap");

    auto* sec = dynamic_cast<Section_prop*>(section);
    const bool can_allocate_callback = AutoMapReasonCanAllocateCallback(reason);


    if (sec) {
        am_show_automap         = sec->Get_bool("enable");
        amw6_show_tooltips      = sec->Get_bool("show_tooltips");
        amw7_show_tooltips      = amw6_show_tooltips;
        amw6_hide_in_dark_zones = sec->Get_bool("hide_in_dark_zones");
        amw7_hide_in_dark_zones = amw6_hide_in_dark_zones;
        am_width                = sec->Get_int("width");
        am_height               = sec->Get_int("height");
        amw7_use_spellPower     = sec->Get_bool("wiz7_sns_mode");
    } else if (!s_automapInitDone) {
        // Safe defaults matching the config defaults.
        am_show_automap         = true;
        amw6_show_tooltips      = true;
        amw7_show_tooltips      = true;
        amw6_hide_in_dark_zones = true;
        amw7_hide_in_dark_zones = true;
        am_width                = 512;
        am_height               = 512;
        amw7_use_spellPower     = false;
    }

    if (s_automapCallbackHandle == 0) {
        if (can_allocate_callback) {
            s_automapCallbackHandle = CALLBACK_Allocate();
            CALLBACK_Setup(s_automapCallbackHandle, AutoMapCallbackHandler, CB_RETF, "automap");
        } else {
        }
    }

    s_automapInitDone = true;
}

void AutoMapInit(Section* section)
{
    (void)section;
    AutoMapEnsureInitialized("AutoMapInit");
}

// DOSBox-X throttle/idempotent state.  This must be declared before AutoMapCreate(),
// because AutoMapCreate() can request a one-shot redraw when reusing an existing window.
static bool s_needUpdateOnce = false;

// ── Public: AutoMapCreate (sdlmain.cpp calls this after main window exists) ───

void AutoMapCreate()
{
    AutoMapEnsureInitialized("AutoMapCreate");
	// Read config from global control object
	auto* section = dynamic_cast<Section_prop*>(control->GetSection("automap"));
	if (section) {
		am_show_automap = section->Get_bool("enable");
		am_width        = section->Get_int("width");
		am_height       = section->Get_int("height");
	}

    if (!am_show_automap) {
        return;
    }

    // DOSBox-X calls AutoMapCreate() once near SDL window creation and again
    // after Wizardry is detected.  DOSBox Staging only reached the create path
    // once in the working port.  Keep AutoMapCreate idempotent so the second
    // call only reuses the existing automap window instead of creating a
    // duplicate renderer/window pair.
    if (s_amWindow && s_amRenderer) {
        s_amWindowID = SDL_GetWindowID(s_amWindow);
        SDL_ShowWindow(s_amWindow);
        s_needUpdateOnce = true;
        return;
    }

    // If a previous partial creation failed after creating the window, clean it
    // up before trying again.
    if (s_amWindow && !s_amRenderer) {
        SDL_DestroyWindow(s_amWindow);
        s_amWindow = nullptr;
        s_amWindowID = 0;
    }

    // Create the secondary window
    s_amWindow = SDL_CreateWindow(
        "undetected - Automap Mod",
        14, SDL_WINDOWPOS_CENTERED,
        am_width, am_height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);

    if (!s_amWindow) {
        return;
    }
    s_amWindowID = SDL_GetWindowID(s_amWindow);

    // Create an accelerated renderer (falls back to software if needed)
    s_amRenderer = SDL_CreateRenderer(s_amWindow, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_amRenderer) {
        s_amRenderer = SDL_CreateRenderer(s_amWindow, -1,
            SDL_RENDERER_SOFTWARE);
    }
    if (!s_amRenderer) {
        SDL_DestroyWindow(s_amWindow);
        s_amWindow = nullptr;
        s_amWindowID = 0;
        return;
    }


    // Upload tile atlases
    s_texW6 = UploadAtlas(s_amRenderer, tiles32,   tiles32_count,   256, 16);
    s_texW7 = UploadAtlas(s_amRenderer, w7tiles32, w7tiles32_count, 256, 32);

    // Windows: install tooltip on the automap HWND
#ifdef WIN32
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (SDL_GetWindowWMInfo(s_amWindow, &wmi)) {
        s_hwndAM = wmi.info.win.window;
        s_hwndTT = CreateTrackingTooltip(s_hwndAM, L"", s_toolItem, false);
    }
#endif
}

uint32_t AutoMapGetWindowID() { return s_amWindowID; }

// ── Public: AutoMapOnMainWindowCreate ────────────────────────────────────────

void AutoMapOnMainWindowCreate(void* hwnd_win32)
{
#ifdef WIN32
    s_hwndMain = (HWND)hwnd_win32;
    s_hwndTTM  = CreateTrackingTooltip(s_hwndMain, L"", s_toolItemM, true);
#else
    (void)hwnd_win32;
#endif
}

// ── Public: AutoMapUpdate (called once per frame from sdlmain.cpp) ─────────────

void AutoMapUpdate()
{
    if (!am_show_automap || !s_amRenderer) return;

    // Match the old Automap control flow. AutoMapUpdate may be called often
    // by the host loop, but Wizardry 7 rendering is skipped unless a one-shot
    // redraw was requested or W7_NeedUpdate() reports an old-Automap event/dirty
    if ((s_amGame == AutoMapGame::Wizardry7) && (!s_needUpdateOnce)) {
        if (!W7_NeedUpdate()) return;
    }

    s_needUpdateOnce = false;

    BeginFrame();

    if (s_amGame == AutoMapGame::Wizardry6) {
        W6_Update(am_width, am_height);
    } else if (s_amGame == AutoMapGame::Wizardry7) {
        W7_Update(am_width, am_height);
    } else {
        char title[128];
        snprintf(title, sizeof(title), "%s - Automap Mod",
                 s_amGameName[(int)s_amGame]);
        SetAutomapWindowTitle(title);
    }

    EndFrame();
}

// ── Public: AutoMapHandleEvent ────────────────────────────────────────────────
// Handles all SDL events directed at the automap window.
// Called from the main event loop in sdlmain.cpp.

static bool  s_dragStart = false;
static int   s_dragStartX = 0, s_dragStartY = 0;
static bool  s_cursorShown = true;
#ifdef WIN32
static BOOL  s_trackingMouse  = FALSE;
static BOOL  s_trackingMouseM = FALSE;
#endif

bool AutoMapHandleEvent(const SDL_Event& event)
{
    if (!am_show_automap || !s_amWindow) return false;

    switch (event.type) {

    case SDL_WINDOWEVENT:
        // SDL2 multi-window note for DOSBox-X:
        // With the automap as a second SDL window, clicking the DOSBox-X main
        // window's close button may arrive as SDL_WINDOWEVENT_CLOSE for the
        // main window instead of a global SDL_QUIT.  DOSBox-X 2026.06.02 does
        // not handle WINDOWEVENT_CLOSE in the main event loop, so translate any
        // non-automap close event into SDL_QUIT and let DOSBox-X handle the
        // queued quit normally on the next poll.
        if (event.window.event == SDL_WINDOWEVENT_CLOSE &&
            event.window.windowID != s_amWindowID) {
            SDL_Event quit_event = {};
            quit_event.type = SDL_QUIT;
            SDL_PushEvent(&quit_event);
            return true;
        }

        if (event.window.windowID != s_amWindowID) return false;
        switch (event.window.event) {
        case SDL_WINDOWEVENT_EXPOSED:
            s_needUpdateOnce = true;
            AutoMapUpdate();
            break;
        case SDL_WINDOWEVENT_RESIZED:
            am_width  = event.window.data1;
            am_height = event.window.data2;
            SDL_RenderSetLogicalSize(s_amRenderer, am_width, am_height);
            s_needUpdateOnce = true;
            AutoMapUpdate();
            break;
        case SDL_WINDOWEVENT_ENTER:
            s_cursorShown = SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE;
            SDL_ShowCursor(SDL_ENABLE);
            break;
        case SDL_WINDOWEVENT_LEAVE:
            SDL_ShowCursor(s_cursorShown ? SDL_ENABLE : SDL_DISABLE);
            TooltipForAutomapWindow_Show(false);
#ifdef WIN32
            s_trackingMouse = FALSE;
#endif
            s_dragStart = false;
            break;
        case SDL_WINDOWEVENT_CLOSE:
            // Closing the automap window closes the application
		    SDL_Event quit_event = {};
		    quit_event.type      = SDL_QUIT;
		    SDL_PushEvent(&quit_event);
            break;
        }
        return true;

    case SDL_MOUSEMOTION:
        if (event.motion.windowID != s_amWindowID) return false;
        {
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            bool alt = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];
            int nx = event.motion.x;
            int ny = event.motion.y;
            static int oldX = 0, oldY = 0;
            if (!alt) {
                if ((nx != oldX || ny != oldY) && s_dragStart) {
                    if (s_amGame == AutoMapGame::Wizardry6) W6_OnAutomapDrag(oldX-nx, oldY-ny);
                    if (s_amGame == AutoMapGame::Wizardry7) W7_OnAutomapDrag(oldX-nx, oldY-ny);
                }
                oldX = nx; oldY = ny;
            }
            if (s_amGame == AutoMapGame::Wizardry6) W6_OnMouseMotionInAutomapWindow(nx, ny, alt);
            if (s_amGame == AutoMapGame::Wizardry7) W7_OnMouseMotionInAutomapWindow(nx, ny, alt);

#ifdef WIN32
            if (s_hwndTT && s_hwndAM) {
                POINT pt = { nx, ny };
                ClientToScreen(s_hwndAM, &pt);
                SendMessageW(s_hwndTT, TTM_TRACKPOSITION, 0,
                    (LPARAM)MAKELONG(pt.x + 20, pt.y - 30));
                if (!s_trackingMouse) {
                    TRACKMOUSEEVENT tme = { sizeof(tme) };
                    tme.hwndTrack = s_hwndAM;
                    tme.dwFlags   = TME_LEAVE;
                    TrackMouseEvent(&tme);
                    s_trackingMouse = TRUE;
                }
            }
#endif
        }
        return true;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        if (event.button.windowID != s_amWindowID) return false;
        {
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            bool alt  = keys[SDL_SCANCODE_LALT]  || keys[SDL_SCANCODE_RALT];
            bool ctrl = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];

            if (!alt && !ctrl &&
                event.button.state == SDL_PRESSED &&
                event.button.button == SDL_BUTTON_LEFT) {
                s_dragStartX = event.button.x;
                s_dragStartY = event.button.y;
                s_dragStart  = true;
            }
            if (event.button.state == SDL_RELEASED) {
                if (event.button.button == SDL_BUTTON_LEFT && s_dragStart) {
                    s_dragStart = false;
                    // If the mouse moved, it was a drag — don't fire a click
                    if (s_dragStartX != event.button.x ||
                        s_dragStartY != event.button.y)
                        return true;
                }
                if (s_amGame == AutoMapGame::Wizardry6)
                    W6_OnMouseButtonInAutomapWindow(
                        const_cast<SDL_MouseButtonEvent*>(&event.button), alt, ctrl);
                if (s_amGame == AutoMapGame::Wizardry7)
                    W7_OnMouseButtonInAutomapWindow(
                        const_cast<SDL_MouseButtonEvent*>(&event.button), alt, ctrl);
            }
        }
        return true;

    default:
        break;
    }
    return false;
}

// ── Main-window mouse event (called by sdlmain.cpp for the game window) ───────

void AutoMapOnMainWindowMouseMotion(int newX, int newY)
{
    if (!am_show_automap) return;
#ifdef WIN32
    if (!s_hwndMain || !s_hwndTTM) return;
    RECT rc;
    GetClientRect(s_hwndMain, &rc);
    if (s_amGame == AutoMapGame::Wizardry6) W6_OnMouseMotionInMainWindow(rc, newX, newY);
    if (s_amGame == AutoMapGame::Wizardry7) W7_OnMouseMotionInMainWindow(rc, newX, newY);

    POINT pt = { newX, newY };
    ClientToScreen(s_hwndMain, &pt);
    SendMessageW(s_hwndTTM, TTM_TRACKPOSITION, 0,
        (LPARAM)MAKELONG(pt.x + 20, pt.y - 30));
    if (!s_trackingMouseM) {
        TRACKMOUSEEVENT tme = { sizeof(tme) };
        tme.hwndTrack = s_hwndMain;
        tme.dwFlags   = TME_LEAVE;
        TrackMouseEvent(&tme);
        s_trackingMouseM = TRUE;
    }
#else
    (void)newX; (void)newY;
#endif
}

// ── InputBox helper (used by note-editing in am_wiz7.cpp) ────────────────────

bool InputBox(const wchar_t* title, const wchar_t* hint, wchar_t* buf, int bufSize)
{
#ifdef WIN32
    CInputBox myinp(GetModuleHandle(nullptr));
    return myinp.ShowInputBox(s_hwndAM, title, hint, buf, bufSize) == IDOK;
#else
    (void)title; (void)hint; (void)buf; (void)bufSize;
    return false;
#endif
}

bool ChooseColorDialog(uint32_t& color, uint32_t* palette)
{
#ifdef WIN32
    CHOOSECOLOR cc = {};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner   = s_hwndAM;
    cc.hInstance   = (HWND)GetModuleHandle(nullptr);
    cc.lpCustColors = (COLORREF*)palette;
    cc.Flags       = CC_ANYCOLOR | CC_FULLOPEN | CC_RGBINIT;
    cc.rgbResult   = color;
    if (ChooseColor(&cc)) {
        color = cc.rgbResult;
        return true;
    }
#else
    (void)color; (void)palette;
#endif
    return false;
}

// ── Bit-array helper (shared between Wiz6 and Wiz7 parsing code) ──────────────

uint16_t GetBitArrayElement(uint8_t* pArray, uint16_t index, uint16_t bitsPerElement)
{
    uint16_t elemOfs   = index * bitsPerElement;
    uint16_t elemShift = elemOfs % 8;
    uint16_t elemMask  = 0xFFFF;
    elemMask >>= 16 - bitsPerElement;
    elemOfs /= 8;
    uint16_t ret = *reinterpret_cast<uint16_t*>(pArray + elemOfs);
    ret >>= elemShift;
    ret &= elemMask;
    return ret;
}

// ── Public: AutoMapDetectGame (dos_execute.cpp) ───────────────────────────────

void AutoMapDetectGame(char* name, uint16_t loadseg, uint16_t headersize)
{
    AutoMapEnsureInitialized("AutoMapDetectGame");
    (void)headersize; // unused — kept for API compatibility
	s_amGame = AutoMapGame::None;

	if (strcasecmp(name, "WROOT.EXE") == 0) {
		s_amGame = AutoMapGame::Wizardry6;
	}
	if (strcasecmp(name, "DS.EXE") == 0 || strcasecmp(name, "DS.OVR") == 0) {
		s_amGame = AutoMapGame::Wizardry7;
	}

	if (s_amGame == AutoMapGame::None) {
		return;
	}

	PhysPt loadaddress = (PhysPt)(loadseg) << 4;

	// ── Wizardry 6 ────────────────────────────────────────────────────────────
	if (s_amGame == AutoMapGame::Wizardry6) {
		// Scan for WMAZE signature
		uint32_t wmaze_offset = 0;
		bool found_wmaze = false;
		for (uint32_t offset = 0; offset < 0x80000; offset++) {
			uint8_t c = 0;
			c = mem_readb(loadaddress + offset);
			if (c != 'W') continue;
			char buf[6] = {};
			for (int i = 0; i < 5; i++) {
				uint8_t b = 0;
				b = mem_readb(loadaddress + offset + i);
				buf[i] = b;
			}
			if (strncmp(buf, "WMAZE", 5) == 0) {
				wmaze_offset = offset;
				found_wmaze  = true;
				break;
			}
		}
		if (!found_wmaze) {
			s_amGame = AutoMapGame::None;
			return;
		}
		// Correct dataseg derived from CheatEngine signature analysis:
		// correct_dataseg = loadaddress + wmaze_offset - 0x57C
		amw6_dataseg_addr = loadaddress + wmaze_offset - 0x57C;
		am_loadaddress    = loadaddress;
		// Set native save path in AppData (avoids Program Files permission issues)
		char* basePath = SDL_GetPrefPath("WizardryAutomap", "Wizardry6");
		if (basePath) { W6_SetSavePath(basePath); SDL_free(basePath); }
		else W6_SetSavePath("./");
		// Load existing visdata and notes if present
		W6_NativeLoadVis();
		W6_NativeLoadNotes();
		AutoMapCreate();
		return;
	}

	// ── Wizardry 7
	// ──────────────────────────────────────────────────────────── Step 1:
	// scan for the Japanese DOS/V VMAZE signature
	uint32_t vmaze_offset = 0;
	bool found_vmaze      = false;
	for (uint32_t offset = 0; offset < 0x80000; offset++) {
		uint8_t c = 0;
		c = mem_readb(loadaddress + offset);
		if (c != 'V') {
			continue;
		}
		char buf[6] = {};
		for (int i = 0; i < 5; i++) {
			uint8_t b = 0;
			b = mem_readb(loadaddress + offset + i);
			buf[i] = b;
		}
		if (strncmp(buf, "VMAZE", 5) == 0) {
			vmaze_offset = offset;
			found_vmaze  = true;
			break;
		}
	}
	if (!found_vmaze) {
		s_amGame = AutoMapGame::None;
		return;
	}

	// Japanese DOS/V DS.EXE only.  Other Wizardry 7 layouts are intentionally
	// treated as unsupported in this branch.
	if (vmaze_offset != 0xFE7D) {
		s_amGame = AutoMapGame::None;
		return;
	}
	// Step 2: scan for INSERT SAVEGAME to compute dataseg address
	uint32_t savegame_offset = 0;
	bool found_savegame      = false;
	for (uint32_t offset = 0; offset < 0x80000; offset++) {
		uint8_t c = 0;
		c = mem_readb(loadaddress + offset);
		if (c != 'I') {
			continue;
		}
		char buf[16] = {};
		for (int i = 0; i < 15; i++) {
			uint8_t b = 0;
			b = mem_readb(loadaddress + offset + i);
			buf[i] = b;
		}
		if (strncmp(buf, "INSERT SAVEGAME", 15) == 0) {
			savegame_offset = offset;
			found_savegame  = true;
			break;
		}
	}
	
	if (found_savegame) {
		amw7_dataseg_addr = loadaddress + savegame_offset - 0xB74;
	} else {
		amw7_dataseg_addr = loadaddress + 0xF250;
	}
	
	amw7_rus       = (strcasecmp(name, "DS.OVR") == 0);
	am_loadaddress = loadaddress;

	// Save native Wiz7 notes only in the local Config folder.
	// AppData and obsolete guest sidecars are not used.
	W7_SetSavePath("./Config");
	W7_NativeLoadNotes();


	AutoMapCreate();
}

static bool AM_AddCheckedBreakpoint(uint32_t off,
                                    uint8_t expected,
                                    bool isInOverlay,
                                    AM_BreakpointCallback cb = nullptr)
{
	const uint32_t encoded = am_loadaddress + off;
	const PhysPt where = AM_PhysFromRealLinear(encoded);

	const uint8_t actual = mem_readb(where);

	if (actual != expected) {
		return false;
	}

	const int bp = AM_AddBreakpoint(encoded, isInOverlay, cb);
	AM_ActivateBreakpoint(bp, true);

	return true;
}


static bool AM_SignatureByteMatches(PhysPt where, uint8_t expected)
{
	const uint8_t actual = mem_readb(where);
	if (actual == expected) {
		return true;
	}
	if (actual == 0xCC) {
		for (const auto& bp : s_bps) {
			if (bp.location == where && bp.oldData == expected) {
				return true;
			}
		}
	}
	return false;
}

static bool AM_AddContextSignedBreakpoint(uint32_t bp_off,
                                          uint32_t sig_off,
                                          const uint8_t* sig,
                                          size_t sig_len,
                                          bool isInOverlay,
                                          AM_BreakpointCallback cb = nullptr)
{
	const uint32_t sig_encoded = am_loadaddress + sig_off;
	const PhysPt sig_where = AM_PhysFromRealLinear(sig_encoded);

	for (size_t i = 0; i < sig_len; ++i) {
		const PhysPt p = sig_where + static_cast<PhysPt>(i);
		if (!AM_SignatureByteMatches(p, sig[i])) {
			return false;
		}
	}

	const uint32_t bp_encoded = am_loadaddress + bp_off;
	const int bp = AM_AddBreakpoint(bp_encoded, isInOverlay, cb);
	AM_ActivateBreakpoint(bp, true);
	return true;
}


static bool AM_AddJpOvrContextSignedBreakpoint(uint32_t source_bp_off,
                                               uint32_t source_sig_off,
                                               const uint8_t* sig,
                                               size_t sig_len,
                                               AM_BreakpointCallback cb = nullptr)
{
	return AM_AddContextSignedBreakpoint(source_bp_off, source_sig_off, sig, sig_len, true, cb);
}


static bool AM_IsShowHideCallback(AM_BreakpointCallback cb)
{
    return cb == W7BP_OnShowEnemyList || cb == W7BP_OnHideEnemyList ||
           cb == W7BP_OnShowSpellWindow || cb == W7BP_OnHideSpellWindow ||
           cb == W7BP_OnShowUseWindow || cb == W7BP_OnHideUseWindow ||
           cb == W7BP_OnShowUnlockWindow || cb == W7BP_OnHideUnlockWindow ||
           cb == W7BP_OnShowDisarmWindow || cb == W7BP_OnHideDisarmWindow ||
           cb == W7BP_OnShowLootWindow || cb == W7BP_OnHideLootWindow ||
           cb == W7BP_OnShowBuyWindow || cb == W7BP_OnHideBuyWindow ||
           cb == W7BP_OnShowSellWindow || cb == W7BP_OnHideSellWindow ||
           cb == W7BP_OnShowGiveWindow || cb == W7BP_OnHideGiveWindow ||
           cb == W7BP_OnShowSaveWindow || cb == W7BP_OnHideSaveWindow ||
           cb == W7BP_OnShowConfigWindow || cb == W7BP_OnHideConfigWindow ||
           cb == W7BP_OnShowAddCharacterWindow || cb == W7BP_OnHideAddCharacterWindow ||
           cb == W7BP_OnShowSecurityWindow || cb == W7BP_OnHideSecurityWindow ||
           cb == W7BP_OnShowLoadWindow || cb == W7BP_OnHideLoadWindow ||
           cb == W7BP_OnShowImportWindow || cb == W7BP_OnHideImportWindow;
}

static bool AM_AddJpOvrPolicyBreakpoint(uint32_t bp_off,
                                        uint32_t sig_off,
                                        const uint8_t* sig,
                                        size_t sig_len,
                                        AM_BreakpointCallback cb)
{
    static bool skip_next_null_pair = false;
    if (AM_IsShowHideCallback(cb)) {
        skip_next_null_pair = true;
        return false;
    }
    if (cb == nullptr && skip_next_null_pair) {
        skip_next_null_pair = false;
        return false;
    }
    skip_next_null_pair = false;
    return AM_AddJpOvrContextSignedBreakpoint(bp_off, sig_off, sig, sig_len, cb);
}

#define AM_JP_OVR_BP_CTX(bp_off, sig_off, cb, ...) do { \
	static const uint8_t sig[] = { __VA_ARGS__ }; \
	AM_AddJpOvrPolicyBreakpoint((bp_off), (sig_off), sig, sizeof(sig), (cb)); \
} while (0)

#define AM_JP_OVR_BP(off, cb, ...) do { \
	static const uint8_t sig[] = { __VA_ARGS__ }; \
	AM_AddJpOvrPolicyBreakpoint((off), (off), sig, sizeof(sig), (cb)); \
} while (0)

// ── Public: AutoMapSetupBreakpoints (dos_execute.cpp) ────────────────────────

void AutoMapSetupBreakpoints()
{
    AutoMapEnsureInitialized("AutoMapSetupBreakpoints");
	if (s_amGame != AutoMapGame::Wizardry7) {
		return;
	}

	s_overlayLoaded          = false;
	s_ignoreOverlayCycleOnce = true;

	AM_AddCheckedBreakpoint(0x4067, 0x8B, false, AMBP_OnOverlayUnload);
	AM_AddCheckedBreakpoint(0x4069, 0x8B, false, nullptr);

	AM_AddCheckedBreakpoint(0x414A, 0x5E, false, AMBP_OnOverlayLoad);
	AM_AddCheckedBreakpoint(0x414B, 0x5F, false, nullptr);

	AM_AddCheckedBreakpoint(0x3B3D, 0xFC, false, AMW7BP_OnMouseLock);
	AM_AddCheckedBreakpoint(0x3B43, 0x8B, false, nullptr);

	AM_AddCheckedBreakpoint(0x3B4A, 0xFC, false, AMW7BP_OnMouseUnlock);
	AM_AddCheckedBreakpoint(0x3B50, 0x8B, false, nullptr);

	AM_AddCheckedBreakpoint(0x38ED, 0x8B, false, AMW7BP_OnBitArrayModification);
	AM_AddCheckedBreakpoint(0x38EF, 0x5D, false, nullptr);

	AM_AddCheckedBreakpoint(0x396E, 0x1F, false, AMW7BP_OnBitArrayModification);
	AM_AddCheckedBreakpoint(0x396F, 0x8B, false, nullptr);
}


// ── Public: AutomapTerminate (dos_execute.cpp) ────────────────────────────────

void AutomapTerminate()
{
    if (s_amGame == AutoMapGame::Wizardry7) {
        AM_ActivateBreakpoints(0, false);
        s_bps.clear();
    }
    s_amGame = AutoMapGame::None;
}

// ── Public: DOS file hooks ────────────────────────────────────────────────────

void AutoMapOnOpenDOSFile(const char* fileName, uint8_t flags, uint16_t entry)
{
    bool isRead  = (flags & 0x03) == 0; // OPEN_READ = 0
    bool isWrite = (flags & 0x03) == 1 || (flags & 0x03) == 2; // OPEN_WRITE=1, OPEN_READWRITE=2
    int  l = (int)strlen(fileName);
    if (l < 1) return;
    int i = l - 1;
    while (i >= 0 && fileName[i] != '\\' && fileName[i] != '/') --i;

    if (!am_show_automap) return;
    const char* base = &fileName[i + 1];

    if (s_amGame == AutoMapGame::Wizardry6) {
        if (isRead) {
            if (strcasecmp(base, "NEWGAME.DBS") == 0) { W6_NewGame(); return; }
            if (strcasecmp(base, "SAVEGAME.DBS") == 0) {
                amw6_isLoading = true;
                W6_Load(fileName);
                amw6_isLoading = false;
                extern uint32_t amw6_lastLoadTime;
                amw6_lastLoadTime = SDL_GetTicks();
            }
        }
        if (isWrite) {
            if (strcasecmp(base, "SAVEGAME.DBS") == 0) {
                extern uint32_t amw6_lastLoadTime;
                uint32_t now = SDL_GetTicks();
                if (now - amw6_lastLoadTime > 2000) {
                    amw6_isSaving = true;
                    W6_Save(fileName);
                    amw6_isSaving = false;
                } else {
                }
            }
        }
        return;
    }
    if (!isRead) return;
    if (s_amGame == AutoMapGame::Wizardry7) {
        int bl = (int)strlen(base);
        if (bl > 4 && strcasecmp(base + bl - 4, ".OVR") == 0) {
            W7_OnOverlayLoad();
        }
        if (strcasecmp(base, "NEWGAME.DBS") == 0) {
            W7_NewGame();
            W7_NativeLoadNotes();
            return;
        }
        W7_Load(fileName);
    }
    (void)entry;
}

void AutoMapOnCreateDOSFile(const char* fileName)
{
    if (!am_show_automap) return;
    int l = (int)strlen(fileName);
    if (l < 1) return;

    int i = l - 1;
    while (i >= 0 && fileName[i] != '\\' && fileName[i] != '/') --i;
    const char* base = &fileName[i + 1];

    if (s_amGame == AutoMapGame::Wizardry7 && strcasecmp(base, "SAVEGAME.DBS") == 0) {
        W7_NativeSaveNotes();
    }
}

// ── AMBP: Overlay load/unload (called by the breakpoint callbacks) ────────────
// These are large functions identical in logic to the original; they add and
// remove overlay-specific breakpoints depending on the game-state register.
// The implementations are at the bottom because they reference all the
// W7BP_On* forward-declared callbacks above.

void AMBP_OnOverlayUnload()
{
	AM_ActivateOverlayBreakpoints(SegPhys(cs) + reg_eip, false);
	AM_DeleteOverlayBreakpoints();
	s_ignoreOverlayCycleOnce = false;
	s_overlayLoaded          = false;
}


void AMBP_OnOverlayLoad()
{
	const uint16_t st = W7GameStateOnOverlayLoad;



	if (st == 5 || st == 6 || st == 25) {
		AM_JP_OVR_BP_CTX(0x5811, 0x5811, W7BP_OnMapLoadStart, 0xB8, 0x00, 0x00, 0xE8, 0x29, 0xEB, 0x6A, 0x00, 0xFF, 0x36, 0x12, 0x84, 0xFF, 0x36, 0x30, 0x0E, 0x6A, 0x02, 0xE8, 0x6A, 0xAE, 0x83, 0xC4, 0x08);
		AM_JP_OVR_BP_CTX(0x58A1, 0x58A1, W7BP_OnMapLoadEnd,   0xC3, 0xB8, 0xFA, 0xFF, 0xE8, 0x98, 0xEA, 0x6A, 0x64, 0xE8, 0x4A, 0xAF, 0x59, 0xB9, 0x04, 0x00, 0x99, 0xF7, 0xF9, 0x89, 0x56, 0xFE, 0x6A, 0x64);
		AM_JP_OVR_BP_CTX(0x9D18, 0x9D18, W7BP_OnMapRedraw, 0xC7, 0x46, 0xDC, 0x00, 0x00, 0xC7, 0x46, 0xDE, 0x04, 0x00, 0xC7, 0x46, 0xE0, 0x01, 0x00, 0xC7, 0x46, 0xE2, 0x03, 0x00, 0xC7, 0x46, 0xE4, 0x02);
		AM_JP_OVR_BP_CTX(0x9D1D, 0x9D1D, nullptr,        0xC7, 0x46, 0xDE, 0x04, 0x00, 0xC7, 0x46, 0xE0, 0x01, 0x00, 0xC7, 0x46, 0xE2, 0x03, 0x00, 0xC7, 0x46, 0xE4, 0x02, 0x00, 0xC7, 0x06, 0x26, 0x90);
		s_overlayLoaded = true;
		return;
	}

	if (st == 23 || st == 27) {
		AM_JP_OVR_BP_CTX(0x89AB, 0x89AB, W7BP_OnMapRedraw, 0xC7, 0x46, 0xDC, 0x00, 0x00, 0xC7, 0x46, 0xDE, 0x04, 0x00, 0xC7, 0x46, 0xE0, 0x01, 0x00, 0xC7, 0x46, 0xE2, 0x03, 0x00, 0xC7, 0x46, 0xE4, 0x02);
		AM_JP_OVR_BP_CTX(0x89B0, 0x89B0, nullptr,        0xC7, 0x46, 0xDE, 0x04, 0x00, 0xC7, 0x46, 0xE0, 0x01, 0x00, 0xC7, 0x46, 0xE2, 0x03, 0x00, 0xC7, 0x46, 0xE4, 0x02, 0x00, 0xC7, 0x06, 0x26, 0x90);
		s_overlayLoaded = true;
		return;
	}

	if (st == 12) {
		AM_JP_OVR_BP_CTX(0x8074, 0x806C, W7BP_OnShowUseWindow, 0x00, 0x74, 0x03, 0xE9, 0xEF, 0xFB, 0x6A, 0x01, 0xFF, 0x76, 0xC8, 0xE8, 0xF6, 0x93, 0x83, 0xC4, 0x04, 0x8B, 0x46, 0xE0, 0xA3, 0x54, 0x91, 0xA1);
		AM_JP_OVR_BP_CTX(0x8077, 0x806F, nullptr, 0xE9, 0xEF, 0xFB, 0x6A, 0x01, 0xFF, 0x76, 0xC8, 0xE8, 0xF6, 0x93, 0x83, 0xC4, 0x04, 0x8B, 0x46, 0xE0, 0xA3, 0x54, 0x91, 0xA1, 0x56, 0x91, 0xC3);
		AM_JP_OVR_BP_CTX(0x8087, 0x807F, W7BP_OnHideUseWindow, 0xE0, 0xA3, 0x54, 0x91, 0xA1, 0x56, 0x91, 0xC3, 0xB8, 0x6C, 0xFF, 0xE8, 0x7E, 0xC3, 0xC7, 0x46, 0x96, 0x00, 0x00, 0xC7, 0x46, 0xB2, 0x00, 0x00);
		AM_JP_OVR_BP_CTX(0x808A, 0x8082, nullptr, 0x91, 0xA1, 0x56, 0x91, 0xC3, 0xB8, 0x6C, 0xFF, 0xE8, 0x7E, 0xC3, 0xC7, 0x46, 0x96, 0x00, 0x00, 0xC7, 0x46, 0xB2, 0x00, 0x00, 0xC7, 0x46, 0xB0);
		AM_JP_OVR_BP_CTX(0x74D1, 0x74CB, W7BP_OnShowSpellWindow, 0x1B, 0x00, 0x50, 0x8B, 0x46, 0xF4, 0x05, 0x72, 0x00, 0x50, 0x8B, 0x46, 0xFA, 0x05, 0x15, 0x00, 0x50, 0x8B, 0x46, 0xF4, 0x05, 0x5E, 0x00, 0x50);
		AM_JP_OVR_BP_CTX(0x74D4, 0x74CE, nullptr, 0x8B, 0x46, 0xF4, 0x05, 0x72, 0x00, 0x50, 0x8B, 0x46, 0xFA, 0x05, 0x15, 0x00, 0x50, 0x8B, 0x46, 0xF4, 0x05, 0x5E, 0x00, 0x50, 0xE8, 0x01, 0xC1);
		AM_JP_OVR_BP_CTX(0x74E2, 0x74DA, W7BP_OnHideSpellWindow, 0x00, 0x50, 0x8B, 0x46, 0xF4, 0x05, 0x5E, 0x00, 0x50, 0xE8, 0x01, 0xC1, 0x83, 0xC4, 0x08, 0x8B, 0x46, 0xDE, 0x2B, 0x46, 0xE8, 0x05, 0x14, 0x00);
		AM_JP_OVR_BP_CTX(0x74E3, 0x74DD, nullptr, 0x46, 0xF4, 0x05, 0x5E, 0x00, 0x50, 0xE8, 0x01, 0xC1, 0x83, 0xC4, 0x08, 0x8B, 0x46, 0xDE, 0x2B, 0x46, 0xE8, 0x05, 0x14, 0x00, 0x89, 0x46, 0xE6);
		AM_JP_OVR_BP_CTX(0x85D3, 0x85CC, W7BP_OnShowEnemyList, 0x00, 0x7C, 0xD2, 0xFF, 0x86, 0x52, 0xFF, 0x8B, 0x86, 0x52, 0xFF, 0x3D, 0x02, 0x00, 0x7C, 0xBF, 0x83, 0xBE, 0x4C, 0xFF, 0x02, 0x7D, 0x0A, 0xC7);
		AM_JP_OVR_BP_CTX(0x85D7, 0x85CD, nullptr, 0x7C, 0xD2, 0xFF, 0x86, 0x52, 0xFF, 0x8B, 0x86, 0x52, 0xFF, 0x3D, 0x02, 0x00, 0x7C, 0xBF, 0x83, 0xBE, 0x4C, 0xFF, 0x02, 0x7D, 0x0A, 0xC7, 0x46);
		AM_JP_OVR_BP_CTX(0x8C2E, 0x8C27, W7BP_OnHideEnemyList, 0x86, 0x54, 0xFF, 0x83, 0x7E, 0xF8, 0x00, 0x74, 0x03, 0xE9, 0x0E, 0x01, 0x3D, 0xFF, 0x00, 0x75, 0x2F, 0x8A, 0x87, 0x4E, 0x5E, 0x2A, 0xE4, 0x3D);
		AM_JP_OVR_BP_CTX(0x8C30, 0x8C28, nullptr, 0x54, 0xFF, 0x83, 0x7E, 0xF8, 0x00, 0x74, 0x03, 0xE9, 0x0E, 0x01, 0x3D, 0xFF, 0x00, 0x75, 0x2F, 0x8A, 0x87, 0x4E, 0x5E, 0x2A, 0xE4, 0x3D, 0xFF);
		s_overlayLoaded = true;
		return;
	}

	if (st == 19 || st == 20) {
		AM_JP_OVR_BP_CTX(0x9052, 0x904A, W7BP_OnShowSpellWindow, 0x68, 0x8E, 0x00, 0x6A, 0x36, 0xE8, 0x95, 0xA5, 0x83, 0xC4, 0x08, 0xFF, 0x36, 0xA2, 0x9E, 0xFF, 0x76, 0xAA, 0xFF, 0x76, 0xAC, 0xFF, 0x76, 0x04);
		// Non-combat spell execution in VDOPT.OVR.  The traditional OVR
		// disassembly/source address is 0x6B8E/0x6B91, but this DOSBox-X
		// hook table stores the corrected runtime BP offset.
		// Japanese OVR rule: runtime = source - 0x00CB.
		//   0x6B8E -> 0x6AC3
		//   0x6B91 -> 0x6AC6
		// At 0x6AC3 the compiler stack frame is already established, so
		// W7BP_OnCastSpell reads the normal BP-frame arguments.
		AM_JP_OVR_BP_CTX(0x6AC3, 0x6ABD, W7BP_OnCastSpell, 0xB8, 0xF8, 0xFF, 0xE8, 0x7D, 0xD8, 0x8B, 0x46, 0x06, 0xE9, 0x9A, 0x05, 0x6B, 0x46, 0x08, 0x32, 0x01, 0x06, 0x04, 0x84, 0x6A, 0x00, 0xFF, 0x36);
		AM_JP_OVR_BP_CTX(0x6AC6, 0x6AC6, nullptr, 0xE9, 0x9A, 0x05, 0x6B, 0x46, 0x08, 0x32, 0x01, 0x06, 0x04, 0x84, 0x6A, 0x00, 0xFF, 0x36, 0x06, 0x84, 0x6B, 0x46, 0x08, 0x05, 0x05, 0x46, 0x00, 0x50);
		AM_JP_OVR_BP_CTX(0x8DD5, 0x8DCD, W7BP_OnShowUseWindow, 0x8C, 0x63, 0x8C, 0x3D, 0x06, 0x00, 0x73, 0x08, 0x93, 0xD1, 0xE3, 0x2E, 0xFF, 0xA7, 0xF9, 0x8C, 0xEB, 0x96, 0x83, 0x7E, 0xE6, 0x00, 0x74, 0x03);
		AM_JP_OVR_BP_CTX(0x8DD8, 0x8DD4, W7BP_OnHideUseWindow, 0x08, 0x93, 0xD1, 0xE3, 0x2E, 0xFF, 0xA7, 0xF9, 0x8C, 0xEB, 0x96, 0x83, 0x7E, 0xE6, 0x00, 0x74, 0x03, 0xE9, 0xEF, 0xFB, 0x6A, 0x01, 0xFF, 0x76);
		AM_JP_OVR_BP_CTX(0x9059, 0x9051, W7BP_OnHideSpellWindow, 0xA5, 0x83, 0xC4, 0x08, 0xFF, 0x36, 0xA2, 0x9E, 0xFF, 0x76, 0xAA, 0xFF, 0x76, 0xAC, 0xFF, 0x76, 0x04, 0xE8, 0x23, 0xDB, 0x83, 0xC4, 0x08, 0xE9);
		s_overlayLoaded = true;
		return;
	}

	if (st == 18) {
		AM_JP_OVR_BP_CTX(0xA80F, 0xA80F, W7BP_OnShowBuyWindow, 0xFF, 0x76, 0x04, 0xE8, 0xE7, 0xF8, 0x59, 0xEB, 0x71, 0x6A, 0x00, 0x6A, 0x01, 0x68, 0x2C, 0x06, 0xFF, 0x76, 0x04, 0xE8, 0xF0, 0xFD, 0x83, 0xC4);
		AM_JP_OVR_BP_CTX(0xA815, 0xA815, W7BP_OnHideBuyWindow, 0x59, 0xEB, 0x71, 0x6A, 0x00, 0x6A, 0x01, 0x68, 0x2C, 0x06, 0xFF, 0x76, 0x04, 0xE8, 0xF0, 0xFD, 0x83, 0xC4, 0x08, 0x85, 0xC0, 0x74, 0x05, 0xC7);
		AM_JP_OVR_BP_CTX(0xA818, 0xA818, W7BP_OnShowSellWindow, 0x6A, 0x00, 0x6A, 0x01, 0x68, 0x2C, 0x06, 0xFF, 0x76, 0x04, 0xE8, 0xF0, 0xFD, 0x83, 0xC4, 0x08, 0x85, 0xC0, 0x74, 0x05, 0xC7, 0x46, 0xFC, 0x04);
		AM_JP_OVR_BP_CTX(0xA825, 0xA825, W7BP_OnHideSellWindow, 0x83, 0xC4, 0x08, 0x85, 0xC0, 0x74, 0x05, 0xC7, 0x46, 0xFC, 0x04, 0x00, 0xEB, 0x56, 0x6A, 0x01, 0x6A, 0x01, 0x68, 0x2F, 0x06, 0xFF, 0x76, 0x04);
		AM_JP_OVR_BP_CTX(0xA833, 0xA833, W7BP_OnShowGiveWindow, 0x6A, 0x01, 0x6A, 0x01, 0x68, 0x2F, 0x06, 0xFF, 0x76, 0x04, 0xE8, 0xD5, 0xFD, 0x83, 0xC4, 0x08, 0x85, 0xC0, 0x74, 0x05, 0xC7, 0x46, 0xFC, 0x04);
		AM_JP_OVR_BP_CTX(0xA840, 0xA840, W7BP_OnHideGiveWindow, 0x83, 0xC4, 0x08, 0x85, 0xC0, 0x74, 0x05, 0xC7, 0x46, 0xFC, 0x04, 0x00, 0xEB, 0x3B, 0xFF, 0x76, 0x04, 0xE8, 0xDE, 0x22, 0x59, 0x6A, 0x02, 0xE8);
		AM_JP_OVR_BP_CTX(0xACCB, 0xACCB, W7BP_OnShowGiveWindow, 0x6A, 0x01, 0x6A, 0x01, 0x68, 0x2F, 0x06, 0xFF, 0x76, 0x04, 0xE8, 0x3D, 0xF9, 0x83, 0xC4, 0x08, 0x85, 0xC0, 0x74, 0x0A, 0xC7, 0x46, 0xA0, 0x05);
		AM_JP_OVR_BP_CTX(0xACD8, 0xACD8, W7BP_OnHideGiveWindow, 0x83, 0xC4, 0x08, 0x85, 0xC0, 0x74, 0x0A, 0xC7, 0x46, 0xA0, 0x05, 0x00, 0xC7, 0x46, 0xA4, 0x00, 0x00, 0xE9, 0x11, 0x01, 0x83, 0x7E, 0xA0, 0x04);
		AM_JP_OVR_BP_CTX(0xB264, 0xB264, W7BP_OnShowSpellWindow, 0xFF, 0x76, 0xF4, 0xE8, 0x14, 0xD4, 0x59, 0xEB, 0x35, 0xFF, 0x76, 0xF4, 0xE8, 0xA3, 0xE9, 0x59, 0xEB, 0x2C, 0xFF, 0x76, 0xF4, 0xE8, 0x17, 0xF6);
		AM_JP_OVR_BP_CTX(0xB26A, 0xB26A, W7BP_OnHideSpellWindow, 0x59, 0xEB, 0x35, 0xFF, 0x76, 0xF4, 0xE8, 0xA3, 0xE9, 0x59, 0xEB, 0x2C, 0xFF, 0x76, 0xF4, 0xE8, 0x17, 0xF6, 0x59, 0x89, 0x46, 0xFE, 0xEB, 0x20);
		AM_JP_OVR_BP_CTX(0xB26D, 0xB26D, W7BP_OnShowUseWindow, 0xFF, 0x76, 0xF4, 0xE8, 0xA3, 0xE9, 0x59, 0xEB, 0x2C, 0xFF, 0x76, 0xF4, 0xE8, 0x17, 0xF6, 0x59, 0x89, 0x46, 0xFE, 0xEB, 0x20, 0xFF, 0x76, 0xF4);
		AM_JP_OVR_BP_CTX(0xB273, 0xB273, W7BP_OnHideUseWindow, 0x59, 0xEB, 0x2C, 0xFF, 0x76, 0xF4, 0xE8, 0x17, 0xF6, 0x59, 0x89, 0x46, 0xFE, 0xEB, 0x20, 0xFF, 0x76, 0xF4, 0xE8, 0x0F, 0xF9, 0x59, 0x89, 0x46);
		s_overlayLoaded = true;
		return;
	}

	if (st == 15 || st == 21 || st == 26) {
		AM_JP_OVR_BP_CTX(0x98CB, 0x98C3, W7BP_OnShowDisarmWindow, 0x68, 0x09, 0x01, 0x68, 0x8E, 0x00, 0x6A, 0x36, 0xE8, 0x19, 0x9D, 0x83, 0xC4, 0x08, 0x6A, 0x01, 0xFF, 0x76, 0x04, 0xE8, 0x9B, 0x81, 0x83, 0xC4);
		AM_JP_OVR_BP_CTX(0x98CE, 0x98C6, nullptr, 0x68, 0x8E, 0x00, 0x6A, 0x36, 0xE8, 0x19, 0x9D, 0x83, 0xC4, 0x08, 0x6A, 0x01, 0xFF, 0x76, 0x04, 0xE8, 0x9B, 0x81, 0x83, 0xC4, 0x04, 0xE8, 0x37);
		AM_JP_OVR_BP_CTX(0x98D6, 0x98CE, W7BP_OnHideDisarmWindow, 0x83, 0xC4, 0x08, 0x6A, 0x01, 0xFF, 0x76, 0x04, 0xE8, 0x9B, 0x81, 0x83, 0xC4, 0x04, 0xE8, 0x37, 0xEA, 0xC3, 0xB8, 0x86, 0xFF, 0xE8, 0x25, 0xAB);
		AM_JP_OVR_BP_CTX(0x98D9, 0x98D1, nullptr, 0x6A, 0x01, 0xFF, 0x76, 0x04, 0xE8, 0x9B, 0x81, 0x83, 0xC4, 0x04, 0xE8, 0x37, 0xEA, 0xC3, 0xB8, 0x86, 0xFF, 0xE8, 0x25, 0xAB, 0xC7, 0x06, 0x30);
		AM_JP_OVR_BP_CTX(0x9CE3, 0x9CDB, W7BP_OnHideLootWindow, 0xC0, 0xFF, 0xE8, 0x2B, 0xA7, 0x8D, 0x46, 0xCC, 0x50, 0x68, 0xEC, 0x13, 0xE8, 0x93, 0x69, 0x83, 0xC4, 0x04, 0x8D, 0x46, 0xCC, 0x50, 0xFF, 0x36);
		s_overlayLoaded = true;
		return;
	}

	if (st == 4 || st == 7 || st == 24) {
		AM_JP_OVR_BP_CTX(0x7037, 0x7037, W7BP_OnShowImportWindow,     0x68, 0xDE, 0x93, 0xE8, 0x15, 0xB0, 0x83, 0xC4, 0x08, 0xB8, 0x01, 0x00, 0x83, 0x7E, 0xF2, 0x00, 0x74, 0x03, 0xB8, 0x00, 0x00, 0x89, 0x46, 0xEE);
		AM_JP_OVR_BP_CTX(0x703D, 0x703D, W7BP_OnHideImportWindow,     0x83, 0xC4, 0x08, 0xB8, 0x01, 0x00, 0x83, 0x7E, 0xF2, 0x00, 0x74, 0x03, 0xB8, 0x00, 0x00, 0x89, 0x46, 0xEE, 0xEB, 0x2B, 0x80, 0x3E, 0x8D, 0x5A);
		AM_JP_OVR_BP_CTX(0x7F6C, 0x7F6C, W7BP_OnOpenSaveFileStart,    0x68, 0xF2, 0x93, 0xE8, 0xCC, 0xC8, 0x83, 0xC4, 0x04, 0x89, 0x46, 0xF6, 0x3D, 0xFF, 0xFF, 0x75, 0x23, 0x8D, 0x46, 0xDC, 0x50, 0xE8, 0x4B, 0xE4);
		AM_JP_OVR_BP_CTX(0x7F72, 0x7F72, W7BP_OnOpenSaveFileEnd,      0x83, 0xC4, 0x04, 0x89, 0x46, 0xF6, 0x3D, 0xFF, 0xFF, 0x75, 0x23, 0x8D, 0x46, 0xDC, 0x50, 0xE8, 0x4B, 0xE4, 0x59, 0x83, 0x7E, 0xF4, 0x00, 0x74);
		AM_JP_OVR_BP_CTX(0x809A, 0x809A, W7BP_OnShowLoadWindow,       0x50, 0xE8, 0x0E, 0xD0, 0x83, 0xC4, 0x0E, 0xBE, 0x48, 0x57, 0xBF, 0xC0, 0x97, 0x8C, 0xD9, 0x8E, 0xC1, 0xB9, 0xA6, 0x01, 0xF3, 0xA5, 0x6A, 0x00);
		AM_JP_OVR_BP_CTX(0x809E, 0x809E, W7BP_OnHideLoadWindow,       0x83, 0xC4, 0x0E, 0xBE, 0x48, 0x57, 0xBF, 0xC0, 0x97, 0x8C, 0xD9, 0x8E, 0xC1, 0xB9, 0xA6, 0x01, 0xF3, 0xA5, 0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x00);
		AM_JP_OVR_BP_CTX(0x8763, 0x8763, W7BP_OnCreateSaveFileStart,  0x68, 0xF2, 0x93, 0xE8, 0xD5, 0xC0, 0x83, 0xC4, 0x04, 0x89, 0x46, 0xF6, 0x3D, 0xFF, 0xFF, 0x75, 0x23, 0x8D, 0x46, 0xC6, 0x50, 0xE8, 0x54, 0xDC);
		AM_JP_OVR_BP_CTX(0x8769, 0x8769, W7BP_OnCreateSaveFileEnd,    0x83, 0xC4, 0x04, 0x89, 0x46, 0xF6, 0x3D, 0xFF, 0xFF, 0x75, 0x23, 0x8D, 0x46, 0xC6, 0x50, 0xE8, 0x54, 0xDC, 0x59, 0x83, 0x7E, 0xF2, 0x00, 0x74);
		s_overlayLoaded = true;
		return;
	}


	s_overlayLoaded = true;
}

SDL_Renderer* AM_GetRenderer() { return s_amRenderer; }
