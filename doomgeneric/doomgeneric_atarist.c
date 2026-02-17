// doomgeneric for Atari ST using MiNTlib SDL 1.2 with the xbios driver.

#include "doomkeys.h"
#include "doomgeneric.h"
#include "i_video.h"

#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#if defined(__MINT__)
#include <mint/osbind.h>
#include <mint/cookie.h>
#endif

#ifndef ATARI_USE_SUPERVISOR
#define ATARI_USE_SUPERVISOR 0
#endif

#ifndef ATARI_TARGET_FPS
#define ATARI_TARGET_FPS 0
#endif

#ifndef ATARI_SHOW_FPS
#define ATARI_SHOW_FPS 0
#endif

#define KEYQUEUE_SIZE 16

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;
static int s_Running = 1;
static uint32_t s_LastPresentMs = 0;

static SDL_Surface *s_ScreenSurface = NULL;
static SDL_Surface *s_FrameSurface = NULL;
static char s_SdlVideoDriverEnv[48];

#if ATARI_SHOW_FPS
static const uint8_t s_FpsFontDigits[10][5] =
{
    { 0x7, 0x5, 0x5, 0x5, 0x7 }, /* 0 */
    { 0x2, 0x6, 0x2, 0x2, 0x7 }, /* 1 */
    { 0x7, 0x1, 0x7, 0x4, 0x7 }, /* 2 */
    { 0x7, 0x1, 0x7, 0x1, 0x7 }, /* 3 */
    { 0x5, 0x5, 0x7, 0x1, 0x1 }, /* 4 */
    { 0x7, 0x4, 0x7, 0x1, 0x7 }, /* 5 */
    { 0x7, 0x4, 0x7, 0x5, 0x7 }, /* 6 */
    { 0x7, 0x1, 0x1, 0x1, 0x1 }, /* 7 */
    { 0x7, 0x5, 0x7, 0x5, 0x7 }, /* 8 */
    { 0x7, 0x5, 0x7, 0x1, 0x7 }  /* 9 */
};
static const uint8_t s_FpsFontF[5] = { 0x7, 0x4, 0x7, 0x4, 0x4 };
static const uint8_t s_FpsFontP[5] = { 0x7, 0x5, 0x7, 0x4, 0x4 };
static const uint8_t s_FpsFontS[5] = { 0x7, 0x4, 0x7, 0x1, 0x7 };
static const uint8_t s_FpsFontColon[5] = { 0x0, 0x2, 0x0, 0x2, 0x0 };
static const uint8_t s_FpsFontBlank[5] = { 0x0, 0x0, 0x0, 0x0, 0x0 };

static uint32_t s_FpsLastMs = 0;
static int s_FpsFrames = 0;
static int s_FpsValue = 0;

static const uint8_t *atariFpsGlyphForChar(char c)
{
    if (c >= '0' && c <= '9')
    {
        return s_FpsFontDigits[(int)(c - '0')];
    }

    switch (c)
    {
    case 'F':
        return s_FpsFontF;
    case 'P':
        return s_FpsFontP;
    case 'S':
        return s_FpsFontS;
    case ':':
        return s_FpsFontColon;
    default:
        return s_FpsFontBlank;
    }
}

static void atariDrawFpsText(SDL_Surface *target, int x, int y,
                             const char *text, uint8_t fg, uint8_t bg)
{
    int i;
    int row;
    int len;
    int textWidth;
    uint8_t *rowDst;
    const uint8_t *glyph;

    if (target == NULL || text == NULL || target->pixels == NULL)
    {
        return;
    }

    len = (int)strlen(text);
    if (len <= 0)
    {
        return;
    }

    textWidth = len * 4;
    if (x < 0 || y < 0 || (x + textWidth) > target->w || (y + 5) > target->h)
    {
        return;
    }

    for (row = 0; row < 5; ++row)
    {
        rowDst = (uint8_t *)target->pixels
               + (size_t)(y + row) * (size_t)target->pitch + (size_t)x;
        memset(rowDst, bg, (size_t)textWidth);
    }

    for (i = 0; i < len; ++i)
    {
        glyph = atariFpsGlyphForChar(text[i]);
        for (row = 0; row < 5; ++row)
        {
            uint8_t bits = glyph[row];
            uint8_t *px = (uint8_t *)target->pixels
                        + (size_t)(y + row) * (size_t)target->pitch
                        + (size_t)x + (size_t)(i * 4);

            if (bits & 0x4) px[0] = fg;
            if (bits & 0x2) px[1] = fg;
            if (bits & 0x1) px[2] = fg;
        }
    }
}

