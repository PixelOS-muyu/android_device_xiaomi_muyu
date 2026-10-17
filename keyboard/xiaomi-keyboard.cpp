/*
 * Copyright (C) 2026 The PixelOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Native keyboard helper for the Xiaomi Pad 7 / 7 Pro magnetic keyboard
 * (nanosic MCU, /dev/nanodev0, HID 0006:15D9:00A3).
 *
 * Protocol and behaviour ported from JohnnyFFM/xiaomi-keyboard-fix (MIT):
 *  - completes the keyboard MiAuth handshake using the device's own
 *    MiDevAuth TrustZone HAL (midevauthd)
 *  - binds the keyboard HID only while the keyboard is attached so the
 *    on-screen keyboard comes back when it is detached
 *  - drives the Caps Lock LED and the keyboard backlight
 *    (Ctrl+Alt+4 brighter / Ctrl+Alt+3 dimmer)
 */

#define LOG_TAG "xiaomi-keyboard"

#include <aidl/vendor/xiaomi/hardware/aidl/midevauth/IMidevauthService.h>
#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <android/binder_manager.h>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using aidl::vendor::xiaomi::hardware::aidl::midevauth::IMidevauthService;
using namespace std::chrono_literals;

namespace {

constexpr char kNanodev[] = "/dev/nanodev0";
constexpr char kStateNode[] = "/sys/class/nanodev/nanodev0/_version176x";
constexpr char kHidDevices[] = "/sys/bus/hid/devices";
constexpr char kHidDriver[] = "/sys/bus/hid/drivers/hid-generic";
constexpr char kHidPrefix[] = "0006:15D9:00A3.";
constexpr char kMidevauthService[] =
        "vendor.xiaomi.hardware.aidl.midevauth.IMidevauthService/default";

constexpr uint16_t kKbdVendor = 0x15d9;
constexpr uint16_t kKbdProduct = 0x00a3;

constexpr size_t kFrameSize = 66;

// Caps Lock feature command (newer MCUs: 0x2e, on=fd off=fc).
constexpr uint8_t kCapsCmd = 0x2e;
constexpr uint8_t kCapsOn = 0xfd;
constexpr uint8_t kCapsOff = 0xfc;

// Backlight (cmd 0x23, level 0-100).
constexpr uint8_t kBacklightCmd = 0x23;
constexpr int kBacklightMax = 100;
constexpr int kBacklightStep = 20;

std::mutex gWriteLock;

std::string Hex(const std::vector<uint8_t>& v) {
    std::string s;
    char buf[3];
    for (uint8_t b : v) {
        snprintf(buf, sizeof(buf), "%02x", b);
        s += buf;
    }
    return s;
}

// Writes one 66-byte frame: 32 00 <body> <sum(body)> zero padded.
bool SendFrame(const std::vector<uint8_t>& body) {
    std::vector<uint8_t> frame = {0x32, 0x00};
    uint8_t sum = 0;
    for (uint8_t b : body) {
        frame.push_back(b);
        sum += b;
    }
    frame.push_back(sum);
    if (frame.size() > kFrameSize) {
        LOG(ERROR) << "frame too large: " << frame.size();
        return false;
    }
    frame.resize(kFrameSize, 0);

    std::lock_guard<std::mutex> lock(gWriteLock);
    android::base::unique_fd fd(open(kNanodev, O_WRONLY | O_CLOEXEC));
    if (fd < 0) {
        PLOG(ERROR) << "open " << kNanodev << " for write";
        return false;
    }
    if (!android::base::WriteFully(fd, frame.data(), frame.size())) {
        PLOG(ERROR) << "write frame";
        return false;
    }
    return true;
}

bool SendFeature(uint8_t cmd, uint8_t value) {
    return SendFrame({0x4e, 0x31, 0x80, 0x38, cmd, 0x01, value});
}

/* ---------------------------------------------------------------------- */
/* MiAuth handshake                                                        */
/* ---------------------------------------------------------------------- */

class Authenticator {
  public:
    void Run();

