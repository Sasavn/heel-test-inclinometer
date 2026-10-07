// Страница «Файлы на карте»: список замеров на карте прибора (files), выбор, скачивание в папку (get: G/D/E,
// проверка CRC-32), прогресс, отмена. Во время записи замера — недоступно (прибор и сам отказывает).
#include "App.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include <imgui.h>

#include "../core/TextUtil.hpp"
#include "Dialogs.hpp"
#include "Widgets.hpp"

namespace ui
{

namespace fs = std::filesystem;

void App::RequestFileList()
{
    if (files_.listReq || !Connected())
        return;
    files_.listReq = link_->Send("files", Origin::App, 15000);
}

void App::SelectAllFiles(bool on)
{
    for (const auto& f : files_.list)
        files_.sel[f.name] = on;
}

void App::SetDownloadDir(const std::string& dir)
{
    std::snprintf(files_.dir, sizeof(files_.dir), "%s", dir.c_str());
}

void App::StartDownload()
{
    if (dl_.running || !Connected())
        return;
    dl_.jobs.clear();
    for (const auto& f : files_.list)
        if (files_.sel[f.name])
        {
            FileJob j;
            j.f = f;
            dl_.jobs.push_back(std::move(j));
        }
    if (dl_.jobs.empty())
    {
        Notify("Отметьте файлы для скачивания", 2);
        return;
    }
    std::error_code ec;
    fs::create_directories(text::PathFromUtf8(files_.dir), ec);
    if (!fs::is_directory(text::PathFromUtf8(files_.dir), ec))
    {
        Notify(std::string("Не удалось создать папку ") + files_.dir, 2);
        return;
    }
    dl_.running = true;
    dl_.cancel = false;
    dl_.current = -1;
    dl_.doneBytes = 0;
    dl_.speed = 0;
    dl_.lastBytes = 0;
    dl_.startMs = dl_.lastSpeedMs = link_->NowMs();
    dl_.waitUntil = 0;
    dl_.summary.clear();
    link_->SetPollPaused(true); // без опроса status и потока — быстрее
}

void App::TickFiles()
{
    const std::int64_t now = link_->NowMs();
    // Ответ files
    if (files_.listReq && files_.listReq->done.load())
    {
        auto r = files_.listReq;
        files_.listReq.reset();
        if (r->ok)
        {
            std::vector<proto::FileEntry> list;
            for (const auto& l : r->lines)
            {
                proto::FileEntry f;
                if (proto::ParseFileLine(l, f))
                    list.push_back(std::move(f));
            }
            std::sort(list.begin(), list.end(), [](const proto::FileEntry& a, const proto::FileEntry& b) {
                if (a.measurement != b.measurement)
                    return a.measurement > b.measurement;
                if (a.sensorAddr != b.sensorAddr)
                    return a.sensorAddr < b.sensorAddr;
                return a.name < b.name;
            });
            files_.list = std::move(list);
            files_.listed = true;
            files_.listMsg.clear();
        }
        else
        {
            files_.listMsg = r->final == "ERR busy recording" ? "Идёт запись замера — список недоступен до её остановки."
                             : r->final == "ERR no card"     ? "В приборе нет карты памяти."
                                                             : "Не удалось получить список: " + r->final;
            files_.listed = true;
        }
    }
    // Список — при первом заходе на страницу
    if (page_ == Page::Files && Connected() && !files_.listed && !files_.listReq && !dl_.running && HaveStatus() &&
        snap_.status.sd == "READY")
        RequestFileList();

    if (!dl_.running)
        return;

    // Скорость
    std::uint64_t cur = dl_.doneBytes;
    if (dl_.current >= 0 && dl_.jobs[static_cast<std::size_t>(dl_.current)].sink)
        cur += dl_.jobs[static_cast<std::size_t>(dl_.current)].sink->Received();
    if (now - dl_.lastSpeedMs >= 500)
    {
        const double inst = static_cast<double>(cur - dl_.lastBytes) * 1000.0 / static_cast<double>(now - dl_.lastSpeedMs);
        dl_.speed = dl_.speed <= 0 ? inst : 0.6 * dl_.speed + 0.4 * inst;
        dl_.lastBytes = cur;
        dl_.lastSpeedMs = now;
    }

    if (dl_.current >= 0)
    {
        auto& j = dl_.jobs[static_cast<std::size_t>(dl_.current)];
        if (!j.req || !j.req->done.load())
            return;
        const auto res = j.sink->GetResult();
        if (res == DownloadSink::Result::Ok)
        {
            j.state = 2;
            j.msg = j.sink->Message();
            dl_.doneBytes += j.sink->Received();
            files_.note[j.f.name] = "✓ скачан, CRC совпал";
            files_.noteKind[j.f.name] = 1;
        }
        else if (!dl_.cancel && res != DownloadSink::Result::Aborted && res != DownloadSink::Result::DeviceError &&
                 res != DownloadSink::Result::IoError && j.attempts < 2)
        {
            j.state = 0; // ещё раз, целиком
            j.attempts++;
            dl_.waitUntil = now + 1500;
            link_->Log('!', "скачивание " + j.f.name + ": " + j.sink->Message() + " — повтор");
        }
        else
        {
            j.state = 3;
            j.msg = res == DownloadSink::Result::Aborted ? std::string("отменено") : j.sink->Message();
            files_.note[j.f.name] = "✗ " + j.msg;
            files_.noteKind[j.f.name] = 2;
        }
        j.req.reset();
        dl_.current = -1;
    }

    if (dl_.cancel)
        for (auto& j : dl_.jobs)
            if (j.state == 0)
            {
                j.state = 3;
                j.msg = "отменено";
            }
    if (now < dl_.waitUntil)
        return;

    // Следующий файл
    for (std::size_t i = 0; i < dl_.jobs.size(); i++)
    {
        auto& j = dl_.jobs[i];
        if (j.state != 0)
            continue;
        if (!Connected())
        {
            if (now - dl_.lastSpeedMs > 30000)
            {
                for (auto& k : dl_.jobs)
                    if (k.state == 0)
                    {
                        k.state = 3;
                        k.msg = "нет связи с прибором";
                    }
                break;
            }
            return; // ждём переподключения
        }
        const fs::path target = text::PathFromUtf8(files_.dir) / text::PathFromUtf8(j.f.name);
        std::error_code ec;
        if (!settings_.overwriteFiles && fs::exists(target, ec) && fs::file_size(target, ec) == j.f.size)
        {
            j.state = 4;
            j.msg = "уже есть в папке";
            files_.note[j.f.name] = "уже есть в папке";
            files_.noteKind[j.f.name] = 3;
            continue;
        }
        j.sink = std::make_shared<DownloadSink>(target, j.f.size);
        j.req = link_->SendGet(j.f.name, j.sink);
        j.state = 1;
        dl_.current = static_cast<int>(i);
        dl_.fileStartMs = now;
        return;
    }

    // Всё
    int ok = 0, fail = 0, skip = 0;
    for (const auto& j : dl_.jobs)
        (j.state == 2 ? ok : j.state == 4 ? skip : fail)++;
    dl_.running = false;
    link_->SetPollPaused(false);
    dl_.summary = Fmt("Скачано %d из %d", ok, static_cast<int>(dl_.jobs.size()));
    if (skip)
        dl_.summary += Fmt(", пропущено %d (уже есть)", skip);
    if (fail)
        dl_.summary += Fmt(", ошибок %d", fail);
    dl_.summary += Fmt(" · %s за %s", text::Bytes(dl_.doneBytes).c_str(),
                       text::Duration(static_cast<std::uint64_t>((now - dl_.startMs) / 1000)).c_str());
    Notify(dl_.summary, fail ? 2 : 1, 6000);
}

void App::PageFiles()
{
    if (!Connected())
    {
        NotConnectedPanel("Здесь — файлы замеров на карте прибора: выбор и скачивание в папку на ПК с проверкой CRC.");
        return;
    }
    const auto& s = snap_.status;
    const bool rec = Recording();
    const float W = ImGui::GetContentRegionAvail().x;

    // Состояние карты и кнопки
    if (BeginCard("##f_head", ImVec2(W, 0)))
    {
        CardTitle("Файлы замеров на карте прибора");
        std::string state = "—";
        if (HaveStatus())
        {
            if (s.sd == "NO_CARD")
                state = "в приборе нет карты памяти";
            else if (s.sd == "ERROR")
                state = Fmt("ошибка карты (FatFs %d) — верните тумблер записи в STOP", s.sdErr);
            else
            {
                state = rec ? "идёт запись замера" : "готова";
                if (s.sdFreeMb)
                    state += " · свободно " + text::MegaBytes(*s.sdFreeMb) + " из " + text::MegaBytes(s.sdTotalMb);
            }
        }
        ImGui::TextUnformatted(("Карта памяти: " + state).c_str());
        if (files_.listed && files_.listMsg.empty())
        {
            std::uint64_t total = 0;
            for (const auto& f : files_.list)
                total += f.size;
            ImGui::SameLine(0, S(16));
            Muted("· %zu %s, %s", files_.list.size(), text::Plural(files_.list.size(), "файл", "файла", "файлов"),
                  text::Bytes(total).c_str());
        }
        ImGui::Spacing();
        const bool canList = HaveStatus() && s.sd == "READY" && !dl_.running;
        if (Button(files_.listReq ? "Обновление…" : "Обновить список", BtnKind::Normal, ImVec2(0, 0),
                   !canList || files_.listReq != nullptr,
                   rec ? "Идёт запись замера" : dl_.running ? "Идёт скачивание" : "Карта не готова"))
            RequestFileList();
        ImGui::SameLine(0, S(8));
        if (Button("Выбрать все", BtnKind::Normal, ImVec2(0, 0), files_.list.empty() || dl_.running))
            SelectAllFiles(true);
        ImGui::SameLine(0, S(8));
        if (Button("Снять выбор", BtnKind::Normal, ImVec2(0, 0), files_.list.empty() || dl_.running))
            SelectAllFiles(false);
        ImGui::SameLine(0, S(8));
        if (Button("Последний замер", BtnKind::Normal, ImVec2(0, 0), files_.list.empty() || dl_.running))
        {
            SelectAllFiles(false);
            const int last = files_.list.front().measurement;
            for (const auto& f : files_.list)
                if (f.measurement == last)
                    files_.sel[f.name] = true;
        }
        if (rec)
        {
            ImGui::Spacing();
            Banner(pal.warn, pal.warnBg, "⚠",
                   "Идёт запись замера: список и скачивание недоступны, пока запись не остановлена тумблером на приборе "
                   "(карта занята записью).");
        }
        if (!files_.listMsg.empty())
        {
            ImGui::Spacing();
            Banner(pal.err, pal.errBg, "✗", files_.listMsg.c_str());
        }
    }
    EndCard();
    ImGui::Dummy(ImVec2(0, S(6)));

    // Нижняя панель (папка, скачивание): её высота — с прошлого кадра
    if (bottomH_ <= 0)
        bottomH_ = S(170);
    const float tableH = std::max(S(160), ImGui::GetContentRegionAvail().y - bottomH_ - S(6) -
                                              2 * ImGui::GetStyle().ItemSpacing.y);

    // Таблица
    if (BeginCard("##f_list", ImVec2(W, tableH)))
    {
        if (!files_.listed && files_.listReq)
            Muted("Чтение списка файлов с карты…");
        else if (files_.listed && files_.list.empty() && files_.listMsg.empty())
            Muted("На карте нет файлов замеров (*.CSV).");
        else if (!files_.list.empty() &&
                 ImGui::BeginTable("##files", 7,
                                   ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                                       ImGuiTableFlags_SizingFixedFit,
                                   ImVec2(0, ImGui::GetContentRegionAvail().y)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(28));
            ImGui::TableSetupColumn("Замер", ImGuiTableColumnFlags_WidthFixed, S(70));
            ImGui::TableSetupColumn("Датчик", ImGuiTableColumnFlags_WidthFixed, S(64));
            ImGui::TableSetupColumn("Файл", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Размер", ImGuiTableColumnFlags_WidthFixed, S(90));
            ImGui::TableSetupColumn("Изменён", ImGuiTableColumnFlags_WidthFixed, S(140));
            ImGui::TableSetupColumn("Состояние", ImGuiTableColumnFlags_WidthFixed, S(210));
            ImGui::TableHeadersRow();
            int prevMeas = -2;
            for (const auto& f : files_.list)
            {
                ImGui::TableNextRow(0, ImGui::GetFrameHeight());
                ImGui::PushID(f.name.c_str());
                ImGui::TableNextColumn();
                bool sel = files_.sel[f.name];
                if (dl_.running)
                    ImGui::BeginDisabled();
                if (ImGui::Checkbox("##sel", &sel))
                    files_.sel[f.name] = sel;
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                const std::string meas = f.measurement >= 0 ? Fmt("M%03d", f.measurement) : std::string("—");
                const bool firstOfMeas = f.measurement != prevMeas;
                prevMeas = f.measurement;
                if (ImGui::Selectable(firstOfMeas ? meas.c_str() : " ", sel,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                      ImVec2(0, ImGui::GetFrameHeight() - ImGui::GetStyle().CellPadding.y)))
                    files_.sel[f.name] = !sel;
                if (dl_.running)
                    ImGui::EndDisabled();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                if (f.sensorAddr >= 0)
                    TextColored(f.sensorAddr == 2 ? pal.sensor[0] : f.sensorAddr == 3 ? pal.sensor[1] : pal.text, "Д%d",
                                f.sensorAddr);
                else
                    Muted("—");
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(f.name.c_str());
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(text::Bytes(f.size).c_str());
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                Muted("%s", f.date.c_str());
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                // Состояние: идёт / готово / ошибка
                int jobState = -1;
                for (const auto& j : dl_.jobs)
                    if (j.f.name == f.name && dl_.running)
                        jobState = j.state;
                if (jobState == 1)
                {
                    const auto& j = *std::find_if(dl_.jobs.begin(), dl_.jobs.end(), [&](const FileJob& k) { return k.f.name == f.name; });
                    const double frac = j.sink && j.sink->Total() ? static_cast<double>(j.sink->Received()) / j.sink->Total() : 0.0;
                    TextColored(pal.info, "⇩ %.0f %%", frac * 100.0);
                }
                else if (jobState == 0)
                    Muted("в очереди");
                else if (files_.note.count(f.name))
                {
                    const int k = files_.noteKind[f.name];
                    TextColored(k == 1 ? pal.ok : k == 2 ? pal.err : pal.muted, "%s", files_.note[f.name].c_str());
                    if (k == 2 && ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", files_.note[f.name].c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    EndCard();
    ImGui::Dummy(ImVec2(0, S(6)));

    // Папка и скачивание
    if (BeginCard("##f_dl", ImVec2(W, 0)))
    {
        int nSel = 0;
        std::uint64_t selBytes = 0;
        for (const auto& f : files_.list)
            if (files_.sel[f.name])
            {
                nSel++;
                selBytes += f.size;
            }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Папка на ПК");
        ImGui::SameLine(S(120));
        ImGui::SetNextItemWidth(W - S(120) - S(320) - S(28));
        if (dl_.running)
            ImGui::BeginDisabled();
        ImGui::InputText("##dir", files_.dir, sizeof(files_.dir));
        ImGui::SameLine(0, S(8));
        if (Button("Выбрать…", BtnKind::Normal, ImVec2(S(130), 0)))
        {
            std::string d = files_.dir;
            if (dialogs::PickFolder(d))
                SetDownloadDir(d);
        }
        if (dl_.running)
            ImGui::EndDisabled();
        ImGui::SameLine(0, S(8));
        if (Button("Открыть папку", BtnKind::Normal, ImVec2(S(170), 0)))
        {
            std::error_code ec;
            fs::create_directories(text::PathFromUtf8(files_.dir), ec);
            dialogs::OpenFolder(files_.dir);
        }
        ImGui::Checkbox("Перезаписывать файлы, которые уже есть в папке", &settings_.overwriteFiles);
        Hint("Без отметки файл, который уже лежит в папке с тем же размером, не скачивается заново.");
        ImGui::Spacing();
        if (!dl_.running)
        {
            const std::string label = nSel ? Fmt("Скачать выбранные (%d · %s)", nSel, text::Bytes(selBytes).c_str())
                                           : std::string("Скачать выбранные");
            const char* why = rec ? "Идёт запись замера — остановите её тумблером на приборе"
                              : nSel == 0 ? "Отметьте файлы в списке"
                                          : nullptr;
            if (Button(label.c_str(), BtnKind::Primary, ImVec2(S(320), S(36)), why != nullptr, why))
                StartDownload();
            if (!dl_.summary.empty())
            {
                ImGui::SameLine(0, S(14));
                ImGui::AlignTextToFramePadding();
                Muted("%s", dl_.summary.c_str());
            }
        }
        else
        {
            int idx = 0, n = static_cast<int>(dl_.jobs.size());
            std::uint64_t total = 0;
            for (const auto& j : dl_.jobs)
                total += j.f.size;
            const FileJob* cur = dl_.current >= 0 ? &dl_.jobs[static_cast<std::size_t>(dl_.current)] : nullptr;
            idx = cur ? dl_.current + 1 : 0;
            const std::uint64_t got = cur && cur->sink ? cur->sink->Received() : 0;
            const std::uint64_t all = dl_.doneBytes + got;
            if (cur)
            {
                const double frac = cur->f.size ? static_cast<double>(got) / static_cast<double>(cur->f.size) : 1.0;
                ImGui::Text("Файл %d из %d: %s", idx, n, cur->f.name.c_str());
                Progress(static_cast<float>(frac), Fmt("%s из %s", text::Bytes(got).c_str(), text::Bytes(cur->f.size).c_str()).c_str());
            }
            else
            {
                ImGui::TextUnformatted("Подготовка…");
                Progress(0.f, "");
            }
            const double speed = dl_.speed;
            std::string eta;
            if (speed > 100)
                eta = " · осталось ~" + text::Duration(static_cast<std::uint64_t>((total > all ? total - all : 0) / speed) + 1);
            Muted("Всего: %s из %s · %s/с%s", text::Bytes(all).c_str(), text::Bytes(total).c_str(),
                  text::Bytes(static_cast<std::uint64_t>(speed)).c_str(), eta.c_str());
            Progress(total ? static_cast<float>(static_cast<double>(all) / static_cast<double>(total)) : 0.f, "", S(8));
            if (Button(dl_.cancel ? "Отмена…" : "Отмена", BtnKind::Danger, ImVec2(S(140), 0), dl_.cancel))
            {
                dl_.cancel = true;
                link_->AbortTransfer();
            }
        }
    }
    EndCard();
    bottomH_ = ImGui::GetItemRectSize().y;
}

} // namespace ui
