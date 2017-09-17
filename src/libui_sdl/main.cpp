/*
    Copyright 2016-2017 StapleButter

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <stdlib.h>
#include <time.h>
#include <stdio.h>

#include <SDL2/SDL.h>
#include "libui/ui.h"

#include "../types.h"
#include "../version.h"
#include "../Config.h"

#include "../NDS.h"
#include "../GPU.h"
#include "../SPU.h"


uiWindow* MainWindow;
uiArea* MainDrawArea;

SDL_Thread* EmuThread;
int EmuRunning;

SDL_mutex* ScreenMutex;
uiDrawBitmap* ScreenBitmap = NULL;


void AudioCallback(void* data, Uint8* stream, int len)
{
    SPU::ReadOutput((s16*)stream, len>>2);
}

int EmuThreadFunc(void* burp)
{
    NDS::Init();

    // DS:
    // 547.060546875 samples per frame
    // 32823.6328125 samples per second
    //
    // 48000 samples per second:
    // 800 samples per frame
    SDL_AudioSpec whatIwant, whatIget;
    memset(&whatIwant, 0, sizeof(SDL_AudioSpec));
    whatIwant.freq = 32824; // 32823.6328125
    whatIwant.format = AUDIO_S16LSB;
    whatIwant.channels = 2;
    whatIwant.samples = 1024;
    whatIwant.callback = AudioCallback;
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &whatIwant, &whatIget, 0);
    if (!audio)
    {
        printf("Audio init failed: %s\n", SDL_GetError());
    }
    else
    {
        SDL_PauseAudioDevice(audio, 0);
    }

    u32 nframes = 0;
    u32 starttick = SDL_GetTicks();
    u32 lasttick = starttick;
    u32 lastmeasuretick = lasttick;
    u32 fpslimitcount = 0;
    bool limitfps = true;

    while (EmuRunning != 0)
    {
        if (EmuRunning == 1)
        {
            // emulate
            //SDL_LockMutex(ScreenMutex);
            u32 nlines = NDS::RunFrame();
            //SDL_UnlockMutex(ScreenMutex);

            uiAreaQueueRedrawAll(MainDrawArea);

            // framerate limiter based off SDL2_gfx
            float framerate;
            if (nlines == 263) framerate = 1000.0f / 60.0f;
            else               framerate = ((1000.0f * nlines) / 263.0f) / 60.0f;

            fpslimitcount++;
            u32 curtick = SDL_GetTicks();
            u32 delay = curtick - lasttick;
            lasttick = curtick;

            u32 wantedtick = starttick + (u32)((float)fpslimitcount * framerate);
            if (curtick < wantedtick && limitfps)
            {
                SDL_Delay(wantedtick - curtick);
            }
            else
            {
                fpslimitcount = 0;
                starttick = curtick;
            }

            nframes++;
            if (nframes >= 30)
            {
                u32 tick = SDL_GetTicks();
                u32 diff = tick - lastmeasuretick;
                lastmeasuretick = tick;

                u32 fps = (nframes * 1000) / diff;
                nframes = 0;

                float fpstarget;
                if (framerate < 1) fpstarget = 999;
                else fpstarget = 1000.0f/framerate;

                char melontitle[100];
                sprintf(melontitle, "%d/%.0f FPS | melonDS " MELONDS_VERSION, fps, fpstarget);
                uiWindowSetTitle(MainWindow, melontitle);
            }
        }
        else
        {
            // paused
            nframes = 0;
            lasttick = SDL_GetTicks();
            starttick = lasttick;
            lastmeasuretick = lasttick;
            fpslimitcount = 0;

            uiAreaQueueRedrawAll(MainDrawArea);
            SDL_Delay(50);
        }
    }

    if (audio) SDL_CloseAudioDevice(audio);

    NDS::DeInit();

    return 44203;
}


void OnAreaDraw(uiAreaHandler* handler, uiArea* area, uiAreaDrawParams* params)
{
    if (!ScreenBitmap)
        ScreenBitmap = uiDrawNewBitmap(params->Context, 256, 384);

    uiRect dorp = {0, 0, 256, 384};

    //SDL_LockMutex(ScreenMutex);
    uiDrawBitmapUpdate(ScreenBitmap, GPU::Framebuffer);
    //SDL_UnlockMutex(ScreenMutex);

    uiDrawBitmapDraw(params->Context, ScreenBitmap, &dorp, &dorp);
    //printf("draw\n");
}

void OnAreaMouseEvent(uiAreaHandler* handler, uiArea* area, uiAreaMouseEvent* evt)
{
    int x = (int)evt->X;
    int y = (int)evt->Y;
    printf("mouse: %08X %d,%d\n", (u32)evt->Held1To64, x, y);
}

void OnAreaMouseCrossed(uiAreaHandler* handler, uiArea* area, int left)
{
    //
}

void OnAreaDragBroken(uiAreaHandler* handler, uiArea* area)
{
    //
}

int OnAreaKeyEvent(uiAreaHandler* handler, uiArea* area, uiAreaKeyEvent* evt)
{
    // TODO: release all keys if the window loses focus? or somehow global key input?
    if (evt->Scancode == 0x38) // ALT
        return 0;
    if (evt->Modifiers == 0x2) // ALT+key
        return 0;

    if (evt->Up)
    {
        for (int i = 0; i < 10; i++)
            if (evt->Scancode == Config::KeyMapping[i]) NDS::ReleaseKey(i);
        if (evt->Scancode == Config::KeyMapping[10]) NDS::ReleaseKey(16);
        if (evt->Scancode == Config::KeyMapping[11]) NDS::ReleaseKey(17);
    }
    else if (!evt->Repeat)
    {
        //printf("key event: %08X %08X - %s\n", evt->Scancode, evt->Modifiers, uiKeyName(evt->Scancode));
        for (int i = 0; i < 10; i++)
            if (evt->Scancode == Config::KeyMapping[i]) NDS::PressKey(i);
        if (evt->Scancode == Config::KeyMapping[10]) NDS::PressKey(16);
        if (evt->Scancode == Config::KeyMapping[11]) NDS::PressKey(17);
    }

    return 1;
}


int OnCloseWindow(uiWindow* window, void* blarg)
{
    uiQuit();
    return 1;
}

void OnOpenFile(uiMenuItem* item, uiWindow* window, void* blarg)
{
    EmuRunning = 2;
    // TODO: ensure the emu thread has indeed stopped at this point

    char* file = uiOpenFile(window, "DS ROM (*.nds)|*.nds;*.srl|Any file|*.*", NULL);
    if (!file) return;

    NDS::LoadROM(file, Config::DirectBoot);

    EmuRunning = 1;
}


int main(int argc, char** argv)
{
    srand(time(NULL));

    printf("melonDS " MELONDS_VERSION "\n");
    printf(MELONDS_URL "\n");

    // http://stackoverflow.com/questions/14543333/joystick-wont-work-using-sdl
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
    {
        printf("SDL shat itself :(\n");
        return 1;
    }

    ScreenMutex = SDL_CreateMutex();

    uiInitOptions ui_opt;
    memset(&ui_opt, 0, sizeof(uiInitOptions));
    const char* ui_err = uiInit(&ui_opt);
    if (ui_err != NULL)
    {
        printf("libui shat itself :( %s\n", ui_err);
        uiFreeInitError(ui_err);
        return 1;
    }

    Config::Load();

    uiMenu* menu;
    uiMenuItem* menuitem;

    menu = uiNewMenu("File");
    menuitem = uiMenuAppendItem(menu, "Open ROM...");
    uiMenuItemOnClicked(menuitem, OnOpenFile, NULL);
    uiMenuAppendSeparator(menu);
    uiMenuAppendItem(menu, "Quit");

    MainWindow = uiNewWindow("melonDS " MELONDS_VERSION, 256, 384, 1);
    uiWindowOnClosing(MainWindow, OnCloseWindow, NULL);

    uiAreaHandler areahandler;

    areahandler.Draw = OnAreaDraw;
    areahandler.MouseEvent = OnAreaMouseEvent;
    areahandler.MouseCrossed = OnAreaMouseCrossed;
    areahandler.DragBroken = OnAreaDragBroken;
    areahandler.KeyEvent = OnAreaKeyEvent;

    MainDrawArea = uiNewArea(&areahandler);
    uiWindowSetChild(MainWindow, uiControl(MainDrawArea));

    EmuRunning = 2;
    EmuThread = SDL_CreateThread(EmuThreadFunc, "melonDS magic", NULL);

    uiControlShow(uiControl(MainWindow));
    uiControlSetFocus(uiControl(MainDrawArea)); // TODO: this needs to be done when the window regains focus
    uiMain();

    EmuRunning = 0;
    SDL_WaitThread(EmuThread, NULL);

    SDL_DestroyMutex(ScreenMutex);
    uiDrawFreeBitmap(ScreenBitmap);

    uiUninit();
    SDL_Quit();
    return 0;
}

#ifdef __WIN32__

#include <windows.h>

int CALLBACK WinMain(HINSTANCE hinst, HINSTANCE hprev, LPSTR cmdline, int cmdshow)
{
    char cmdargs[16][256];
    int arg = 0;
    int j = 0;
    bool inquote = false;
    int len = strlen(cmdline);
    for (int i = 0; i < len; i++)
    {
        char c = cmdline[i];
        if (c == '\0') break;
        if (c == '"') inquote = !inquote;
        if (!inquote && c==' ')
        {
            if (j > 255) j = 255;
            if (arg < 16) cmdargs[arg][j] = '\0';
            arg++;
            j = 0;
        }
        else
        {
            if (arg < 16 && j < 255) cmdargs[arg][j] = c;
            j++;
        }
    }
    if (j > 255) j = 255;
    if (arg < 16) cmdargs[arg][j] = '\0';

    return main(arg, (char**)cmdargs);
}

#endif
