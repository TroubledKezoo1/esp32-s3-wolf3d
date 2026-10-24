#include "virtual_gamepad.h"
#include "wolf4sdl/id_in.h"
#include "wolf4sdl/wl_def.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static LGFX_Waveshare7* g_lcd = nullptr;
bool VirtualKeyboard[256] = {false};
uint8_t* ui_mask = nullptr;   // 800x480 bits, allocated in PSRAM (keeps 48KB of internal RAM free)
uint8_t ui_blk[480][10] = {{0}};
int ui_needs_redraw = 2;
portMUX_TYPE virtualGamepadMux = portMUX_INITIALIZER_UNLOCKED;

struct Btn {
    int x, y, r; // Circle
    int w, h;    // Rect
    bool is_rect;
    ScanCode sc;
    bool pressed;
    uint16_t color;
    const char* label;
};

#define BG_COLOR        g_lcd->color565(30, 30, 30)
#define BTN_BASE        g_lcd->color565(50, 50, 50)
#define BTN_HIGHLIGHT   g_lcd->color565(200, 200, 200)
#define DPAD_BASE       g_lcd->color565(40, 40, 40)
#define TEXT_COLOR      g_lcd->color565(220, 220, 220)

// Left side: 0 to 160. Center of D-Pad: X=80, Y=340
// Right side: 640 to 800. Center of ABXY: X=720, Y=340
static Btn buttons[] = {
    // D-Pad (Left side) - Rectangular/polygon blocks (Center: 130, 350, Gap: 60)
    { 100, 260, 0, 60, 60, true, sc_UpArrow,    false, BTN_HIGHLIGHT, "^" },
    { 100, 380, 0, 60, 60, true, sc_DownArrow,  false, BTN_HIGHLIGHT, "v" },
    { 40,  320, 0, 60, 60, true, sc_LeftArrow,  false, BTN_HIGHLIGHT, "<" },
    { 160, 320, 0, 60, 60, true, sc_RightArrow, false, BTN_HIGHLIGHT, ">" },

    // Action Buttons (Right side) - Xbox Layout (Center: 660, 350)
    // A (Bottom): Open (Green)
    { 660, 430, 35, 0, 0, false, sc_Space,   false, 0x07E0, "A" },
    // B (Right): Run (Red)
    { 740, 350, 35, 0, 0, false, sc_LShift,  false, 0xF800, "B" },
    // X (Left): Fire (Blue)
    { 580, 350, 35, 0, 0, false, sc_Control, false, 0x001F, "X" },
    // Y (Top): Strafe (Yellow)
    { 660, 270, 35, 0, 0, false, sc_Alt,     false, 0xFFE0, "Y" },
    // Menu (Top Right)
    { 750, 50, 25, 0, 0, false, sc_Escape,  false, 0x7BEF, "M" }
};
#define NUM_BTNS (sizeof(buttons)/sizeof(buttons[0]))

static LGFX_Sprite* btnSprite = nullptr;

static void drawButton(Btn* b, bool highlighted, LovyanGFX* target) {
    if (!btnSprite) return;
    uint16_t c = highlighted ? b->color : (b->is_rect ? DPAD_BASE : BTN_BASE);
    
    btnSprite->fillSprite(0xF81F); // Magenta (Transparent key)
    btnSprite->setTextColor(TEXT_COLOR);
    btnSprite->setTextSize(2);
    
    int cx = 36;
    int cy = 36;
    
    if (b->is_rect) {
        btnSprite->fillRoundRect(cx - b->w/2, cy - b->h/2, b->w, b->h, 10, c);
        btnSprite->drawRoundRect(cx - b->w/2, cy - b->h/2, b->w, b->h, 10, g_lcd->color565(80, 80, 80));
        btnSprite->drawCenterString(b->label, cx, cy - 8);
    } else {
        btnSprite->fillCircle(cx, cy, b->r, c);
        btnSprite->drawCircle(cx, cy, b->r, g_lcd->color565(80, 80, 80));
        btnSprite->drawCenterString(b->label, cx, cy - 8);
    }
    if (target) btnSprite->pushSprite(target, b->x - 36, b->y - 36, 0xF81F);
}

