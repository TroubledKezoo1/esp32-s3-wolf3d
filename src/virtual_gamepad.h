#ifndef VIRTUAL_GAMEPAD_H
#define VIRTUAL_GAMEPAD_H

#include "DisplayDriver.h"

// Initialize the virtual gamepad (draws UI and starts the touch task)
void VirtualGamepad_Init(LGFX_Waveshare7* lcd_ptr);

// Task to poll touch and update Wolf3D Keyboard state
void VirtualGamepad_Task(void* pvParameters);

// Shared buffer for virtual keys, so they aren't erased by IN_ProcessEvents
extern bool VirtualKeyboard[256];

// Draw the UI over the current framebuffer
void VirtualGamepad_DrawOverlay(LovyanGFX* target = nullptr);

// Mutex for safe multicore access to VirtualKeyboard array
extern portMUX_TYPE virtualGamepadMux;

extern uint8_t* ui_mask;
extern uint8_t ui_blk[480][10];
extern int ui_needs_redraw;
void VirtualGamepad_InitMask();

// On-screen QWERTY keyboard (blocking, call from the game task).
// Returns 1 = OK (buf updated), 0 = cancelled, -1 = unavailable.
int VirtualKeyboard_LineInput(char* buf, int maxchars, bool escok);
// Display task hooks: true while the keyboard is open, and its renderer.
bool VirtualKeyboard_IsActive();
void VirtualKeyboard_Render(LovyanGFX* target, bool is_direct);
#endif
