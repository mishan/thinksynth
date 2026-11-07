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

/* thinkbox: patches compiled for an ESP32-S3-BOX (generate.sh), eight
   voices of the one playing on one core, through the speaker; on the
   other, the touchscreen and the IMU read 250 times a second and played
   through the BOX's profile and the patch's block by the same mapper the
   desktop's controllers have. The side button steps to the next patch. */

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
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "audio.h"
#include "thhBox.h"
#include "thhMapper.h"
#include "patches.h"

#define VOICES 8
#define BLOCK 128
#define GAIN 0.25f
#define BOX_RATE 32000
/* How long the screen must read no finger for one to have lifted. */
#define TOUCH_LIFTED_US 40000
/* How long the side button must read the same to have moved: it
   bounces. */
#define BUTTON_STEADY_US 20000

/* A patch, whichever compiled voice it is: its voices, its knobs, and
   what a mapper's actions do to them. */
struct Patch
{
    const char *name;
    float gain;                 /* to peak as loud as the others */

    virtual ~Patch () {}
    virtual thcHidBlock profile () const = 0;
    /* owner: whose the note is, a finger's, so a NOTE_OFF or a bend
       reaches only the voice its own NOTE_ON started. */
    virtual void play (const thhAction &a, int owner) = 0;
    /* Every sounding voice into l and r, added. */
    virtual void render (float *l, float *r) = 0;
};

template <class V>
struct PatchOf : Patch
{
    V voice[VOICES];
    typename V::Knobs knobs;
    struct
    {
        int     note = -1, owner = -1;  /* as played, and whose */
        bool    held = false, sounding = false;
        int64_t since = 0;          /* when it started, to steal the oldest */
    } slot[VOICES];

    PatchOf (const char *n, float g) { name = n; gain = g; }

    thcHidBlock profile () const override { return V::profile(); }

    void play (const thhAction &a, int owner) override
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
            voice[v] = V();
            voice[v].start(a.note, a.vel, (uint32_t)esp_timer_get_time());
            slot[v].note = a.note;
            slot[v].owner = owner;
            slot[v].held = slot[v].sounding = true;
            slot[v].since = esp_timer_get_time();
            break;
        case thhAction::NOTE_OFF:
            for (int i = 0; i < VOICES; i++)
                if (slot[i].held && slot[i].note == a.note && slot[i].owner == owner)
                {
                    voice[i].release();
                    slot[i].held = false;
                }
            break;
        case thhAction::NOTE_BEND:
            for (int i = 0; i < VOICES; i++)
                if (slot[i].held && slot[i].note == a.note && slot[i].owner == owner)
                    voice[i].note = a.note + (float)a.t;
            break;
        case thhAction::VALUE:
        {
            float lo = 0, hi = 1;

            if (V::Knobs::range(a.arg.c_str(), lo, hi))
                knobs.set(a.arg.c_str(), (float)thhActionValue(a, lo, hi));
            break;
        }
        default:
            break;
        }
    }

    void render (float *l, float *r) override
    {
        static float vl[BLOCK], vr[BLOCK];
        float *outs[2] = { vl, vr };

        for (int v = 0; v < VOICES; v++)
            if (slot[v].sounding)
            {
                memset(vr, 0, sizeof(vr));
                slot[v].sounding = voice[v].render(knobs, outs, BLOCK);
                for (int i = 0; i < BLOCK; i++)
                {
                    l[i] += gain * vl[i];
                    /* A mono patch is both sides. */
                    r[i] += gain * (V::channels > 1 ? vr[i] : vl[i]);
                }
            }
    }
};

#define PATCH(type, name, gain) new PatchOf<thv##type>(name, gain),
static Patch *const patches[] = { PATCHES };
#undef PATCH
#define NPATCHES (int)(sizeof(patches) / sizeof(patches[0]))

/* The patch playing, and every patch's voices, the audio task's and the
   control task's both, under `lock'. */