void VirtualGamepad_Init(LGFX_Waveshare7* lcd_ptr) {
    g_lcd = lcd_ptr;
    
    btnSprite = new LGFX_Sprite(g_lcd);
    btnSprite->setPsram(true);
    btnSprite->setColorDepth(16);
    btnSprite->createSprite(72, 72);
    
    // Start the touch polling task
    xTaskCreatePinnedToCore(VirtualGamepad_Task, "GamepadTask", 4096, NULL, 1, NULL, 0);
}

void VirtualGamepad_InitMask() {
    if (!ui_mask) ui_mask = (uint8_t*)heap_caps_malloc(48000, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ui_mask) return;   // ui_blk stays all-zero: overlay still draws, just without masking
    memset(ui_mask, 0, 48000);
    for (int i = 0; i < NUM_BTNS; i++) {
        Btn* b = &buttons[i];
        
        // 72x72 sprite'ın merkezi Ekranda (b->x, b->y)'ye oturması için sol üst köşe (-36, -36)'dır.
        int bx = b->x - 36;
        int by = b->y - 36;
        
        // [AI-LOCKED-FIX #14] 2026-10-04: Gamepad Siyah Kutu (Byte-Swap) İkinci Düzeltmesi
        // Neden: LovyanGFX'in DMA byte-swap ve anti-aliasing (text rendering) mekanizmaları yüzünden,
        // sprite üzerinden pixel-pixel (renk tabanlı) maske okuması şeffaf köşeleri de hatalı maskeliyordu.
        // DOKUNMA: Bu matematiksel geometrik maske hesaplama algoritmasını (Pisagor ve sınır testleri) silme!
        for (int yy = 0; yy < 72; yy++) {
            for (int xx = 0; xx < 72; xx++) {
                bool inside = false;
                if (b->is_rect) {
                    int hw = b->w / 2;
                    int hh = b->h / 2;
                    if (xx >= 36 - hw && xx < 36 + hw && yy >= 36 - hh && yy < 36 + hh) {
                        int cr = 10;
                        int cl = 36 - hw + cr;
                        int cr_r = 36 + hw - cr;
                        int ct = 36 - hh + cr;
                        int cb = 36 + hh - cr;
                        
                        if (xx < cl && yy < ct) {
                            inside = (xx - cl)*(xx - cl) + (yy - ct)*(yy - ct) <= cr*cr;
                        } else if (xx >= cr_r && yy < ct) {
                            inside = (xx - cr_r)*(xx - cr_r) + (yy - ct)*(yy - ct) <= cr*cr;
                        } else if (xx < cl && yy >= cb) {
                            inside = (xx - cl)*(xx - cl) + (yy - cb)*(yy - cb) <= cr*cr;
                        } else if (xx >= cr_r && yy >= cb) {
                            inside = (xx - cr_r)*(xx - cr_r) + (yy - cb)*(yy - cb) <= cr*cr;
                        } else {
                            inside = true;
                        }
                    }
                } else {
                    int dx = xx - 36;
                    int dy = yy - 36;
                    // Menü tuşu r=25, ABXY r=35 olduğu için b->r kullanıldı. Siyah halkayı önlemek için r+1 genişletme yapıldı.
                    if (dx*dx + dy*dy <= (b->r + 1)*(b->r + 1)) {
                        inside = true;
                    }
                }
                
                if (inside) {
                    int screen_x = bx + xx;
                    int screen_y = by + yy;
                    if (screen_x >= 0 && screen_x < 800 && screen_y >= 0 && screen_y < 480) {
                        ui_mask[(screen_y * 800 + screen_x) >> 3] |= (1 << (screen_x & 7));
                    }
                }
            }
        }
    }

    // Per 10-pixel block flag (display task stretches 4 source px -> 10 dest px at a time):
    // lets the hot loop test one bit instead of ten mask bits per block.
    memset(ui_blk, 0, sizeof(ui_blk));
    for (int y = 0; y < 480; y++) {
        for (int blk = 0; blk < 80; blk++) {
            for (int p = 0; p < 10; p++) {
                int px = blk * 10 + p;
                if (ui_mask[(y * 800 + px) >> 3] & (1 << (px & 7))) {
                    ui_blk[y][blk >> 3] |= (1 << (blk & 7));
                    break;
                }
            }
        }
    }
}

