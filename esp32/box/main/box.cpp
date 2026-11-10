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
   voices of the one playing, rendered on both cores, through the
   speaker; and two fingers on the touchscreen and the IMU, read 250
   times a second and played through the BOX's profile and the patch's
   block by the same mapper the desktop's controllers have. Or marbles
   and pegs (game/thgMarbles), the balls rolling as the BOX is tipped and
   playing what they strike, the walls on a percussion patch. Or Conway's
   Life (game/thgLife), a playhead sweeping the board and playing its
   live cells, painted with a finger. The side button steps to the next
   patch, the last's notes ringing out, and held, to the next of the
   keys, the marbles and Life. */

#include <cmath>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "box_hal.h"
#include "debugcon.h"
#include "thhBox.h"
#include "thgLife.h"
#include "thgMarbles.h"
#include "thhMapper.h"
#include "patches.h"
#include "composer.h"
#include "pool.h"

#define GAIN 0.25f
/* Two blocks in a row over this much of their time shed a voice, and a
   new note where the blocks run near it takes a sounding voice rather
   than a free one. */
#define SHED_AT 0.75f
/* How long the screen must read no finger for one to have lifted. */
#define TOUCH_LIFTED_US 40000
/* How long the side button must read the same to have moved: it
   bounces. */
#define BUTTON_STEADY_US 20000

/* How much of a block the last took, and the last few, smoothed: one
   block running long isn't the blocks running late, the I2S's buffers
   some 45 ms deep. And what the voices started since will add to the
   next, as their patches' costs say. */
static volatile float load = 0, loadAvg = 0, loadAdded = 0;
static volatile int playing = 0;
static volatile int voicesNow = 0;

#define PATCH(type, name, gain) new PatchOf<thv##type>(name, gain),
static Patch *const patches[] = { PATCHES };
#undef PATCH
#define PERC_OF(type) PatchOf<thv##type>
#define PERC_TYPE(type) PERC_OF(type)
static Patch *const percussion = new PERC_TYPE(PERCUSSION)("percussion", PERCUSSION_GAIN);

/* What the screen is. */
enum { ON_KEYS, ON_MARBLES, ON_LIFE, ON_COMPOSER, SCREENS };
static const char *const SCREEN_NAME[SCREENS] = { "keys", "marbles", "life", "composer" };
static volatile int screen = ON_KEYS;
#define NPATCHES (int)(sizeof(patches) / sizeof(patches[0]))

/* The patch playing, and every patch's voices, the audio task's and the
   control task's both, under `lock'. */
static SemaphoreHandle_t lock;

/* The keys fingers are on, a bit each, for the screen; and which patch it
   shows as playing. */
static volatile uint32_t lit = 0;
static volatile int shown = 0;
/* The whole screen drawn again: the keys, the marbles or Life, switched. */
static volatile bool redraw = false;

/* What the screen draws of the marbles, copied under `seen' from the
   control task's at its own pace. */
static portMUX_TYPE seen = portMUX_INITIALIZER_UNLOCKED;
static thgBall shownBalls[thgMarbles::BALLS];
static int shownBallCount = 0;
static thgPeg shownPegs[thgMarbles::PEGS];
static int shownPegCount = 0;
static volatile unsigned pegsChanged = 0;
/* And of Life, under `seen' too. */
static bool shownCells[thgLife::COLS][thgLife::ROWS];
static int shownColumn = -1;
static bool shownPaused = false;
static int shownGeneration = 0;

/* The odd voices, on the other core, while the audio task does the even:
   started by `go', done when it gives `done'. */
static SemaphoreHandle_t go, done;
static float oddMix[2][BLOCK];

/* This block's pools: every patch's, the one playing and any switched
   from, ringing out; the percussion's; and the composer's channels'. Set
   by the audio task before the helper starts. */
static Patch *pools[NPATCHES + 1 + COMPOSER_CHANNELS];
static int poolCount = 0;
volatile int boxNonFinite = 0;

static void
renderTimed (Patch *p, float mix[2][BLOCK], int half)
{
    const int64_t t = esp_timer_get_time();

    p->render(mix[0], mix[1], half);
    p->spent[half] = esp_timer_get_time() - t;
}

static void
helperTask (void *)
{
    for (;;)
    {
        xSemaphoreTake(go, portMAX_DELAY);
        memset(oddMix, 0, sizeof(oddMix));
        for (int i = 0; i < poolCount; i++)
            renderTimed(pools[i], oddMix, 1);
        xSemaphoreGive(done);
    }
}


