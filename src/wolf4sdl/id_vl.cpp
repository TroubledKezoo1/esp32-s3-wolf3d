// ID_VL.C - Video Library (ESP32-S3 LovyanGFX port)
// DUAL-CORE DISPLAY PIPELINE:
//   Core 1: Wolf3D game loop (raycaster ~5ms)
//   Core 0: Display task - scales 320x200 → 800x480, pushes to LCD (~15-20ms)
//   Both run in parallel → effective FPS limited by the slower core.
//   Since raycaster = 5ms < display = ~18ms, display is the new limit → ~55 FPS.

#include <string.h>
#include "wl_def.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "esp32s3/rom/cache.h"
#endif

#ifdef IGNORE_BAD_DEST
#undef assert
#define assert(x) if(!(x)) return
#define assert_ret(x) if(!(x)) return 0
#else
#define assert_ret(x) assert(x)
#endif

boolean fullscreen = false;
boolean aspect = true;
unsigned screenWidth = 320;
unsigned screenHeight = 200;
int screenBits = 16;

#include "../DisplayDriver.h"
extern LGFX_Waveshare7 lcd;

// Subclass trick: Panel_FrameBufferBase::_lines_buffer is protected, but a subclass can expose it.
// This lets us write directly to the LCD's framebuffer, eliminating pushImage.
class Panel_Hack : public lgfx::v1::Panel_FrameBufferBase {
public:
    uint8_t** getLines() { return _lines_buffer; }
};

uint16_t *renderBuffer;
static uint8_t** fb_lines = nullptr;
byte *screenBufferData;

VL_Surface screenBufferObj = {320, 200, 320, screenBufferData};
VL_Surface *screenBuffer = &screenBufferObj;
VL_Surface *curSurface = &screenBufferObj;

unsigned screenPitch = 320;
unsigned bufferPitch = 320;
unsigned curPitch = 320;
unsigned scaleFactor = 1;

boolean screenfaded;
unsigned bordercolor;

VL_Color palette1[256], palette2[256];
VL_Color curpal[256];

#define CASSERT(x) extern int ASSERT_COMPILE[((x) != 0) * 2 - 1];
#define RGB(r, g, b) {(r)*255/63, (g)*255/63, (b)*255/63, 255}

VL_Color gamepal[] __attribute__((aligned(16))) ={
#ifdef SPEAR
    #include "sodpal.inc"
#else
    #include "wolfpal.inc"
#endif
};

CASSERT(lengthof(gamepal) == 256)

void VL_Shutdown(void) {}

static uint16_t palette16[256];
static bool     paletteDirty = true;
portMUX_TYPE palMutex = portMUX_INITIALIZER_UNLOCKED;
static bool     directFB     = false;

// ============================================================
// DUAL-CORE DISPLAY PIPELINE
// ============================================================
// flipBuf: 64KB snapshot of screenBuffer for Core 0 display task.
// Allocated in PSRAM to avoid consuming precious DRAM heap (wolfTask needs 81KB stack).
static byte*     flipBuf = nullptr;
// flipPalette: 512 bytes, fine in DRAM.
static DRAM_ATTR uint16_t flipPalette[256] __attribute__((aligned(16)));


// sem_flip_ready: Core 1 gives → Core 0 takes (new frame available)
// sem_flip_done:  Core 0 gives → Core 1 takes (display finished, safe to write next)
static SemaphoreHandle_t sem_flip_ready = NULL;
static SemaphoreHandle_t sem_flip_done  = NULL;

