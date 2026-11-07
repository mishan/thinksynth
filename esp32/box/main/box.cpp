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

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
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
/* How long the screen must read no finger for one to have lifted. */
#define TOUCH_LIFTED_US 40000

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

/* The key the finger is on, for the screen: -1 for none. */
static volatile int lit = -1;

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
                /* A NaN would clamp to -1, a full-scale DC: silence. */
                const float y = mix[c][i] * GAIN;
                const float x = y == y ? fminf(1, fmaxf(-1, y)) : 0;

                frames[2 * i + c] = (int16_t)(x * 32000);
            }
        audio_write(frames, BLOCK);
    }
}

/* The screen: the keys dxbell's block lays out, `x = pitch { scale =
   minor_pentatonic; range = 10; }', the degree under x * 10 rounded, so
   eleven keys, the ends half as wide. Each a color of its note of the
   scale, darker the further up the screen, where it plays softer; the
   Cs with a white bar at the foot; the one touched lit. */
#define W 320
#define H 240
#define KEYS 11
#define STRIP 20                /* rows drawn at a time */

static esp_lcd_panel_handle_t panel;

static uint16_t
rgb (int r, int g, int b)
{
    const uint16_t c = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));

    return (uint16_t)((c >> 8) | (c << 8));         /* as the wire takes it */
}

static int
keyAt (int px)
{
    return (int)lroundf(px * (KEYS - 1) / (float)(W - 1));
}

/* The columns key k spans. */
static void
keySpan (int k, int &from, int &to)
{
    from = 0;
    while (from < W && keyAt(from) < k)
        from++;
    to = from;
    while (to < W && keyAt(to) == k)
        to++;
}

static void
drawKey (int k, bool on)
{
    /* C, E flat, F, G, B flat. */
    static const int HUE[5][3] = {
        { 230, 70, 70 }, { 230, 160, 40 }, { 200, 210, 60 }, { 60, 190, 120 },
        { 70, 120, 230 } };
    /* Two, each filled while the other is still being sent: a draw only
       queues its pixels, and waits for the last one's only as it starts. */
    static uint16_t *strips[2] = { NULL, NULL };
    int from, to;

    keySpan(k, from, to);
    if (to <= from)
        return;
    for (int b = 0; b < 2; b++)
        if (!strips[b])
            strips[b] = (uint16_t *)heap_caps_malloc(W * STRIP * 2, MALLOC_CAP_DMA);
    if (!strips[0] || !strips[1])
        return;

    const int *h = HUE[k % 5];
    const int w = to - from;

    for (int y0 = 0; y0 < H; y0 += STRIP)
    {
        uint16_t *strip = strips[(y0 / STRIP) % 2];

        for (int y = y0; y < y0 + STRIP; y++)
            for (int x = 0; x < w; x++)
            {
                /* Darker toward the top, softer there; a dark seam between
                   keys; the Cs' white foot; the touched key bright. */
                const float shade = on ? 1 : 0.35f + 0.65f * y / (H - 1);
                const bool seam = x == 0 || x == w - 1;
                const bool foot = k % 5 == 0 && y >= H - 14 && x > 2 && x < w - 3;
                uint16_t c = seam ? rgb(10, 10, 10)
                           : foot ? rgb(255, 255, 255)
                           : on ? rgb(255, 255, 255)
                                : rgb((int)(h[0] * shade), (int)(h[1] * shade),
                                      (int)(h[2] * shade));

                if (on && !seam && !foot)
                    c = rgb(240 - (240 - h[0]) / 3, 240 - (240 - h[1]) / 3,
                            240 - (240 - h[2]) / 3);
                strip[(y - y0) * w + x] = c;
            }
        esp_lcd_panel_draw_bitmap(panel, from, y0, to, y0 + STRIP, strip);
    }
}

