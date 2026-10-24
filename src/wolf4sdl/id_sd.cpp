//
//      ID Engine
//      ID_SD.cpp - Sound Manager for Wolfenstein 3D (ESP32 Port)
//

#include "wl_def.h"
#include <Arduino.h>
#include "id_sd.h"
#include "AudioEngine.h"

// AudioEngine global instance
AudioEngine engine(AudioEngine::OutputMode::PWM_BUZZER, -1);

globalsoundpos channelSoundPos[8];

boolean         AdLibPresent = false,
                SoundBlasterPresent = false,
                SoundPositioned = false;

SDMode          SoundMode = sdm_Off;
SMMode          MusicMode = smm_Off;
SDSMode         DigiMode = sds_Off;

static byte   **SoundTable;
int             DigiMap[LASTSOUND];
int             DigiChannel[STARTMUSIC - STARTDIGISOUNDS];

static boolean  SD_Started = false;
static boolean  nextsoundpos = false;
static soundnames SoundNumber;
static uint16_t     SoundPriority;
static int      LeftPosition;
static int      RightPosition;

static boolean  DigiPlaying = false;
static uint16_t     DigiPriority;
static soundnames DigiNumber;

void SD_Startup(void)
{
    if (SD_Started)
        return;

    ledcSetup(0, 2000, 8); // channel 0, 2000 Hz, 8-bit resolution
    // ledcAttachPin(18, 0);  // GPIO 18 is used by LCD (B2) - causes screen flickering!

    SD_SetSoundMode(sdm_Off);
    SD_SetMusicMode(smm_Off);

    SD_Started = true;
}


void SD_Shutdown(void)
{
    if (!SD_Started)
        return;

    SD_MusicOff();
    SD_StopSound();

    engine.stop();

    SD_Started = false;
}

void SD_PositionSound(int leftvol, int rightvol)
{
    LeftPosition = leftvol;
    RightPosition = rightvol;
    nextsoundpos = true;
}

boolean SD_PlaySound(soundnames sound)
{
    if(sound == GETGATLINGSND || sound == GETMACHINESND || sound == GETAMMOSND) {
        ledcWriteTone(0, 1000); // 1 kHz beep
        // We'd ideally want an async timer to turn it off, but for a basic stub:
        // delay(100); 
        // ledcWriteTone(0, 0); 
    } else if (sound >= ATKKNIFESND && sound <= ATKGATLINGSND) {
        ledcWriteTone(0, 400); // lower tone for attack
    } else {
        ledcWriteTone(0, 800);
    }
    
    // Auto turn off sound later or just beep quickly
    // Actually, ledcWriteTone will stay on. Better to just do a quick delay for simple sound
    // if we don't have async sound processing yet.
    // Let's just leave it as a tone that stays on until next sound, or quickly turns off.
    return true;
}


uint16_t SD_SoundPlaying(void)
{
    // A simple timer/status check could be added if needed
    // For now we assume no sound is continuously blocking or return SoundNumber if a timer was set
    return 0;
}

void SD_StopSound(void)
{
    ledcWriteTone(0, 0);
}


void SD_WaitSoundDone(void)
{
    // Stub
}

void SD_MusicOn(void)
{
}

int SD_MusicOff(void)
{
    engine.stop();
    return 0;
}

void SD_StartMusic(int chunk)
{
}

void SD_ContinueMusic(int chunk, int startoffs)
{
}

void SD_FadeOutMusic(void)
{
    SD_MusicOff();
}

boolean SD_MusicPlaying(void)
{
    return false;
}

boolean SD_SetSoundMode(SDMode mode)
{
    SoundMode = mode;
    return true;
}

boolean SD_SetMusicMode(SMMode mode)
{
    MusicMode = mode;
    return true;
}

void SD_SetDigiDevice(SDSMode mode)
{
    DigiMode = mode;
}

void SD_PrepareSound(int which)
{
}

int SD_PlayDigitized(uint16_t which, int leftpos, int rightpos)
{
    engine.playTone(800, 200); // Temporary map digitized to beep
    return 1;
}

void SD_StopDigitized(void)
{
    engine.stop();
    DigiPlaying = false;
}

int SD_GetChannelForDigi(int which)
{
    return 0;
}

void SD_SetPosition(int channel, int leftvol, int rightvol)
{
}