// ============================================================
// Display task (Core 0)
// Scales 320x200 flipBuf → 480x300 (centered), then pushes to LCD.
// ============================================================
#include "../virtual_gamepad.h"
#ifndef DISPLAY_INTEGER_2X
#define DISPLAY_INTEGER_2X 0   // 1 = 640x400 centered integer 2x (faster, black bars); 0 = fullscreen 2.5x
#endif
static LGFX_Sprite* bbSprite = nullptr;
static bool border_dirty = true;
static bool force_full = true;                 // next game frame must rewrite every block
static byte* flipBufs[2] = {nullptr, nullptr}; // current / previous frame (changed-block skipping)
static volatile int flipWr = 0;
static uint8_t changedBits[(320 / 4) * 200 / 8];  // 1 bit per 4-pixel source block, filled by VL_Flip (Core 1)
static volatile uint32_t perf_skipped = 0;
// Frame timing is logged every 60 flips, only in the debug env (ESP32_DEBUG_AUTOSTART)
#ifdef ESP32_DEBUG_AUTOSTART
#define PERF_LOG(...) log_e(__VA_ARGS__)
#else
#define PERF_LOG(...) ((void)0)
#endif
static volatile uint32_t perf_display_us = 0;

static void IRAM_ATTR displayTask(void* pv)
{
    static int y_lookup[480];
    static bool lookup_init = false;
    
    if (!lookup_init) {
        for (int y = 0; y < 480; y++) y_lookup[y] = (y * 200) / 480;
        lookup_init = true;
    }
    
    // Ensure mask is initialized
    VirtualGamepad_InitMask();
    ui_needs_redraw = 2;

    while (true) {
        xSemaphoreTake(sem_flip_ready, portMAX_DELAY);
        int64_t perf_t0 = esp_timer_get_time();
        const byte* src = flipBuf;

        // On-screen keyboard replaces the game frame while open (save game name entry)
        if (bbSprite && VirtualKeyboard_IsActive()) {
            bool kb_direct = (fb_lines && fb_lines[0]);
            VirtualKeyboard_Render(bbSprite, kb_direct);
            border_dirty = true;   // keyboard paints the whole screen; clear the bars afterwards
            force_full = true;
            if (!kb_direct) bbSprite->pushSprite(0, 0);
            xSemaphoreGive(sem_flip_done);
            continue;
        }

        if (bbSprite) {
            uint16_t* bb_buf = (uint16_t*)bbSprite->getBuffer();
            if (bb_buf) {
                // If it's direct FB, we use masked stretch to prevent flicker
                bool is_direct = (fb_lines && fb_lines[0]);
                
                // Skip blocks whose 4 source pixels did not change since the previous frame
                // (HUD, flat ceiling/floor, standing still). Anything else touching the
                // framebuffer (palette change, keyboard, non-direct mode) forces a full redraw.
                static uint16_t prevPal[256];
                const byte* prev = flipBufs[1] ? flipBufs[flipWr ^ 1] : nullptr;
                bool pal_changed = memcmp(prevPal, flipPalette, sizeof(prevPal)) != 0;
                if (pal_changed) memcpy(prevPal, flipPalette, sizeof(prevPal));
                bool full = force_full || pal_changed || !prev || !is_direct;
                force_full = false;
                uint32_t skipped = 0;
                
#if DISPLAY_INTEGER_2X
                // Integer 2x scale: 320x200 -> 640x400 centered (80px / 40px black bars).
                // ~33% fewer PSRAM framebuffer writes than the 2.5x stretch, bars are written only
                // when border_dirty (startup, after the on-screen keyboard).
                if (border_dirty) {
                    for (int y = 0; y < 480; y++) {
                        uint32_t* row = (uint32_t*)&bb_buf[y * 800];
                        if (y < 40 || y >= 440) {
                            for (int x = 0; x < 400; x++) row[x] = 0;
                        } else {
                            for (int x = 0; x < 40; x++) { row[x] = 0; row[360 + x] = 0; }
                        }
                    }
                    border_dirty = false;
                    ui_needs_redraw = 2;
                }

                for (int y = 40; y < 440; y++) {
                    uint16_t* dest16 = &bb_buf[y * 800 + 80];
                    uint32_t* dest32 = (uint32_t*)dest16;
                    const byte* src_row = &src[((y - 40) >> 1) * 320];
                    
                    for (int g = 0; g < 64; g++) {
                        int blk = 8 + g;   // 10-pixel UI block index (x = 80 + g*10)
                        uint32_t c0 = flipPalette[src_row[0]];
                        uint32_t c1 = flipPalette[src_row[1]];
                        uint32_t c2 = flipPalette[src_row[2]];
                        uint32_t c3 = flipPalette[src_row[3]];
                        uint32_t c4 = flipPalette[src_row[4]];
                        src_row += 5;
                        
                        if (!(is_direct && ((ui_blk[y][blk >> 3] >> (blk & 7)) & 1))) {
                            dest32[0] = c0 | (c0 << 16);
                            dest32[1] = c1 | (c1 << 16);
                            dest32[2] = c2 | (c2 << 16);
                            dest32[3] = c3 | (c3 << 16);
                            dest32[4] = c4 | (c4 << 16);
                        } else {
                            uint16_t pixels[10] = { (uint16_t)c0, (uint16_t)c0, (uint16_t)c1, (uint16_t)c1,
                                                    (uint16_t)c2, (uint16_t)c2, (uint16_t)c3, (uint16_t)c3,
                                                    (uint16_t)c4, (uint16_t)c4 };
                            int start_x = 80 + g * 10;
                            for (int p = 0; p < 10; p++) {
                                if (!(ui_mask[(y * 800 + start_x + p) >> 3] & (1 << ((start_x + p) & 7)))) {
                                    dest16[g * 10 + p] = pixels[p];
                                }
                            }
                        }
                        dest32 += 5;
                    }
                }
#else
                for (int y = 0; y < 480; y++) {
                    uint16_t* dest16 = &bb_buf[y * 800];
                    uint32_t* dest32 = (uint32_t*)dest16;
                    const byte* src_row = &src[y_lookup[y] * 320];
                    const int sbase = y_lookup[y] * 80;
                    
                    int ui_row_offset = y * 800;
                    
                    for (int i = 0; i < 80; i++) {
                        int start_x = i * 10;
                        bool has_ui = false;
                        
                        if (is_direct) {
                            // Precomputed in VirtualGamepad_InitMask: any UI mask bit in this block?
                            has_ui = (ui_blk[y][i >> 3] >> (i & 7)) & 1;
                        }
                        
                        if (!full && !((changedBits[(sbase + i) >> 3] >> ((sbase + i) & 7)) & 1)) {
                            src_row += 4; dest32 += 5; skipped++;
                            continue;
                        }
                        uint32_t c0 = flipPalette[*src_row++];
                        uint32_t c1 = flipPalette[*src_row++];
                        uint32_t c2 = flipPalette[*src_row++];
                        uint32_t c3 = flipPalette[*src_row++];
                        
                        if (!has_ui) {
                            // Fast unrolled path
                            *dest32++ = c0 | (c0 << 16);
                            *dest32++ = c0 | (c1 << 16);
                            *dest32++ = c1 | (c2 << 16);
                            *dest32++ = c2 | (c2 << 16);
                            *dest32++ = c3 | (c3 << 16);
                        } else {
                            // Slow masked path
                            uint16_t pixels[10];
                            pixels[0] = c0; pixels[1] = c0;
                            pixels[2] = c0; pixels[3] = c1;
                            pixels[4] = c1; pixels[5] = c2;
                            pixels[6] = c2; pixels[7] = c2;
                            pixels[8] = c3; pixels[9] = c3;
                            
                            for (int p = 0; p < 10; p++) {
                                if (!(ui_mask[(ui_row_offset + start_x + p) >> 3] & (1 << ((start_x + p) & 7)))) {
                                    dest16[start_x + p] = pixels[p];
                                }
                            }
                            dest32 += 5;
                        }
                    }
                }
                
#endif
                
                perf_skipped += skipped;
                // Draw Gamepad Overlay only if needed
                if (ui_needs_redraw > 0 || !is_direct) {
                    VirtualGamepad_DrawOverlay(bbSprite);
                    if (is_direct && ui_needs_redraw > 0) {
                        ui_needs_redraw--;
                    }
                }
                
                // Push to hardware ONLY if using fallback double buffer
                if (!is_direct) {
                    bbSprite->pushSprite(0, 0);
                }
            }
        }

        perf_display_us += (uint32_t)(esp_timer_get_time() - perf_t0);
        xSemaphoreGive(sem_flip_done);
    }
}