static void
displayTask (void *)
{
    int shown = -1;

    for (int k = 0; k < KEYS; k++)
        drawKey(k, false);
    for (;;)
    {
        const int now = lit;

        if (now != shown)
        {
            if (shown >= 0)
                drawKey(shown, false);
            if (now >= 0)
                drawKey(now, true);
            shown = now;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

/* The BOX's LCD, an ILI9342C on SPI as Espressif's esp-box BSP drives it. */
static bool
displayOpen (void)
{
    spi_bus_config_t sb = {};
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t ic = {};
    esp_lcd_panel_dev_config_t pc = {};

    sb.mosi_io_num = GPIO_NUM_6;
    sb.miso_io_num = GPIO_NUM_NC;
    sb.sclk_io_num = GPIO_NUM_7;
    sb.quadwp_io_num = GPIO_NUM_NC;
    sb.quadhd_io_num = GPIO_NUM_NC;
    sb.max_transfer_sz = W * STRIP * 2;
    ic.dc_gpio_num = GPIO_NUM_4;
    ic.cs_gpio_num = GPIO_NUM_5;
    ic.pclk_hz = 40 * 1000 * 1000;
    ic.lcd_cmd_bits = 8;
    ic.lcd_param_bits = 8;
    ic.trans_queue_depth = 2;
    pc.reset_gpio_num = GPIO_NUM_48;
    pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    pc.bits_per_pixel = 16;
    if (spi_bus_initialize(SPI2_HOST, &sb, SPI_DMA_CH_AUTO) != ESP_OK ||
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &ic, &io) != ESP_OK ||
        esp_lcd_new_panel_st7789(io, &pc, &panel) != ESP_OK)
        return false;
    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_mirror(panel, true, true);
    esp_lcd_panel_disp_on_off(panel, true);

    gpio_config_t bl = {};

    bl.pin_bit_mask = 1ULL << GPIO_NUM_45;
    bl.mode = GPIO_MODE_OUTPUT;
    gpio_config(&bl);
    gpio_set_level(GPIO_NUM_45, 1);
    return true;
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
            if (v < 0)
            {
                v = 0;
                for (int i = 1; i < VOICES; i++)
                    if (slot[i].since < slot[v].since)
                        v = i;
            }
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
    int line, n = 0, presses = 0;
    int64_t last = esp_timer_get_time(), touched = 0;

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

        /* The controller reports a held finger in bursts, with readings of
           none between them: lifted is none for a while, not once. */
        if (touch)
            esp_lcd_touch_read_data(touch);
        if (touch && esp_lcd_touch_get_coordinates(touch, &x, &y, &strength, &count, 1) &&
            count)
        {
            s.axis[THH_BOX_X] = x / (float)(W - 1);
            s.axis[THH_BOX_Y] = y / (float)(H - 1);
            if (!(s.buttons & (1u << THH_BOX_TOUCH)))
                presses++;
            s.buttons |= 1u << THH_BOX_TOUCH;
            touched = now;
            lit = keyAt(x);
        }
        else if (now - touched > TOUCH_LIFTED_US)
        {
            s.buttons &= ~(1u << THH_BOX_TOUCH);
            lit = -1;
        }
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
            printf("presses %d  touch %d x %.2f y %.2f  tilt %+.2f roll %+.2f twist %+.2f turn %+.2f  "
                   "gravity %+.2f %+.2f %+.2f\n", presses, (s.buttons & 1) ? 1 : 0,
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

    /* With nothing to write to, the audio task would never wait. */
    const bool speaker = audio_open(bus, thvDxbell::rate, 70);

    if (!speaker)
        printf("thinkbox: no speaker\n");

    /* The IMU, an ICM-42607 at 0x68: accelerometer and gyro on, low noise. */
    i2c_device_config_t ic = {};
    uint8_t on[2] = { 0x1f, 0x0f };

    ic.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    ic.device_address = 0x68;
    ic.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &ic, &imu));
    if (i2c_master_transmit(imu, on, 2, 100) != ESP_OK)
        printf("thinkbox: no IMU; tilt and roll stay level\n");

    /* The touchscreen's controller, a TT21100 at 0x24, its interrupt on 3. */
    esp_lcd_panel_io_handle_t tio;
    esp_lcd_panel_io_i2c_config_t tc = ESP_LCD_TOUCH_IO_I2C_TT21100_CONFIG();
    esp_lcd_touch_config_t touchc = {};

    tc.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &tc, &tio));
    /* No touch is no keys, not a reboot with the amplifier already on. */
    touchc.x_max = 320;
    touchc.y_max = 240;
    touchc.rst_gpio_num = GPIO_NUM_NC;
    touchc.int_gpio_num = GPIO_NUM_3;
    touchc.flags.mirror_x = 1;
    if (esp_lcd_touch_new_i2c_tt21100(tio, &touchc, &touch) != ESP_OK)
    {
        printf("thinkbox: no touch\n");
        touch = NULL;
    }

    for (int v = 0; v < VOICES; v++)
        slot[v].note = -1;
    lock = xSemaphoreCreateMutex();
    configASSERT(lock);
    printf("thinkbox: dxbell, %d voices at %d Hz\n", VOICES, (int)thvDxbell::rate);
    if (speaker)
        xTaskCreatePinnedToCore(audioTask, "audio", 8192, NULL, 20, NULL, 1);
    xTaskCreatePinnedToCore(controlTask, "control", 8192, NULL, 10, NULL, 0);
    if (displayOpen())
        xTaskCreatePinnedToCore(displayTask, "display", 4096, NULL, 5, NULL, 0);
    else
        printf("thinkbox: no screen\n");
}
