// «Запись на ПК» (карточка на странице «Измерение»): показания датчиков — сразу в CSV на этом компьютере тем же
// форматом, что файлы на карте прибора, и по желанию в книгу Excel. Отсчёты — строки R команды samples (прошивка 1.5):
// их включает DeviceLink (SetSamples; после каждого переподключения — снова), файлы пишет pcrec::Recorder
// (core/PcRecorder.hpp), отсчёты из очереди связи забирает TickRecord() (каждый проход цикла окна, и свёрнутого).
//
// Связь пропала во время записи — файлы остаются открытыми, запись продолжается после переподключения: перерыв виден
// по столбцам Time и Ms, отмечен в «Сводке» книги .xlsx, потерянные отсчёты считаются по номерам n. Прибор без
// команды samples (прошивка до 1.5) — запись недоступна (или останавливается, если такой прибор подключили посреди
// записи). Скачивание файлов с карты и запись на ПК не идут одновременно: передача файла заняла бы канал USB, и
// прибор выбрасывал бы строки R.
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

namespace
{

// 12345 -> «12 345»
std::string Thousands(std::uint64_t n)
{
    std::string s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
        s.insert(static_cast<std::size_t>(i), " ");
    return s;
}

// Текст, обрезанный справа до ширины w текущим шрифтом: «…длинный текс…»
std::string FitRight(const std::string& s, float w)
{
    if (ImGui::CalcTextSize(s.c_str()).x <= w)
        return s;
    const float ell = ImGui::CalcTextSize("…").x;
    std::size_t cut = s.size();
    while (cut > 0)
    {
        do
            cut--;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80);
        if (ImGui::CalcTextSize(s.c_str(), s.c_str() + cut).x + ell <= w)
            break;
    }
    return s.substr(0, cut) + "…";
}

// Путь, обрезанный слева: «…\Документы\Регистратор крена»
std::string FitLeft(const std::string& s, float w)
{
    if (ImGui::CalcTextSize(s.c_str()).x <= w)
        return s;
    const float ell = ImGui::CalcTextSize("…").x;
    std::size_t start = 0;
    while (start < s.size())
    {
        do
            start++;
        while (start < s.size() && (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80);
        if (ImGui::CalcTextSize(s.c_str() + start).x + ell <= w)
            break;
    }
    return "…" + s.substr(start);
}

float CheckboxWidth(const char* label)
{
    return IM_ROUND(ImGui::GetFontSize() * 1.05f) + ImGui::GetStyle().ItemInnerSpacing.x + S(2) +
           ImGui::CalcTextSize(label).x;
}

float ButtonWidth(const char* label)
{
    return ImGui::CalcTextSize(label).x + 2 * ImGui::GetStyle().FramePadding.x;
}

// Итог записи одной строкой: «2 мин 05 с · Д2 1 234 · Д3 1 230 строк · потеряно 3»
std::string ResultText(const pcrec::Recorder& r, std::int64_t linkNow)
{
    std::string s = text::Duration(static_cast<std::uint64_t>(r.ElapsedMs(linkNow) / 1000));
    for (const auto& d : r.Sensors())
        s += Fmt(" · Д%d %s", d.addr, Thousands(d.rows).c_str());
    s += r.Sensors().empty() ? " · строк нет" : " строк";
    if (r.TotalLost())
        s += " · потеряно " + Thousands(r.TotalLost());
    return s;
}

// Файлы записи — для подсказок: имена через перевод строки
std::string FileList(const pcrec::Recorder& r)
{
    std::string s;
    for (const auto& d : r.Sensors())
        if (!d.fileName.empty())
            s += (s.empty() ? "" : "\n") + d.fileName;
    if (r.Opts().xlsx && !r.Running() && r.XlsxOk())
        s += "\n" + text::PathToUtf8(r.XlsxPath().filename());
    return s;
}

} // namespace

void App::SetRecordDir(const std::string& dir, bool xlsx)
{
    std::snprintf(pcrec_.dir, sizeof(pcrec_.dir), "%s", dir.c_str());
    settings_.recordXlsx = xlsx;
}

