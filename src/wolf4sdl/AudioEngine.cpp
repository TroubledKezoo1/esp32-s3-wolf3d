#include "AudioEngine.h"
#include <driver/i2s.h>

AudioEngine::AudioEngine(OutputMode mode, int pin) 
    : _mode(mode), _pin(pin), _volume(255), _bclkPin(-1), _wselPin(-1), _doutPin(-1) 
{
}

AudioEngine::~AudioEngine() {
    stop();
}

void AudioEngine::begin() {
    if (_mode == OutputMode::PWM_BUZZER) {
        // Setup PWM for buzzer
        // using simple tone() API or ledc
#ifdef ESP32
        if (_pin != -1) {
            ledcSetup(0, 2000, 8); // Channel 0, 2000 Hz, 8-bit resolution
            ledcAttachPin(_pin, 0);
        }
#else
        if (_pin != -1) pinMode(_pin, OUTPUT);
#endif
    } else if (_mode == OutputMode::I2S_DAC) {
        // Setup I2S for MAX98357A or similar
        // We'll configure standard I2S port 0
        
        i2s_config_t i2s_config = {
            .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
            .sample_rate = 11025, // Default, update on play
            .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
            .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // or I2S_CHANNEL_FMT_RIGHT_LEFT
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count = 8,
            .dma_buf_len = 64,
            .use_apll = false,
            .tx_desc_auto_clear = true,
            .fixed_mclk = 0
        };

        i2s_pin_config_t pin_config = {
            .bck_io_num = _bclkPin != -1 ? _bclkPin : 26,
            .ws_io_num = _wselPin != -1 ? _wselPin : 25,
            .data_out_num = _doutPin != -1 ? _doutPin : 22,
            .data_in_num = I2S_PIN_NO_CHANGE
        };

        i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
        i2s_set_pin(I2S_NUM_0, &pin_config);
        i2s_zero_dma_buffer(I2S_NUM_0);
    }
}

void AudioEngine::playTone(unsigned int frequency, unsigned long duration) {
    if (_mode == OutputMode::PWM_BUZZER) {
#ifdef ESP32
        if (_pin != -1) {
            if (frequency > 0) {
                ledcWriteTone(0, frequency);
                if (duration > 0) {
                    delay(duration);
                    ledcWriteTone(0, 0);
                }
            } else {
                ledcWriteTone(0, 0);
            }
        }
#else
        if (frequency > 0) {
            tone(_pin, frequency, duration);
        } else {
            noTone(_pin);
        }
#endif
    } else if (_mode == OutputMode::I2S_DAC) {
        // Generating square wave for I2S tone playback is possible but complex.
        // Left unimplemented for now, focus on PCM
    }
}

void AudioEngine::playPCM(const uint8_t* data, size_t length, uint32_t sampleRate) {
    if (_mode == OutputMode::I2S_DAC) {
        i2s_set_sample_rates(I2S_NUM_0, sampleRate);
        size_t bytes_written = 0;
        
        // Simple blocking write for now. Should be done async or via FreeRTOS task.
        i2s_write(I2S_NUM_0, data, length, &bytes_written, portMAX_DELAY);
    }
}

void AudioEngine::stop() {
    if (_mode == OutputMode::PWM_BUZZER) {
#ifdef ESP32
        if (_pin != -1) {
            ledcWriteTone(0, 0);
        }
#else
        if (_pin != -1) {
            noTone(_pin);
        }
#endif
    } else if (_mode == OutputMode::I2S_DAC) {
        i2s_zero_dma_buffer(I2S_NUM_0);
    }
}

void AudioEngine::setVolume(uint8_t volume) {
    _volume = volume;
    // Volume control for I2S would involve multiplying PCM data before writing
    // or using I2S configuration options if available.
}