void VirtualGamepad_DrawOverlay(LovyanGFX* target) {
    if (!target) target = g_lcd;
    if (!target) return;

    for (int i = 0; i < NUM_BTNS; i++) {
        drawButton(&buttons[i], buttons[i].pressed, target);
    }
}

extern void VW_UpdateScreen();

// ---------------------------------------------------------------------------
// On-screen QWERTY keyboard for text entry (save game name, high score name).
// Blocking call on Core 1 (wolfTask); touch is handled by VirtualGamepad_Task and
// rendering by the display task (VirtualKeyboard_Render), same split as the gamepad.
// ---------------------------------------------------------------------------
struct VKKey {
    int16_t x, y, w, h;
    char key;          // printable char, or VK_OK / VK_DEL / VK_CANCEL (< 32)
    const char* label;
    bool pressed;
};
enum { VK_OK = 1, VK_DEL = 2, VK_CANCEL = 3 };
#define VK_NUM 40
#define VK_BUF_MAX 36
#define VK_BAND_H 80

static VKKey vk_keys[VK_NUM];
static volatile bool vk_active = false;
static volatile bool vk_done = false;
static volatile bool vk_cancel = false;
static volatile bool vk_release_guard = false;
static volatile int vk_dirty = 0;
static char vk_buf[VK_BUF_MAX + 1] = "";
static int vk_len = 0;
static int vk_max = VK_BUF_MAX;
static LGFX_Sprite* vkBand = nullptr;

static void VK_Init() {
    const int btnW = 60, btnH = 60, gap = 10;
    int idx = 0;
    const char* digits = "0123456789";
    for (int i = 0; i < 10; i++)
        vk_keys[idx++] = { (int16_t)(50 + i * (btnW + gap)), 110, btnW, btnH, digits[i], "", false };
    const char* row1 = "QWERTYUIOP";
    for (int i = 0; i < 10; i++)
        vk_keys[idx++] = { (int16_t)(50 + i * (btnW + gap)), 180, btnW, btnH, row1[i], "", false };
    const char* row2 = "ASDFGHJKL";
    for (int i = 0; i < 9; i++)
        vk_keys[idx++] = { (int16_t)(85 + i * (btnW + gap)), 250, btnW, btnH, row2[i], "", false };
    const char* row3 = "ZXCVBNM";
    for (int i = 0; i < 7; i++)
        vk_keys[idx++] = { (int16_t)(155 + i * (btnW + gap)), 320, btnW, btnH, row3[i], "", false };
    vk_keys[idx++] = { 50,  390, 130, btnH, VK_CANCEL, "CANCEL", false };
    vk_keys[idx++] = { 190, 390, 270, btnH, ' ',       "SPACE",  false };
    vk_keys[idx++] = { 470, 390, 100, btnH, VK_DEL,    "DEL",    false };
    vk_keys[idx++] = { 580, 390, 160, btnH, VK_OK,     "OK",     false };
}

// Draws the whole keyboard into a VK_BAND_H tall band whose top is screen row yoff.
static void VK_DrawBand(LGFX_Sprite* s, int yoff, const char* text) {
    s->fillSprite(g_lcd->color565(20, 20, 20));
    s->setTextSize(3);
    s->setTextColor(TEXT_COLOR);

    s->fillRoundRect(50, 20 - yoff, 700, 70, 10, g_lcd->color565(40, 40, 40));
    s->drawRoundRect(50, 20 - yoff, 700, 70, 10, g_lcd->color565(100, 100, 100));
    s->drawString(text, 70, 44 - yoff);

    for (int i = 0; i < VK_NUM; i++) {
        VKKey* k = &vk_keys[i];
        if (k->y + k->h <= yoff || k->y >= yoff + VK_BAND_H) continue;
        s->fillRoundRect(k->x, k->y - yoff, k->w, k->h, 8, k->pressed ? BTN_HIGHLIGHT : BTN_BASE);
        s->drawRoundRect(k->x, k->y - yoff, k->w, k->h, 8, g_lcd->color565(80, 80, 80));
        s->setTextColor(k->pressed ? g_lcd->color565(0, 0, 0) : TEXT_COLOR);
        if (k->key < 32 || k->key == ' ') {
            s->drawCenterString(k->label, k->x + k->w / 2, k->y - yoff + 18);
        } else {
            char c[2] = { k->key, 0 };
            s->drawCenterString(c, k->x + k->w / 2, k->y - yoff + 18);
        }
    }
}

