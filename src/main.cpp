#include "DisplayDriver.h"
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <driver/i2c.h>

LGFX_Waveshare7 lcd;

extern "C" int main(int argc, char *argv[]);

#include "virtual_gamepad.h"

void wolfTask(void* pv) {
    // Wait a bit to let the screen initialize and the gamepad draw
    vTaskDelay(100 / portTICK_PERIOD_MS);
    VirtualGamepad_Init(&lcd);
    
    char* argv[] = {(char*)"wolf3d", NULL};
    main(1, argv);
    vTaskDelete(NULL);
}

void setup() {
    // Enable Native USB CDC Serial
    Serial.begin(115200);
    delay(3000);
    // I2C Scanner removed

    lcd.init();
    lcd.setBrightness(255);
    
    // Check if PSRAM is available
    if (ESP.getPsramSize() == 0) {
        lcd.fillScreen(TFT_RED);
        lcd.setTextColor(TFT_WHITE);
        lcd.setTextSize(3);
        lcd.setCursor(0, 0);
        lcd.println("FATAL: PSRAM NOT FOUND!");
        lcd.println("Hardware/Config Error.");
        Serial.println("FATAL: PSRAM NOT FOUND!");
        while(1) delay(100);
    }

    // Initialize SPI for the SD card (SCK=12, MISO=13, MOSI=11, CS is handled via CH422G)
    // First, set CS HIGH to disable the SD card
    lcd._light_instance.write_pin(4, true);
    delay(50);

    // Provide a dummy CS pin (15) so the SD library doesn't reject the begin() call.
    SPI.setHwCs(false);
    SPI.begin(12, 13, 11, 15);

    // Manually send 80 dummy clocks to wake up the SD card in case it's in a bad state
    SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    for (int i = 0; i < 10; i++) {
        SPI.transfer(0xFF);
    }
    SPI.endTransaction();

    // Now set CS LOW to enable the SD card for SD.begin()
    lcd._light_instance.write_pin(4, false);
    delay(50);
    
    // [AI-LOCKED-FIX #1] 2026-10-02: SD Card Mount Failure & Blue Screen
    // Neden: GPIO 10 ekranın mavi rengini bozuyor. Frekans verilirse kart okunamıyor (4MHz default olmalı).
    // DOKUNMA: "temizlik/optimizasyon" gerekçesiyle eski haline getirme. Önce AI_MEMORY.md'deki #1 girdisini oku.
    if (!SD.begin(15, SPI)) {
        Serial.println("ERROR: SD Card Mount Failed!");
        lcd.setTextColor(TFT_RED, TFT_BLACK);
        lcd.println("ERROR: SD Card Mount Failed!");
        while(1) delay(1000);
    }

    Serial.println("DEADBEEF SD Card Mounted!");
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.println("SD Card Mounted!");
    
    // Scan root directory for game data files (case-insensitive)
    bool gameFilesFound = false;
    File root = SD.open("/");
    if (root) {
        File file = root.openNextFile();
        while(file) {
            String fname = String(file.name());
            fname.toLowerCase();
            if (fname.endsWith(".wl1") || fname.endsWith(".wl3") || fname.endsWith(".wl6") ||
                fname.endsWith(".sod") || fname.endsWith(".sd1") || fname.endsWith(".sd2") || 
                fname.endsWith(".sd3") || fname.endsWith(".sdm")) {
                gameFilesFound = true;
                break;
            }
            file = root.openNextFile();
        }
    }
    
    if (gameFilesFound) {
        lcd.println("Found Game data files.");
    } else {
        lcd.setTextColor(TFT_RED, TFT_BLACK);
        lcd.println("ERROR: No Game files found on root!");
        if (root) {
            root.rewindDirectory();
            lcd.println("Listing root directory:");
            File file = root.openNextFile();
            int count = 0;
            while(file && count < 10) {
                lcd.print("- ");
                lcd.println(file.name());
                file = root.openNextFile();
                count++;
            }
        }
        while(1) {
            Serial.println("ERROR: No Game files found on root!");
            delay(1000);
        }
    }
    
    lcd.println("Starting Game...");
    
    // Start Wolf3D game on Core 1 with a large stack size
    xTaskCreatePinnedToCore(wolfTask, "wolfTask", 81920, NULL, 1, NULL, 1);
}

void loop() {
    Serial.println("SUCCESS: Game Running!");
    delay(2000);
}