  private:
    void SendAuthStart();
    void SendStep3();
    void SendStep5(const std::vector<uint8_t>& token);
    std::shared_ptr<IMidevauthService> GetService();
    // Parses as many packets from buf as possible, returns bytes consumed.
    size_t Parse(const std::vector<uint8_t>& buf);

    std::vector<uint8_t> uid_;
    std::vector<uint8_t> keyMeta_;
    std::shared_ptr<IMidevauthService> service_;
};

void Authenticator::SendAuthStart() {
    SendFrame({0x4f, 0x31, 0x80, 0x38, 0x31, 0x06, 0x4d, 0x49, 0x41, 0x55, 0x54, 0x48});
}

void Authenticator::SendStep3() {
    std::vector<uint8_t> challenge(16);
    android::base::unique_fd rnd(open("/dev/urandom", O_RDONLY | O_CLOEXEC));
    if (rnd < 0 || !android::base::ReadFully(rnd, challenge.data(), challenge.size())) {
        PLOG(ERROR) << "read /dev/urandom";
        return;
    }
    std::vector<uint8_t> body = {0x4f, 0x31, 0x80, 0x38, 0x32, 0x14};
    body.insert(body.end(), keyMeta_.begin(), keyMeta_.end());
    body.insert(body.end(), challenge.begin(), challenge.end());
    SendFrame(body);
}

void Authenticator::SendStep5(const std::vector<uint8_t>& token) {
    std::vector<uint8_t> body = {0x4f, 0x31, 0x80, 0x38, 0x33, 0x10};
    body.insert(body.end(), token.begin(), token.end());
    SendFrame(body);
}

std::shared_ptr<IMidevauthService> Authenticator::GetService() {
    if (service_) return service_;
    // midevauthd is a lazy (disabled) VINTF HAL, waitForService starts it on demand.
    ndk::SpAIBinder binder(AServiceManager_waitForService(kMidevauthService));
    if (binder.get() != nullptr) service_ = IMidevauthService::fromBinder(binder);
    if (!service_) LOG(ERROR) << "midevauth service not available";
    return service_;
}

size_t Authenticator::Parse(const std::vector<uint8_t>& b) {
    size_t i = 0;
    const size_t size = b.size();
    auto at = [&](size_t off) -> int { return i + off < size ? b[i + off] : -1; };

    while (i < size) {
        size_t n;
        switch (b[i]) {
            case 0x22: n = 16; break;
            case 0x23: n = 32; break;
            case 0x24:
            case 0x26: n = 0; break;
            default: i++; continue;
        }
        if (size - i < 8) return i;  // need more data for the header
        if (!(at(1) == 0x31 && at(2) == 0x38 && at(3) == 0x80)) {
            i++;
            continue;
        }
        if (at(5) == 0x31 && at(6) == 0x38 && at(7) == 0x80) {
            i += 4;
            continue;
        }
        const uint8_t cmd = b[i + 4];
        const uint8_t len = b[i + 5];
        const uint8_t val = b[i + 6];
        if (n == 0) n = 7 + len;
        size_t need = n;
        if (cmd == 0x31) need = std::max<size_t>(need, 32);
        if (cmd == 0x32 && len == 0x20) need = std::max<size_t>(need, 38);
        if (size - i < need) return i;  // wait for the rest of the packet

        switch (cmd) {
            case 0x24:
                if (val == 0x64) {
                    LOG(INFO) << "REAUTH -> AUTH_START";
                    SendAuthStart();
                } else if (val == 0x01) {
                    LOG(INFO) << "INIT -> AUTH_START";
                    SendAuthStart();
                }
                break;
            case 0x31: {
                uid_.assign(b.begin() + i + 8, b.begin() + i + 24);
                std::vector<uint8_t> km1(b.begin() + i + 24, b.begin() + i + 28);
                std::vector<uint8_t> km2(b.begin() + i + 28, b.begin() + i + 32);
                keyMeta_ = (km1[0] & 0x40) != 0x40 ? km1 : km2;
                LOG(INFO) << "0x31 uid=" << Hex(uid_) << " km1=" << Hex(km1)
                          << " km2=" << Hex(km2) << " chosen=" << Hex(keyMeta_);
                SendStep3();
                LOG(INFO) << "-> STEP3 (random challenge)";
                break;
            }
            case 0x32: {
                if (len != 0x20) {
                    LOG(WARNING) << "0x32 len=" << int(len) << " not 32 bytes, skip";
                    break;
                }
                std::vector<uint8_t> kbdChallenge(b.begin() + i + 22, b.begin() + i + 38);
                auto svc = GetService();
                if (!svc || uid_.empty() || keyMeta_.empty()) {
                    LOG(ERROR) << "cannot compute token, STEP5 not sent";
                    break;
                }
                std::vector<uint8_t> token;
                auto status = svc->devauth_token_get(1, uid_, keyMeta_, kbdChallenge, &token);
                if (!status.isOk() || token.empty()) {
                    LOG(ERROR) << "devauth_token_get failed: " << status.getDescription();
                    service_.reset();
                    break;
                }
                SendStep5(token);
                LOG(INFO) << "-> STEP5 sent";
                break;
            }
            case 0x22:
                LOG(DEBUG) << "KB_STATUS val=" << int(val);
                break;
        }
        i += n;
    }
    return i;
}

void Authenticator::Run() {
    for (;;) {
        android::base::unique_fd fd(open(kNanodev, O_RDONLY | O_CLOEXEC));
        if (fd < 0) {
            PLOG(WARNING) << "open " << kNanodev;
            std::this_thread::sleep_for(2s);
            continue;
        }
        LOG(INFO) << "auth: cold AUTH_START kick";
        SendAuthStart();

        std::vector<uint8_t> buf;
        uint8_t chunk[128];
        for (;;) {
            ssize_t r = TEMP_FAILURE_RETRY(read(fd, chunk, sizeof(chunk)));
            if (r < 0) {
                PLOG(ERROR) << "read " << kNanodev;
                break;
            }
            if (r == 0) {
                std::this_thread::sleep_for(1s);
                continue;
            }
            buf.insert(buf.end(), chunk, chunk + r);
            size_t used = Parse(buf);
            buf.erase(buf.begin(), buf.begin() + used);
            if (buf.size() > 1024) buf.clear();  // never grow unbounded on garbage
        }
        std::this_thread::sleep_for(1s);
    }
}

/* ---------------------------------------------------------------------- */
/* HID bind / unbind depending on the attach state                         */
/* ---------------------------------------------------------------------- */

std::string FindHidDevice() {
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(kHidDevices), closedir);
    if (!dir) return "";
    while (dirent* e = readdir(dir.get())) {
        if (android::base::StartsWith(e->d_name, kHidPrefix)) return e->d_name;
    }
    return "";
}