// Call once after renderBuffer is allocated.
static void VL_InitDisplayPipeline()
{
    if (!bbSprite) {
        bbSprite = new LGFX_Sprite(&lcd);
        if (fb_lines && fb_lines[0]) {
            // Direct Framebuffer wrapper (Zero Copy!)
            bbSprite->setBuffer(fb_lines[0], 800, 480, 16);
        } else {
            // Fallback for non-RGB panels
            bbSprite->setPsram(true);
            bbSprite->setColorDepth(16);
            bbSprite->createSprite(800, 480);
        }
    }
    sem_flip_ready = xSemaphoreCreateBinary();
    sem_flip_done  = xSemaphoreCreateBinary();
    // Initially "done" so the first VL_Flip call doesn't block waiting for display
    xSemaphoreGive(sem_flip_done);

    // Pin display task to Core 0 (Wolf3D runs on Core 1)
    // Priority 2: above idle (0), below Wolf3D (default 1 via xTaskCreatePinnedToCore in main.cpp → actually 1).
    // Give it stack of 8KB — it only needs rowBuf + small locals.
    xTaskCreatePinnedToCore(displayTask, "Display", 8192, NULL, 2, NULL, 0);
}

// ============================================================

void VL_SetVGAPlaneMode(void)
{
    portENTER_CRITICAL(&palMutex);
    memcpy(curpal, gamepal, sizeof(VL_Color) * 256);
    paletteDirty = true;
    portEXIT_CRITICAL(&palMutex);

    screenPitch = 320;
    bufferPitch = 320;
    curSurface  = screenBuffer;
    curPitch    = bufferPitch;
    scaleFactor = 1;

    // Grab the framebuffer pointer to bypass pushImage later
    fb_lines = ((Panel_Hack*)lcd.getPanel())->getLines();
    if (fb_lines && fb_lines[0]) {
        Serial.printf("Panel_Hack successful. fb_lines[0]=%p\n", fb_lines[0]);
    } else {
        Serial.println("Panel_Hack failed. Will fallback to pushImage.");
    }

    if (fb_lines && fb_lines[0]) {
        directFB = true;
    } else {
        directFB = false;
        renderBuffer = (uint16_t*)heap_caps_malloc(800 * 480 * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!renderBuffer) {
            Serial.println("FATAL: renderBuffer PSRAM alloc failed");
            while (1) vTaskDelay(100);
        }
        Serial.printf("Fallback renderBuffer allocated: %p\n", renderBuffer);
        memset(renderBuffer, 0, 800 * 480 * sizeof(uint16_t));
    }

    screenBufferData = (byte *) heap_caps_malloc(320 * 200 * sizeof(byte), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!screenBufferData) screenBufferData = (byte *) malloc(320 * 200 * sizeof(byte));
    CHECKMALLOCRESULT(screenBufferData);
    screenBufferObj.pixels = screenBufferData;

    pixelangle = (short *) malloc(screenWidth * sizeof(short));
    CHECKMALLOCRESULT(pixelangle);
    wallheight  = (int *)   malloc(screenWidth * sizeof(int));
    CHECKMALLOCRESULT(wallheight);

    // Allocate flipBuf from INTERNAL RAM to prevent PSRAM cache incoherency across cores
    if (!flipBuf) {
        flipBuf = (byte*)heap_caps_malloc(320 * 200, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!flipBuf) {
            Serial.println("Warning: flipBuf INTERNAL alloc failed, falling back to SPIRAM");
            flipBuf = (byte*)heap_caps_malloc(320 * 200, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!flipBuf) {
                Serial.println("FATAL: flipBuf alloc failed");
                while (1) vTaskDelay(100);
            }
        }
    }

    // Initialize the dual-core display pipeline
    VL_InitDisplayPipeline();
    Serial.printf("Dual-core display pipeline started. directFB=%d\n", (int)directFB);
}

