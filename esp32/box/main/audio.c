/* The ESP32-S3-BOX's speaker: I2S into the ES8311, the amplifier on GPIO 46.
   Pins as Espressif's esp-box BSP has them. */
#include "audio.h"

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

static esp_codec_dev_handle_t out;

int
audio_open (i2c_master_bus_handle_t bus, int rate, int volume)
{
    i2s_chan_handle_t tx;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = 2, .bclk = 17, .ws = 47, .dout = 15, .din = 16 },
    };

    /* Silence, not the last buffers again, once nothing more is written. */
    cc.auto_clear = true;
    if (i2s_new_channel(&cc, &tx, NULL) != ESP_OK ||
        i2s_channel_init_std_mode(tx, &sc) != ESP_OK ||
        i2s_channel_enable(tx) != ESP_OK)
        return 0;

    audio_codec_i2s_cfg_t i2s_cfg = { .port = I2S_NUM_0, .tx_handle = tx };
    audio_codec_i2c_cfg_t i2c_cfg = { .port = I2C_NUM_0,
                                      .addr = ES8311_CODEC_DEFAULT_ADDR,
                                      .bus_handle = bus };
    es8311_codec_cfg_t es = {
        .ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg),
        .gpio_if = audio_codec_new_gpio(),
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = 46, .use_mclk = true,
    };
    esp_codec_dev_cfg_t dc = { .dev_type = ESP_CODEC_DEV_TYPE_OUT,
                               .codec_if = es8311_codec_new(&es),
                               .data_if = audio_codec_new_i2s_data(&i2s_cfg) };
    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 2,
                                       .sample_rate = rate };

    out = esp_codec_dev_new(&dc);
    if (!out || esp_codec_dev_open(out, &fs) != ESP_CODEC_DEV_OK)
        return 0;
    esp_codec_dev_set_out_vol(out, volume);
    return 1;
}

void
audio_write (const int16_t *frames, int count)
{
    esp_codec_dev_write(out, (void *)frames, count * 4);
}