bool IsKeyboardConnected() {
    std::string state;
    if (!android::base::ReadFileToString(kStateNode, &state)) return false;
    return state.find("Connected=[1]") != std::string::npos;
}

void HidLoop() {
    int last = -1;
    for (;;) {
        const int cur = IsKeyboardConnected() ? 1 : 0;
        if (cur != last) {
            const std::string hid = FindHidDevice();
            if (hid.empty()) {
                LOG(WARNING) << "keyboard HID device not found";
            } else {
                const std::string bound = std::string(kHidDriver) + "/" + hid;
                const bool isBound = access(bound.c_str(), F_OK) == 0;
                if (cur) {
                    if (isBound) {
                        android::base::WriteStringToFile(hid, std::string(kHidDriver) + "/unbind");
                        std::this_thread::sleep_for(1s);
                    }
                    if (android::base::WriteStringToFile(hid, std::string(kHidDriver) + "/bind")) {
                        LOG(INFO) << "keyboard attached: HID " << hid << " bound";
                    } else {
                        PLOG(ERROR) << "bind " << hid;
                    }
                } else if (isBound) {
                    if (android::base::WriteStringToFile(hid,
                                                         std::string(kHidDriver) + "/unbind")) {
                        LOG(INFO) << "keyboard detached: HID " << hid << " unbound";
                    } else {
                        PLOG(ERROR) << "unbind " << hid;
                    }
                }
            }
            last = cur;
        }
        std::this_thread::sleep_for(3s);
    }
}