void VL_ConvertPalette(byte *srcpal, VL_Color *destpal, int numColors)
{
    if (!srcpal) return;
    for (int i = 0; i < numColors; i++) {
        destpal[i].r = *srcpal++ * 255 / 63;
        destpal[i].g = *srcpal++ * 255 / 63;
        destpal[i].b = *srcpal++ * 255 / 63;
        destpal[i].a = 255;
    }
}

void VL_FillPalette(int red, int green, int blue)
{
    VL_Color pal[256];
    for (int i = 0; i < 256; i++) {
        pal[i].r = red;
        pal[i].g = green;
        pal[i].b = blue;
    }
    VL_SetPalette(pal, true);
}

void VL_GetColor(int color, int *red, int *green, int *blue)
{
    VL_Color *col = &curpal[color];
    *red = col->r;
    *green = col->g;
    *blue = col->b;
}

#ifdef ESP32_DEBUG_AUTOSTART
#include <mbedtls/base64.h>
// Debug env only: dump the 320x200 8-bit frame and its palette over UART0 as base64 lines,
// used to produce README screenshots (see tools in the repo history). Blocks for a few seconds.
static void DebugDumpFrame(int n)
{
    static uint8_t buf[64000 + 768];
    memcpy(buf, screenBuffer->pixels, 64000);
    for (int i = 0; i < 256; i++) {
        buf[64000 + i * 3 + 0] = curpal[i].r;
        buf[64000 + i * 3 + 1] = curpal[i].g;
        buf[64000 + i * 3 + 2] = curpal[i].b;
    }
    static bool uart_ready = false;
    if (!uart_ready) { Serial0.begin(115200); uart_ready = true; }
    static unsigned char line[200];
    for (size_t off = 0; off < sizeof(buf); off += 90) {
        size_t chunk = sizeof(buf) - off < 90 ? sizeof(buf) - off : 90, olen = 0;
        mbedtls_base64_encode(line, sizeof(line), &olen, buf + off, chunk);
        Serial0.printf("SHOT:%d:", n);
        Serial0.write(line, olen);
        Serial0.write('\n');
    }
    Serial0.printf("SHOTEND:%d\n", n);
}
#endif