static void atariDrawFpsOverlay(SDL_Surface *target)
{
    char text[12];
    uint32_t now;
    uint32_t elapsed;

    if (target == NULL || target->format == NULL || target->format->BitsPerPixel != 8)
    {
        return;
    }

    now = SDL_GetTicks();
    if (s_FpsLastMs == 0 || now < s_FpsLastMs)
    {
        s_FpsLastMs = now;
        s_FpsFrames = 0;
        s_FpsValue = 0;
    }

    s_FpsFrames++;
    elapsed = now - s_FpsLastMs;
    if (elapsed >= 1000)
    {
        s_FpsValue = (int)((s_FpsFrames * 1000U + (elapsed / 2U)) / elapsed);
        s_FpsFrames = 0;
        s_FpsLastMs = now;
    }

    if (s_FpsValue < 0)
    {
        s_FpsValue = 0;
    }
    else if (s_FpsValue > 999)
    {
        s_FpsValue = 999;
    }

    snprintf(text, sizeof(text), "FPS:%3d", s_FpsValue);
    atariDrawFpsText(target, 2, 2, text, 255, 0);
}
#endif

#if defined(__MINT__)
#define MCH_MEGA_STE 0x00010010L

static int is_megaste(void)
{
    long mch = 0;

    if (C_FOUND != Getcookie(C__MCH, &mch))
    {
        return 0;
    }

    return mch == MCH_MEGA_STE;
}

static long megaste_enable_16mhz_cache_super(void)
{
    *(volatile uint8_t *)0xFFFF8E21 = 0x03;
    return 0;
}

static void AtariEnableMegaSTE16MHz(void)
{
    if (is_megaste())
    {
        Supexec(megaste_enable_16mhz_cache_super);
    }
}

#if ATARI_USE_SUPERVISOR
static int s_InSupervisorMode = 0;
static long s_SupervisorStack = 0;

__attribute__((noinline)) static void AtariEnterSupervisorMode(void)
{
    s_SupervisorStack = Super(0L);
    s_InSupervisorMode = 1;
}

__attribute__((noinline)) static void AtariLeaveSupervisorMode(void)
{
    if (!s_InSupervisorMode)
    {
        return;
    }

    SuperToUser(s_SupervisorStack);
    s_InSupervisorMode = 0;
}
#else
static void AtariEnterSupervisorMode(void) {}
static void AtariLeaveSupervisorMode(void) {}
#endif
#endif

static unsigned char convertToDoomKey(unsigned int key)
{
    switch (key)
    {
    case SDLK_RETURN:
        return KEY_ENTER;
    case SDLK_ESCAPE:
        return KEY_ESCAPE;
    case SDLK_LEFT:
        return KEY_LEFTARROW;
    case SDLK_RIGHT:
        return KEY_RIGHTARROW;
    case SDLK_UP:
        return KEY_UPARROW;
    case SDLK_DOWN:
        return KEY_DOWNARROW;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
        return KEY_FIRE;
    case SDLK_SPACE:
        return KEY_USE;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        return KEY_RSHIFT;
    case SDLK_LALT:
    case SDLK_RALT:
        return KEY_LALT;
    case SDLK_F2:
        return KEY_F2;
    case SDLK_F3:
        return KEY_F3;
    case SDLK_F4:
        return KEY_F4;
    case SDLK_F5:
        return KEY_F5;
    case SDLK_F6:
        return KEY_F6;
    case SDLK_F7:
        return KEY_F7;
    case SDLK_F8:
        return KEY_F8;
    case SDLK_F9:
        return KEY_F9;
    case SDLK_F10:
        return KEY_F10;
    case SDLK_F11:
        return KEY_F11;
    case SDLK_EQUALS:
    case SDLK_PLUS:
        return KEY_EQUALS;
    case SDLK_MINUS:
        return KEY_MINUS;
    default:
        return (unsigned char)tolower((int)key);
    }
}

static void addKeyToQueue(int pressed, unsigned int keyCode)
{
    unsigned short keyData;
    unsigned char key;

    key = convertToDoomKey(keyCode);
    keyData = (unsigned short)((pressed << 8) | key);

    s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
    s_KeyQueueWriteIndex = (s_KeyQueueWriteIndex + 1) % KEYQUEUE_SIZE;
}

static void handleKeyInput(void)
{
    SDL_Event e;

    while (SDL_PollEvent(&e))
    {
        if (e.type == SDL_QUIT)
        {
#if !defined(__MINT__)
            s_Running = 0;
#endif
            continue;
        }

        if (e.type == SDL_KEYDOWN)
        {
            addKeyToQueue(1, e.key.keysym.sym);
        }
        else if (e.type == SDL_KEYUP)
        {
            addKeyToQueue(0, e.key.keysym.sym);
        }
    }
}

static void atariSetVideoDriver(const char *driver)
{
    if (driver == NULL || *driver == '\0')
    {
        return;
    }

    snprintf(s_SdlVideoDriverEnv, sizeof(s_SdlVideoDriverEnv),
             "SDL_VIDEODRIVER=%s", driver);
    SDL_putenv(s_SdlVideoDriverEnv);
}

