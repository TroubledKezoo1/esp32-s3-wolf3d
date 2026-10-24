// ID_IN.C - Input Manager (ESP32-S3 Touch port)

#include "wl_def.h"
#include "id_in.h"
#include "../virtual_gamepad.h"
#include <Arduino.h>
#include "../DisplayDriver.h"
#include <lgfx/v1/platforms/common.hpp>

extern LGFX_Waveshare7 lcd;

boolean MousePresent;
boolean grabmouse = true;

std::unordered_map<ScanCode, boolean> Keyboard;
volatile boolean Paused;
volatile char    LastASCII;
ScanCode LastScan;

static KeyboardDef KbdDefs = {
    sc_Control, sc_Alt,
    sc_Home, sc_UpArrow, sc_PgUp,
    sc_LeftArrow, sc_RightArrow,
    sc_End, sc_DownArrow, sc_PgDn
};

int JoyNumButtons;
static int JoyNumHats;

byte ASCIINames[] = {
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,8  ,9  ,0  ,0  ,0  ,13 ,0  ,0  ,
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,27 ,0  ,0  ,0  ,
    ' ',0  ,0  ,0  ,0  ,0  ,0  ,39 ,0  ,0  ,'*','+',',','-','.','/',
    '0','1','2','3','4','5','6','7','8','9',0  ,';',0  ,'=',0  ,0  ,
    '`','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
    'p','q','r','s','t','u','v','w','x','y','z','[',92 ,']',0  ,0  ,
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0
};
byte ShiftNames[] = {
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,8  ,9  ,0  ,0  ,0  ,13 ,0  ,0  ,
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,27 ,0  ,0  ,0  ,
    ' ',0  ,0  ,0  ,0  ,0  ,0  ,34 ,0  ,0  ,'*','+','<','_','>','?',
    ')','!','@','#','$','%','^','&','*','(',0  ,':',0  ,'+',0  ,0  ,
    '~','A','B','C','D','E','F','G','H','I','J','K','L','M','N','O',
    'P','Q','R','S','T','U','V','W','X','Y','Z','{','|','}',0  ,0  ,
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0
};
byte SpecialNames[] = {
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,13 ,0  ,0  ,0  ,
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,
    0  ,0  ,0  ,0  ,0  ,'/',0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0
};

static boolean IN_Started;

static Direction DirTable[] = {
    dir_NorthWest, dir_North, dir_NorthEast,
    dir_West,      dir_None,  dir_East,
    dir_SouthWest, dir_South, dir_SouthEast
};

static boolean screenvisible;
static boolean window_focused;
static int window_w, window_h;

static boolean MouseShouldBeGrabbed(void)
{
    if (!window_focused) return false;
    if (fullscreen)      return true;
    if (!grabmouse)      return false;
    if (Paused || menuactive) return false;
    return ingame && !demoplayback;
}

void IN_UpdateGrab(void) {}

static int INL_GetMouseButtons(void) { return 0; }

void IN_GetJoyDelta(int *dx, int *dy)      { *dx = 0; *dy = 0; }
void IN_GetJoyFineDelta(int *dx, int *dy)  { *dx = 0; *dy = 0; }
void IN_GetMouseDelta(int *dx, int *dy)    { *dx = 0; *dy = 0; }
int  IN_JoyButtons()                       { return 0; }
boolean IN_JoyPresent()                    { return false; }

// Touch state written by touchTask (Core 0), read by game loop (Core 1).
// volatile so compiler doesn't cache them in registers.
// touch_active is no longer used but kept for compilation compatibility if referenced
static volatile bool touch_active  = false;
static volatile int32_t touch_x = 0;
static volatile int32_t touch_y = 0;

void IN_WaitAndProcessEvents()
{
    IN_ProcessEvents();
    taskYIELD();
}

extern bool VirtualKeyboard[256];

