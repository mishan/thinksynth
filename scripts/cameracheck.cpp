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

/*
 * cameracheck -- the camera reader, fed JPEGs this file encodes itself.
 *
 * No camera is opened: a source here is a list of frames, and what happens
 * when it runs dry, breaks or comes back is the test's to say.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include <jpeglib.h>

#include "thiCamera.h"

using std::string;
using Clock = std::chrono::steady_clock;

static int failed = 0;

static void check (const char *name, bool ok, const string &why = "")
{
    if (ok)
        printf("ok    %s\n", name);
    else
    {
        printf("FAIL  %s\n  %s\n", name, why.c_str());
        failed++;
    }
}

static string fmt (const char *f, double a, double b = 0, double c = 0)
{
    char s[256];
    snprintf(s, sizeof s, f, a, b, c);
    return s;
}

/* A grayscale JPEG of `draw(x, y)'. */
template <typename Draw>
static std::vector<uint8_t> encode (int w, int h, Draw draw)
{
    jpeg_compress_struct c;
    jpeg_error_mgr e;
    unsigned char *out = NULL;
    unsigned long size = 0;
    std::vector<uint8_t> row(w);

    c.err = jpeg_std_error(&e);
    jpeg_create_compress(&c);
    jpeg_mem_dest(&c, &out, &size);
    c.image_width = w;
    c.image_height = h;
    c.input_components = 1;
    c.in_color_space = JCS_GRAYSCALE;
    jpeg_set_defaults(&c);
    jpeg_set_quality(&c, 90, TRUE);
    jpeg_start_compress(&c, TRUE);
    while (c.next_scanline < c.image_height)
    {
        for (int x = 0; x < w; x++)
            row[x] = draw(x, (int)c.next_scanline);

        JSAMPROW r = row.data();
        jpeg_write_scanlines(&c, &r, 1);
    }
    jpeg_finish_compress(&c);
    jpeg_destroy_compress(&c);

    std::vector<uint8_t> jpeg(out, out + size);
    free(out);
    return jpeg;
}

/* A bright square at (cx, cy) on a dark ground, in a w x h picture. */
static std::vector<uint8_t> square (int w, int h, double cx, double cy)
{
    return encode(w, h, [&] (int x, int y) {
        return fabs(x - cx) < w / 20.0 && fabs(y - cy) < w / 20.0 ? 220 : 40;
    });
}

static void decoding (void)
{
    /* Left half dark, right half bright, at sizes a camera gives. */
    static const struct { int w, h, wantW, wantH; } rows[] = {
        { 640, 480, 160, 120 },
        { 320, 240, 160, 120 },
        { 1280, 720, 160, 90 },
        { 100, 80, 100, 80 },
    };

    for (const auto &r : rows)
    {
        std::vector<uint8_t> jpeg = encode(r.w, r.h, [&] (int x, int) {
            return x < r.w / 2 ? 30 : 200;
        });
        std::vector<uint8_t> luma;
        int w = 0, h = 0;
        const bool ok = thiDecodeLuma(jpeg.data(), jpeg.size(), 160, luma, w, h);
        const bool sized = ok && w == r.wantW && h == r.wantH && luma.size() == (size_t)w * h;
        const int left = sized ? luma[h / 2 * w + w / 4] : -1;
        const int right = sized ? luma[h / 2 * w + 3 * w / 4] : -1;
        char name[96];

        snprintf(name, sizeof name, "a %dx%d frame decodes to %dx%d", r.w, r.h, r.wantW, r.wantH);
        check(name, sized && abs(left - 30) <= 3 && abs(right - 200) <= 3,
              fmt("%.0fx%.0f, left %.0f", w, h, left) + fmt(", right %.0f", right));
    }

    std::vector<uint8_t> luma;
    int w = 0, h = 0;
    const uint8_t junk[] = { 0xff, 0xd8, 0xff, 0x00, 1, 2, 3, 4 };

    check("bytes that aren't a JPEG don't decode",
          !thiDecodeLuma(junk, sizeof junk, 160, luma, w, h));

    /* Stray bytes before a marker, as some webcams send in every frame:
       libjpeg warns, and the picture is whole. */
    std::vector<uint8_t> stray = square(640, 480, 320, 240);
    const size_t app0End = 4 + (stray[4] << 8 | stray[5]);

    stray.insert(stray.begin() + app0End, { 0x01, 0x02, 0x03 });
    check("a frame with stray bytes before a marker decodes",
          thiDecodeLuma(stray.data(), stray.size(), 160, luma, w, h) && w == 160 && h == 120);

    std::vector<uint8_t> cut = square(640, 480, 320, 240);

    cut.resize(cut.size() * 3 / 4);
    check("a frame cut short doesn't decode",
          !thiDecodeLuma(cut.data(), cut.size(), 160, luma, w, h));
}

