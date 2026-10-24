# ESP32-S3 Wolfenstein 3D

**Waveshare ESP32-S3-Touch-LCD-7** (800x480 RGB panel, kapasitif dokunmatik) üzerinde tam ekran çalışan Wolfenstein 3D. Ekrandaki sanal gamepad ile oynanır. Değiştirilmiş [Wolf4SDL](https://github.com/fabiangreffrath/wolf4sdl) üzerine PlatformIO ve Arduino framework ile yazıldı.

[English README](README.md)

![ESP32-S3 dokunmatik ekranda Wolfenstein 3D (erken sürüm, 22 fps)](docs/images/gameplay.jpg)

*Erken bir sürüm, hâlâ 22 fps. Güncel sürüm hareket ederken yaklaşık 33 fps, dururken yaklaşık 70 fps veriyor.*

## Ekran görüntüleri

| Oyun içi | Level yükleniyor |
|---|---|
| ![Oyun içi, 1. kat koridor](docs/images/screen-gameplay.png) | ![Get Psyched yükleme ekranı](docs/images/screen-getpsyched.png) |

*Debug sürümü oyunun 320x200 çerçeve tamponundan aldı, bu yüzden dokunmatik arayüz üzerlerinde görünmez.*

## Özellikler

- Doğrudan RGB framebuffer'a tam ekran 800x480 çıkış
- Dokunmatik gamepad: D-pad, A / B / X / Y ve menü tuşu, titremesiz çizim
- Kayıt ve high score isimleri için ekrandaki QWERTY klavye
- SD karta kaydet / yükle
- Hareket ederken ~33 fps, dururken ~70 fps (oyunun 70 Hz sınırı)
- Doğrudan bir level'a girip UART'tan kare süresi basan debug sürümü

## Donanım

| Parça | Not |
|---|---|
| Kart | Waveshare ESP32-S3-Touch-LCD-7 (ESP32-S3, 16 MB flash, 8 MB OPI PSRAM) |
| Ekran | 800x480 RGB, LovyanGFX ile |
| Dokunmatik | GT911 (I2C) |
| Depolama | SPI üzerinden microSD, 4 MHz (CS hattı CH422G genişletici üzerinde) |
| Ses | henüz bağlı değil, yol haritasına bak |

## Derleme ve yükleme

[PlatformIO](https://platformio.org/) kurulu olmalı:

```bash
pio run -e esp32s3box -t upload
```

Seri log için `pio device monitor -b 115200`. Bu kartta UART yazan USB portu bir CH343 köprüsünden geçer. `Serial` çıktıları yalnızca yerel USB portunda görünür, `log_e` çıktıları ise UART portunda çıkar.

### Oyun verisi

Oyun verisi **repoda yok** ve dağıtılamaz. Kendi Wolfenstein 3D kopyandan (tam sürüm, `.WL6`) şu dosyaları FAT formatlı SD kartın ana dizinine kopyala:

`AUDIOHED.WL6`, `AUDIOT.WL6`, `GAMEMAPS.WL6`, `MAPHEAD.WL6`, `VGADICT.WL6`, `VGAGRAPH.WL6`, `VGAHEAD.WL6`, `VSWAP.WL6`

Ayrıntı için `sd_card_files/README.txt`. Bu sürüm tam oyunun Activision / GOODTIMES sürümünü hedefler.

Orijinal oyuna sahipsen kendi dosyalarını kullan. Shareware bölümü (`.WL1`) bu sürümde henüz desteklenmiyor ve repoda hiçbir oyun dosyası yok.

## Kontroller

| Tuş | Oyun | Menü |
|---|---|---|
| D-pad | yürü / dön | gezin |
| A | kapı aç | seç |
| B | koş | geri |
| X | ateş | |
| Y | yan adım | |
| M | menü | |

## Nasıl çalışıyor

- **Core 1** oyunu (`wolfTask`), **Core 0** ekran ve dokunmatik görevlerini çalıştırır.
- Oyun 320x200 8 bit bir tampona çizer. `VL_Flip` her kareyi display görevine verir, o da 800x480'e büyütüp PSRAM'deki RGB framebuffer'a yazar.
- Darboğaz PSRAM framebuffer'a yazmaktı (kare başına 42 ms). `VL_Flip` artık kareyi kopyalarken öncekiyle karşılaştırıyor, display görevi yalnızca değişen 4 piksellik blokları yazıyor.
- Gamepad butonları büyütme geçişinde maskeleniyor (`ui_mask` / `ui_blk`) ve yalnızca bir buton değişince yeniden çiziliyor.
- Büyük tamponlar PSRAM'de. İç RAM dar olduğu için büyük statik diziler dışarı taşındı (`signon` flash'a, `ui_mask` PSRAM'e).

### Debug sürümü

```bash
pio run -e esp32s3box_dbg -t upload
```

Bu ortam intro ve menüyü atlar, kendi kendine yürüyüp döner, 45 saniyede bir level'ı zorla bitirir ve 60 karede bir süre satırı basar. Uzaktan performans ve kararlılık testi içindir, oynamak için değil.

## Yol haritası

- [ ] **Ses.** Karta bir hoparlör takılacak, ardından `id_sd.cpp` içindeki ses stub'ları `AudioEngine` (I2S) ve projede hazır duran OPL3 emülasyonuna bağlanacak.
- [ ] İsteğe bağlı 4:3 mod
- [ ] Shareware (`.WL1`) derleme seçeneği, böylece proje tam oyun olmadan denenebilir
- [ ] Daha hızlı framebuffer yolu (GDMA kopyalama)

## Kaynaklar ve lisanslar

- Oyun motoru: id Software'in orijinal Wolfenstein 3D kaynağına dayanan [Wolf4SDL](https://github.com/fabiangreffrath/wolf4sdl). `src/wolf4sdl` altındaki dosyalar kendi lisanslarını korur (`license-id.txt`, `license-gpl.txt`).
- Ekran ve dokunmatik: [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- Wolfenstein 3D oyun verisinin telifi sahiplerine aittir, bu repoda yoktur.
- `src/` altındaki ESP32 kodu Wolf4SDL ile aynı koşullara tabidir (ticari olmayan kullanım, bkz. `src/wolf4sdl/license-id.txt`).
- Antigravity ve Claude Code yardımıyla geliştirildi.