static void
audioTask (void *)
{
    static float mix[2][BLOCK];
    static int16_t frames[2 * BLOCK];
    int over = 0;

    for (;;)
    {
        memset(mix, 0, sizeof(mix));
        xSemaphoreTake(lock, portMAX_DELAY);

        const int64_t t = esp_timer_get_time();

        composerApply(t);
        poolCount = 0;
        for (Patch *p : patches)
            pools[poolCount++] = p;
        pools[poolCount++] = percussion;
        for (Patch *p : composerChannel)
            if (p)
                pools[poolCount++] = p;
        for (int i = 0; i < poolCount; i++)
            pools[i]->glide();
        xSemaphoreGive(go);
        for (int i = 0; i < poolCount; i++)
            renderTimed(pools[i], mix, 0);
        xSemaphoreTake(done, portMAX_DELAY);
        for (int i = 0; i < BLOCK; i++)
        {
            mix[0][i] += oddMix[0][i];
            mix[1][i] += oddMix[1][i];
        }
        load = (esp_timer_get_time() - t) / (1e6f * BLOCK / BOX_RATE);
        loadAvg += 0.25f * (load - loadAvg);
        loadAdded = 0;
        voicesNow = 0;
        for (int i = 0; i < poolCount; i++)
        {
            pools[i]->account();
            voicesNow += pools[i]->sounding();
        }
        /* Two blocks over shed one voice, whichever pool's goes first;
           the block it fades out over, still paying for it, starts the
           count again from behind. */
        over = load > SHED_AT ? over + 1 : 0;
        if (over >= 2)
        {
            Patch *first = pools[0];

            for (int i = 1; i < poolCount; i++)
                if (pools[i]->oldest() < first->oldest())
                    first = pools[i];
            first->shed();
            over = -1;
        }
        if (load > patches[playing]->peak)
            patches[playing]->peak = load;
        patches[playing]->late += load > 1;
        xSemaphoreGive(lock);

        for (int i = 0; i < BLOCK; i++)
            for (int c = 0; c < 2; c++)
            {
                /* A NaN would clamp to -1, an infinity to 1, a full-scale DC:
                   silence. */
                const float y = mix[c][i] * GAIN;
                const float x = std::isfinite(y) ? fminf(1, fmaxf(-1, y)) : 0;

                boxNonFinite += !std::isfinite(y);

                frames[2 * i + c] = (int16_t)(x * 32000);
            }
        box_audio_write(frames, BLOCK);
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

/* C, E flat, F, G, B flat. */
static const int HUE[5][3] = {
    { 230, 70, 70 }, { 230, 160, 40 }, { 200, 210, 60 }, { 60, 190, 120 },
    { 70, 120, 230 } };

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

    for (int y0 = TOP; y0 < H; y0 += STRIP)
    {
        uint16_t *strip = strips[((y0 - TOP) / STRIP) % 2];

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
        box_lcd_draw(from, y0, to, y0 + STRIP, strip);
    }
}

/* The patches along the top, a square each, the playing one lit; at the
   right end, Life's paused as a pause sign. */
static void
drawPatches (int n, int on, bool paused)
{
    static uint16_t *row = NULL;

    if (!row)
        row = (uint16_t *)heap_caps_malloc(W * TOP * 2, MALLOC_CAP_DMA);
    if (!row)
        return;
    for (int y = 0; y < TOP; y++)
        for (int x = 0; x < W; x++)
        {
            const int p = (x - 6) / 20, in = (x - 6) % 20;
            const bool square = p >= 0 && p < n && in < 14 && y >= 2 && y < TOP - 2;
            const bool bar = paused && y >= 3 && y < TOP - 3 &&
                             ((x >= 301 && x < 305) || (x >= 308 && x < 312));

            row[y * W + x] = bar ? rgb(255, 255, 255)
                           : !square ? rgb(0, 0, 0)
                           : p == on ? rgb(255, 255, 255) : rgb(70, 70, 70);
        }
    box_lcd_draw(0, 0, W, TOP, row);
}

/* The marbles' field, below the patches' row: a pixel of it, the balls
   white, the pegs their column's note's color, and the columns behind
   them dimly, so where a peg would play is in sight. */
static thgBall frameBalls[thgMarbles::BALLS];
static int frameBallCount = 0;
static thgPeg framePegs[thgMarbles::PEGS];
static int framePegCount = 0;

static uint16_t
fieldAt (int x, int y)
{
    const float wy = (float)(y - TOP);

    for (int i = 0; i < frameBallCount; i++)
    {
        const float dx = x - frameBalls[i].x, dy = wy - frameBalls[i].y;

        if (dx * dx + dy * dy <= thgMarbles::BALL_R * thgMarbles::BALL_R)
            return rgb(255, 255, 255);
    }
    for (int i = 0; i < framePegCount; i++)
    {
        const float dx = x - framePegs[i].x, dy = wy - framePegs[i].y;
        const float d2 = dx * dx + dy * dy, r = thgMarbles::PEG_R;

        if (d2 <= r * r)
        {
            const int *h = HUE[keyAt((int)framePegs[i].x) % 5];
            const float f = d2 > (r - 1.5f) * (r - 1.5f) ? 0.6f : 1;

            return rgb((int)(h[0] * f), (int)(h[1] * f), (int)(h[2] * f));
        }
    }

    const int *h = HUE[keyAt(x) % 5];

    return rgb(h[0] / 6, h[1] / 6, h[2] / 6);
}

/* A rectangle of the field drawn, a strip of rows at a time. */
static void
drawField (int x0, int y0, int x1, int y1)
{
    /* Two, as the keys' strips, so one is filled while the other is sent. */
    static uint16_t *bufs[2] = { NULL, NULL };
    static int next = 0;

    x0 = x0 < 0 ? 0 : x0;
    x1 = x1 > W ? W : x1;
    y0 = y0 < TOP ? TOP : y0;
    y1 = y1 > H ? H : y1;
    if (x1 <= x0 || y1 <= y0)
        return;
    for (int b = 0; b < 2; b++)
        if (!bufs[b])
            bufs[b] = (uint16_t *)heap_caps_malloc(W * STRIP * 2, MALLOC_CAP_DMA);
    if (!bufs[0] || !bufs[1])
        return;
    for (int ya = y0; ya < y1; ya += STRIP)
    {
        const int yb = ya + STRIP < y1 ? ya + STRIP : y1;
        uint16_t *buf = bufs[next];

        next ^= 1;

        for (int y = ya; y < yb; y++)
            for (int x = x0; x < x1; x++)
                buf[(y - ya) * (x1 - x0) + (x - x0)] = fieldAt(x, y);
        box_lcd_draw(x0, ya, x1, yb, buf);
    }
}

/* The marbles as they are now, copied; the places the balls left and
   reached drawn again, or all of it when the pegs moved. */
static void
drawMarbles (bool all)
{
    static thgBall last[thgMarbles::BALLS];
    static int lastCount = 0;
    static unsigned pegsSeen = 0;

    portENTER_CRITICAL(&seen);
    const unsigned pegsNow = pegsChanged;

    frameBallCount = shownBallCount;
    for (int i = 0; i < frameBallCount; i++)
        frameBalls[i] = shownBalls[i];
    framePegCount = shownPegCount;
    for (int i = 0; i < framePegCount; i++)
        framePegs[i] = shownPegs[i];
    portEXIT_CRITICAL(&seen);

    if (all || pegsSeen != pegsNow)
    {
        pegsSeen = pegsNow;
        drawField(0, TOP, W, H);
    }
    else
    {
        const int r = (int)thgMarbles::BALL_R + 2;

        for (int i = 0; i < frameBallCount || i < lastCount; i++)
        {
            float xa, ya, xb, yb;

            if (i < frameBallCount && i < lastCount)
            {
                xa = fminf(last[i].x, frameBalls[i].x);
                xb = fmaxf(last[i].x, frameBalls[i].x);
                ya = fminf(last[i].y, frameBalls[i].y);
                yb = fmaxf(last[i].y, frameBalls[i].y);
            }
            else
            {
                const thgBall &b = i < frameBallCount ? frameBalls[i] : last[i];

                xa = xb = b.x;
                ya = yb = b.y;
            }
            drawField((int)xa - r, (int)ya - r + TOP, (int)xb + r + 1, (int)yb + r + 1 + TOP);
        }
    }
    lastCount = frameBallCount;
    for (int i = 0; i < frameBallCount; i++)
        last[i] = frameBalls[i];
}

/* Life's board, below the patches' row: a cell 20 by 28, alive its row's
   note's color, dead a dim one, the playhead's column lighter. */
#define CELL_W (W / thgLife::COLS)
#define CELL_H ((H - TOP) / thgLife::ROWS)

static void
drawCell (int c, int r, bool alive, bool head)
{
    /* Two, as the keys' strips, so one is filled while the other is sent. */
    static uint16_t *bufs[2] = { NULL, NULL };
    static int next = 0;
    const int *h = HUE[r % 5];
    const int x0 = c * CELL_W, y0 = TOP + (thgLife::ROWS - 1 - r) * CELL_H;

    for (int b = 0; b < 2; b++)
        if (!bufs[b])
            bufs[b] = (uint16_t *)heap_caps_malloc(CELL_W * CELL_H * 2, MALLOC_CAP_DMA);
    if (!bufs[0] || !bufs[1])
        return;

    uint16_t *buf = bufs[next];
    const uint16_t fill = alive && head ? rgb(240 - (240 - h[0]) / 3, 240 - (240 - h[1]) / 3,
                                              240 - (240 - h[2]) / 3)
                        : alive ? rgb(h[0], h[1], h[2])
                        : head ? rgb(h[0] / 3, h[1] / 3, h[2] / 3)
                               : rgb(h[0] / 6, h[1] / 6, h[2] / 6);

    next ^= 1;
    for (int y = 0; y < CELL_H; y++)
        for (int x = 0; x < CELL_W; x++)
            buf[y * CELL_W + x] = x == 0 || y == 0 ? rgb(0, 0, 0) : fill;
    box_lcd_draw(x0, y0, x0 + CELL_W, y0 + CELL_H, buf);
}

/* The cells whose life or playhead changed since they were drawn, or all. */
static void
drawLife (bool all)
{
    static uint8_t drawn[thgLife::COLS][thgLife::ROWS];
    bool cells[thgLife::COLS][thgLife::ROWS];
    int column;

    portENTER_CRITICAL(&seen);
    memcpy(cells, shownCells, sizeof(cells));
    column = shownColumn;
    portEXIT_CRITICAL(&seen);

    for (int c = 0; c < thgLife::COLS; c++)
        for (int r = 0; r < thgLife::ROWS; r++)
        {
            const uint8_t now = cells[c][r] | (c == column) << 1;

            if (all || drawn[c][r] != now)
            {
                drawn[c][r] = now;
                drawCell(c, r, cells[c][r], c == column);
            }
        }
}

static void
displayTask (void *)
{
    uint32_t keysShown = 0;
    int patchShown = -1;
    bool pausedShown = false;

    for (int k = 0; k < KEYS; k++)
        drawKey(k, false);
    for (;;)
    {
        const uint32_t now = lit;
        /* Cleared only where it was read set: a request between the
           reading and the clearing would be lost. */
        const bool again = redraw;

        if (again)
            redraw = false;

        const int on = screen;
        const bool paused = on == ON_LIFE && shownPaused;

        /* The whole screen is its own, the patches' row too: drawn again
           after. */
        if (on == ON_COMPOSER)
        {
            composerDraw(again);
            patchShown = -1;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (shown != patchShown || paused != pausedShown)
        {
            patchShown = shown;
            pausedShown = paused;
            drawPatches(NPATCHES, patchShown, pausedShown);
        }
        if (again)
        {
            keysShown = 0;
            if (on == ON_KEYS)
                for (int k = 0; k < KEYS; k++)
                    drawKey(k, false);
            else if (on == ON_MARBLES)
                drawMarbles(true);
            else
                drawLife(true);
        }
        if (on == ON_MARBLES)
        {
            drawMarbles(false);
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }
        if (on == ON_LIFE)
        {
            drawLife(false);
            vTaskDelay(pdMS_TO_TICKS(15));
            continue;
        }

        for (int k = 0; k < KEYS; k++)
            if (((now ^ keysShown) >> k) & 1)
                drawKey(k, (now >> k) & 1);
        keysShown = now;
        vTaskDelay(pdMS_TO_TICKS(15));
    }
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

void
boxPlay (Patch *p, const thhAction &a, int owner, float level)
{
    if (p->play(a, owner, esp_timer_get_time(),
                loadAvg + loadAdded + p->perVoice / 2 <= SHED_AT, level))
        loadAdded += p->perVoice / 2;
}

/* What a finger's actions do, under the lock: owner, which finger. */
static void
playAll (const std::vector<thhAction> &acts, int owner)
{
    if (acts.empty())
        return;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (const thhAction &a : acts)
        boxPlay(a.channel == 1 ? percussion : patches[playing], a, owner, 1);
    xSemaphoreGive(lock);
}

/* A ball's mapper, its controls what it strikes, and when its `hit' and
   `wall' let go: held a little, so a sustained patch's note is a note. */
#define HIT_HELD_US 120000

static struct Ball
{
    thhState  s;
    thhMapper mapper;
    int64_t   until[2] = { 0, 0 };
} ball[thgMarbles::BALLS];

/* Life's, a mapper a row: a hundred kilobytes, for the heap and its PSRAM. */
static thgLifePlayer *const player = new thgLifePlayer;
/* Its notes' owner, after the fingers' and the balls': its rows each
   play a note of their own. */
#define LIFE_OWNER (FINGERS + thgMarbles::BALLS)

/* Every finger's mapper given the playing patch's profile for the keys,
   every ball's for the marbles and Life's player its, the notes they held
   let go. */
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
    for (Ball &b : ball)
    {
        acts.clear();
        b.mapper.release(acts);
        playAll(acts, FINGERS + (int)(&b - ball));
        acts.clear();
        if (!b.mapper.configure(patches[playing]->marbles(), thgMarblesControls(), acts,
                                err, line))
            printf("thinkbox: %s's marbles profile doesn't fit, line %d: %s\n",
                   patches[playing]->name, line, err.c_str());
        b.s.connected = true;
    }
    acts.clear();
    player->release(acts);
    playAll(acts, LIFE_OWNER);
    acts.clear();
    if (!player->configure(patches[playing]->life(), acts, err, line))
        printf("thinkbox: %s's life profile doesn't fit, line %d: %s\n",
               patches[playing]->name, line, err.c_str());
    printf("thinkbox: %s, %s\n", patches[playing]->name, SCREEN_NAME[screen]);
}

/* The marbles, the control task's. */
static thgMarbles marbles(W, H - TOP, 0x5eed);

/* Gravity in pixels a second squared at one g, and a shake: more than
   this many g at once. */
#define MARBLE_G 900.0f
#define SHAKE_G 2.0f

static bool
shaken (const float accel[3], int64_t now)
{
    static int64_t last = 0;

    if (sqrtf(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]) <= SHAKE_G ||
        now - last <= 400000)
        return false;
    last = now;
    return true;
}

/* One step of the marbles: gravity from the IMU in the screen's axes,
   a tap, a shake, and what the balls strike played. Upright facing you,
   the sensor's +x is up, and its +y the screen's right. */
static void
playMarbles (const thhBoxImu &imu, const float accel[3], bool tapped, float tx, float ty,
             int64_t now)
{
    std::vector<thgHit> hits;
    std::vector<thhAction> acts;
    const float gx = -imu.g[1] * MARBLE_G, gy = imu.g[0] * MARBLE_G;

    /* The patches' row is no part of the field. */
    const bool onField = tapped && ty >= TOP;

    if (onField)
        marbles.tap(tx, ty - TOP);
    if (shaken(accel, now))
        marbles.shake();
    marbles.step(gx, gy, 0.004f, hits);

    for (const thgHit &h : hits)
    {
        Ball &b = ball[h.ball];
        const int which = h.peg >= 0 ? THG_HIT : THG_WALL;

        acts.clear();

        b.s.axis[THG_X] = h.x / marbles.width();
        b.s.axis[THG_Y] = h.y / marbles.height();
        b.s.axis[THG_SPEED] = fminf(1, h.speed / 800);
        /* Struck again while held: let go first, so it's played again. */
        if (b.s.buttons & (1u << which))
        {
            b.s.buttons &= ~(1u << which);
            b.s.time = now / 1e6;
            b.mapper.update(b.s, 0, acts);
        }
        b.s.buttons |= 1u << which;
        b.until[which] = now + HIT_HELD_US;
        playAll(acts, FINGERS + h.ball);
    }
    for (Ball &b : ball)
    {
        for (int which : { THG_HIT, THG_WALL })
            if (now > b.until[which])
                b.s.buttons &= ~(1u << which);
        b.s.time = now / 1e6 + 1e-4;
        acts.clear();
        b.mapper.update(b.s, 0, acts);
        playAll(acts, FINGERS + (int)(&b - ball));
    }

    portENTER_CRITICAL(&seen);
    /* Bumped with the copy, so the screen never sees the one without
       the other. */
    pegsChanged += onField;
    shownBallCount = (int)marbles.balls().size();
    for (int i = 0; i < shownBallCount; i++)
        shownBalls[i] = marbles.balls()[i];
    shownPegCount = (int)marbles.pegs().size();
    for (int i = 0; i < shownPegCount; i++)
        shownPegs[i] = marbles.pegs()[i];
    portEXIT_CRITICAL(&seen);
}

/* Life, the control task's, started with a glider and a blinker, so it
   plays at once. */
static thgLife life(0x5eed);

/* One step of Life: the finger painting it, a tap on the patches' row its
   pause, a shake, and the column the playhead reached played. A finger is
   the first contact, down again only after TOUCH_LIFTED_US of none, as a
   key's: a press toggles its cell. */
static void
playLife (const float accel[3], float dt, int count, const box_hal_contact &at, int64_t now)
{
    static int64_t seenAt = -TOUCH_LIFTED_US - 1;
    static bool onTop = false;
    static int col = -1, row = -1;
    std::vector<thgCell> cells;
    std::vector<thhAction> acts;

    if (count > 0)
    {
        int c = -1, r = -1;
        const bool on = thgLife::cellAt(at.x, at.y - TOP, W, H - TOP, c, r);

        if (now - seenAt > TOUCH_LIFTED_US)
        {
            onTop = at.y < TOP;
            if (onTop)
                life.pause(!life.paused());
            else if (on)
                life.press(c, r);
            col = !onTop && on ? c : -1;
            row = r;
        }
        else if (col >= 0 && on && (c != col || r != row))
        {
            /* The cells between too, so a quick stroke is still a line. */
            const int n = abs(c - col) > abs(r - row) ? abs(c - col) : abs(r - row);

            for (int i = 1; i <= n; i++)
                life.drag(col + (int)lroundf((c - col) * i / (float)n),
                          row + (int)lroundf((r - row) * i / (float)n));
            col = c;
            row = r;
        }
        seenAt = now;
    }
    if (shaken(accel, now))
        life.shake();
    if (life.step(dt, cells))
    {
        player->play(cells, now / 1e6, 0, acts);
        playAll(acts, LIFE_OWNER);
    }

    portENTER_CRITICAL(&seen);
    for (int c = 0; c < thgLife::COLS; c++)
        for (int r = 0; r < thgLife::ROWS; r++)
            shownCells[c][r] = life.alive(c, r);
    shownColumn = life.column();
    shownPaused = life.paused();
    shownGeneration = life.generation();
    portEXIT_CRITICAL(&seen);
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

    bool down = false, wasDown = false, reads = false;
    int64_t readSince = 0;

    for (;; n++)
    {
        const int64_t now = esp_timer_get_time();
        float accel[3] = { 1, 0, 0 }, gyro[3] = { 0, 0, 0 };

        const float dt = (now - last) / 1e6f;

        if (box_imu_read(accel, gyro))
            thhBoxFeedImu(accel, gyro, dt, imu, pose);
        last = now;

        /* A press steps to the next patch, let go, the notes held let go
           and left to ring out, or on the composer's screen to the next
           piece; held, it switches to the next screen: the keys, the
           marbles, Life, the composer. */
        const bool reading = box_button_down(BOX_HAL_SIDE);
        static int64_t pressed = 0;
        static bool switched = false;

        if (reading != reads)
        {
            reads = reading;
            readSince = now;
        }
        if (now - readSince >= BUTTON_STEADY_US)
            down = reads;
        if (down && !wasDown)
        {
            pressed = now;
            switched = false;
        }
        if (down && !switched && now - pressed > 600000)
        {
            switched = true;
            for (Finger &f : finger)
            {
                acts.clear();
                f.mapper.release(acts);
                playAll(acts, (int)(&f - finger));
            }
            screen = (screen + 1) % SCREENS;
            composerShow(screen == ON_COMPOSER);
            lit = 0;
            redraw = true;
            configureAll();
        }
        if (!down && wasDown && !switched && screen == ON_COMPOSER)
            composerNext();
        else if (!down && wasDown && !switched)
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
        box_hal_contact points[FINGERS];
        static bool wasHome = false;
        const int count = box_touch_read(points, FINGERS);
        const bool home = box_button_down(BOX_HAL_HOME);

        /* The red button: the pose it's in now is level. */
        if (home && !wasHome)
            thhBoxLevel(imu);
        wasHome = home;

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

        if (screen == ON_COMPOSER)
        {
            vTaskDelay(pdMS_TO_TICKS(4));
            continue;
        }
        if (screen == ON_LIFE)
        {
            playLife(accel, dt, count, points[0], now);
            if (n % 125 == 0)
                printf("%s life, load %3.0f%%  generation %d%s\n", patches[playing]->name,
                       load * 100, life.generation(), life.paused() ? ", paused" : "");
            vTaskDelay(pdMS_TO_TICKS(4));
            continue;
        }
        if (screen == ON_MARBLES)
        {
            /* A tap a touch, down again only after TOUCH_LIFTED_US of
               none, as a key's: the controller reports one in bursts. */
            static int64_t touchedAt = -TOUCH_LIFTED_US - 1;
            const bool tapped = count > 0 && now - touchedAt > TOUCH_LIFTED_US;

            if (count > 0)
                touchedAt = now;
            playMarbles(imu, accel, tapped, points[0].x, points[0].y, now);
            if (n % 125 == 0)
                printf("%s marbles, load %3.0f%%  gravity %+.2f %+.2f %+.2f\n",
                       patches[playing]->name, load * 100, imu.g[0], imu.g[1], imu.g[2]);
            vTaskDelay(pdMS_TO_TICKS(4));
            continue;
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
                printf("  %s %.0f%% %d late", patches[p]->name, patches[p]->peak * 100,
                       patches[p]->late);
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

/* For the console's `status'. */
static void
status (char *line, size_t size)
{
    const int on = screen;
    const int n = snprintf(line, size, "screen %s  patch %s (%d of %d)  voices %d  load %.0f%%",
                           SCREEN_NAME[on], patches[playing]->name, playing + 1, NPATCHES,
                           voicesNow, load * 100);

    if (on == ON_LIFE && n > 0 && n < (int)size)
    {
        portENTER_CRITICAL(&seen);
        const int g = shownGeneration;
        const bool p = shownPaused;
        portEXIT_CRITICAL(&seen);

        snprintf(line + n, size - n, "  generation %d%s", g, p ? ", paused" : "");
    }
    if (on == ON_COMPOSER && n > 0 && n < (int)size)
        composerStatus(line + n, size - n);
}

extern "C" void
app_main (void)
{
    debugcon_start(BOX_RATE, DEBUGCON_TOUCH | DEBUGCON_HOME | DEBUGCON_SHAKE, status);

    const unsigned opened = box_hal_open(BOX_HAL_SPEAKER | BOX_HAL_IMU | BOX_HAL_TOUCH |
                                         BOX_HAL_SIDE_BUTTON, BOX_RATE, 70);
    /* With nothing to write to, the audio task would never wait. */
    const bool speaker = opened & BOX_HAL_SPEAKER;

    if (!speaker)
        printf("thinkbox: no speaker\n");
    if (!(opened & BOX_HAL_IMU))
        printf("thinkbox: no IMU; tilt and roll stay level\n");

    marbles.scatter(6);
    marbles.shake();
    life.board(".O............../"
               "..O............./"
               "OOO............./"
               "................/"
               "................/"
               "...........OOO../");
    lock = xSemaphoreCreateMutex();
    go = xSemaphoreCreateBinary();
    done = xSemaphoreCreateBinary();
    configASSERT(lock && go && done);
    printf("thinkbox: %d patches, %d voices each at %d Hz\n", NPATCHES, VOICES, BOX_RATE);
    if (speaker)
    {
        xTaskCreatePinnedToCore(audioTask, "audio", 8192, NULL, 20, NULL, 1);
        composerStart();
    }
    xTaskCreatePinnedToCore(helperTask, "helper", 8192, NULL, 19, NULL, 0);
    xTaskCreatePinnedToCore(controlTask, "control", 8192, NULL, 10, NULL, 0);
    if (box_hal_open(BOX_HAL_SCREEN, BOX_RATE, 70))
        xTaskCreatePinnedToCore(displayTask, "display", 4096, NULL, 5, NULL, 0);
    else
        printf("thinkbox: no screen\n");
}
