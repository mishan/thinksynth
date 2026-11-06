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
 * hidreadcheck -- finding a wired Index controller and reading it on a
 * thread, without one plugged in.
 *
 * Discovery runs against a sysfs tree built in a scratch directory, with
 * the controller's real report descriptors (scripts/hid/index-descriptors
 * .txt). The reader runs against socket pairs, which keep each report whole
 * the way a hidraw node does; closing the far end is an unplug.
 *
 * Linux only, as the reader is.
 */

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "thhIndex.h"
#include "thhReader.h"

namespace fs = std::filesystem;
using std::string;

static int failed = 0;

static void
report (const string &name, const string &why)
{
    if (why.empty())
        printf("ok    %s\n", name.c_str());
    else
    {
        printf("FAIL  %s\n  %s\n", name.c_str(), why.c_str());
        failed++;
    }
}

static std::map<string, string> descriptors;    /* input, imu, light: bytes */

static void
readDescriptors (const char *path)
{
    std::ifstream in(path);
    string line;

    while (std::getline(in, line))
    {
        const size_t tab = line.find('\t');
        string bytes;

        for (size_t i = tab + 1; i + 1 < line.size(); i += 2)
            bytes += (char)strtoul(line.substr(i, 2).c_str(), NULL, 16);
        descriptors[line.substr(0, tab)] = bytes;
    }
}

/* One interface of a device, as sysfs shows it. */
struct Node
{
    const char *usb;        /* the USB device's directory, e.g. 1-2 */
    int         iface;
    const char *name;       /* HID_NAME */
    const char *id;         /* HID_ID */
    const char *desc;       /* a key into descriptors */
    int         hidraw;
};

#define INDEX   "Valve Index Controller"
#define VALVE   "0003:000028DE:00002300"

static string
sysTree (const string &root, const std::vector<Node> &nodes)
{
    fs::remove_all(root);
    for (size_t i = 0; i < nodes.size(); i++)
    {
        const Node &n = nodes[i];
        const string usb = root + "/devices/usb1/" + n.usb;
        const string dir = usb + "/" + n.usb + ":1." + std::to_string(n.iface) +
                           "/0003:28DE:2300.00" + std::to_string(10 + i);
        const string link = root + "/class/hidraw/hidraw" +
                            std::to_string(n.hidraw);

        fs::create_directories(dir);
        fs::create_directories(link);
        std::ofstream(dir + "/uevent") << "DRIVER=hid-generic\nHID_ID=" << n.id
                                       << "\nHID_NAME=" << n.name << "\n";
        std::ofstream(dir + "/report_descriptor", std::ios::binary)
            << descriptors[n.desc];
        fs::create_directory_symlink(dir, link + "/device");
    }
    return root;
}

static string
describe (const thhIndexNodes &n)
{
    string s = "input " + n.input + ", imu " + n.imu + ", all";

    for (size_t i = 0; i < n.all.size(); i++)
        s += " " + n.all[i];
    return s;
}