void VL_Flip(void)
{
    if (!sem_flip_ready) {
        // Pipeline not initialized yet (very early boot), do a simple sync flip
        return;
    }

    // Wait for previous display frame to finish (Core 0 signals when done).
    // If Core 0 is still displaying the previous frame, we block here.
    // In practice Core 0 takes ~18ms and Core 1 raycaster ~5ms, so we wait ~13ms.
    // OPTIMIZATION OPPORTUNITY: with faster display path (directFB) this drops to <10ms.
    int64_t perf_enter = esp_timer_get_time();
    static int64_t perf_prev_exit = 0;
    static uint32_t perf_work = 0, perf_wait = 0, perf_n = 0;
    xSemaphoreTake(sem_flip_done, portMAX_DELAY);
    int64_t perf_got = esp_timer_get_time();
    { static int dbg_first = 0; if (dbg_first < 3) { dbg_first++; PERF_LOG("[DBG] VL_Flip #%d", dbg_first); } }
    if (perf_prev_exit) perf_work += (uint32_t)(perf_enter - perf_prev_exit);
    perf_wait += (uint32_t)(perf_got - perf_enter);
    if (++perf_n == 60) {
        PERF_LOG("[PERF] per frame us: game+raycast=%u flipwait=%u display=%u skippedblocks=%u/38400\n",
                      perf_work / 60, perf_wait / 60, perf_display_us / 60, (unsigned)(perf_skipped / 60));
        perf_skipped = 0;
        PERF_LOG("[PERF] wolfTask stack free(min)=%u | internal free=%u largest=%u psram free=%u | screenBuf in psram=%d flipBuf in psram=%d\n",
                      (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                      (int)esp_ptr_external_ram(screenBuffer->pixels), (int)esp_ptr_external_ram(flipBuf));
        perf_work = perf_wait = perf_n = 0;
        perf_display_us = 0;
    }

    // Rebuild palette if dirty — done here on Core 1 inside the flip lock
    portENTER_CRITICAL(&palMutex);
    if (paletteDirty) {
        for (int i = 0; i < 256; i++) {
            // Byte swap IS required: Bus_RGB.cpp rgb565sig_tbl routes
            // pin_data[0..7] ← data_sigs[8..15] (Byte 1) and
            // pin_data[8..15] ← data_sigs[0..7] (Byte 0).
            // So DMA framebuffer must store byte-swapped RGB565.
            uint16_t c = lgfx::color565(curpal[i].r, curpal[i].g, curpal[i].b);
            palette16[i] = (c >> 8) | (c << 8);
        }
        paletteDirty = false;
    }
    portEXIT_CRITICAL(&palMutex);

    // Snapshot palette for Core 0 (avoids race if palette changes mid-display)
    memcpy(flipPalette, palette16, 256 * sizeof(uint16_t));

    // Copy 320×200 = 64 KB DRAM → DRAM: ~0.1 ms (well within L1 cache bandwidth)
    if (!flipBufs[0]) {
        flipBufs[0] = flipBuf;
        flipBufs[1] = (byte*)heap_caps_malloc(320 * 200, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (flipBufs[1]) {
        flipWr ^= 1;
        flipBuf = flipBufs[flipWr];
    }
    // Copy the frame and, in the same pass, flag 4-pixel blocks that changed since the previous
    // frame (display task skips the unchanged ones).
    {
        const uint32_t* scr32 = (const uint32_t*)screenBuffer->pixels;
        uint32_t* cur32 = (uint32_t*)flipBuf;
        const uint32_t* prev32 = flipBufs[1] ? (const uint32_t*)flipBufs[flipWr ^ 1] : nullptr;
        if (prev32) {
            for (int b = 0; b < (320 / 4) * 200 / 8; b++) {
                uint8_t bits = 0;
                for (int k = 0; k < 8; k++) {
                    int idx = b * 8 + k;
                    uint32_t v = scr32[idx];
                    bits |= (uint8_t)((v != prev32[idx]) << k);
                    cur32[idx] = v;
                }
                changedBits[b] = bits;
            }
        } else {
            memcpy(flipBuf, screenBuffer->pixels, 320 * 200);
            memset(changedBits, 0xFF, sizeof(changedBits));
        }
    }

    // Signal Core 0 to start displaying — we return immediately!
    xSemaphoreGive(sem_flip_ready);

    // Yield so Core 0 display task can start while Core 1 moves on to AI/physics/raycaster
    taskYIELD();
#ifdef ESP32_DEBUG_AUTOSTART
    {
        static const uint32_t shot_at_ms[] = { 14000, 24000, 34000, 52000 };
        static int shots_done = 0;
        if (shots_done < 4 && millis() > shot_at_ms[shots_done]) { DebugDumpFrame(shots_done); shots_done++; }
    }
#endif
    perf_prev_exit = esp_timer_get_time();
}

void VL_SetPalette(VL_Color *palette, bool forceupdate)
{
    portENTER_CRITICAL(&palMutex);
    memcpy(curpal, palette, sizeof(VL_Color) * 256);
    paletteDirty = true;
    portEXIT_CRITICAL(&palMutex);
    if (forceupdate) VL_Flip();
}

void VL_GetPalette(VL_Color *palette)
{
    portENTER_CRITICAL(&palMutex);
    memcpy(palette, curpal, sizeof(VL_Color) * 256);
    portEXIT_CRITICAL(&palMutex);
}

void VL_FadeOut(int start, int end, int red, int green, int blue, int steps)
{
    int i, j, orig, delta;
    VL_Color *origptr, *newptr;

    red   = red   * 255 / 63;
    green = green * 255 / 63;
    blue  = blue  * 255 / 63;

    VL_WaitVBL(1);
    VL_GetPalette(palette1);
    memcpy(palette2, palette1, sizeof(VL_Color) * 256);

    for (i = 0; i < steps; i++) {
        origptr = &palette1[start];
        newptr  = &palette2[start];
        for (j = start; j <= end; j++) {
            orig = origptr->r; delta = red   - orig; newptr->r = orig + delta * i / steps;
            orig = origptr->g; delta = green - orig; newptr->g = orig + delta * i / steps;
            orig = origptr->b; delta = blue  - orig; newptr->b = orig + delta * i / steps;
            origptr++; newptr++;
        }
        VL_SetPalette(palette2, true);
    }

    VL_FillPalette(red, green, blue);
    screenfaded = true;
}

void VL_FadeIn(int start, int end, VL_Color *palette, int steps)
{
    int i, j, delta;

    VL_WaitVBL(1);
    VL_GetPalette(palette1);
    memcpy(palette2, palette1, sizeof(VL_Color) * 256);

    for (i = 0; i < steps; i++) {
        for (j = start; j <= end; j++) {
            delta = palette[j].r - palette1[j].r; palette2[j].r = palette1[j].r + delta * i / steps;
            delta = palette[j].g - palette1[j].g; palette2[j].g = palette1[j].g + delta * i / steps;
            delta = palette[j].b - palette1[j].b; palette2[j].b = palette1[j].b + delta * i / steps;
        }
        VL_SetPalette(palette2, true);
    }

    VL_SetPalette(palette, true);
    screenfaded = false;
}

byte *VL_LockSurface(VL_Surface *surface)   { return surface ? (byte *) surface->pixels : NULL; }
void  VL_UnlockSurface(VL_Surface *surface) {}

void VL_Plot(int x, int y, int color)
{
    assert(x >= 0 && (unsigned)x < screenWidth && y >= 0 && (unsigned)y < screenHeight && "VL_Plot: Pixel out of bounds!");
    byte *ptr = VL_LockSurface(curSurface);
    if (!ptr) return;
    ptr[y * curPitch + x] = color;
    VL_UnlockSurface(curSurface);
}

byte VL_GetPixel(int x, int y)
{
    assert_ret(x >= 0 && (unsigned)x < screenWidth && y >= 0 && (unsigned)y < screenHeight && "VL_GetPixel: Pixel out of bounds!");
    byte *ptr = VL_LockSurface(curSurface);
    if (!ptr) return 0;
    byte col = ((byte *)curSurface->pixels)[y * curPitch + x];
    VL_UnlockSurface(curSurface);
    return col;
}

void VL_Hlin(unsigned x, unsigned y, unsigned width, int color)
{
    assert(x >= 0 && x + width <= screenWidth && y >= 0 && y < screenHeight && "VL_Hlin: Destination rectangle out of bounds!");
    byte *ptr = VL_LockSurface(curSurface);
    if (!ptr) return;
    memset(ptr + y * curPitch + x, color, width);
    VL_UnlockSurface(curSurface);
}

void VL_Vlin(int x, int y, int height, int color)
{
    assert(x >= 0 && (unsigned)x < screenWidth && y >= 0 && (unsigned)y + height <= screenHeight && "VL_Vlin: Destination rectangle out of bounds!");
    byte *ptr = VL_LockSurface(curSurface);
    if (!ptr) return;
    ptr += y * curPitch + x;
    while (height--) { *ptr = color; ptr += curPitch; }
    VL_UnlockSurface(curSurface);
}

void VL_BarScaledCoord(int scx, int scy, int scwidth, int scheight, int color)
{
    assert(scx >= 0 && (unsigned)scx + scwidth <= screenWidth && scy >= 0 && (unsigned)scy + scheight <= screenHeight && "VL_BarScaledCoord: Destination rectangle out of bounds!");
    byte *ptr = VL_LockSurface(curSurface);
    if (!ptr) return;
    ptr += scy * curPitch + scx;
    while (scheight--) { memset(ptr, color, scwidth); ptr += curPitch; }
    VL_UnlockSurface(curSurface);
}

void VL_MemToLatch(byte *source, int width, int height, VL_Surface *destSurface, int x, int y)
{
    if (!source) return; // [AI-LOCKED-FIX #1]
    assert(x >= 0 && (unsigned)x + width <= screenWidth && y >= 0 && (unsigned)y + height <= screenHeight && "VL_MemToLatch: Destination rectangle out of bounds!");
    byte *ptr = VL_LockSurface(destSurface);
    if (!ptr) return;
    int pitch = destSurface->pitch;
    ptr += y * pitch + x;
    for (int ysrc = 0; ysrc < height; ysrc++)
        for (int xsrc = 0; xsrc < width; xsrc++)
            ptr[ysrc * pitch + xsrc] = source[(ysrc * (width >> 2) + (xsrc >> 2)) + (xsrc & 3) * (width >> 2) * height];
    VL_UnlockSurface(destSurface);
}

void VL_MemToScreenScaledCoord(byte *source, int width, int height, int destx, int desty)
{
    if (!source) return; // [AI-LOCKED-FIX #1] Prevent crash if chunk is NULL (e.g. running WL1 with WL6 binary)
    assert(destx >= 0 && destx + width * scaleFactor <= (int)screenWidth && desty >= 0 && desty + height * scaleFactor <= (int)screenHeight && "VL_MemToScreenScaledCoord: Destination rectangle out of bounds!");
    byte *ptr = VL_LockSurface(curSurface);
    if (!ptr) return;
    for (int j = 0, scj = 0; j < height; j++, scj += scaleFactor)
        for (int i = 0, sci = 0; i < width; i++, sci += scaleFactor) {
            byte col = source[(j * (width >> 2) + (i >> 2)) + (i & 3) * (width >> 2) * height];
            for (unsigned m = 0; m < scaleFactor; m++)
                for (unsigned n = 0; n < scaleFactor; n++)
                    ptr[(scj + m + desty) * curPitch + sci + n + destx] = col;
        }
    VL_UnlockSurface(curSurface);
}

void VL_MemToScreenScaledCoord(byte *source, int origwidth, int origheight, int srcx, int srcy, int destx, int desty, int width, int height)
{
    if (!source) return; // [AI-LOCKED-FIX #1]
    assert(destx >= 0 && destx + width * scaleFactor <= (int)screenWidth && desty >= 0 && desty + height * scaleFactor <= (int)screenHeight && "VL_MemToScreenScaledCoord: Destination rectangle out of bounds!");
    byte *ptr = VL_LockSurface(curSurface);
    if (!ptr) return;
    for (int j = 0, scj = 0; j < height; j++, scj += scaleFactor)
        for (int i = 0, sci = 0; i < width; i++, sci += scaleFactor) {
            byte col = source[((j + srcy) * (origwidth >> 2) + ((i + srcx) >> 2)) + ((i + srcx) & 3) * (origwidth >> 2) * origheight];
            for (unsigned m = 0; m < scaleFactor; m++)
                for (unsigned n = 0; n < scaleFactor; n++)
                    ptr[(desty + scj + m) * curPitch + destx + sci + n] = col;
        }
    VL_UnlockSurface(curSurface);
}

void VL_LatchToScreenScaledCoord(VL_Surface *source, int xsrc, int ysrc, int width, int height, int scxdest, int scydest)
{
    assert(scxdest >= 0 && scxdest + width * scaleFactor <= (int)screenWidth && scydest >= 0 && scydest + height * scaleFactor <= (int)screenHeight && "VL_LatchToScreenScaledCoord: Destination rectangle out of bounds!");
    byte *src = VL_LockSurface(source);
    if (!src) return;
    unsigned srcPitch = source->pitch;
    byte *dest = VL_LockSurface(curSurface);
    if (!dest) return;
    for (int j = 0, scj = 0; j < height; j++, scj += scaleFactor)
        for (int i = 0, sci = 0; i < width; i++, sci += scaleFactor) {
            byte col = src[(ysrc + j) * srcPitch + xsrc + i];
            for (unsigned m = 0; m < scaleFactor; m++)
                for (unsigned n = 0; n < scaleFactor; n++)
                    dest[(scydest + scj + m) * curPitch + scxdest + sci + n] = col;
        }
    VL_UnlockSurface(curSurface);
    VL_UnlockSurface(source);
}

void VL_ScreenToScreen(VL_Surface *source, VL_Surface *dest) {}