void App::SetRecordLabel(const std::string& label)
{
    std::snprintf(pcrec_.label, sizeof(pcrec_.label), "%s", label.c_str());
}

// Почему запись на ПК сейчас не начать ("" — можно).
static std::string WhyNoPcRecord(const LinkSnapshot& s, bool download, bool firmware, bool converting)
{
    if (s.state != LinkState::Connected)
        return "нет связи с прибором";
    if (s.samplesSupport < 0)
        return s.ver.version.empty() ? std::string("нужна прошивка 1.5 или новее")
                                     : "нужна прошивка 1.5 или новее (у прибора v" + s.ver.version + ")";
    if (download)
        return "идёт скачивание файлов с карты";
    if (firmware)
        return "идёт обновление прошивки";
    if (converting)
        return "сохраняется книга .xlsx прошлой записи";
    return {};
}

bool App::StartPcRecording()
{
    if (PcRecording())
        return false;
    const std::string why = WhyNoPcRecord(snap_, dl_.running, updater_.Busy(), pcrec_.rec && pcrec_.rec->Converting());
    if (!why.empty())
    {
        Notify("Запись на ПК не начата: " + why, 2, 6000);
        return false;
    }
    auto rec = std::make_unique<pcrec::Recorder>();
    pcrec::Recorder::Options o;
    o.dir = text::PathFromUtf8(pcrec_.dir);
    o.label = pcrec_.label;
    o.xlsx = settings_.recordXlsx;
    o.device = snap_.demo ? std::string("демо-режим (имитатор прибора)")
                          : "прошивка v" + (snap_.ver.version.empty() ? std::string("?") : snap_.ver.version) + ", порт " +
                                snap_.port;
    std::string err;
    if (!rec->Start(o, pcrec::WallNowMs(), link_->NowMs(), &err))
    {
        Notify("Запись на ПК не начата: " + err, 2, 6000);
        return false;
    }
    pcrec_.rec = std::move(rec);
    pcrec_.linkLost = false;
    pcrec_.converting = false;
    link_->SetSamples(true);
    link_->Log('!', "запись на ПК: " + pcrec_.rec->Base() + "_PC_D*.CSV, папка " + Shown(pcrec_.dir));
    return true;
}

void App::StopPcRecording()
{
    if (!PcRecording())
        return;
    auto& r = *pcrec_.rec;
    pcrec_.buf.clear();
    link_->TakeSamples(pcrec_.buf); // всё принятое — в файлы
    for (const auto& x : pcrec_.buf)
        r.Add(x.s, x.wallMs, x.linkMs);
    pcrec_.buf.clear();
    link_->SetSamples(false);
    const std::int64_t now = link_->NowMs();
    r.Stop(pcrec::WallNowMs(), now);
    pcrec_.converting = r.Converting();
    pcrec_.linkLost = false;
    std::string msg = "Запись на ПК остановлена: " + ResultText(r, now);
    if (pcrec_.converting)
        msg += ". Собирается книга .xlsx…";
    else if (!r.XlsxError().empty())
        msg += ". Книга .xlsx: " + r.XlsxError();
    if (!r.Error().empty())
        msg += ". Ошибка: " + r.Error();
    Notify(msg, r.Error().empty() ? 1 : 2, 7000);
    link_->Log('!', "запись на ПК остановлена: " + ResultText(r, now));
}