static SDL_Surface *atariTrySetVideoMode(int width, int height, Uint32 flags)
{
    static const int bppTry[] = {8, 0, 4, 16, 32};
    int i;
    SDL_Surface *surface;

    surface = NULL;
    for (i = 0; i < (int)(sizeof(bppTry) / sizeof(bppTry[0])); ++i)
    {
        surface = SDL_SetVideoMode(width, height, bppTry[i], flags);
        if (surface != NULL)
        {
            break;
        }
    }

    return surface;
}

static void AtariApplyPalette(void)
{
    SDL_Color pal[256];
    int i;

    if (!palette_changed || s_FrameSurface == NULL)
    {
        return;
    }

    for (i = 0; i < 256; ++i)
    {
        pal[i].r = (Uint8)colors[i].r;
        pal[i].g = (Uint8)colors[i].g;
        pal[i].b = (Uint8)colors[i].b;
        pal[i].unused = 0;
    }

    SDL_SetColors(s_FrameSurface, pal, 0, 256);
    if (s_ScreenSurface != NULL
        && s_ScreenSurface != s_FrameSurface
        && s_ScreenSurface->format != NULL
        && s_ScreenSurface->format->BitsPerPixel == 8)
    {
        SDL_SetColors(s_ScreenSurface, pal, 0, 256);
    }

    palette_changed = false;
}

static void DG_Shutdown(void)
{
    if (s_FrameSurface != NULL)
    {
        SDL_FreeSurface(s_FrameSurface);
        s_FrameSurface = NULL;
    }

    if (SDL_WasInit(SDL_INIT_EVERYTHING) != 0)
    {
        SDL_Quit();
    }

#if defined(__MINT__)
    AtariLeaveSupervisorMode();
#endif
}

void DG_Init(void)
{
    Uint32 flags;
    char driverName[32];
    const char *activeDriver;

    atariSetVideoDriver("xbios");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        exit(1);
    }

    activeDriver = SDL_VideoDriverName(driverName, (int)sizeof(driverName));
    if (activeDriver == NULL || strcmp(activeDriver, "xbios") != 0)
    {
        fprintf(stderr, "Expected SDL xbios driver, got '%s'\n",
                activeDriver != NULL ? activeDriver : "(null)");
        exit(1);
    }

    flags = SDL_SWSURFACE | SDL_FULLSCREEN;
    s_ScreenSurface = atariTrySetVideoMode(DOOMGENERIC_RESX, DOOMGENERIC_RESY, flags);
    if (s_ScreenSurface == NULL)
    {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        exit(1);
    }

    SDL_WM_SetCaption("DOOM", NULL);
    SDL_ShowCursor(SDL_DISABLE);

    s_FrameSurface = SDL_CreateRGBSurfaceFrom(
        DG_ScreenBuffer,
        DOOMGENERIC_RESX,
        DOOMGENERIC_RESY,
        8,
        DOOMGENERIC_RESX * (int)sizeof(pixel_t),
        0, 0, 0, 0);

    if (s_FrameSurface == NULL)
    {
        fprintf(stderr, "SDL_CreateRGBSurfaceFrom failed: %s\n", SDL_GetError());
        exit(1);
    }
}

void DG_DrawFrame(void)
{
#if ATARI_TARGET_FPS > 0
    uint32_t now;
    uint32_t frameMs;

    frameMs = (uint32_t)(1000 / ATARI_TARGET_FPS);
    if (frameMs == 0)
    {
        frameMs = 1;
    }

    now = SDL_GetTicks();
    if (s_LastPresentMs != 0 && (uint32_t)(now - s_LastPresentMs) < frameMs)
    {
        handleKeyInput();
        return;
    }
    s_LastPresentMs = now;
#endif

    AtariApplyPalette();
#if ATARI_SHOW_FPS
    atariDrawFpsOverlay(s_FrameSurface);
#endif
    SDL_BlitSurface(s_FrameSurface, NULL, s_ScreenSurface, NULL);
    SDL_Flip(s_ScreenSurface);

    handleKeyInput();
}

void DG_SleepMs(uint32_t ms)
{
    SDL_Delay(ms);
}

uint32_t DG_GetTicksMs(void)
{
    return SDL_GetTicks();
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    unsigned short keyData;

    if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex)
    {
        return 0;
    }

    keyData = s_KeyQueue[s_KeyQueueReadIndex];
    s_KeyQueueReadIndex = (s_KeyQueueReadIndex + 1) % KEYQUEUE_SIZE;

    *pressed = keyData >> 8;
    *doomKey = keyData & 0xFF;

    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    SDL_WM_SetCaption(title, NULL);
}

int main(int argc, char **argv)
{
    atexit(DG_Shutdown);

#if defined(__MINT__)
    AtariEnableMegaSTE16MHz();
    AtariEnterSupervisorMode();
#endif

    doomgeneric_Create(argc, argv);

    while (s_Running)
    {
        doomgeneric_Tick();
    }

    return 0;
}
