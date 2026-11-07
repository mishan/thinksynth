/*
 * Copyright (C) 2004-2026 The thinksynth authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* thinkbox: dsp/dxbell.dsp on an ESP32-S3-BOX. Eight compiled voices on
   one core, through the speaker; on the other, the touchscreen and the
   IMU read 250 times a second, played through the patch's controller
   block by the same mapper the desktop's controllers have. */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_tt21100.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "audio.h"
#include "thhBox.h"
#include "thhMapper.h"
#include "thvDxbell.h"

#define VOICES 8
#define BLOCK 128
#define GAIN 0.25f

/* What the controls are doing to the voices, the audio task's and the
   control task's both, under `lock'. */
static SemaphoreHandle_t lock;
static thvDxbell voice[VOICES];
static thvDxbell::Knobs knobs;
static struct
{
    int   note;                 /* as played, -1 for none */
    bool  held, sounding;
    int64_t since;              /* when it started, to steal the oldest */
} slot[VOICES];

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t imu;
static esp_lcd_touch_handle_t touch;

static void
audioTask (void *)
{
    static float l[BLOCK], r[BLOCK], mix[2][BLOCK];
    static int16_t frames[2 * BLOCK];
    float *outs[2] = { l, r };

    for (;;)
    {
        memset(mix, 0, sizeof(mix));
        xSemaphoreTake(lock, portMAX_DELAY);
        for (int v = 0; v < VOICES; v++)
            if (slot[v].sounding)
            {
                slot[v].sounding = voice[v].render(knobs, outs, BLOCK);
                for (int i = 0; i < BLOCK; i++)
                {
                    mix[0][i] += l[i];
                    mix[1][i] += r[i];
                }
            }
        xSemaphoreGive(lock);

        for (int i = 0; i < BLOCK; i++)
            for (int c = 0; c < 2; c++)
            {
                const float x = fminf(1, fmaxf(-1, mix[c][i] * GAIN));

                frames[2 * i + c] = (int16_t)(x * 32000);
            }
        audio_write(frames, BLOCK);
    }
}

/* A mapper's actions on the voices, under the lock. */
static void
play (const std::vector<thhAction> &acts)
{
    const int64_t now = esp_timer_get_time();

    for (const thhAction &a : acts)
    {
        int v = -1;

        switch (a.kind)
        {
        case thhAction::NOTE_ON:
            /* A free voice, else the oldest. */
            for (int i = 0; i < VOICES && v < 0; i++)
                if (!slot[i].sounding)
                    v = i;
            for (int i = 0; i < VOICES && v < 0; i++)
                v = i;
            for (int i = 0; i < VOICES; i++)
                if (slot[i].sounding && slot[i].since < slot[v].since)
                    v = i;
            voice[v] = thvDxbell();
            voice[v].start(a.note, a.vel);
            slot[v] = { a.note, true, true, now };
            break;
        case thhAction::NOTE_OFF:
            for (int i = 0; i < VOICES; i++)
                if (slot[i].held && slot[i].note == a.note)
                {
                    voice[i].release();
                    slot[i].held = false;
                }
            break;
        case thhAction::NOTE_BEND:
            for (int i = 0; i < VOICES; i++)
                if (slot[i].held && slot[i].note == a.note)
                    voice[i].note = a.note + (float)a.t;
            break;
        case thhAction::VALUE:
        {
            float lo = 0, hi = 1;

            if (thvDxbell::Knobs::range(a.arg.c_str(), lo, hi))
                knobs.set(a.arg.c_str(), (float)thhActionValue(a, lo, hi));
            break;
        }
        default:
            break;
        }
    }
}

static bool
imuRead (float accel[3], float gyro[3])
{
    uint8_t reg = 0x0b, d[12];

    if (i2c_master_transmit_receive(imu, &reg, 1, d, sizeof(d), 20) != ESP_OK)
        return false;
    for (int k = 0; k < 3; k++)
    {
        /* At its defaults: 2048 to the g, 16.4 to the degree a second. */
        accel[k] = (int16_t)((d[2 * k] << 8) | d[2 * k + 1]) / 2048.0f;
        gyro[k] = (int16_t)((d[6 + 2 * k] << 8) | d[7 + 2 * k]) / 16.4f;
    }
    return true;
}