void App::TickRecord()
{
    auto* r = pcrec_.rec.get();
    if (!r)
        return;
    if (r->Running())
    {
        const std::int64_t wall = pcrec::WallNowMs();
        pcrec_.buf.clear();
        link_->TakeSamples(pcrec_.buf);
        for (const auto& x : pcrec_.buf)
            r->Add(x.s, x.wallMs, x.linkMs);
        pcrec_.buf.clear();
        // Связь пропала — перерыв (файлы открыты, после переподключения DeviceLink снова включит samples)
        if (!Connected() && !pcrec_.linkLost)
        {
            r->LinkLost(wall);
            pcrec_.linkLost = true;
            link_->Log('!', "запись на ПК: нет связи с прибором — продолжится после переподключения");
        }
        else if (Connected() && pcrec_.linkLost && snap_.samplesOn)
        {
            r->LinkBack(wall);
            pcrec_.linkLost = false;
            link_->Log('!', "запись на ПК продолжается");
        }
        // Подключили прибор со старой прошивкой — писать нечем
        if (Connected() && snap_.samplesSupport < 0)
        {
            StopPcRecording();
            Notify("Запись на ПК остановлена: в прошивке прибора нет команды samples (нужна 1.5 или новее)", 2, 8000);
            return;
        }
        r->Tick(link_->NowMs());
    }
    else if (pcrec_.converting && !r->Converting())
    {
        pcrec_.converting = false;
        if (r->XlsxOk())
            Notify("Книга Excel сохранена: " + text::PathToUtf8(r->XlsxPath().filename()), 1, 6000);
        else if (!r->XlsxError().empty())
            Notify("Книга .xlsx не сохранена: " + r->XlsxError() + " (данные — в CSV)", 2, 8000);
    }
}