/* ---------------------------------------------------------------------- */
/* Caps Lock LED + backlight                                               */
/* ---------------------------------------------------------------------- */

bool TestBit(const uint8_t* bits, int bit) {
    return bits[bit / 8] & (1 << (bit % 8));
}

std::string FindCapsEventNode() {
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir("/dev/input"), closedir);
    if (!dir) return "";
    while (dirent* e = readdir(dir.get())) {
        if (!android::base::StartsWith(e->d_name, "event")) continue;
        std::string path = std::string("/dev/input/") + e->d_name;
        android::base::unique_fd fd(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK));
        if (fd < 0) continue;
        input_id id{};
        if (ioctl(fd, EVIOCGID, &id) < 0) continue;
        if (id.vendor != kKbdVendor || id.product != kKbdProduct) continue;
        uint8_t ledBits[(LED_MAX + 8) / 8] = {};
        if (ioctl(fd, EVIOCGBIT(EV_LED, sizeof(ledBits)), ledBits) < 0) continue;
        if (TestBit(ledBits, LED_CAPSL)) return path;
    }
    return "";
}

void LedLoop() {
    int backlight = 0;
    for (;;) {
        const std::string node = FindCapsEventNode();
        if (node.empty()) {
            std::this_thread::sleep_for(2s);
            continue;
        }
        android::base::unique_fd fd(open(node.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK));
        if (fd < 0) {
            std::this_thread::sleep_for(2s);
            continue;
        }
        LOG(INFO) << "keyboard present: " << node;

        bool caps = false, ctrl = false, alt = false;
        for (;;) {
            pollfd pfd = {.fd = fd.get(), .events = POLLIN};
            int pr = poll(&pfd, 1, 1000);
            if (pr < 0 && errno != EINTR) break;
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
            if (access(node.c_str(), F_OK) != 0) break;
            if (pr <= 0 || !(pfd.revents & POLLIN)) continue;

            input_event ev[64];
            ssize_t r = read(fd, ev, sizeof(ev));
            if (r < 0) {
                if (errno == EAGAIN || errno == EINTR) continue;
                break;  // ENODEV: node was removed
            }
            for (size_t k = 0; k < r / sizeof(input_event); k++) {
                const input_event& e = ev[k];
                if (e.type == EV_KEY) {
                    const bool down = e.value == 1;
                    switch (e.code) {
                        case KEY_LEFTCTRL:
                        case KEY_RIGHTCTRL:
                            if (e.value != 2) ctrl = down;
                            break;
                        case KEY_LEFTALT:
                        case KEY_RIGHTALT:
                            if (e.value != 2) alt = down;
                            break;
                        case KEY_CAPSLOCK:
                            if (down) {
                                caps = !caps;
                                SendFeature(kCapsCmd, caps ? kCapsOn : kCapsOff);
                            }
                            break;
                        case KEY_3:
                        case KEY_4:
                            if (down && ctrl && alt) {
                                backlight += e.code == KEY_4 ? kBacklightStep : -kBacklightStep;
                                backlight = std::clamp(backlight, 0, kBacklightMax);
                                SendFeature(kBacklightCmd, static_cast<uint8_t>(backlight));
                                LOG(INFO) << "backlight=" << backlight;
                            }
                            break;
                    }
                } else if (e.type == EV_LED && e.code == LED_CAPSL) {
                    const bool on = e.value != 0;
                    if (on != caps) {
                        caps = on;
                        SendFeature(kCapsCmd, caps ? kCapsOn : kCapsOff);
                    }
                }
            }
        }
        LOG(INFO) << "keyboard gone: " << node;
    }
}

}  // namespace

int main() {
    android::base::InitLogging(nullptr, android::base::LogdLogger(android::base::SYSTEM));

    std::thread hid(HidLoop);
    std::thread leds(LedLoop);
    Authenticator().Run();
    return 0;
}