void IN_ProcessEvents()
{
    Keyboard[sc_UpArrow]    = false;
    Keyboard[sc_DownArrow]  = false;
    Keyboard[sc_LeftArrow]  = false;
    Keyboard[sc_RightArrow] = false;
    Keyboard[sc_Alt]        = false;
    Keyboard[sc_LShift]     = false;
    Keyboard[sc_Control]    = false;
    Keyboard[sc_Space]      = false;
    Keyboard[sc_Enter]      = false;
    Keyboard[sc_Escape]     = false;
    Keyboard[sc_1]          = false;
    Keyboard[sc_2]          = false;
    Keyboard[sc_3]          = false;
    Keyboard[sc_4]          = false;

    // --- Virtual Gamepad Integration ---
    static bool prevVirtualKeyboard[256] = {false};
    
    extern portMUX_TYPE virtualGamepadMux;
    portENTER_CRITICAL(&virtualGamepadMux);
    
    // A Button (Mapped to sc_Space in VirtualGamepad) -> Open (Game) + Select (Menu)
    if (VirtualKeyboard[sc_Space]) {
        Keyboard[sc_Enter] = true;
        if (!prevVirtualKeyboard[sc_Space]) LastScan = sc_Enter;
    }
    
    // B Button (Mapped to sc_LShift in VirtualGamepad) -> Run (Game) + Back (Menu)
    if (VirtualKeyboard[sc_LShift]) {
        if (menuactive) {
            Keyboard[sc_Escape] = true;
            if (!prevVirtualKeyboard[sc_LShift]) LastScan = sc_Escape;
        }
    }

    for (int i = 0; i < 256; i++) {
        if (VirtualKeyboard[i]) {
            Keyboard[(ScanCode)i] = true;
            // Edge trigger LastScan to prevent menu cursor from flying at 70fps
            if (!prevVirtualKeyboard[i]) {
                LastScan = (ScanCode)i;
            }
        }
        prevVirtualKeyboard[i] = VirtualKeyboard[i];
    }
    
    portEXIT_CRITICAL(&virtualGamepadMux);
    // -----------------------------------

    static int current_weapon = 0;
    static bool weapon_button_pressed = false;

    // ADC logic removed - it was causing phantom inputs
    weapon_button_pressed = false;
    
    // Check Serial for WASD keyboard control (from PC Serial Monitor)
    static uint32_t last_serial_rx = 0;
    static char last_key = 0;
    
    while (Serial.available()) {
        last_key = Serial.read();
        last_serial_rx = millis();
    }
    
    // Hold the key state for 150ms after the last character is received
    // (Serial monitors usually auto-repeat keys when held down)
    if (millis() - last_serial_rx < 150 && last_key != 0) {
        // Only set LastScan for specific keys to avoid instantly selecting menu items
        switch(last_key) {
            case 'w': case 'W': Keyboard[sc_UpArrow] = true; LastScan = sc_UpArrow; break;
            case 's': case 'S': Keyboard[sc_DownArrow] = true; LastScan = sc_DownArrow; break;
            case 'a': case 'A': Keyboard[sc_LeftArrow] = true; LastScan = sc_LeftArrow; break;
            case 'd': case 'D': Keyboard[sc_RightArrow] = true; LastScan = sc_RightArrow; break;
            case '\n': case '\r': case 'p': case 'P': Keyboard[sc_Enter] = true; LastScan = sc_Enter; break; // Enter to select (P is alternative)
            case 'f': case 'F': Keyboard[sc_Control] = true; break; // Fire
            case 'e': case 'E': Keyboard[sc_Space] = true; break;   // Use
            case 'q': case 'Q': 
                if (!weapon_button_pressed) {
                    current_weapon = (current_weapon + 1) % 4;
                    weapon_button_pressed = true;
                }
                if (current_weapon == 0) Keyboard[sc_1] = true;
                if (current_weapon == 1) Keyboard[sc_2] = true;
                if (current_weapon == 2) Keyboard[sc_3] = true;
                if (current_weapon == 3) Keyboard[sc_4] = true;
                break;
            case 'r': case 'R': Keyboard[sc_LShift] = true; Keyboard[sc_UpArrow] = true; break; // Run
            case 'c': case 'C': Keyboard[sc_Alt] = true; break;     // Strafe
            case 'x': case 'X': Keyboard[sc_Escape] = true; LastScan = sc_Escape; break;  // ESC (since 27 is hard to send from monitor)
        }
    } else {
        last_key = 0;
    }
    
#ifdef ESP32_DEBUG_AUTOSTART
    // Debug env: keep turning and walking so frame timing is measured with a moving view
    Keyboard[sc_UpArrow] = true;
    Keyboard[sc_RightArrow] = ((millis() / 3000) % 2) == 0;
    Keyboard[sc_LeftArrow]  = !Keyboard[sc_RightArrow];
    // Acknowledge intermission screens: tap Enter every 1.5s
    { static uint32_t last_ack = 0;
      if (millis() - last_ack > 1500) { last_ack = millis(); Keyboard[sc_Enter] = true; LastScan = sc_Enter; } }
    // Force a level completion every 45s to exercise level transitions (heap fragmentation test)
    { static uint32_t last_complete = 0;
      if (millis() - last_complete > 45000 && playstate == ex_stillplaying) { last_complete = millis(); playstate = ex_completed; log_e("[DBG] forcing level completion"); } }
#endif
}