static void
checkFind (const string &scratch)
{
    struct Row
    {
        const char       *name;
        std::vector<Node> nodes;
        const char       *want;     /* describe(), or NULL for none found */
    };
    const Row rows[] = {
        { "a controller among a tracker and a keyboard",
          { { "1-1", 0, "HTC Vive Tracker", VALVE, "input", 3 },
            { "1-2", 0, INDEX, VALVE, "imu", 8 },
            { "1-2", 1, INDEX, VALVE, "light", 13 },
            { "1-2", 2, INDEX, VALVE, "input", 14 },
            { "1-5", 0, "Some Keyboard", "0003:0000046D:0000C31C", "input", 2 } },
          "input /dev/hidraw14, imu /dev/hidraw8, all /dev/hidraw13 "
          "/dev/hidraw14 /dev/hidraw8" },
        { "the first of two controllers, not a mix of both",
          { { "1-4", 0, INDEX, VALVE, "input", 20 },
            { "1-4", 1, INDEX, VALVE, "imu", 21 },
            { "1-3", 0, INDEX, VALVE, "imu", 30 },
            { "1-3", 1, INDEX, VALVE, "input", 31 } },
          "input /dev/hidraw31, imu /dev/hidraw30, all /dev/hidraw30 "
          "/dev/hidraw31" },
        { "no controller",
          { { "1-1", 0, "HTC Vive Tracker", VALVE, "input", 3 } }, NULL },
        { "a controller with no input interface",
          { { "1-2", 0, INDEX, VALVE, "imu", 8 },
            { "1-2", 1, INDEX, VALVE, "light", 13 } }, NULL },
    };

    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++)
    {
        const Row &r = rows[i];
        thhIndexNodes n;
        const bool found = thhFindIndex(sysTree(scratch + "/sys", r.nodes),
                                        "/dev", n);

        report(r.name, !r.want ? (found ? "found " + describe(n) : "") :
               !found ? "found none" :
               describe(n) == r.want ? "" :
               "found\n    " + describe(n) + "\n  wanted\n    " + r.want);
    }
}

static std::vector<uint8_t>
input (uint32_t a, uint8_t trigger)
{
    std::vector<uint8_t> r(64, 0);

    r[0x00] = 0x01;
    r[0x02] = 0x07;
    r[0x03] = 0x3c;
    r[0x08] = a ? 0x04 : 0;
    r[0x0c] = trigger;
    return r;
}

static bool
waitFor (const std::function<bool ()> &done)
{
    for (int i = 0; i < 300; i++)
    {
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

static string
describe (const std::vector<thhState> &edges)
{
    string s;

    for (size_t i = 0; i < edges.size(); i++)
        s += string(i ? " " : "") + (!edges[i].connected ? "gone" :
             edges[i].buttons & (1u << THH_INDEX_A) ? "a" : "-");
    return s;
}

/* A reader on one end of a socket pair, the test holding the other. */
struct Rig
{
    int                        far, farImu;
    std::atomic<int>           connects, wakes;
    std::unique_ptr<thhReader> reader;
    std::vector<thhState>      edges;
    thhState                   latest;

    /* Silence well past any wait here, unless it is what's being checked. */
    Rig (int silenceMs = 60000) : far(-1), farImu(-1), connects(0), wakes(0)
    {
        int sv[2], mv[2];

        if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) < 0 ||
            socketpair(AF_UNIX, SOCK_SEQPACKET, 0, mv) < 0)
            abort();
        far = sv[1];
        farImu = mv[1];
        reader.reset(new thhReader(
            [this, sv, mv](int &in, int &imu, std::vector<int> &fds,
                           string &) {
                if (connects++ > 0)
                    return false;
                in = sv[0];
                imu = mv[0];
                fds.assign({ sv[0], mv[0] });
                return true;
            },
            [this]() { wakes++; }, silenceMs));
    }

    ~Rig ()
    {
        reader.reset();
        if (far >= 0)
            close(far);
        close(farImu);
    }

    void send (const std::vector<uint8_t> &r, bool toImu = false)
    {
        const int to = toImu ? farImu : far;

        if (write(to, r.data(), r.size()) != (ssize_t)r.size())
            abort();
    }

    /* Drains until the edges come to n, or a while has passed. */
    string settle (size_t n)
    {
        std::vector<thhState> e;

        edges.clear();
        waitFor([&]() {
            if (reader->drain(e, latest))
                edges.insert(edges.end(), e.begin(), e.end());
            return edges.size() >= n;
        });
        return describe(edges);
    }
};