// Called from the display task (Core 0) instead of the game stretch while the keyboard is open.
// Direct framebuffer: only redraws when something changed. Fallback path: every frame.
void VirtualKeyboard_Render(LovyanGFX* target, bool is_direct) {
    if (!vk_active || !vkBand || !target) return;

    static bool lastBlink = false;
    bool blink = (millis() / 500) & 1;
    if (blink != lastBlink) { lastBlink = blink; vk_dirty = 2; }
    if (is_direct && vk_dirty <= 0) return;

    char text[VK_BUF_MAX + 2];
    portENTER_CRITICAL(&virtualGamepadMux);
    memcpy(text, vk_buf, sizeof(vk_buf));
    int len = vk_len;
    portEXIT_CRITICAL(&virtualGamepadMux);
    if (blink && len < vk_max) { text[len] = '_'; text[len + 1] = 0; }

    for (int y0 = 0; y0 < 480; y0 += VK_BAND_H) {
        VK_DrawBand(vkBand, y0, text);
        vkBand->pushSprite(target, 0, y0);
    }
    if (vk_dirty > 0) vk_dirty--;
}

bool VirtualKeyboard_IsActive() { return vk_active; }

static void VK_ProcessTouch(const lgfx::touch_point_t* tp, int touched) {
    bool state[VK_NUM] = {false};
    for (int t = 0; t < touched; t++) {
        for (int i = 0; i < VK_NUM; i++) {
            VKKey* k = &vk_keys[i];
            if (tp[t].x >= k->x && tp[t].x <= k->x + k->w && tp[t].y >= k->y && tp[t].y <= k->y + k->h)
                state[i] = true;
        }
    }
    for (int i = 0; i < VK_NUM; i++) {
        VKKey* k = &vk_keys[i];
        if (state[i] == k->pressed) continue;
        k->pressed = state[i];
        vk_dirty = 2;
        if (!state[i]) continue;  // act on press only
        portENTER_CRITICAL(&virtualGamepadMux);
        if (k->key == VK_OK) {
            vk_done = true;
        } else if (k->key == VK_CANCEL) {
            vk_cancel = true;
            vk_done = true;
        } else if (k->key == VK_DEL) {
            if (vk_len > 0) vk_buf[--vk_len] = 0;
        } else if (vk_len < vk_max) {
            vk_buf[vk_len++] = k->key;
            vk_buf[vk_len] = 0;
        }
        portEXIT_CRITICAL(&virtualGamepadMux);
    }
}

