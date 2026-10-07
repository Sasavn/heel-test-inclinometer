#pragma once
// Приём файла с карты прибора (команда get): строки ответа G / D / E -> файл <папка>/<имя>.part, по успеху
// (байты и CRC-32 сошлись с итогом E) — переименование в <имя>. Ошибка — .part удаляется: файл скачивается
// заново целиком (так каждый скачанный файл целиком проверен CRC).
//
// OnLine / OnFinish вызывает поток связи (DeviceLink), прогресс читает интерфейс — атомарные счётчики.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "Codec.hpp"
#include "Protocol.hpp"

class DownloadSink
{
public:
    enum class Result
    {
        Running,
        Ok,
        CrcMismatch,  // данные пришли, но CRC / число байт не сошлись с итогом E
        DeviceError,  // прибор ответил ERR (нет файла, идёт запись, карту вынули…)
        IoError,      // не удалось записать файл на ПК
        BadData,      // строка D не base64 / нет G
        Aborted,      // отменено
        LinkLost,     // связь пропала / нет ответа
    };

    DownloadSink(std::filesystem::path target, std::uint64_t expectedSize)
        : target_(std::move(target)), expected_(expectedSize)
    {
        part_ = target_;
        part_ += L".part";
    }

    ~DownloadSink()
    {
        CloseFile();
        if (result_ == Result::Running)
        {
            std::error_code ec;
            std::filesystem::remove(part_, ec); // программу закрыли посреди скачивания
        }
    }

    DownloadSink(const DownloadSink&) = delete;
    DownloadSink& operator=(const DownloadSink&) = delete;

    // Строка ответа get (кроме последней OK/ERR).
    void OnLine(std::string_view line)
    {
        std::lock_guard lock(m_);
        if (Done())
            return;
        if (proto::StartsWith(line, "G,"))
        {
            // G,<имя>,<размер>,<смещение> (имя может содержать запятые — с конца)
            const auto c2 = line.rfind(',');
            const auto c1 = c2 == std::string_view::npos || c2 == 0 ? std::string_view::npos : line.rfind(',', c2 - 1);
            std::uint64_t size = 0, offset = 0;
            if (c1 == std::string_view::npos || !proto::ToU64(line.substr(c1 + 1, c2 - c1 - 1), size) ||
                !proto::ToU64(line.substr(c2 + 1), offset) || offset != 0)
            {
                Finish(Result::BadData, "непонятная строка G: " + std::string(line));
                return;
            }
            total_ = size;
            std::error_code ec;
            std::filesystem::create_directories(target_.parent_path(), ec);
#ifdef _WIN32
            file_ = _wfopen(part_.c_str(), L"wb");
#else
            file_ = std::fopen(part_.string().c_str(), "wb");
#endif
            if (!file_)
            {
                Finish(Result::IoError, "не удалось создать файл " + PathUtf8(part_));
                return;
            }
            haveG_ = true;
            return;
        }
        if (proto::StartsWith(line, "D,"))
        {
            if (!haveG_)
            {
                Finish(Result::BadData, "данные без строки G");
                return;
            }
            buf_.clear();
            if (!codec::Base64Decode(line.substr(2), buf_))
            {
                Finish(Result::BadData, "строка D — не base64");
                return;
            }
            if (!buf_.empty() && std::fwrite(buf_.data(), 1, buf_.size(), file_) != buf_.size())
            {
                Finish(Result::IoError, "ошибка записи " + PathUtf8(part_) + " (диск заполнен?)");
                return;
            }
            crc_ = codec::Crc32(crc_, buf_.data(), buf_.size());
            received_ += buf_.size();
            return;
        }
        if (proto::StartsWith(line, "E,"))
        {
            const auto parts = proto::Split(line.substr(2), ',');
            std::uint64_t bytes = 0;
            if (parts.size() < 2 || !proto::ToU64(parts[0], bytes))
            {
                Finish(Result::BadData, "непонятная строка E: " + std::string(line));
                return;
            }
            std::uint32_t crc = 0;
            for (char c : parts[1])
            {
                crc <<= 4;
                if (c >= '0' && c <= '9')
                    crc |= static_cast<std::uint32_t>(c - '0');
                else if (c >= 'a' && c <= 'f')
                    crc |= static_cast<std::uint32_t>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F')
                    crc |= static_cast<std::uint32_t>(c - 'A' + 10);
            }
            haveE_ = true;
            deviceBytes_ = bytes;
            deviceCrc_ = crc;
            return;
        }
        // Прочие строки (не должно быть) — пропускаем.
    }