/* Frames handed out as a camera would, until the list runs out; then the
   source fails if `dies', or waits. */
class ListSource : public thiSource
{
public:
    struct Frame { std::vector<uint8_t> jpeg; double seconds; };

    ListSource (std::deque<Frame> frames, bool dies, std::atomic<bool> *dry = NULL)
        : frames_(std::move(frames)), dies_(dies), dry_(dry) { }

    bool next (std::vector<uint8_t> &jpeg, double &seconds, int waitMs,
               string &status) override
    {
        if (frames_.empty())
        {
            if (dry_)
                *dry_ = true;
            if (dies_)
            {
                status = "unplugged";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
            return false;
        }
        jpeg = frames_.front().jpeg;
        seconds = frames_.front().seconds;
        frames_.pop_front();
        return true;
    }

private:
    std::deque<Frame> frames_;
    bool dies_;
    std::atomic<bool> *dry_;
};

/* A square crossing a w x h picture at 120 fps, from `from' seconds. */
static std::deque<ListSource::Frame> crossing (int n, double from = 0, int w = 640, int h = 480)
{
    std::deque<ListSource::Frame> frames;

    for (int i = 0; i < n; i++)
        frames.push_back({ square(w, h, w * (0.125 + 0.625 * i / n), h / 2.0), from + i / 120.0 });

    return frames;
}

/* Waits until `done' or `ms' pass. */
template <typename Done>
static bool within (int ms, Done done)
{
    const auto until = Clock::now() + std::chrono::milliseconds(ms);

    while (!done())
    {
        if (Clock::now() > until)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

static void reading (void)
{
    /* Every frame reaches the drainer, in order, and the square is seen
       moving from the player's right toward their left. */
    {
        int connects = 0;
        std::atomic<int> wakes(0);
        thiCamera cam([&] (string &) {
            return connects++ ? nullptr : std::unique_ptr<thiSource>(new ListSource(crossing(40), false));
        }, [&] { wakes++; });
        std::vector<thiAxes> seen;

        within(3000, [&] {
            std::vector<thiAxes> f;
            if (cam.drain(f))
                seen.insert(seen.end(), f.begin(), f.end());
            return seen.size() >= 40;
        });

        bool ordered = true, moving = false;

        for (size_t i = 1; i < seen.size(); i++)
        {
            ordered = ordered && seen[i].seconds > seen[i - 1].seconds && seen[i].connected;
            moving = moving || seen[i].axis[thMotion::MOTION] > 0;
        }

        check("every frame is drained, in order", seen.size() == 40 && ordered,
              fmt("%.0f frames drained", seen.size()));
        check("a crossing square is seen moving toward the player's left",
              moving && !seen.empty() && seen.back().axis[thMotion::X] < 0.5,
              fmt("last x %.3f", seen.empty() ? -1 : seen.back().axis[thMotion::X]));
    }

    /* A drainer that comes late gets the newest KEPT frames, oldest first. */
    {
        std::atomic<int> wakes(0);
        std::atomic<bool> dry(false);
        std::deque<ListSource::Frame> frames = crossing(200);
        bool once = false;
        thiCamera cam([&] (string &) {
            if (once)
                return std::unique_ptr<thiSource>();
            once = true;
            return std::unique_ptr<thiSource>(new ListSource(frames, false, &dry));
        }, [&] { wakes++; });
        std::vector<thiAxes> f;

        /* Dry is said after the last frame is published. */
        within(30000, [&] { return dry.load(); });
        cam.drain(f);

        check("a late drain gets the newest frames, oldest first",
              f.size() == thiCamera::KEPT && fabs(f.back().seconds - 199 / 120.0) < 1e-9
              && fabs(f.front().seconds - (200 - (double)thiCamera::KEPT) / 120.0) < 1e-9,
              fmt("%.0f frames, from %.4f s to %.4f s", f.size(),
                  f.empty() ? 0 : f.front().seconds, f.empty() ? 0 : f.back().seconds));
        check("wake is called once until a drain", wakes == 1, fmt("%.0f wakes", wakes));
    }
}

/* Frames held back until the test lets them out. */
class GatedSource : public thiSource
{
public:
    GatedSource (std::atomic<int> &open) : frames_(crossing(20)), open_(open), given_(0) { }

    bool next (std::vector<uint8_t> &jpeg, double &seconds, int waitMs, string &) override
    {
        if (given_ >= open_ || given_ >= (int)frames_.size())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(waitMs, 5)));
            return false;
        }
        jpeg = frames_[given_].jpeg;
        seconds = frames_[given_].seconds;
        given_++;
        return true;
    }

private:
    std::deque<ListSource::Frame> frames_;
    std::atomic<int> &open_;
    int given_;
};

