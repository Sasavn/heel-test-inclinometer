#pragma once
// Positive identity and control policy. USB IDs select candidates, never a board.
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include "Protocol.hpp"

namespace device
{
enum class Platform { Unknown, Stm32Legacy15, Stm32, Tab5 };
inline constexpr std::int64_t kStatusMaxAgeMs = 3000;

inline bool FreshStatus(bool connected, bool have, std::int64_t at, std::int64_t connectedAt, std::int64_t now)
{
    return connected && have && at >= connectedAt && now >= at && now - at <= kStatusMaxAgeMs;
}

struct Profile
{
    Platform platform = Platform::Unknown;
    bool Known() const { return platform != Platform::Unknown; }
    bool IsTab5() const { return platform == Platform::Tab5; }
    bool IsStm32() const { return platform == Platform::Stm32 || platform == Platform::Stm32Legacy15; }
    const char* Name() const
    {
        switch (platform) {
        case Platform::Stm32Legacy15: return "STM32 · legacy 1.5";
        case Platform::Stm32: return "STM32F411 · протокол 2";
        case Platform::Tab5: return "Tab5 · ESP32-P4";
        default: return "Неизвестная платформа";
        }
    }
    bool AddressChange() const { return IsStm32(); }
    bool Setting(std::string_view key) const { return Known() && (key != "batalarm" || IsStm32()); }
    int MinGap() const { return IsTab5() ? 15 : 2; }
    bool ValidGap(double gap) const { return Known() && std::isfinite(gap) && gap >= MinGap() && gap <= 100 && gap == std::floor(gap); }
};

inline bool Legacy15(const std::string& v)
{
    return v == "1.5" || v.rfind("1.5.", 0) == 0;
}

inline Profile Detect(const proto::VersionInfo& v, const proto::DeviceStatus& s, bool fresh)
{
    if (!fresh || !v.ours || (s.protocol != 1 && s.protocol != 2)) return {};
    if (s.protocol != v.protocol) return {};
    if (!s.fw.empty() && !v.version.empty() && s.fw != v.version) return {};
    auto matches = [](const std::string& value, const char* expected) { return value.empty() || value == expected; };
    if (v.IsTab5() && matches(s.board, "tab5") && matches(s.platform, "esp32p4")) return {Platform::Tab5};
    if (v.board == "stm32f411" && v.chip == "stm32f411" && v.protocol == 2 &&
        matches(s.board, "stm32f411") && matches(s.platform, "stm32f411")) return {Platform::Stm32};
    // Only the known legacy family is inferred from its old BWM427 version;
    // explicit unknown/mismatched identities never fall back to STM32.
    if (v.board.empty() && v.chip.empty() && v.protocol == 1 && Legacy15(v.version) &&
        matches(s.board, "stm32f411") && matches(s.platform, "stm32f411") &&
        (s.fw.empty() || Legacy15(s.fw))) return {Platform::Stm32Legacy15};
    return {};
}

inline bool InstrumentControl(const Profile& p, const proto::DeviceStatus& s)
{
    return p.Known() && s.dualModeCap && s.instrumentControlCap;
}

// A known UID allows continuation on the same STM32. Without a unit identity,
// a new connection starts a new recording, rather than mixing different units.
inline std::string RecordingIdentity(const proto::VersionInfo& v)
{
    const auto at=v.details.find("UID ");
    if(at==std::string::npos)return {};
    const auto uid=v.details.substr(at+4,24);
    if(uid.size()!=24 || uid==std::string(24,'0'))return {};
    for(char c:uid)if(!((c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F')))return {};
    return v.board+"/"+v.chip+"/"+v.version+"/"+uid;
}

// Match VID/PID as a pair (including Windows USB\\VID_xxxx&PID_yyyy forms).
inline bool UsbCandidate(std::string id)
{
    for (auto& c : id) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return id == "0483:5740" || id == "303a:1001" ||
        id.find("vid_0483&pid_5740") != std::string::npos || id.find("vid_303a&pid_1001") != std::string::npos;
}

struct Activity
{
    bool pcRecording = false, levelRecording = false, deviceRecording = false;
    bool transfer = false, firmware = false, settings = false;
    bool Blocked(bool allowDeviceRecording = false) const
    {
        return pcRecording || levelRecording || (!allowDeviceRecording && deviceRecording) || transfer || firmware || settings;
    }
};
} // namespace device