void IN_Startup(void)
{
    if (IN_Started) return;
    
    IN_ClearKeysDown();
    MousePresent = false;
    IN_Started   = true;
}

void IN_Shutdown(void)
{
    if (!IN_Started) return;
    IN_Started = false;
}

void IN_ClearKeysDown(void)
{
    LastScan  = sc_None;
    LastASCII = key_None;
    Keyboard.clear();
}

void IN_ReadControl(int player, ControlInfo *info)
{
    uint16_t buttons = 0;
    int dx = 0, dy = 0;
    Motion mx = motion_None, my = motion_None;

    IN_ProcessEvents();

    if      (Keyboard[(ScanCode)KbdDefs.upleft])    { mx = motion_Left;  my = motion_Up;   }
    else if (Keyboard[(ScanCode)KbdDefs.upright])   { mx = motion_Right; my = motion_Up;   }
    else if (Keyboard[(ScanCode)KbdDefs.downleft])  { mx = motion_Left;  my = motion_Down; }
    else if (Keyboard[(ScanCode)KbdDefs.downright]) { mx = motion_Right; my = motion_Down; }

    if      (Keyboard[(ScanCode)KbdDefs.up])    my = motion_Up;
    else if (Keyboard[(ScanCode)KbdDefs.down])  my = motion_Down;
    if      (Keyboard[(ScanCode)KbdDefs.left])  mx = motion_Left;
    else if (Keyboard[(ScanCode)KbdDefs.right]) mx = motion_Right;

    if (Keyboard[(ScanCode)KbdDefs.btn0]) buttons += 1 << 0;
    if (Keyboard[(ScanCode)KbdDefs.btn1]) buttons += 1 << 1;


    info->x     = mx * 127;
    info->xaxis = mx;
    info->y     = my * 127;
    info->yaxis = my;
    info->btn0  = (buttons & (1 << 0)) != 0;
    info->btn1  = (buttons & (1 << 1)) != 0;
    info->btn2  = (buttons & (1 << 2)) != 0;
    info->btn3  = (buttons & (1 << 3)) != 0;
    info->dir   = DirTable[((my + 1) * 3) + (mx + 1)];
}

ScanCode IN_WaitForKey(void)
{
    ScanCode result;
    while ((result = LastScan) == 0) IN_WaitAndProcessEvents();
    LastScan = 0;
    return result;
}

char IN_WaitForASCII(void)
{
    char result;
    while ((result = LastASCII) == 0) IN_WaitAndProcessEvents();
    LastASCII = '\0';
    return result;
}

boolean btnstate[NUMBUTTONS];

void IN_StartAck(void)
{
    IN_ProcessEvents();
    IN_ClearKeysDown();
    memset(btnstate, 0, sizeof(btnstate));

    int buttons = IN_JoyButtons() << 4;
    if (MousePresent) buttons |= IN_MouseButtons();
    for (int i = 0; i < NUMBUTTONS; i++, buttons >>= 1)
        if (buttons & 1) btnstate[i] = true;
}

boolean IN_CheckAck(void)
{
    IN_ProcessEvents();
    if (LastScan) return true;

    int buttons = IN_JoyButtons() << 4;
    if (MousePresent) buttons |= IN_MouseButtons();

    for (int i = 0; i < NUMBUTTONS; i++, buttons >>= 1) {
        if (buttons & 1) {
            if (!btnstate[i]) {
                do {
                    IN_WaitAndProcessEvents();
                    buttons = IN_JoyButtons() << 4;
                    if (MousePresent) buttons |= IN_MouseButtons();
                } while (buttons & (1 << i));
                return true;
            }
        } else {
            btnstate[i] = false;
        }
    }
    return false;
}

void IN_Ack(void)
{
    IN_StartAck();
    do { IN_WaitAndProcessEvents(); } while (!IN_CheckAck());
}

bool Wolf_IN_UserInput(uint32_t delay)
{
    longword lasttime = GetTimeCount();
    IN_StartAck();
    do {
        IN_ProcessEvents();
        if (IN_CheckAck()) return true;
        vTaskDelay(5);
    } while (GetTimeCount() - lasttime < delay);
    return false;
}

int IN_MouseButtons(void)
{
    return MousePresent ? INL_GetMouseButtons() : 0;
}