static void
checkReader (void)
{
    {
        Rig r;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        report("a connect alone, before a report, says nothing",
               !r.reader->drain(r.edges, r.latest) ? "" :
               "edges: " + describe(r.edges));
        r.send(input(0, 0x10));
        report("the first report after a connect is the connect's edge",
               r.settle(1) == "-" &&
               std::fabs(r.latest.axis[THH_INDEX_TRIGGER] - 0x10 / 255.0f) <
                   1e-6 ? "" : "edges: " + describe(r.edges));
        r.send(input(1, 0));
        report("a press is an edge", r.settle(1) == "a" ? "" :
               "edges: " + describe(r.edges));

        r.send(input(1, 0x10));
        r.send(input(1, 0x20));
        r.send(input(1, 0x30));
        waitFor([&]() { return r.wakes > 2; });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        r.settle(0);
        report("moves between presses are only the latest",
               r.edges.empty() &&
               std::fabs(r.latest.axis[THH_INDEX_TRIGGER] - 0x30 / 255.0f) <
                   1e-6 ? "" : "edges: " + describe(r.edges));

        /* The IMU's report: three samples of the front tipped straight up. */
        std::vector<uint8_t> up(52, 0);

        up[0] = 0x20;
        for (int k = 0; k < 3; k++)
        {
            up[1 + 17 * k] = 0x00;
            up[2 + 17 * k] = 0xf0;      /* x -4096: the front is -x */
        }
        r.send(up, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        r.settle(0);
        report("the IMU moves tilt and is no edge",
               r.edges.empty() && r.latest.axis[THH_INDEX_TILT] > 0.99f ? "" :
               "edges: " + describe(r.edges) + ", tilt " +
               std::to_string(r.latest.axis[THH_INDEX_TILT]));

        r.send(input(0, 0));
        r.send(input(1, 0));
        r.send(input(0, 0));
        r.send(input(1, 0));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        report("a slow drain loses no press", r.settle(4) == "- a - a" ? "" :
               "edges: " + describe(r.edges));

        close(r.far);
        r.far = -1;
        report("an unplug is an edge", r.settle(1) == "gone" ? "" :
               "edges: " + describe(r.edges));
        report("connect is asked again after an unplug",
               waitFor([&]() { return r.connects >= 2; }) ? "" :
               "asked " + std::to_string(r.connects) + " times");

        const auto t0 = std::chrono::steady_clock::now();

        r.reader.reset();

        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();

        report("stopping doesn't wait out the second between connects",
               ms < 500 ? "" : std::to_string(ms) + " ms");
    }
}

static void
checkSilence (void)
{
    Rig r(300);

    r.send(input(0, 0));
    r.settle(1);
    report("silence is an unplug", r.settle(1) == "gone" ? "" :
           "edges: " + describe(r.edges));
}

static void
checkPermission (const string &scratch)
{
    if (geteuid() == 0)
    {
        printf("skip  a node it may not open (running as root)\n");
        return;
    }

    const string sys = sysTree(scratch + "/sys",
        { { "1-2", 0, INDEX, VALVE, "input", 14 } });
    const string dev = scratch + "/dev";

    fs::create_directories(dev);
    std::ofstream(dev + "/hidraw14") << "";
    chmod((dev + "/hidraw14").c_str(), 0);

    int in = -1, imu = -1;
    std::vector<int> fds;
    string status;
    const bool ok = thhConnectIndex(sys, dev, in, imu, fds, status);

    report("a node it may not open says how to allow it",
           !ok && status.find("udev") != string::npos ? "" :
           ok ? "connected" : "said \"" + status + "\"");
}

int
main (int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: hidreadcheck "
                "<scripts/hid/index-descriptors.txt>\n");
        return 2;
    }

    char tmpl[] = "/tmp/hidreadcheck.XXXXXX";

    /* Without one, every path below would be the real system's. */
    if (!mkdtemp(tmpl))
    {
        perror("hidreadcheck: a scratch directory");
        return 1;
    }

    const string scratch = tmpl;

    readDescriptors(argv[1]);
    checkFind(scratch);
    checkReader();
    checkSilence();
    checkPermission(scratch);
    fs::remove_all(scratch);

    printf("\n%d failure%s\n", failed, failed == 1 ? "" : "s");

    return failed;
}
