#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <bcrypt.h>
#include <windows.h>
#endif
#include "../comm/DeviceLink.hpp"
#include "../core/TextUtil.hpp"
namespace fw
{
inline std::string Hex(const unsigned char *d, std::size_t n)
{
    static const char h[] = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i)
    {
        s += h[d[i] >> 4];
        s += h[d[i] & 15];
    }
    return s;
}
inline std::array<unsigned char, 32> Sha256(const unsigned char *d, std::size_t n)
{
    std::array<unsigned char, 32> out{};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD size = 0, used = 0;
    std::vector<unsigned char> obj;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA256 unavailable");
    auto clean = [&] {
        if (hash)
            BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
    };
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &used, 0) < 0)
    {
        clean();
        throw std::runtime_error("SHA256 property");
    }
    obj.resize(size);
    if (BCryptCreateHash(alg, &hash, obj.data(), size, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, const_cast<PUCHAR>(d), static_cast<ULONG>(n), 0) < 0 ||
        BCryptFinishHash(hash, out.data(), 32, 0) < 0)
    {
        clean();
        throw std::runtime_error("SHA256 failed");
    }
    clean();
#else
    throw std::runtime_error("SHA256 requires Windows backend");
#endif
    return out;
}
struct EspImage
{
    bool ok = false;
    std::string error, version, sha;
    std::size_t size = 0;
    unsigned minRevision = 0, maxRevision = 0;
};
inline EspImage CheckEspImage(const std::vector<unsigned char> &d)
{
    EspImage r;
    r.size = d.size();
    auto fail = [&](const char *e) {
        r.error = e;
        return r;
    };
    if (d.size() < 320 || d.size() > 6291456)
        return fail("ESP application size outside allowed range");
    auto u32 = [&](std::size_t o) {
        return std::uint32_t(d[o]) | (std::uint32_t(d[o + 1]) << 8) | (std::uint32_t(d[o + 2]) << 16) |
               (std::uint32_t(d[o + 3]) << 24);
    };
    if (d[0] != 0xe9 || d[1] == 0 || d[1] > 16 || d[12] != 0x12 || d[13] != 0)
        return fail("Expected ESP32-P4 application, not STM32/C6/full flash");
    if (u32(28) < 256 || u32(32) != 0xabcd5432)
        return fail("Missing ESP application descriptor");
    auto str = [&](std::size_t o) {
        auto end = std::find(d.begin() + o, d.begin() + o + 32, 0);
        return end == d.begin() + o + 32 ? std::string() : std::string(d.begin() + o, end);
    };
    r.minRevision = d[15] | (unsigned(d[16]) << 8);
    if (!r.minRevision)
        r.minRevision = unsigned(d[14]) * 100;
    r.maxRevision = d[17] | (unsigned(d[18]) << 8);
    if (r.maxRevision && r.maxRevision < r.minRevision)
        return fail("Invalid chip revision interval");
    r.version = str(48);
    if (str(80) != "krenomer-esp32")
        return fail("Wrong board project; expected krenomer-esp32");
    if (r.version.empty() || !r.version.ends_with("-esp32") ||
        !std::all_of(r.version.begin(), r.version.end(),
                     [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '-' || c == '_'; }))
        return fail("Invalid Tab5 version");
    std::size_t pos = 24;
    unsigned char checksum = 0xef;
    for (unsigned i = 0; i < d[1]; ++i)
    {
        if (pos + 8 > d.size())
            return fail("Truncated segment header");
        auto n = u32(pos + 4);
        pos += 8;
        if (n % 4 || n > d.size() - pos)
            return fail("Truncated segment");
        for (std::size_t k = 0; k < n; ++k)
            checksum ^= d[pos + k];
        pos += n;
    }
    auto end = (pos + 16) & ~std::size_t(15);
    if (end > d.size() || d[end - 1] != checksum)
        return fail("ESP segment checksum mismatch");
    if (d[23] != 1 || d.size() != end + 32)
        return fail("Require hashed OTA application only (no factory/signed trailer)");
    try
    {
        auto digest = Sha256(d.data(), end);
        if (!std::equal(digest.begin(), digest.end(), d.begin() + end))
            return fail("ESP appended SHA256 mismatch");
        auto full = Sha256(d.data(), d.size());
        r.sha = Hex(full.data(), full.size());
    }
    catch (const std::exception &e)
    {
        r.error = e.what();
        return r;
    }
    r.ok = true;
    return r;
}
class EspOta
{
  public:
    enum class State
    {
        Idle,
        Info,
        Begin,
        Chunk,
        End,
        WaitReboot,
        Verify,
        Done,
        Failed
    };
    bool Busy() const
    {
        return state_ != State::Idle && state_ != State::Done && state_ != State::Failed;
    }
    State GetState() const
    {
        return state_;
    }
    const std::string &Message() const
    {
        return message_;
    }
    double Progress() const
    {
        return data_.empty() ? 0 : double(offset_) / data_.size();
    }
    bool Start(DeviceLink *link, const std::string &path, std::int64_t now)
    {
        if (Busy())
            return false;
        link_ = link;
        message_.clear();
        offset_ = 0;
        retry_ = 0;
        committed_ = false;
        session_ = false;
        if (!link || !link->IsConnected() || !link->Snapshot().ver.IsTab5())
            return Fail("Exact Tab5/ESP32-P4/proto=2 identity required");
        if (link->Snapshot().status.recMask || link->Snapshot().samplesWanted)
            return Fail("Stop device and PC recording first");
        std::ifstream in(text::PathFromUtf8(path), std::ios::binary | std::ios::ate);
        if (!in)
            return Fail("Cannot read OTA application");
        auto n = in.tellg();
        if (n < 320 || n > 6291456)
            return Fail("Invalid OTA application size");
        in.seekg(0);
        data_.resize(static_cast<std::size_t>(n));
        if (!in.read(reinterpret_cast<char *>(data_.data()), n))
            return Fail("Cannot read OTA application");
        image_ = CheckEspImage(data_);
        if (!image_.ok)
            return Fail(image_.error);
        // Same-version reinstall is allowed for recovery; board/chip/hash checks
        // still apply.
        seq_ = link->Snapshot().connectSeq;
        link_->SetPollPaused(true);
        state_ = State::Info;
        req_ = link_->Send("ota info");
        deadline_ = now + 120000;
        return true;
    }
    void Cancel()
    {
        if (Busy() && link_)
        {
            if (session_ && !committed_ && link_->IsConnected())
                link_->Send("ota abort");
            link_->SetPollPaused(false);
            state_ = State::Failed;
            message_ = committed_ ? "Cancelled verification; committed image may reboot"
                                  : "Cancelled; restart upload from byte zero";
        }
        link_ = nullptr;
        req_.reset();
    }
    void Tick(std::int64_t now)
    {
        if (!Busy() || !link_)
            return;
        auto s = link_->Snapshot();
        // A slow UI frame may observe the commit ACK and socket EOF together.
        // Consume an acknowledged commit before classifying the expected reboot.
        if(state_==State::End && req_ && req_->done && req_->ok &&
           req_->final=="OK ota committed scheduled reboot") {
            committed_=true;state_=State::WaitReboot;deadline_=now+120000;
            message_="Committed; awaiting reboot and expected version";
        }
        if (state_ == State::WaitReboot)
        {
            if (now > deadline_)
            {
                Fail("Reboot/version verification timed out; reconnect and inspect "
                     "device rollback state");
                return;
            }
            if (s.state == LinkState::Connected && s.connectSeq > seq_)
            {
                if (!s.ver.IsTab5() || s.ver.version != image_.version)
                {
                    Fail("Device returned wrong platform/version (possible rollback)");
                    return;
                }
                seq_ = s.connectSeq;
                state_ = State::Verify;
                req_ = link_->Send("ota info");
            }
            return;
        }
        if (s.state != LinkState::Connected || s.connectSeq != seq_)
        {
            Fail("Disconnected; device aborts session. Retry starts from zero");
            return;
        }
        if (state_ == State::Verify && now > deadline_)
        {
            Fail("Boot self-test confirmation timed out; trial image is not verified");
            return;
        }
        if (state_ == State::Verify && !req_)
        {
            if (now >= verifyAt_)
                req_ = link_->Send("ota info");
            return;
        }
        if (!req_ || !req_->done)
            return;
        if (!req_->ok)
        {
            if (state_ == State::Chunk && req_->timedOut && retry_++ == 0 && link_->IsConnected())
            {
                req_ = link_->Send(chunk_);
                return;
            }
            Fail("OTA failed: " + req_->final);
            return;
        }
        if (state_ == State::Info || state_ == State::Verify)
        {
            json::Value j;
            std::string error;
            if (req_->lines.empty() || !json::Parse(req_->lines.back(), j, &error) || j["board"].Str() != "tab5" ||
                j["chip"].Str() != "esp32p4" || !j["proto"].IsNumber() || j["proto"].Num() != 2)
            {
                Fail("Invalid OTA identity reply");
                return;
            }
            if (state_ == State::Verify)
            {
                if (!s.ver.IsTab5() || j["version"].Str() != image_.version)
                {
                    Fail("Running version mismatch; possible rollback");
                    return;
                }
                if (!j["boot_confirmed"].IsBool())
                {
                    Fail("Missing boot confirmation field");
                    return;
                }
                if (!j["boot_confirmed"].Bool())
                {
                    message_ = "Expected version is running as trial; awaiting self-test "
                               "confirmation";
                    req_.reset();
                    verifyAt_ = now + 1000;
                    return;
                }
                state_ = State::Done;
                message_ = "Uploaded; expected version reconnected and boot self-test "
                           "confirmed";
                link_->SetPollPaused(false);
                return;
            }
            if (!j["recording"].IsBool() || !j["max_size"].IsNumber() || j["recording"].Bool() ||
                j["max_size"].Int() < static_cast<std::int64_t>(data_.size()) || j["version"].Str() != s.ver.version)
            {
                Fail("OTA device busy/capacity/version mismatch");
                return;
            }
            if (!j["chip_revision"].IsNumber() || j["chip_revision"].Int() < image_.minRevision ||
                (image_.maxRevision && j["chip_revision"].Int() > image_.maxRevision))
            {
                Fail("Image incompatible with device chip revision");
                return;
            }
            if(!j["boot_confirmed"].IsBool() || !j["boot_confirmed"].Bool()) {
                Fail("Running trial is not boot-confirmed; wait for self-test before updating");return;
            }
            session_ = true;
            state_ = State::Begin;
            req_ = link_->Send("ota begin tab5 esp32p4 2 " + image_.version + " " + std::to_string(data_.size()) + " " +
                                   image_.sha,
                               Origin::App, 15000);
            return;
        }
        if (state_ == State::Begin || state_ == State::Chunk)
        {
            std::size_t expected = state_ == State::Begin ? 0 : offset_ + chunkSize_;
            if (req_->final != "OK ota offset=" + std::to_string(expected))
            {
                Fail("Unexpected OTA offset acknowledgement");
                return;
            }
            offset_ = expected;
            retry_ = 0;
            if (offset_ == data_.size())
            {
                state_ = State::End;
                req_ = link_->Send("ota end", Origin::App, 30000);
            }
            else
            {
                chunkSize_ = std::min<std::size_t>(256, data_.size() - offset_);
                chunk_ = "ota chunk " + std::to_string(offset_) + " " + Hex(data_.data() + offset_, chunkSize_);
                state_ = State::Chunk;
                req_ = link_->Send(chunk_);
            }
            return;
        }
        if (state_ == State::End)
        {
            if (req_->final != "OK ota committed scheduled reboot")
            {
                Fail("Commit not acknowledged; inspect ota status before retry");
                return;
            }
            committed_ = true;
            state_ = State::WaitReboot;
            deadline_ = now + 120000;
            message_ = "Committed; awaiting reboot and expected version";
        }
    }

  private:
    bool Fail(std::string e)
    {
        message_ = std::move(e);
        state_ = State::Failed;
        if (link_)
        {
            if (session_ && !committed_ && link_->IsConnected())
                link_->Send("ota abort");
            link_->SetPollPaused(false);
        }
        return false;
    }
    DeviceLink *link_ = nullptr;
    State state_ = State::Idle;
    std::string message_, chunk_;
    RequestPtr req_;
    std::vector<unsigned char> data_;
    EspImage image_;
    std::size_t offset_ = 0, chunkSize_ = 0;
    int retry_ = 0;
    bool committed_ = false, session_ = false;
    std::uint64_t seq_ = 0;
    std::int64_t deadline_ = 0, verifyAt_ = 0;
};
} // namespace fw