static SemaphoreHandle_t lock;
static volatile int playing = 0;
static volatile float load = 0;         /* of the audio task's time */
/* Each patch's most, and the blocks it took longer than they last. */
static float peak[64];
static int late[64];

/* The keys fingers are on, a bit each, for the screen; and which patch it
   shows as playing. */
static volatile uint32_t lit = 0;
static volatile int shown = 0;

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t imu, touch;

/* A contact the touch controller reports. */
struct Contact
{
    int id, x, y;
};

/* The BOX's touch controller, a TT21100 at 0x24, read as it asks to be:
   the length of its report, then the whole report, whatever its length.
   A report left unread is offered again and again, and nothing newer
   behind it, so every one is read through. Finger records are ten bytes
   each after a seven-byte header; a 14-byte report is the buttons'. */
static int
touchRead (Contact *out, int most)
{
    static uint8_t report[512];
    uint8_t len[2];

    if (i2c_master_receive(touch, len, 2, 20) != ESP_OK)
        return 0;

    const int n = len[0] | (len[1] << 8);

    if (n < 2 || n > (int)sizeof(report) ||
        i2c_master_receive(touch, report, n, 20) != ESP_OK || n < 7 || n == 14)
        return 0;

    int count = 0;

    for (int r = 0; r < (report[5] & 0x1f) && 7 + 10 * (r + 1) <= n; r++)
    {
        const uint8_t *t = report + 7 + 10 * r;

        if ((t[0] & 0x07) != 0 || count >= most)    /* a finger, not a pen */
            continue;
        /* A finger's last record says it has lifted: its tip off, its
           event 3. */
        if (!(t[1] & 0x80) || ((t[1] >> 5) & 3) == 3)
            continue;
        out[count].id = t[1] & 0x1f;
        /* Mirrored across the 320 columns, as the screen is drawn. */
        out[count].x = 319 - (t[2] | (t[3] << 8));
        out[count].y = t[4] | (t[5] << 8);
        count++;
    }
    return count;
}