// Карточка «Запись на ПК». full — две строки (вторая: папка и метка / файлы записи), иначе одна (малый экран).
void App::RecordCard(float w, bool full)
{
    const bool rec = PcRecording();
    const pcrec::Recorder* r = pcrec_.rec.get();
    const bool conv = r && r->Converting();
    const std::int64_t now = link_->NowMs();
    const std::string why = WhyNoPcRecord(snap_, dl_.running, updater_.Busy(), conv);
    const float fh = ImGui::GetFrameHeight();
    const float padX = S(14), padY = full ? S(10) : S(7);
    const float h = 2 * padY + fh + (full ? fh + S(6) : 0.f);
    const float inner = w - 2 * padX;
    const float gap = S(8);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.card);
    ImGui::PushStyleColor(ImGuiCol_Border, rec ? Alpha(pal.err, 0.55f) : pal.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, padY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, S(6)));
    ImGui::BeginChild("##pcrec", ImVec2(w, h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (rec)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos();
        dl->AddRectFilled(p, ImVec2(p.x + S(6), p.y + h), U32(pal.err), S(8), ImDrawFlags_RoundCornersLeft);
    }

    // --- Строка 1: справа кнопки, слева заголовок и состояние ---
    const char* startLabel = full ? "● Начать запись на ПК" : "● Начать запись";
    const char* xlsxLabel = full ? "также в Excel (.xlsx)" : ".xlsx";
    const char* stopLabel = full ? "■ Остановить запись" : "■ Остановить";
    const char* openLabel = "Открыть папку";
    const char* dirLabel = "Папка…";
    float rightW = 0;
    if (rec)
        rightW = ButtonWidth(stopLabel) + (full ? ButtonWidth(openLabel) + gap : 0.f);
    else
        rightW = CheckboxWidth(xlsxLabel) + gap + std::max(ButtonWidth(startLabel), full ? S(220) : 0.f) +
                 (full ? 0.f : ButtonWidth(dirLabel) + gap);
    float room = inner - rightW - S(16); // ширина под левую часть

    ImGui::AlignTextToFramePadding();
    if (!rec)
    {
        {
            FontScope f(fontBold);
            ImGui::TextUnformatted("Запись на ПК");
            room -= ImGui::GetItemRectSize().x + gap;
        }
        ImGui::SameLine(0, gap);
        std::string status;
        ImVec4 c = pal.muted;
        std::string tip;
        if (conv)
            status = "сохраняется книга .xlsx…", c = pal.info;
        else if (!why.empty() && Connected())
            status = why, c = pal.warn;
        else if (r && r->StopWallMs() > 0)
        {
            status = (full ? "✓ записано: " : "✓ ") + ResultText(*r, now);
            c = r->Error().empty() ? pal.ok : pal.err;
            tip = "Последняя запись на ПК — папка " + Shown(text::PathToUtf8(r->Opts().dir)) + ":\n" + FileList(*r);
            if (!r->XlsxError().empty())
                tip += "\nКнига .xlsx: " + r->XlsxError();
            if (!r->Error().empty())
                tip += "\nОшибка: " + r->Error();
        }
        else if (!Connected())
            status = "нет связи с прибором";
        else
            status = full ? "показания датчиков — сразу в CSV на этом компьютере (формат как у файлов на карте)"
                          : "показания — в CSV на этом ПК";
        TextColored(c, "%s", FitRight(status, std::max(S(40), room)).c_str());
        if (!tip.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tip.c_str());

        ImGui::SameLine(w - padX - rightW);
        ui::Checkbox(xlsxLabel, &settings_.recordXlsx);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("По остановке записи собрать книгу Excel «…_PC.xlsx»: лист на датчик (числа — числами) и "
                              "лист «Сводка». CSV пишутся всегда.");
        if (!full)
        {
            ImGui::SameLine(0, gap);
            if (Button(dirLabel, BtnKind::Normal))
            {
                std::string d = pcrec_.dir;
                if (dialogs::PickFolder(d))
                    SetRecordDir(d, settings_.recordXlsx);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Папка записи: %s", Shown(pcrec_.dir).c_str());
        }
        ImGui::SameLine(0, gap);
        const std::string whyTip = why.empty() ? std::string() : "Нельзя: " + why;
        if (Button(startLabel, BtnKind::Primary, ImVec2(full ? std::max(ButtonWidth(startLabel), S(220)) : 0.f, 0),
                   !why.empty(), whyTip.c_str()))
            StartPcRecording();
        if (why.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("Каждый отсчёт обоих датчиков — строкой в CSV (файл на датчик) в папке\n%s", Shown(pcrec_.dir).c_str());
    }
    else
    {
        // ● ЗАПИСЬ НА ПК 00:01:23 · Д2 1 234 · 15.9 Гц · Д3 … · потеряно 0 · состояние
        {
            FontScope f(fontBold);
            const std::string head = (full ? "● ЗАПИСЬ НА ПК  " : "● ПК  ") +
                                     text::Clock(static_cast<std::uint64_t>(r->ElapsedMs(now) / 1000));
            TextColored(pal.err, "%s", head.c_str());
            room -= ImGui::GetItemRectSize().x + gap;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Запись на ПК идёт с %s\n%s", pcrec::WallString(r->StartWallMs()).c_str(),
                              FileList(*r).c_str());
        std::string state;
        ImVec4 sc = pal.muted;
        if (!Connected())
            state = full ? "нет связи — ждём прибор" : "нет связи", sc = pal.warn;
        else if (!snap_.samplesOn)
            state = "включение…", sc = pal.info;
        else if (r->Sensors().empty())
            state = "ожидание отсчётов…";
        for (const auto& d : r->Sensors())
        {
            const ImVec4 col = d.addr == 2 ? pal.sensor[0] : d.addr == 3 ? pal.sensor[1] : pal.text;
            const std::string name = Fmt("Д%d", d.addr);
            const std::string val = full && Connected() ? Fmt(" %s · %.1f Гц", Thousands(d.rows).c_str(), d.hz)
                                                        : " " + Thousands(d.rows);
            const float need = ImGui::CalcTextSize((name + val).c_str()).x;
            if (need > room)
                break;
            ImGui::SameLine(0, S(14));
            {
                FontScope f(fontBold);
                TextColored(col, "%s", name.c_str());
            }
            ImGui::SameLine(0, 0);
            ImGui::TextUnformatted(val.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Датчик Д%d: %s строк, %.1f в секунду, потеряно %s\n%s", d.addr,
                                  Thousands(d.rows).c_str(), d.hz, Thousands(d.lost).c_str(), d.fileName.c_str());
            room -= need + S(14);
        }
        const std::uint64_t lost = r->TotalLost();
        const std::string lostText = "потеряно " + Thousands(lost);
        if (ImGui::CalcTextSize(lostText.c_str()).x + S(14) <= room)
        {
            ImGui::SameLine(0, S(14));
            TextColored(lost ? pal.warn : pal.muted, "%s", lostText.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Отсчётов, которые не дошли до ПК (пропуски номеров n): прибор не успел отправить "
                                  "строку или пропала связь.");
            room -= ImGui::CalcTextSize(lostText.c_str()).x + S(14);
        }
        if (!state.empty() && room > S(60))
        {
            ImGui::SameLine(0, S(14));
            TextColored(sc, "%s", FitRight(state, room - S(14)).c_str());
            if (!Connected() && ImGui::IsItemHovered())
                ImGui::SetTooltip("Связь с прибором пропала. Файлы открыты: когда прибор снова подключится, запись\n"
                                  "продолжится в те же файлы (перерыв виден по столбцам Time и Ms и в «Сводке» .xlsx).");
        }
        ImGui::SameLine(w - padX - rightW);
        if (full)
        {
            if (Button(openLabel, BtnKind::Normal))
                dialogs::OpenFolder(text::PathToUtf8(r->Opts().dir));
            ImGui::SameLine(0, gap);
        }
        if (Button(stopLabel, BtnKind::Danger))
            StopPcRecording();
    }

    // --- Строка 2: папка и метка (или файлы идущей записи) ---
    if (full)
    {
        if (!rec)
        {
            ImGui::AlignTextToFramePadding();
            Muted("Папка");
            ImGui::SameLine(0, gap);
            const float labelW = S(190);
            const float tail = ButtonWidth("Выбрать…") + ButtonWidth(openLabel) + ImGui::CalcTextSize("Метка").x +
                               labelW + ImGui::CalcTextSize("(?)").x + 4 * gap + S(12) + S(4);
            ImGui::SetNextItemWidth(std::max(S(120), inner - ImGui::GetCursorPosX() + padX - tail));
            PathInput("##recdir", pcrec_.dir, sizeof(pcrec_.dir));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Shown(pcrec_.dir).c_str());
            ImGui::SameLine(0, gap);
            if (Button("Выбрать…", BtnKind::Normal))
            {
                std::string d = pcrec_.dir;
                if (dialogs::PickFolder(d))
                    SetRecordDir(d, settings_.recordXlsx);
            }
            ImGui::SameLine(0, gap);
            if (Button(openLabel, BtnKind::Normal))
            {
                std::error_code ec;
                fs::create_directories(text::PathFromUtf8(pcrec_.dir), ec);
                dialogs::OpenFolder(pcrec_.dir);
            }
            ImGui::SameLine(0, S(12) + gap);
            Muted("Метка");
            ImGui::SameLine(0, gap);
            ImGui::SetNextItemWidth(labelW);
            ImGui::InputTextWithHint("##reclabel", "необязательно", pcrec_.label, sizeof(pcrec_.label));
            Hint("Метка замера в имени файлов, например «опыт 3»: 2026-10-08_17-19-28_опыт 3_PC_D2.CSV. Без метки — "
                 "только дата и время начала. Существующие файлы не перезаписываются.");
        }
        else
        {
            FontScope f(fontSmall);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (fh - ImGui::GetFontSize()) * 0.5f);
            std::string files;
            for (const auto& d : r->Sensors())
                if (!d.fileName.empty())
                    files += (files.empty() ? "" : ", ") + d.fileName;
            std::string line = files.empty() ? std::string("Файлы появятся с первыми отсчётами") : "Файлы: " + files;
            line += " · папка " + Shown(text::PathToUtf8(r->Opts().dir));
            if (r->Opts().xlsx)
                line += " · книга .xlsx — после остановки";
            std::string err = r->Error();
            if (!err.empty())
                TextColored(pal.err, "%s", FitRight("Ошибка записи: " + err, inner).c_str());
            else
                Muted("%s", FitRight(line, inner).c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", line.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

} // namespace ui