// Returns 1 = OK (buf updated), 0 = cancelled, -1 = keyboard unavailable (caller falls back).
int VirtualKeyboard_LineInput(char* buf, int maxchars, bool escok) {
    if (!g_lcd || vk_active) return -1;

    vkBand = new LGFX_Sprite(g_lcd);
    vkBand->setPsram(true);
    vkBand->setColorDepth(16);
    vkBand->createSprite(800, VK_BAND_H);
    if (!vkBand->getBuffer()) {
        delete vkBand;
        vkBand = nullptr;
        return -1;
    }

    vk_max = (maxchars > 0 && maxchars < VK_BUF_MAX) ? maxchars : VK_BUF_MAX;
    strncpy(vk_buf, buf, vk_max);
    vk_buf[vk_max] = 0;
    vk_len = strlen(vk_buf);
    vk_done = false;
    vk_cancel = false;
    VK_Init();

    // Release anything the gamepad thinks is held so nothing stays stuck while typing.
    portENTER_CRITICAL(&virtualGamepadMux);
    memset(VirtualKeyboard, 0, sizeof(VirtualKeyboard));
    for (int i = 0; i < (int)NUM_BTNS; i++) buttons[i].pressed = false;
    portEXIT_CRITICAL(&virtualGamepadMux);

    vk_dirty = 2;
    vk_release_guard = true;
    vk_active = true;

    while (!vk_done) {
        VW_UpdateScreen();
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    vk_active = false;
    // Two flips: the second only returns once the display task has finished any frame
    // that still saw the keyboard, so the band sprite can be freed safely.
    VW_UpdateScreen();
    VW_UpdateScreen();
    delete vkBand;
    vkBand = nullptr;
    ui_needs_redraw = 2;
    LastScan = sc_None;
    LastASCII = key_None;

    if (vk_cancel && escok) return 0;
    strcpy(buf, vk_buf);
    return 1;
}

void VirtualGamepad_Task(void* pvParameters) {
    while (1) {
        lgfx::touch_point_t tp[5];
        // [AI-LOCKED-FIX #3] 2026-10-03: Dokunmatik Algılama
        // Neden: getTouchRaw basılı tutma ve ekran sınırlarını yanlış okuyordu.
        // DOKUNMA: Raw metoduna geri döndürme!
        int touched = g_lcd->getTouch(tp, 5); // Read up to 5 points (multi-touch)

        if (vk_active) {
            VK_ProcessTouch(tp, touched > 0 ? touched : 0);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        // After the keyboard closes, ignore the gamepad until the finger is lifted
        // (OK key overlaps the A button, which would otherwise fire a stray Enter).
        if (vk_release_guard) {
            if (touched > 0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
            vk_release_guard = false;
        }

        bool current_state[NUM_BTNS] = {false};
        
        if (touched >= 0) { // also log 0 touches
            const char* matched_button_str = "NONE";
            int usedX = 0, usedY = 0;
            
            for (int t = 0; t < touched; t++) {
                int tx = tp[t].x;
                int ty = tp[t].y;
                if (t == 0) { usedX = tx; usedY = ty; }
                
                // Check all buttons
                for (int i = 0; i < NUM_BTNS; i++) {
                    Btn* b = &buttons[i];
                    bool hit = false;
                    if (b->is_rect) {
                        if (tx >= b->x && tx <= b->x + b->w && ty >= b->y && ty <= b->y + b->h) {
                            hit = true;
                        }
                    } else {
                        // Circle collision
                        int dx = tx - b->x;
                        int dy = ty - b->y;
                        if (dx*dx + dy*dy <= b->r * b->r) {
                            hit = true;
                        }
                    }
                    if (hit) {
                        current_state[i] = true;
                        matched_button_str = b->label; // naive: last hit wins
                    }
                }
            }
            // TOUCH_DEBUG log
            Serial.printf("[TOUCH] count=%d, matched_button=%s, usedX=%d, usedY=%d\n", touched, matched_button_str, usedX, usedY);
        }
        
        // Update states
        for (int i = 0; i < NUM_BTNS; i++) {
            if (current_state[i] != buttons[i].pressed) {
                buttons[i].pressed = current_state[i];
                ui_needs_redraw = 2; // Draw on next 2 frames (for double buffering)
                
                // Inject into shared VirtualKeyboard buffer safely
                if (buttons[i].sc < 256) {
                    portENTER_CRITICAL(&virtualGamepadMux);
                    VirtualKeyboard[buttons[i].sc] = buttons[i].pressed;
                    
                    // Alias A (index 4) to 'Y' and B (index 5) to 'N' for menu prompts
                    if (i == 4) VirtualKeyboard[sc_Y] = buttons[i].pressed;
                    if (i == 5) VirtualKeyboard[sc_N] = buttons[i].pressed;
                    portEXIT_CRITICAL(&virtualGamepadMux);
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); // Poll every 10ms (100Hz) for reduced input lag
    }
}
