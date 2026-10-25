#pragma once
// Existing offline set-time command: local calendar plus advertised fixed offset.
#include "Protocol.hpp"
#include "TextUtil.hpp"
#include <optional>

namespace clockpc
{
inline bool CurrentStatus(bool have, std::int64_t statusAt, std::int64_t connectedAt)
{
    return have && statusAt >= connectedAt;
}

inline std::optional<int> OffsetMinutes(const std::tm& local, const std::tm& utc)
{
    int days = local.tm_yday - utc.tm_yday;
    if (days > 1) days = -1; // local December 31, UTC January 1
    if (days < -1) days = 1; // local January 1, UTC December 31
    const int seconds = days * 86400 + (local.tm_hour - utc.tm_hour) * 3600 +
                        (local.tm_min - utc.tm_min) * 60 + local.tm_sec - utc.tm_sec;
    if (seconds % 60) return {};
    return seconds / 60;
}

inline std::optional<std::string> Command(std::time_t now, bool offsetCapability)
{
    std::tm local{}, utc{};
#ifdef _WIN32
    if (localtime_s(&local, &now) || gmtime_s(&utc, &now)) return {};
#else
    if (!localtime_r(&now, &local) || !gmtime_r(&now, &utc)) return {};
#endif
    if (local.tm_year < 100 || local.tm_year > 199) return {};
    char calendar[32];
    std::strftime(calendar, sizeof(calendar), "%Y-%m-%d %H:%M:%S", &local);
    std::string command = std::string("set time ") + calendar;
    if (offsetCapability)
    {
        const auto offset = OffsetMinutes(local, utc);
        if (!offset || *offset < -720 || *offset > 840 || *offset % 15) return {};
        command += " tz=" + std::to_string(*offset);
    }
    return command;
}

// Device wall calendar is not necessarily in the PC timezone.
inline std::time_t StatusTime(const proto::DeviceStatus& status)
{
    if (!status.timeTzOffset || !status.timezoneMinutes)
        return text::ParseLocalTime(status.time);
    std::tm t{};
    char extra;
    int year, month;
    if (status.time.size() != 19 || std::sscanf(status.time.c_str(), "%d-%d-%d %d:%d:%d%c",
        &year, &month, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec, &extra) != 6 ||
        year < 2000 || year > 2099) return -1;
    t.tm_year = year - 1900; t.tm_mon = month - 1;
    const auto original = t;
#ifdef _WIN32
    const auto naive = _mkgmtime64(&t);
#else
    const auto naive = timegm(&t);
#endif
    if (naive == -1 || t.tm_year != original.tm_year || t.tm_mon != original.tm_mon ||
        t.tm_mday != original.tm_mday || t.tm_hour != original.tm_hour ||
        t.tm_min != original.tm_min || t.tm_sec != original.tm_sec) return -1;
    return static_cast<std::time_t>(naive - *status.timezoneMinutes * 60);
}

enum class Ack { Failed, SystemOnly, Complete };
inline Ack Acknowledgement(bool requestOk, std::string_view reply)
{
    if (!requestOk || !(reply == "OK time" || proto::StartsWith(reply, "OK time=") ||
                       proto::StartsWith(reply, "OK time "))) return Ack::Failed;
    return reply.find("(rtc no)") != std::string_view::npos ? Ack::SystemOnly : Ack::Complete;
}
} // namespace clockpc
