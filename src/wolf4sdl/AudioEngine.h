#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <Arduino.h>

class AudioEngine {
public:
    enum class OutputMode {
        PWM_BUZZER,
        I2S_DAC
    };

    AudioEngine(OutputMode mode, int pin);
    ~AudioEngine();

    void begin();
    void playTone(unsigned int frequency, unsigned long duration);
    void playPCM(const uint8_t* data, size_t length, uint32_t sampleRate);
    void stop();

    void setVolume(uint8_t volume);

private:
    OutputMode _mode;
    int _pin;
    uint8_t _volume;

    // I2S specific pins (could be configurable)
    int _bclkPin;
    int _wselPin;
    int _doutPin;
};

#endif // AUDIO_ENGINE_H
