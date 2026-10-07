#pragma once
// История углов для графиков: кольцевой буфер отсчётов потока stream (время ПК, X/Y обоих датчиков).
// Пишет поток связи (DeviceLink), читает интерфейс — под мьютексом History::mutex (держать коротко: на время
// отрисовки графика). Нет данных датчика (не отвечает) — NaN: на графике разрыв линии.
#include <cmath>
#include <cstddef>
#include <limits>
#include <mutex>
#include <vector>

class History
{
public:
    static constexpr int kSensors = 2;     // Д2, Д3
    static constexpr int kAddr[kSensors] = {2, 3};
    static constexpr std::size_t kCapacity = 15000; // 10 мин при 25 Гц, 12,5 мин при 20 Гц

    struct Sample
    {
        double t = 0.0; // с, часы ПК (DeviceLink::NowMs() / 1000)
        float x[kSensors]{};
        float y[kSensors]{};
    };

    mutable std::mutex mutex;

    void Clear()
    {
        std::lock_guard lock(mutex);
        size_ = 0;
        head_ = 0;
    }

    // Под mutex (вызывающий не держит).
    void Push(const Sample& s)
    {
        std::lock_guard lock(mutex);
        if (buf_.size() != kCapacity)
            buf_.resize(kCapacity);
        buf_[head_] = s;
        head_ = (head_ + 1) % kCapacity;
        if (size_ < kCapacity)
            size_++;
    }

    // Дальше — только под lock(mutex).
    std::size_t Size() const { return size_; }
    // i = 0 — самый старый.
    const Sample& At(std::size_t i) const { return buf_[(head_ + kCapacity - size_ + i) % kCapacity]; }

    // Первый индекс с t >= t0 (двоичный поиск: время растёт).
    std::size_t LowerBound(double t0) const
    {
        std::size_t lo = 0, hi = size_;
        while (lo < hi)
        {
            const std::size_t mid = (lo + hi) / 2;
            if (At(mid).t < t0)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }

private:
    std::vector<Sample> buf_;
    std::size_t head_ = 0, size_ = 0;
};