static void waking (void)
{
    std::atomic<int> open(5), wakes(0);
    bool once = false;
    thiCamera cam([&] (string &) {
        if (once)
            return std::unique_ptr<thiSource>();
        once = true;
        return std::unique_ptr<thiSource>(new GatedSource(open));
    }, [&] { wakes++; });
    std::vector<thiAxes> f;

    within(2000, [&] { return wakes > 0; });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const int first = wakes;

    cam.drain(f);
    open = 10;
    within(2000, [&] { return wakes > 1; });

    check("wake is called once until a drain, and again after one",
          first == 1 && wakes == 2, fmt("%.0f wakes before the drain, %.0f after", first, wakes));
}

static void startingSlowly (void)
{
    /* A scan that passed a busy camera on its way to this one says so; a
       camera slow to its first frame is still there. */
    std::atomic<int> open(0);
    bool once = false;
    thiCamera cam([&] (string &status) {
        status = "/dev/video0: in use";
        if (once)
            return std::unique_ptr<thiSource>();
        once = true;
        return std::unique_ptr<thiSource>(new GatedSource(open));
    }, [] { });
    std::vector<thiAxes> seen;

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    open = 5;
    within(2000, [&] {
        std::vector<thiAxes> f;
        if (cam.drain(f))
            seen.insert(seen.end(), f.begin(), f.end());
        return seen.size() >= 5;
    });

    bool connected = true;

    for (const auto &a : seen)
        connected = connected && a.connected;
    check("a camera slow to its first frame is not a disconnect",
          seen.size() == 5 && connected && cam.status().empty(),
          fmt("%.0f frames", seen.size()) + ", status \"" + cam.status() + "\"");
}

static void swapping (void)
{
    /* Another camera, plugged in at once with stamps running on and a
       picture of its own: its first frame is not a gesture. */
    int connects = 0;
    thiCamera cam([&] (string &) {
        connects++;
        if (connects == 1)
            return std::unique_ptr<thiSource>(new ListSource(crossing(10), true));
        /* The same ground with a bright patch over a fifth of it: too little
           change for the noise floor to rise over. */
        std::deque<ListSource::Frame> other;
        for (int i = 0; i < 10; i++)
            other.push_back({ encode(640, 480, [] (int x, int y) { return x < 320 && y < 192 ? 200 : 40; }),
                              (10 + i) / 120.0 + 0.05 });
        return std::unique_ptr<thiSource>(new ListSource(other, false));
    }, [] { }, 1);
    std::vector<thiAxes> seen;

    within(3000, [&] {
        std::vector<thiAxes> f;
        if (cam.drain(f))
            seen.insert(seen.end(), f.begin(), f.end());
        return seen.size() >= 21;
    });

    double most = 0;

    for (size_t i = 11; i < seen.size(); i++)
        most = std::max(most, (double)seen[i].axis[thMotion::MOTION]);

    check("a camera swapped in is not a gesture", seen.size() == 21 && most == 0,
          fmt("%.0f frames, motion reached %.3f", seen.size(), most));
}