    // Последняя строка ответа (ok — «OK…», иначе ERR / потеря связи / отмена).
    void OnFinish(bool ok, const std::string& terminator, Result failAs = Result::DeviceError)
    {
        std::lock_guard lock(m_);
        if (Done())
            return;
        if (!ok)
        {
            Finish(failAs, terminator);
            return;
        }
        if (!haveG_ || !haveE_)
        {
            Finish(Result::BadData, "ответ без строки " + std::string(!haveG_ ? "G" : "E"));
            return;
        }
        if (deviceBytes_ != received_ || received_ != total_ || deviceCrc_ != crc_)
        {
            char msg[160];
            std::snprintf(msg, sizeof(msg), "не сошлось: получено %llu из %llu байт, CRC %08x, у прибора %08x",
                          static_cast<unsigned long long>(received_.load()), static_cast<unsigned long long>(deviceBytes_),
                          crc_, deviceCrc_);
            Finish(Result::CrcMismatch, msg);
            return;
        }
        if (std::fflush(file_) != 0)
        {
            Finish(Result::IoError, "ошибка записи " + PathUtf8(part_));
            return;
        }
        CloseFile();
        std::error_code ec;
        std::filesystem::rename(part_, target_, ec);
        if (ec)
        {
            Finish(Result::IoError, "не удалось переименовать в " + PathUtf8(target_) + ": " + ec.message());
            return;
        }
        char msg[64];
        std::snprintf(msg, sizeof(msg), "CRC-32 %08x совпал", crc_);
        result_ = Result::Ok;
        message_ = msg;
    }

    Result GetResult() const
    {
        std::lock_guard lock(m_);
        return result_;
    }
    std::string Message() const
    {
        std::lock_guard lock(m_);
        return message_;
    }
    std::uint64_t Received() const { return received_.load(); }
    std::uint64_t Total() const
    {
        const std::uint64_t t = total_.load();
        return t ? t : expected_;
    }
    std::uint32_t Crc() const
    {
        std::lock_guard lock(m_);
        return crc_;
    }
    const std::filesystem::path& Target() const { return target_; }

    static std::string PathUtf8(const std::filesystem::path& p)
    {
        const auto u = p.u8string();
        return std::string(u.begin(), u.end());
    }

private:
    bool Done() const { return result_ != Result::Running; }

    void CloseFile()
    {
        if (file_)
        {
            std::fclose(file_);
            file_ = nullptr;
        }
    }

    void Finish(Result r, std::string msg)
    {
        CloseFile();
        std::error_code ec;
        std::filesystem::remove(part_, ec); // недокачанное не оставляем
        result_ = r;
        message_ = std::move(msg);
    }

    mutable std::mutex m_;
    std::filesystem::path target_, part_;
    std::uint64_t expected_ = 0;
    std::atomic<std::uint64_t> total_{0}, received_{0};
    std::FILE* file_ = nullptr;
    std::vector<std::uint8_t> buf_;
    std::uint32_t crc_ = 0;
    bool haveG_ = false, haveE_ = false;
    std::uint64_t deviceBytes_ = 0;
    std::uint32_t deviceCrc_ = 0;
    Result result_ = Result::Running;
    std::string message_;
};
