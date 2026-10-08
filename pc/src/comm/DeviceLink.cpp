#include "DeviceLink.hpp"

#include <algorithm>
#include <chrono>
#include <limits>

#include "../core/Download.hpp"

namespace
{

std::string FirstWord(const std::string& s)
{
    const auto sp = s.find(' ');
    return sp == std::string::npos ? s : s.substr(0, sp);
}

std::string LowerAscii(std::string s)
{
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::int64_t WallMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

DeviceLink::DeviceLink(std::unique_ptr<IConnection> conn, bool threaded) : conn_(std::move(conn))
{
    conn_->SetLineHandler([this](std::string line) {
        {
            std::lock_guard lock(rxMutex_);
            rx_.push_back(std::move(line));
        }
        cv_.notify_one();
    });
    if (threaded)
    {
        running_ = true;
        thread_ = std::thread(&DeviceLink::Run, this);
    }
}

DeviceLink::~DeviceLink()
{
    running_ = false;
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
    // Канал закрывается раньше остальных полей: его поток чтения вызывает обработчик строк.
    conn_.reset();
}

void DeviceLink::Run()
{
    const auto t0 = std::chrono::steady_clock::now();
    while (running_)
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        Step(static_cast<std::int64_t>(ms));
        std::unique_lock lk(cvMutex_);
        cv_.wait_for(lk, std::chrono::milliseconds(5));
    }
}

bool DeviceLink::IsDemo() const
{
    return conn_->IsDemo();
}

void DeviceLink::Step(std::int64_t now)
{
    now_ = now;
    conn_->Tick(now);
    const bool open = conn_->IsOpen();
    const std::uint64_t oc = conn_->OpenCount();
    bool reject = false, reopen = false, foreign = false;
    std::string toWrite;

    std::vector<std::string> lines;
    {
        std::lock_guard lock(rxMutex_);
        lines.swap(rx_);
    }

    {
        std::lock_guard lock(m_);
        const bool reopened = open && (!wasOpen_ || oc != lastOpenCount_);
        // Строки, пришедшие до закрытия порта (например, «DFU...» перед уходом в загрузчик), — ответ прежнему
        // соединению: разобрать до того, как закрытие отменит команду в полёте.
        if (!reopened)
            for (const auto& l : lines)
                HandleLine(l, now);
        if (reopened)
        {
            if (wasOpen_)
                OnClosed(now, "порт переоткрыт");
            lastOpenCount_ = oc;
            wasOpen_ = true;
            OnOpened(now);
            for (const auto& l : lines)
                HandleLine(l, now);
        }
        else if (!open && wasOpen_)
        {
            wasOpen_ = false;
            OnClosed(now, conn_->Problem());
        }
        if (!open)
        {
            const LinkState want = conn_->Enabled() ? LinkState::Searching : LinkState::Off;
            if (state_.load() != want)
                state_ = want;
            const std::string p = conn_->Problem();
            if (conn_->Enabled() && !p.empty() && p != lastProblem_)
                LogLocked('!', p, Origin::Internal, now);
            lastProblem_ = p;
        }

        // Отмена передачи: get abort вне очереди
        if (abortWanted_)
        {
            abortWanted_ = false;
            if (inFlight_ && inFlight_->sink && open && !expectAbortReply_)
            {
                toWrite += "get abort\r\n";
                expectAbortReply_ = true;
                abortSentAt_ = now;
                cmdsTx_++;
                LogLocked('>', "get abort", Origin::App, now);
            }
        }
        if (expectAbortReply_ && now - abortSentAt_ > 3000)
        {
            expectAbortReply_ = false;
            if (inFlight_ && inFlight_->sink)
            {
                auto r = inFlight_;
                r->aborted = true;
                Complete(r, false, "отменено (прибор не подтвердил)", now);
            }
        }

        // Проверка прибора: ver
        if (state_.load() == LinkState::Probing && !probe_ && now >= probeAt_ && !inFlight_ && !rejectWanted_)
        {
            probe_ = std::make_shared<Request>();
            probeOurs_ = false;
            probeStray_ = false;
            probe_->cmd = "ver";
            probe_->origin = Origin::Internal;
            probe_->timeoutMs = 1000;
            queue_.push_front(probe_);
        }

        // Таймаут команды в полёте
        if (inFlight_ && now >= inFlight_->deadline)
        {
            auto r = inFlight_;
            r->timedOut = true;
            timeouts_++;
            consecutiveTimeouts_++;
            LogLocked('!', "нет ответа на «" + r->cmd + "»", r->origin, now);
            Complete(r, false, "нет ответа от прибора", now);
            if (consecutiveTimeouts_ >= 3 && state_.load() == LinkState::Connected)
            {
                LogLocked('!', "прибор не отвечает — порт переоткрывается", Origin::Internal, now);
                consecutiveTimeouts_ = 0;
                reopen = true;
            }
        }
        if (rejectWanted_)
        {
            rejectWanted_ = false;
            reject = true;
            foreign = rejectForeign_;
        }

        // Опрос status и сторож потока — когда очередь пуста
        if (state_.load() == LinkState::Connected && !inFlight_ && queue_.empty() && !expectAbortReply_)
        {
            const int period = now < fastPollUntil_ ? fastPollMs_ : 1000;
            if (!pollPaused_ && now - lastStatusReq_ >= period)
            {
                auto r = std::make_shared<Request>();
                r->cmd = "status";
                r->origin = Origin::Poll;
                r->timeoutMs = 2000;
                queue_.push_back(r);
                lastStatusReq_ = now;
            }
            const int want = pollPaused_ ? kPausedStreamMs : streamMs_;
            const std::int64_t quiet = now - std::max(sampleAt_, lastStreamCmd_);
            if (streamDirty_ || (want > 0 && quiet > std::max<std::int64_t>(3000, 5LL * want)))
            {
                auto r = std::make_shared<Request>();
                r->cmd = "stream " + std::to_string(want);
                r->origin = Origin::Internal;
                queue_.push_back(r);
                lastStreamCmd_ = now;
                streamDirty_ = false;
            }
            // Запись на ПК: samples on (после каждого подключения) / off. Сторож: включено, датчик по потоку
            // отвечает, а строк R нет 5 с — включить ещё раз.
            if (!samplesReq_ && SamplesSupportLocked() >= 0 && now >= samplesRetryAt_)
            {
                bool sensorOk = false;
                if (haveSample_ && now - sampleAt_ < 2000)
                    for (const auto& d : sample_.sens)
                        sensorOk = sensorOk || d.st == 'O';
                const bool stale = samplesOn_ && sensorOk && now - std::max(samplesAt_, samplesOnAt_) > 5000;
                const char* cmd = samplesWanted_ && (!samplesOn_ || stale) ? "samples on"
                                  : !samplesWanted_ && samplesOn_ && samplesOffPending_ ? "samples off"
                                                                                        : nullptr;
                if (cmd)
                {
                    samplesReq_ = std::make_shared<Request>();
                    samplesReq_->cmd = cmd;
                    samplesReq_->origin = Origin::Internal;
                    samplesReq_->timeoutMs = 2000;
                    queue_.push_back(samplesReq_);
                    if (stale && samplesWanted_)
                        LogLocked('!', "строк R нет 5 с — samples on ещё раз", Origin::Internal, now);
                }
            }
        }

        // Следующая команда
        if (open && !inFlight_ && !queue_.empty() && !expectAbortReply_)
        {
            auto head = queue_.front();
            if (state_.load() == LinkState::Connected || head == probe_)
            {
                queue_.pop_front();
                inFlight_ = head;
                head->deadline = now + head->timeoutMs;
                toWrite += head->cmd + "\r\n";
                cmdsTx_++;
                LogLocked('>', head->cmd, head->origin, now);
            }
        }
        if (!open && (!queue_.empty() || inFlight_))
            FailAll("нет связи с прибором", true);
    }

    if (!toWrite.empty() && !conn_->Write(toWrite))
    {
        std::lock_guard lock(m_);
        LogLocked('!', "ошибка записи в порт", Origin::Internal, now);
    }
    if (reopen)
        conn_->Reopen(now);
    if (reject)
        conn_->Reject(now, foreign);
}

void DeviceLink::OnOpened(std::int64_t now)
{
    state_ = LinkState::Probing;
    probeAt_ = now + 200;
    probeTries_ = 0;
    probe_.reset();
    header_ = {};
    haveSample_ = false;
    sampleTimes_.clear();
    consecutiveTimeouts_ = 0;
    expectAbortReply_ = false;
    // Прибор выключает samples при открытии порта; прошивка может быть другой (после перепрошивки)
    samplesOn_ = false;
    samplesOffPending_ = false;
    samplesSupported_ = 0;
    samplesRetryAt_ = 0;
    samplesMsg_.clear();
    LogLocked('!', "порт " + conn_->PortName() + " открыт — проверка прибора (ver)", Origin::Internal, now);
}

void DeviceLink::OnClosed(std::int64_t now, const std::string& why)
{
    if (state_.load() == LinkState::Connected)
        LogLocked('!', "связь с прибором потеряна" + (why.empty() ? std::string() : ": " + why), Origin::Internal, now);
    FailAll("связь с прибором потеряна", true);
    probe_.reset();
    expectAbortReply_ = false;
    samplesOn_ = false;
    samplesOffPending_ = false;
    state_ = conn_->Enabled() ? LinkState::Searching : LinkState::Off;
}

void DeviceLink::FailAll(const std::string& why, bool linkLost)
{
    const std::int64_t now = now_.load();
    std::vector<RequestPtr> all;
    if (inFlight_)
        all.push_back(inFlight_);
    for (auto& r : queue_)
        all.push_back(r);
    queue_.clear();
    for (auto& r : all)
    {
        r->linkLost = linkLost;
        Complete(r, false, why, now);
    }
    inFlight_.reset();
}

void DeviceLink::Complete(const RequestPtr& r, bool ok, const std::string& final, std::int64_t now)
{
    if (r->done.load())
        return;
    r->ok = ok;
    r->final = final;
    if (inFlight_ == r)
        inFlight_.reset();
    if (r->sink)
    {
        const auto failAs = r->aborted ? DownloadSink::Result::Aborted
                            : (r->timedOut || r->linkLost) ? DownloadSink::Result::LinkLost
                                                           : DownloadSink::Result::DeviceError;
        r->sink->OnFinish(ok, final, failAs);
        if (r->dataLines)
            LogLocked('<', "(" + std::to_string(r->dataLines) + " строк D)", r->origin, now);
    }
    AfterReply(r, now);
    r->done.store(true, std::memory_order_release);
}

void DeviceLink::AfterReply(const RequestPtr& r, std::int64_t now)
{
    if (r == probe_)
    {
        probe_.reset();
        const auto v = proto::ParseVer(r->lines);
        if (r->ok && v.ours)
        {
            ver_ = v;
            haveVer_ = true;
            state_ = LinkState::Connected;
            connectedAt_ = now;
            if (++connectSeq_ > 1)
                reconnects_++;
            consecutiveTimeouts_ = 0;
            LogLocked('!',
                      "прибор подключён (" + conn_->PortName() + "), прошивка " +
                          (v.version.empty() ? std::string("без номера версии") : "v" + v.version),
                      Origin::Internal, now);
            if (probeStray_)
            {
                // Прибор досылал чужую передачу: остановить её до первых своих команд
                auto a = std::make_shared<Request>();
                a->cmd = "get abort";
                a->origin = Origin::Internal;
                a->timeoutMs = 2000;
                a->syncAbort = true;
                queue_.push_back(a);
            }
            auto s = std::make_shared<Request>();
            s->cmd = "stream " + std::to_string(pollPaused_ ? kPausedStreamMs : streamMs_);
            s->origin = Origin::Internal;
            queue_.push_back(s);
            lastStreamCmd_ = now;
            streamDirty_ = false;
            if (!pollPaused_)
            {
                auto st = std::make_shared<Request>();
                st->cmd = "status";
                st->origin = Origin::Poll;
                st->timeoutMs = 2000;
                queue_.push_back(st);
                lastStatusReq_ = now;
            }
        }
        else if (r->timedOut && ++probeTries_ < 3)
            probeAt_ = now + 300;
        else
        {
            rejectForeign_ = !r->timedOut; // что-то ответил — точно чужое устройство
            LogLocked('!', "на порту " + conn_->PortName() + " не регистратор крена (" +
                               (r->timedOut ? std::string("не отвечает на ver")
                                            : "ответ на ver: «" + (r->lines.empty() ? r->final : r->lines.front()) + "»") +
                               ")",
                      Origin::Internal, now);
            rejectWanted_ = true;
        }
        return;
    }
    const std::string cmd = LowerAscii(FirstWord(r->cmd));
    if (r->ok && cmd == "status" && !r->lines.empty())
    {
        proto::DeviceStatus st;
        std::string err;
        if (proto::ParseStatus(r->lines.back(), st, &err))
        {
            status_ = std::move(st);
            haveStatus_ = true;
            statusAt_ = now;
            statusSeq_++;
        }
        else
        {
            badLines_++;
            LogLocked('!', "status: не разобран JSON — " + err, Origin::Internal, now);
        }
    }
    else if (r->ok && cmd == "ver")
    {
        const auto v = proto::ParseVer(r->lines);
        if (v.ours)
        {
            ver_ = v;
            haveVer_ = true;
        }
    }
    else if (cmd == "samples")
    {
        if (r == samplesReq_)
            samplesReq_.reset();
        bool on = false;
        std::uint64_t dropped = 0;
        if (r->ok && proto::ParseSamplesReply(r->final, on, dropped))
        {
            samplesSupported_ = 1;
            if (on)
                samplesOnAt_ = now;
            if (on && !samplesOn_)
                samplesSession_ = 0;
            if (!on && samplesOn_ && samplesSession_)
                LogLocked('<', "(" + std::to_string(samplesSession_) + " строк R)", r->origin, now);
            samplesOn_ = on;
            if (!on)
                samplesOffPending_ = false;
            samplesDropped_ = dropped;
            samplesMsg_.clear();
        }
        else if (proto::StartsWith(r->final, "ERR unknown command"))
        {
            samplesSupported_ = -1;
            samplesOn_ = false;
            samplesMsg_ = "в прошивке прибора нет команды samples — нужна прошивка 1.5 или новее";
            LogLocked('!', "запись на ПК: " + samplesMsg_, Origin::Internal, now);
        }
        else
        {
            samplesMsg_ = r->final;
            samplesRetryAt_ = now + 3000;
        }
    }
}

int DeviceLink::SamplesSupportLocked() const
{
    if (samplesSupported_ != 0)
        return samplesSupported_;
    if (!haveVer_)
        return 0;
    const auto v = proto::VersionAtLeast(ver_.version, proto::kSamplesMajor, proto::kSamplesMinor);
    return !v ? 0 : *v ? 1 : -1;
}

void DeviceLink::HandleLine(const std::string& line, std::int64_t now)
{
    linesRx_++;
    if (proto::IsStreamHeader(line))
    {
        if (!proto::ParseStreamHeader(line, header_))
            badLines_++;
        LogLocked('S', line, Origin::Internal, now);
        return;
    }
    if (proto::IsStreamLine(line))
    {
        proto::StreamSample s;
        if (proto::ParseStreamLine(line, header_.Valid() ? header_ : proto::DefaultStreamHeader(), s))
        {
            sample_ = s;
            haveSample_ = true;
            sampleAt_ = now;
            sampleTimes_.push_back(now);
            while (!sampleTimes_.empty() && now - sampleTimes_.front() > 3000)
                sampleTimes_.pop_front();
            History::Sample h;
            h.t = static_cast<double>(now) / 1000.0;
            for (int i = 0; i < History::kSensors; i++)
            {
                const auto* d = s.Sensor(History::kAddr[i]);
                const bool ok = d && d->st == 'O';
                h.x[i] = ok ? static_cast<float>(d->x) : std::numeric_limits<float>::quiet_NaN();
                h.y[i] = ok ? static_cast<float>(d->y) : std::numeric_limits<float>::quiet_NaN();
            }
            history_.Push(h);
        }
        else
            badLines_++;
        LogLocked('S', line, Origin::Internal, now);
        return;
    }
    // Отсчёты samples: для записи на ПК — в очередь (в журнал не пишутся: до 50 строк в секунду); включены вручную
    // (терминал) — в журнал, как поток
    if (proto::IsSampleLine(line))
    {
        proto::RawSample s;
        if (!proto::ParseSampleLine(line, s))
        {
            badLines_++;
            LogLocked('S', line, Origin::Internal, now);
            return;
        }
        samplesRx_++;
        samplesSession_++;
        samplesAt_ = now;
        if (!samplesWanted_)
            LogLocked('S', line, Origin::Internal, now);
        else if (samplesQueue_.size() < kMaxQueuedSamples)
            samplesQueue_.push_back({s, now, WallMs()});
        else
            samplesOverflow_++;
        return;
    }
    if (expectAbortReply_ && (line == "OK aborted" || line == "OK nothing to abort"))
    {
        expectAbortReply_ = false;
        LogLocked('<', line, Origin::App, now);
        if (inFlight_ && inFlight_->sink)
        {
            auto r = inFlight_;
            r->aborted = true;
            Complete(r, false, "отменено", now);
        }
        return;
    }
    // Проверка ver: прибор мог ещё досылать хвост передачи прежнему хосту (строки D/E/G/F, итоговый OK) —
    // такие строки и «лишний» OK до строки BWM427 пропускаем, а не принимаем за ответ.
    if (inFlight_ && inFlight_ == probe_)
    {
        auto r = inFlight_;
        LogLocked('<', line, r->origin, now);
        if (proto::StartsWith(line, "BWM427"))
        {
            probeOurs_ = true;
            r->lines.push_back(line);
        }
        else if (proto::IsTerminator(line))
        {
            if (probeOurs_ || (!probeStray_ && !r->lines.empty()))
                Complete(r, proto::IsOkLine(line), line, now);
            else
                probeStray_ = true;
        }
        else if (proto::StartsWith(line, "D,") || proto::StartsWith(line, "E,") || proto::StartsWith(line, "G,") ||
                 proto::StartsWith(line, "F,") || proto::StartsWith(line, "{"))
            probeStray_ = true;
        else
            r->lines.push_back(line);
        return;
    }
    if (inFlight_ && inFlight_->syncAbort)
    {
        auto r = inFlight_;
        LogLocked('<', line, r->origin, now);
        if (line == "OK aborted" || line == "OK nothing to abort" || proto::IsErrLine(line))
            Complete(r, proto::IsOkLine(line), line, now);
        return; // хвост передачи — пропустить
    }
    if (inFlight_)
    {
        auto r = inFlight_;
        if (proto::IsTerminator(line))
        {
            consecutiveTimeouts_ = 0;
            LogLocked('<', line, r->origin, now);
            Complete(r, proto::IsOkLine(line), line, now);
            return;
        }
        if (r->inactivity)
            r->deadline = now + r->timeoutMs;
        if (r->sink)
        {
            r->sink->OnLine(line);
            if (proto::StartsWith(line, "D,"))
                r->dataLines++;
            else
                LogLocked('<', line, r->origin, now);
            return;
        }
        r->lines.push_back(line);
        LogLocked('<', line, r->origin, now);
        return;
    }
    LogLocked('<', line, Origin::App, now); // без запроса (например, ответ после таймаута)
}

RequestPtr DeviceLink::Send(const std::string& cmd, Origin origin, int timeoutMs)
{
    auto r = std::make_shared<Request>();
    r->cmd = cmd;
    r->origin = origin;
    r->timeoutMs = timeoutMs;
    std::lock_guard lock(m_);
    const LinkState st = state_.load();
    if (st == LinkState::Off || st == LinkState::Searching)
    {
        r->linkLost = true;
        r->final = "нет связи с прибором";
        if (origin == Origin::User)
            LogLocked('!', "не отправлено (нет связи): " + cmd, origin, now_.load());
        r->done.store(true, std::memory_order_release);
        return r;
    }
    queue_.push_back(r);
    cv_.notify_one();
    return r;
}

RequestPtr DeviceLink::SendGet(const std::string& name, std::shared_ptr<DownloadSink> sink)
{
    auto r = std::make_shared<Request>();
    r->cmd = "get " + name;
    r->origin = Origin::App;
    r->timeoutMs = 6000;
    r->inactivity = true;
    r->sink = std::move(sink);
    std::lock_guard lock(m_);
    const LinkState st = state_.load();
    if (st == LinkState::Off || st == LinkState::Searching)
    {
        r->linkLost = true;
        r->final = "нет связи с прибором";
        r->sink->OnFinish(false, r->final, DownloadSink::Result::LinkLost);
        r->done.store(true, std::memory_order_release);
        return r;
    }
    queue_.push_back(r);
    return r;
}

void DeviceLink::AbortTransfer()
{
    std::lock_guard lock(m_);
    abortWanted_ = true;
    // Ещё в очереди — просто убрать
    for (auto it = queue_.begin(); it != queue_.end();)
    {
        if ((*it)->sink)
        {
            auto r = *it;
            it = queue_.erase(it);
            r->aborted = true;
            Complete(r, false, "отменено", now_.load());
        }
        else
            ++it;
    }
}

void DeviceLink::SetEnabled(bool on)
{
    conn_->SetEnabled(on);
    std::lock_guard lock(m_);
    LogLocked('!', on ? "подключение включено" : "отключено пользователем", Origin::Internal, now_.load());
    if (!on)
        state_ = LinkState::Off;
    else if (state_.load() == LinkState::Off)
        state_ = LinkState::Searching;
}

void DeviceLink::SetPortChoice(const std::string& port)
{
    conn_->SetPortChoice(port);
}

void DeviceLink::SetStreamPeriod(int ms)
{
    std::lock_guard lock(m_);
    ms = std::clamp(ms, proto::limits::kStreamMinMs, proto::limits::kStreamMaxMs);
    if (ms != streamMs_)
    {
        streamMs_ = ms;
        streamDirty_ = true;
    }
}

void DeviceLink::SetPollPaused(bool paused)
{
    std::lock_guard lock(m_);
    if (paused == pollPaused_)
        return;
    pollPaused_ = paused;
    if (state_.load() == LinkState::Connected)
    {
        auto r = std::make_shared<Request>();
        r->cmd = "stream " + std::to_string(paused ? kPausedStreamMs : streamMs_);
        r->origin = Origin::Internal;
        queue_.push_back(r);
        lastStreamCmd_ = now_.load();
        streamDirty_ = false;
    }
}

void DeviceLink::FastPoll(std::int64_t untilMs, int periodMs)
{
    std::lock_guard lock(m_);
    fastPollUntil_ = untilMs;
    fastPollMs_ = periodMs;
}

void DeviceLink::SetSamples(bool on)
{
    std::lock_guard lock(m_);
    if (on == samplesWanted_)
        return;
    samplesWanted_ = on;
    samplesRetryAt_ = 0;
    samplesOffPending_ = !on;
    if (on)
        samplesQueue_.clear();
    cv_.notify_one();
}

std::size_t DeviceLink::TakeSamples(std::vector<RxSample>& out)
{
    std::lock_guard lock(m_);
    const std::size_t n = samplesQueue_.size();
    out.insert(out.end(), samplesQueue_.begin(), samplesQueue_.end());
    samplesQueue_.clear();
    return n;
}

LinkSnapshot DeviceLink::Snapshot() const
{
    std::lock_guard lock(m_);
    LinkSnapshot s;
    s.state = state_.load();
    s.demo = conn_->IsDemo();
    s.port = conn_->PortName();
    s.problem = conn_->Problem();
    s.ports = conn_->Ports();
    s.ver = ver_;
    s.haveVer = haveVer_;
    s.status = status_;
    s.haveStatus = haveStatus_;
    s.statusAtMs = statusAt_;
    s.statusSeq = statusSeq_;
    s.sample = sample_;
    s.haveSample = haveSample_;
    s.sampleAtMs = sampleAt_;
    const std::int64_t now = now_.load();
    std::size_t recent = 0;
    for (auto t : sampleTimes_)
        if (now - t <= 2000)
            recent++;
    s.streamHz = recent / 2.0;
    s.streamMs = streamMs_;
    s.nowMs = now;
    s.connectedAtMs = connectedAt_;
    s.connectSeq = connectSeq_;
    s.linesRx = linesRx_;
    s.cmdsTx = cmdsTx_;
    s.timeouts = timeouts_;
    s.reconnects = reconnects_;
    s.badLines = badLines_;
    s.busy = inFlight_ != nullptr || !queue_.empty();
    s.pollPaused = pollPaused_;
    s.getActive = inFlight_ && inFlight_->sink;
    s.samplesWanted = samplesWanted_;
    s.samplesOn = samplesOn_;
    s.samplesSupport = s.state == LinkState::Connected ? SamplesSupportLocked() : 0;
    s.samplesMsg = samplesMsg_;
    s.samplesRx = samplesRx_;
    s.samplesAtMs = samplesAt_;
    s.samplesDropped = samplesDropped_;
    s.samplesOverflow = samplesOverflow_;
    return s;
}

std::vector<TermLine> DeviceLink::TermSince(std::uint64_t after, std::size_t max) const
{
    std::lock_guard lock(m_);
    std::vector<TermLine> out;
    for (auto it = term_.rbegin(); it != term_.rend() && it->seq > after && out.size() < max; ++it)
        out.push_back(*it);
    std::reverse(out.begin(), out.end());
    return out;
}

void DeviceLink::Log(char kind, const std::string& text, Origin origin)
{
    std::lock_guard lock(m_);
    LogLocked(kind, text, origin, now_.load());
}

void DeviceLink::LogLocked(char kind, const std::string& text, Origin origin, std::int64_t now)
{
    TermLine t;
    t.seq = ++termSeq_;
    t.ms = now;
    t.kind = kind;
    t.origin = origin;
    t.text = text;
    term_.push_back(std::move(t));
    while (term_.size() > 3000)
        term_.pop_front();
}