static void unplugging (void)
{
    /* The camera goes after its frames, and comes back a retry later, at
       another size. */
    int connects = 0;
    thiCamera cam([&] (string &status) {
        connects++;
        if (connects == 2)
        {
            status = "no camera";
            return std::unique_ptr<thiSource>();
        }
        return std::unique_ptr<thiSource>(connects == 1
            ? new ListSource(crossing(20, 0), true)
            : new ListSource(crossing(20, 10, 1280, 720), false));
    }, [] { }, 300);
    std::vector<thiAxes> seen;
    bool gone = false, stillWhenGone = false, statusWhenGone = false;
    int goneFrames = 0;

    within(3000, [&] {
        std::vector<thiAxes> f;
        if (cam.drain(f))
            for (const auto &a : f)
            {
                goneFrames += !a.connected;
                if (!a.connected && !gone)
                {
                    gone = true;
                    stillWhenGone = a.axis[thMotion::MOTION] == 0 && a.axis[thMotion::ZONE6] == 0;
                    statusWhenGone = !cam.status().empty();
                }
                seen.push_back(a);
            }
        return !seen.empty() && seen.back().connected && seen.back().seconds >= 10 + 19 / 120.0;
    });

    check("an unplugged camera is heard once, still, with a reason",
          goneFrames == 1 && stillWhenGone && statusWhenGone,
          fmt("%.0f frames unplugged", goneFrames));
    check("a camera that comes back at another size is read again",
          !seen.empty() && seen.back().connected && seen.back().seconds >= 10
          && seen.back().axis[thMotion::MOTION] > 0 && fabs(seen.back().axis[thMotion::Y] - 0.5) < 0.05
          && cam.status().empty(),
          fmt("%.0f frames, last at %.3f s", seen.size(), seen.empty() ? 0 : seen.back().seconds)
          + fmt(", y %.3f", seen.empty() ? 0 : seen.back().axis[thMotion::Y]));
}

static void corruptFrame (void)
{
    /* One frame in the middle that isn't a JPEG. */
    std::deque<ListSource::Frame> frames = crossing(10);

    frames[5].jpeg.assign(64, 0xff);

    bool once = false;
    thiCamera cam([&] (string &) {
        if (once)
            return std::unique_ptr<thiSource>();
        once = true;
        return std::unique_ptr<thiSource>(new ListSource(frames, false));
    }, [] { });
    std::vector<thiAxes> seen;

    within(2000, [&] {
        std::vector<thiAxes> f;
        if (cam.drain(f))
            seen.insert(seen.end(), f.begin(), f.end());
        return seen.size() >= 9;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<thiAxes> more;
    cam.drain(more);
    seen.insert(seen.end(), more.begin(), more.end());

    bool connected = true;
    for (const auto &a : seen)
        connected = connected && a.connected;

    check("a frame that won't decode is skipped, not a disconnect",
          seen.size() == 9 && connected, fmt("%.0f frames", seen.size()));
}

static void stopping (void)
{
    /* No camera, ever: it goes away promptly all the same. */
    const auto start = Clock::now();
    {
        std::atomic<int> asked(0);
        thiCamera cam([&] (string &status) {
            asked++;
            status = "no camera";
            return std::unique_ptr<thiSource>();
        }, [] { });

        within(1000, [&] { return !cam.status().empty(); });
        check("a camera that can't connect says why", cam.status() == "no camera",
              "status \"" + cam.status() + "\"");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        check("a camera that can't connect is asked again only a retry later",
              asked == 1, fmt("asked %.0f times in 300 ms", asked));
    }
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    check("a reader waiting to retry stops promptly", ms < 500, fmt("%.0f ms", ms));
}

int main (void)
{
    decoding();
    reading();
    waking();
    startingSlowly();
    swapping();
    unplugging();
    corruptFrame();
    stopping();

    if (failed)
        printf("%d failed\n", failed);

    return failed ? 1 : 0;
}