static void
audioTask (void *)
{
    static float mix[2][BLOCK];
    static int16_t frames[2 * BLOCK];

    for (;;)
    {
        memset(mix, 0, sizeof(mix));
        xSemaphoreTake(lock, portMAX_DELAY);

        const int64_t t = esp_timer_get_time();

        /* Every patch: the one playing, and any switched from, ringing
           out. */
        for (Patch *p : patches)
            p->render(mix[0], mix[1]);
        load = (esp_timer_get_time() - t) / (1e6f * BLOCK / BOX_RATE);
        if (load > peak[playing])
            peak[playing] = load;
        late[playing] += load > 1;
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

/* The screen: the keys THH_BOX_PROFILE lays out, `x = pitch { scale =
   minor_pentatonic; range = 10; }', the degree under x * 10 rounded, so
   eleven keys, the ends half as wide. Each a color of its note of the
   scale, darker the further up the screen, where it plays softer; the
   Cs with a white bar at the foot; the one touched lit. */
#define W 320
#define H 240
#define KEYS 11
#define STRIP 16                /* rows drawn at a time */
#define TOP 16                  /* the patches' row, above the keys */

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
    static uint16_t *strip = NULL;
    int from, to;

    keySpan(k, from, to);
    if (to <= from)
        return;
    if (!strip)
        strip = (uint16_t *)heap_caps_malloc(W * STRIP * 2, MALLOC_CAP_DMA);

    const int *h = HUE[k % 5];
    const int w = to - from;

    for (int y0 = TOP; y0 < H; y0 += STRIP)
    {
        for (int y = y0; y < y0 + STRIP; y++)
            for (int x = 0; x < w; x++)
            {
                /* Darker toward the top, softer there; a dark seam between
                   keys; the Cs' white foot; the touched key bright. */
                const float shade = on ? 1 : 0.35f + 0.65f * (y - TOP) / (H - 1 - TOP);
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

/* The patches along the top, a square each, the playing one lit. */
static void
drawPatches (int n, int on)
{
    static uint16_t *row = NULL;

    if (!row)
        row = (uint16_t *)heap_caps_malloc(W * TOP * 2, MALLOC_CAP_DMA);
    for (int y = 0; y < TOP; y++)
        for (int x = 0; x < W; x++)
        {
            const int p = (x - 6) / 20, in = (x - 6) % 20;
            const bool square = p >= 0 && p < n && in < 14 && y >= 2 && y < TOP - 2;

            row[y * W + x] = !square ? rgb(0, 0, 0)
                           : p == on ? rgb(255, 255, 255) : rgb(70, 70, 70);
        }
    esp_lcd_panel_draw_bitmap(panel, 0, 0, W, TOP, row);
}

static void
displayTask (void *)
{
    uint32_t keysShown = 0;
    int patchShown = -1;

    for (int k = 0; k < KEYS; k++)
        drawKey(k, false);
    for (;;)
    {
        const uint32_t now = lit;

        if (shown != patchShown)
        {
            patchShown = shown;
            drawPatches(NPATCHES, patchShown);
        }

        for (int k = 0; k < KEYS; k++)
            if (((now ^ keysShown) >> k) & 1)
                drawKey(k, (now >> k) & 1);
        keysShown = now;
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

/* A finger on the screen: the controller's id for its contact, the key
   it's on, and its own mapper, so each plays its own note at its own
   pitch; the IMU is every finger's. Two, as many as the BOX's TT21100
   reports at once. A mapper's note tables are ten kilobytes, not a
   task's stack's to hold. */
#define FINGERS 2

static struct Finger
{
    int       id = -1;              /* the contact's, or -1 */
    bool      down = false;
    int       key = -1;
    int64_t   seen = 0;             /* when it was last reported */
    thhState  s;
    thhMapper mapper;
} finger[FINGERS];

/* What a finger's actions do, under the lock: owner, which finger. */
static void
playAll (const std::vector<thhAction> &acts, int owner)
{
    if (acts.empty())
        return;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (const thhAction &a : acts)
        patches[playing]->play(a, owner);
    xSemaphoreGive(lock);
}

/* Every finger's mapper given the playing patch's profile, the notes
   they held let go. */
static void
configureAll ()
{
    std::vector<thhAction> acts;
    std::string err;
    int line;

    for (Finger &f : finger)
    {
        acts.clear();
        f.mapper.release(acts);
        playAll(acts, (int)(&f - finger));
        acts.clear();
        if (!f.mapper.configure(patches[playing]->profile(), thhBoxControls(), acts,
                                err, line))
            printf("thinkbox: %s's profile doesn't fit, line %d: %s\n",
                   patches[playing]->name, line, err.c_str());
        f.s.connected = true;
    }
    printf("thinkbox: %s\n", patches[playing]->name);
}

static void
controlTask (void *)
{
    thhState pose;
    thhBoxImu imu;
    std::vector<thhAction> acts;
    int n = 0, presses = 0, most = 0;
    int64_t last = esp_timer_get_time();

    configureAll();

    /* The side button, BOOT, low while pressed. */
    gpio_config_t bc = {};
    bool down = false, wasDown = false, reads = false;
    int64_t readSince = 0;

    bc.pin_bit_mask = 1ULL << GPIO_NUM_0;
    bc.mode = GPIO_MODE_INPUT;
    bc.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&bc);

    for (;; n++)
    {
        const int64_t now = esp_timer_get_time();
        float accel[3], gyro[3];

        if (imuRead(accel, gyro))
            thhBoxFeedImu(accel, gyro, (now - last) / 1e6f, imu, pose);
        last = now;

        /* A press steps to the next patch, the notes held let go and
           left to ring out. */
        const bool reading = gpio_get_level(GPIO_NUM_0) == 0;

        if (reading != reads)
        {
            reads = reading;
            readSince = now;
        }
        if (now - readSince >= BUTTON_STEADY_US)
            down = reads;
        if (down && !wasDown)
        {
            for (Finger &f : finger)
            {
                acts.clear();
                f.mapper.release(acts);
                playAll(acts, (int)(&f - finger));
            }
            xSemaphoreTake(lock, portMAX_DELAY);
            playing = (playing + 1) % NPATCHES;
            xSemaphoreGive(lock);
            shown = playing;
            configureAll();
        }
        wasDown = down;

        /* Each contact to the finger with its id, or to a free one. */
        Contact points[FINGERS];
        const int count = touchRead(points, FINGERS);

        most = count > most ? count : most;
        for (int p = 0; p < count; p++)
        {
            Finger *f = NULL;

            for (Finger &g : finger)
                if (g.id == points[p].id)
                    f = &g;
            for (Finger &g : finger)
                if (!f && g.id < 0)
                    f = &g;
            if (!f)
                continue;
            f->id = points[p].id;
            f->seen = now;
            f->s.axis[THH_BOX_X] = points[p].x / (float)(W - 1);
            f->s.axis[THH_BOX_Y] = points[p].y / (float)(H - 1);
        }

        uint32_t keys = 0;

        for (Finger &f : finger)
        {
            /* The controller reports a held finger in bursts, with
               readings of none between them: lifted is none for a while,
               not once. */
            const bool on = f.id >= 0 && now - f.seen <= TOUCH_LIFTED_US;
            const int key = keyAt((int)(f.s.axis[THH_BOX_X] * (W - 1)));

            for (int a = THH_BOX_TILT; a <= THH_BOX_TURN; a++)
                f.s.axis[a] = pose.axis[a];
            f.s.time = now / 1e6;
            acts.clear();

            /* Onto another key is that key played, as on a keyboard: the
               pitch axis sets a note's pitch only as it starts. */
            if (on && f.down && key != f.key)
            {
                f.s.buttons &= ~(1u << THH_BOX_TOUCH);
                f.mapper.update(f.s, 0, acts);
                f.s.time += 1e-4;
            }
            if (on)
                f.s.buttons |= 1u << THH_BOX_TOUCH;
            else
                f.s.buttons &= ~(1u << THH_BOX_TOUCH);
            presses += on && (!f.down || key != f.key);
            f.mapper.update(f.s, 0, acts);
            playAll(acts, (int)(&f - finger));

            f.down = on;
            f.key = on ? key : -1;
            if (!on)
                f.id = -1;
            if (on)
                keys |= 1u << key;
        }
        lit = keys;

        if (n % 125 == 0)
        {
            printf("peaks");
            for (int p = 0; p < NPATCHES; p++)
                printf("  %s %.0f%% %d late", patches[p]->name, peak[p] * 100, late[p]);
            printf("\n");
        }
        if (n % 125 == 0)
            printf("%s load %3.0f%%  presses %d  most at once %d  keys %03lx  tilt %+.2f roll %+.2f twist "
                   "%+.2f turn %+.2f\n", patches[playing]->name, load * 100, presses, most,
                   (unsigned long)keys, pose.axis[THH_BOX_TILT], pose.axis[THH_BOX_ROLL],
                   pose.axis[THH_BOX_TWIST], pose.axis[THH_BOX_TURN]);
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

    if (!audio_open(bus, BOX_RATE, 70))
        printf("thinkbox: no speaker\n");

    /* The IMU, an ICM-42607 at 0x68: accelerometer and gyro on, low noise. */
    i2c_device_config_t ic = {};
    uint8_t on[2] = { 0x1f, 0x0f };

    ic.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    ic.device_address = 0x68;
    ic.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &ic, &imu));
    i2c_master_transmit(imu, on, 2, 100);

    ic.device_address = 0x24;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &ic, &touch));

    lock = xSemaphoreCreateMutex();
    printf("thinkbox: %d patches, %d voices each at %d Hz\n", NPATCHES, VOICES, BOX_RATE);
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, NULL, 20, NULL, 1);
    xTaskCreatePinnedToCore(controlTask, "control", 8192, NULL, 10, NULL, 0);
    if (displayOpen())
        xTaskCreatePinnedToCore(displayTask, "display", 4096, NULL, 5, NULL, 0);
    else
        printf("thinkbox: no screen\n");
}
