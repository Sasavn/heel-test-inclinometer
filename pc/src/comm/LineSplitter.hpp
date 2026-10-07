#pragma once
#include <functional>
#include <string>

// Собирает поток байтов в строки, разделённые '\n' (прибор шлёт "\r\n"); USB режет строки на куски по 64 байта.
// (Как LineSplitter в ShagomerPCModule.)
class LineSplitter
{
public:
    explicit LineSplitter(std::function<void(std::string)> onLine) : onLine_(std::move(onLine)) {}

    void Feed(const char* data, std::size_t size)
    {
        buffer_.append(data, size);
        std::size_t start = 0, pos;
        while ((pos = buffer_.find('\n', start)) != std::string::npos)
        {
            std::string line = buffer_.substr(start, pos - start);
            start = pos + 1;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty() && onLine_)
                onLine_(std::move(line));
        }
        buffer_.erase(0, start);
        // Защита от мусора без переводов строк.
        if (buffer_.size() > 64 * 1024)
            buffer_.clear();
    }

    // Новое соединение — хвост от старого не склеивать.
    void Reset() { buffer_.clear(); }

private:
    std::function<void(std::string)> onLine_;
    std::string buffer_;
};