/* Its note tables are ten kilobytes: not a task's stack's to hold. */
static thhMapper mapper;

static void
controlTask (void *)
{
    thhState s;
    thhBoxImu pose;
    std::vector<thhAction> acts;
    std::string err;
    int line, n = 0;
    int64_t last = esp_timer_get_time();

    if (!mapper.configure(thvDxbell::profile(), thhBoxControls(), acts, err, line))
        printf("thinkbox: the profile doesn't fit, line %d: %s\n", line, err.c_str());
    s.connected = true;

    for (;; n++)
    {
        const int64_t now = esp_timer_get_time();
        float accel[3], gyro[3];

        if (imuRead(accel, gyro))
            thhBoxFeedImu(accel, gyro, (now - last) / 1e6f, pose, s);
        last = now;

        uint16_t x, y, strength;
        uint8_t count = 0;

        esp_lcd_touch_read_data(touch);
        if (esp_lcd_touch_get_coordinates(touch, &x, &y, &strength, &count, 1) && count)
        {
            s.axis[THH_BOX_X] = x / 320.0f;
            s.axis[THH_BOX_Y] = y / 240.0f;
            s.buttons |= 1u << THH_BOX_TOUCH;
        }
        else
            s.buttons &= ~(1u << THH_BOX_TOUCH);
        s.time = now / 1e6;

        acts.clear();
        mapper.update(s, 0, acts);
        if (!acts.empty())
        {
            xSemaphoreTake(lock, portMAX_DELAY);
            play(acts);
            xSemaphoreGive(lock);
        }

        if (n % 125 == 0)
            printf("touch %d x %.2f y %.2f  tilt %+.2f roll %+.2f twist %+.2f turn %+.2f  "
                   "gravity %+.2f %+.2f %+.2f\n", (s.buttons & 1) ? 1 : 0,
                   s.axis[THH_BOX_X], s.axis[THH_BOX_Y], s.axis[THH_BOX_TILT],
                   s.axis[THH_BOX_ROLL], s.axis[THH_BOX_TWIST], s.axis[THH_BOX_TURN],
                   pose.g[0], pose.g[1], pose.g[2]);
        vTaskDelay(pdMS_TO_TICKS(4));
    }
}

extern "C" void
app_main (void)
{
    i2c_master_bus_config_t bc = {};

    bc.i2c_port = I2C_NUM_0;
    bc.sda_io_num = GPIO_NUM_8;
    bc.scl_io_num = GPIO_NUM_18;
    bc.clk_source = I2C_CLK_SRC_DEFAULT;
    bc.glitch_ignore_cnt = 7;
    bc.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &bus));

    if (!audio_open(bus, thvDxbell::rate, 70))
        printf("thinkbox: no speaker\n");

    /* The IMU, an ICM-42607 at 0x68: accelerometer and gyro on, low noise. */
    i2c_device_config_t ic = {};
    uint8_t on[2] = { 0x1f, 0x0f };

    ic.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    ic.device_address = 0x68;
    ic.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &ic, &imu));
    i2c_master_transmit(imu, on, 2, 100);

    /* The touchscreen's controller, a TT21100 at 0x24, its interrupt on 3. */
    esp_lcd_panel_io_handle_t tio;
    esp_lcd_panel_io_i2c_config_t tc = ESP_LCD_TOUCH_IO_I2C_TT21100_CONFIG();
    esp_lcd_touch_config_t touchc = {};

    tc.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &tc, &tio));
    touchc.x_max = 320;
    touchc.y_max = 240;
    touchc.rst_gpio_num = GPIO_NUM_NC;
    touchc.int_gpio_num = GPIO_NUM_3;
    touchc.flags.mirror_x = 1;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_tt21100(tio, &touchc, &touch));

    for (int v = 0; v < VOICES; v++)
        slot[v].note = -1;
    lock = xSemaphoreCreateMutex();
    printf("thinkbox: dxbell, %d voices at %d Hz\n", VOICES, (int)thvDxbell::rate);
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, NULL, 20, NULL, 1);
    xTaskCreatePinnedToCore(controlTask, "control", 8192, NULL, 10, NULL, 0);
}
