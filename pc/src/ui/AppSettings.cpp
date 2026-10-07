// Страница «Настройки»: параметры прибора (set …) с проверкой пределов app.h; часы (set time); ноль; смена
// Modbus-адреса датчика (addr OLD NEW, итог — поле svc в status); настройки самой программы.
#include "App.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>

#include <imgui.h>

#include "../core/TextUtil.hpp"
#include "Widgets.hpp"

namespace ui
{

namespace
{

struct FieldInfo
{
    const char* label;
    const char* unit;
    const char* hint;
    bool isInt;
    double min, max, def, step;
    int decimals;
};

// Порядок — как SetField в App.hpp: freq, alpha, gap, rollwin, rollhz, rollcalm, rollhyst, batalarm, theme
const FieldInfo kFields[9] = {
    {"Частота опроса", "Гц",
     "Как часто прибор опрашивает датчики и пишет строки в файл замера. Если шина не успевает, фактическая частота "
     "будет ниже (видно на «Измерении»).",
     true, proto::limits::kFreq.min, proto::limits::kFreq.max, proto::limits::kFreq.def, 1, 0},
    {"Сглаживание α", "",
     "Коэффициент экспоненциального сглаживания углов: меньше — плавнее, но медленнее отклик; больше — быстрее и "
     "шумнее.",
     false, proto::limits::kAlpha.min, proto::limits::kAlpha.max, proto::limits::kAlpha.def, 0.01, 2},
    {"Пауза шины RS485", "мс",
     "Тишина на линии перед каждым запросом. Меньше — быстрее опрос, но датчик может не успеть переключиться на "
     "приём.",
     true, proto::limits::kGap.min, proto::limits::kGap.max, proto::limits::kGap.def, 1, 0},
    {"Окно качки", "с", "За сколько последних секунд считается размах угла (максимум − минимум) по каждой оси.",
     true, proto::limits::kRollWin.min, proto::limits::kRollWin.max, proto::limits::kRollWin.def, 1, 0},
    {"Отсчётов в секунду", "1/с", "Сколько раз в секунду значение угла попадает в окно качки.", true,
     proto::limits::kRollHz.min, proto::limits::kRollHz.max, proto::limits::kRollHz.def, 1, 0},
    {"Порог покоя", "°", "Размах угла за окно меньше порога — ось «в покое». Все оси в покое — «ГОТОВ».", false,
     proto::limits::kRollCalm.min, proto::limits::kRollCalm.max, proto::limits::kRollCalm.def, 0.1, 2},
    {"Гистерезис покоя", "°", "«Покой» снимается, только когда размах больше порога + гистерезис (чтобы не мигало).",
     false, proto::limits::kRollHyst.min, proto::limits::kRollHyst.max, proto::limits::kRollHyst.def, 0.05, 2},
    {"Порог тревоги АКБ", "В", "Ниже этого напряжения прибор показывает тревогу «АКБ разряжена» (3S Li-ion: 9,9…12,6 В).",
     false, proto::limits::kBatAlarm.min, proto::limits::kBatAlarm.max, proto::limits::kBatAlarm.def, 0.1, 1},
    {"Тема экрана", "", "Оформление экрана самого прибора (тёмная или светлая).", true, 0, 1, 0, 1, 0},
};

double DeviceValue(const proto::DeviceStatus& s, int i)
{
    switch (i)
    {
    case 0: return s.freq;
    case 1: return s.alpha;
    case 2: return s.gap;
    case 3: return s.rollWin;
    case 4: return s.rollHz;
    case 5: return s.rollCalm;
    case 6: return s.rollHyst;
    case 7: return s.batAlarm;
    case 8: return s.theme == "light" ? 1.0 : 0.0;
    default: return 0.0;
    }
}

bool Valid(int i, double v)
{
    const auto& f = kFields[i];
    const double eps = 0.5 * std::pow(10.0, -f.decimals);
    if (!std::isfinite(v) || v < f.min - eps || v > f.max + eps)
        return false;
    return !f.isInt || std::fabs(v - std::round(v)) < 1e-9;
}

bool Same(int i, double a, double b)
{
    return std::fabs(a - b) < 0.5 * std::pow(10.0, -kFields[i].decimals);
}

// «1…50», «0.01…0.99»
std::string RangeText(int i)
{
    const auto& f = kFields[i];
    return proto::Fixed(f.min, f.decimals) + "…" + proto::Fixed(f.max, f.decimals);
}

std::string SetCommand(int i, const char* key, double v)
{
    if (i == 8)
        return std::string("set theme ") + (v >= 0.5 ? "light" : "dark");
    if (kFields[i].isInt)
        return std::string("set ") + key + " " + std::to_string(static_cast<int>(std::lround(v)));
    return std::string("set ") + key + " " + proto::Fixed(v, kFields[i].decimals);
}

} // namespace

void App::EditSetting(const std::string& name, double value)
{
    for (auto& f : set_.f)
        if (name == f.key)
        {
            f.value = value;
            f.edited = true;
        }
}

void App::ApplySettings()
{
    if (!HaveStatus() || !set_.applying.empty())
        return;
    set_.applyLog.clear();
    set_.applyOk = true;
    for (int i = 0; i < 9; i++)
    {
        auto& f = set_.f[i];
        if (f.edited && Valid(i, f.value) && !Same(i, f.value, DeviceValue(snap_.status, i)))
            set_.applying.emplace_back(i, link_->Send(SetCommand(i, f.key, f.value)));
        else
            f.edited = false;
    }
}

void App::SyncTime()
{
    if (!Connected() || set_.timeReq)
        return;
    // Секунда в секунду: команда уходит в ближайшие миллисекунды
    set_.timeReq = link_->Send("set time " + text::LocalTimeString(std::time(nullptr)));
}

void App::StartAddressChange(int from, int to)
{
    set_.addrFrom = from;
    set_.addrTo = to;
    set_.addrPhase = 1;
    set_.addrMsg.clear();
    set_.addrReq = link_->Send(Fmt("addr %d %d", from, to));
}

void App::TickSettings()
{
    const std::int64_t now = link_->NowMs();
    // Применение настроек
    if (!set_.applying.empty() && std::all_of(set_.applying.begin(), set_.applying.end(),
                                              [](const auto& a) { return a.second->done.load(); }))
    {
        for (const auto& [i, r] : set_.applying)
        {
            set_.applyLog.push_back(r->cmd + "  →  " + r->final);
            set_.applyOk = set_.applyOk && r->ok;
            // Принятое поле снова показывает значение прибора (с его округлением)
            if (r->ok)
                set_.f[i].edited = false;
        }
        set_.applying.clear();
        Notify(set_.applyOk ? "Настройки приняты прибором" : "Часть настроек не принята — см. «Настройки»",
               set_.applyOk ? 1 : 2);
    }
    if (set_.timeReq && set_.timeReq->done.load())
    {
        set_.timeOk = set_.timeReq->ok;
        set_.timeMsg = set_.timeReq->ok ? "Часы прибора установлены: " + set_.timeReq->final.substr(std::min<std::size_t>(8, set_.timeReq->final.size()))
                                        : "Не удалось: " + set_.timeReq->final;
        Notify(set_.timeOk ? "Время прибора синхронизировано с ПК" : set_.timeMsg, set_.timeOk ? 1 : 2);
        set_.timeReq.reset();
    }
    if (set_.zeroReq && set_.zeroReq->done.load())
    {
        const auto& r = set_.zeroReq;
        if (r->ok)
            Notify(r->cmd == "zero reset" ? "Ноль сброшен" : "Ноль установлен (" + r->final.substr(std::min<std::size_t>(9, r->final.size())) + ")", 1);
        else
            Notify("Ноль: " + r->final, 2);
        set_.zeroReq.reset();
    }
    // Смена адреса
    if (set_.addrPhase == 1 && set_.addrReq && set_.addrReq->done.load())
    {
        if (set_.addrReq->ok)
        {
            set_.addrPhase = 2;
            set_.addrDeadline = now + 10000;
            set_.addrSeq = snap_.statusSeq;
            link_->FastPoll(now + 12000, 300);
        }
        else
        {
            set_.addrPhase = 4;
            const std::string& f = set_.addrReq->final;
            if (f.find("answers on the bus") != std::string::npos)
                set_.addrMsg = Fmt("Адрес %d уже занят отвечающим датчиком — выберите другой или отключите тот датчик.",
                                   set_.addrTo);
            else if (f.find("recording") != std::string::npos)
                set_.addrMsg = "Идёт запись замера — остановите её тумблером.";
            else
                set_.addrMsg = "Прибор отказал: " + f;
        }
        set_.addrReq.reset();
    }
    if (set_.addrPhase == 2)
    {
        if (snap_.statusSeq > set_.addrSeq && snap_.haveStatus && snap_.status.svc != "BUSY")
        {
            if (snap_.status.svc == "OK")
            {
                set_.addrPhase = 3;
                set_.addrMsg = Fmt("Готово: датчик сохранил новый адрес %d. Подключите остальные датчики.", set_.addrTo);
                Notify(Fmt("Адрес датчика изменён: %d → %d", set_.addrFrom, set_.addrTo), 1);
            }
            else
            {
                set_.addrPhase = 4;
                set_.addrMsg = Fmt("Датчик с адресом %d не ответил или отказался менять адрес. Проверьте, что на шине "
                                   "только он и что его текущий адрес — %d.",
                                   set_.addrFrom, set_.addrFrom);
            }
        }
        else if (now > set_.addrDeadline)
        {
            set_.addrPhase = 4;
            set_.addrMsg = "Нет итога за 10 с (связь с прибором?)";
        }
    }
}

void App::PageSettings()
{
    if (!Connected())
    {
        NotConnectedPanel("Здесь — настройки прибора, часы, ноль и смена Modbus-адреса датчика.");
        ImGui::Dummy(ImVec2(0, S(10)));
        SettingsPcCard();
        return;
    }
    const float W = ImGui::GetContentRegionAvail().x;
    const float gap = S(12);
    if (W < S(1000))
    {
        // Узкое окно: одна колонка (страница прокручивается)
        SettingsDeviceCard();
        ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
        SettingsClockCard();
        ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
        SettingsAddrCard();
        ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
        SettingsPcCard();
        return;
    }
    const float leftW = std::floor(W * 0.54f);
    ImGui::BeginGroup();
    ImGui::PushID("left");
    ImGui::BeginChild("##colL", ImVec2(leftW, 0), ImGuiChildFlags_AutoResizeY);
    SettingsDeviceCard();
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::EndGroup();
    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    ImGui::BeginChild("##colR", ImVec2(W - leftW - gap, 0), ImGuiChildFlags_AutoResizeY);
    SettingsClockCard();
    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
    SettingsAddrCard();
    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
    SettingsPcCard();
    ImGui::EndChild();
    ImGui::EndGroup();
}

void App::SettingsDeviceCard()
{
    const bool have = snap_.haveStatus;
    if (BeginCard("##s_dev"))
    {
        CardTitle("Настройки прибора",
                  "Меняются так же, как из меню прибора, и сохраняются в его флеш-памяти через 3 с (во время записи "
                  "замера — после её остановки).");
        int changed = 0;
        bool invalid = false;
        static const char* groups[] = {"Опрос и фильтр", "Оценка качки (ГОТОВ / КАЧКА)", "Питание и экран"};
        const int groupStart[] = {0, 3, 7};
        for (int g = 0; g < 3; g++)
        {
            ImGui::Dummy(ImVec2(0, S(2)));
            {
                FontScope f(fontBold);
                TextColored(pal.accent, "%s", groups[g]);
            }
            const int end = g < 2 ? groupStart[g + 1] : 9;
            char tid[16];
            std::snprintf(tid, sizeof(tid), "##g%d", g);
            if (!ImGui::BeginTable(tid, 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
                continue;
            ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("i", ImGuiTableColumnFlags_WidthFixed, S(176));
            ImGui::TableSetupColumn("r", ImGuiTableColumnFlags_WidthFixed, S(132));
            for (int i = groupStart[g]; i < end; i++)
            {
                auto& fld = set_.f[i];
                const auto& info = kFields[i];
                const double dev = have ? DeviceValue(snap_.status, i) : info.def;
                if (!fld.edited)
                    fld.value = dev;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(info.label);
                Hint(info.hint);
                ImGui::TableNextColumn();
                const bool ok = Valid(i, fld.value);
                invalid |= fld.edited && !ok;
                if (!ok)
                {
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, pal.errBg);
                    ImGui::PushStyleColor(ImGuiCol_Border, pal.err);
                }
                ImGui::PushID(i);
                ImGui::SetNextItemWidth(info.unit[0] ? S(118) : S(160));
                bool edited = false;
                if (i == 8)
                {
                    int t = fld.value >= 0.5 ? 1 : 0;
                    const char* themes[] = {"тёмная", "светлая"};
                    if (ImGui::Combo("##v", &t, themes, 2))
                    {
                        fld.value = t;
                        edited = true;
                    }
                }
                else if (info.isInt)
                {
                    int v = static_cast<int>(std::lround(fld.value));
                    if (ImGui::InputInt("##v", &v, 1, 5))
                    {
                        fld.value = v;
                        edited = true;
                    }
                }
                else
                {
                    double v = fld.value;
                    char fmt[8];
                    std::snprintf(fmt, sizeof(fmt), "%%.%df", info.decimals);
                    if (ImGui::InputDouble("##v", &v, info.step, info.step * 10, fmt))
                    {
                        fld.value = v;
                        edited = true;
                    }
                }
                if (edited)
                    fld.edited = true;
                ImGui::PopID();
                if (!ok)
                    ImGui::PopStyleColor(2);
                if (info.unit[0])
                {
                    ImGui::SameLine(0, S(6));
                    ImGui::TextUnformatted(info.unit);
                }
                ImGui::TableNextColumn();
                FontScope f(fontSmall);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(3));
                if (!ok)
                    TextColored(pal.err, "допустимо %s", RangeText(i).c_str());
                else if (fld.edited && !Same(i, fld.value, dev))
                {
                    changed++;
                    if (i == 8)
                        TextColored(pal.warn, "на приборе: %s", dev >= 0.5 ? "светлая" : "тёмная");
                    else
                        TextColored(pal.warn, "на приборе: %.*f", info.decimals, dev);
                }
                else if (i != 8)
                    Muted("%s", RangeText(i).c_str());
            }
            ImGui::EndTable();
        }
        ImGui::Dummy(ImVec2(0, S(4)));
        const bool busy = !set_.applying.empty();
        std::string applyLabel = changed ? Fmt("Применить изменения (%d)", changed) : std::string("Применить изменения");
        if (Button(applyLabel.c_str(), BtnKind::Primary, ImVec2(0, 0), !have || changed == 0 || invalid || busy,
                   invalid ? "Исправьте значения вне допустимых пределов" : busy ? "Идёт отправка…" : "Нет изменений"))
            ApplySettings();
        ImGui::SameLine(0, S(8));
        if (Button("Отменить", BtnKind::Normal, ImVec2(0, 0), changed == 0 && !invalid))
            for (auto& f : set_.f)
                f.edited = false;
        ImGui::SameLine(0, S(8));
        if (Button("По умолчанию", BtnKind::Normal))
            for (int i = 0; i < 9; i++)
            {
                set_.f[i].value = kFields[i].def;
                set_.f[i].edited = true;
            }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Подставить заводские значения (их ещё нужно применить)");
        if (busy)
        {
            ImGui::SameLine(0, S(12));
            ImGui::AlignTextToFramePadding();
            Muted("отправка…");
        }
        if (have && snap_.status.savePending)
        {
            FontScope f(fontSmall);
            TextColored(pal.warn, snap_.status.Recording() ? "Изменения сохранятся во флеш после остановки записи."
                                                           : "Изменения сохраняются во флеш…");
        }
        if (!set_.applyLog.empty())
        {
            ImGui::Spacing();
            FontScope f(fontSmall);
            for (const auto& l : set_.applyLog)
                TextColored(l.find("→  OK") != std::string::npos ? pal.ok : pal.err, "%s", l.c_str());
        }
    }
    EndCard();
}

void App::SettingsClockCard()
{
    if (BeginCard("##s_clock"))
    {
        CardTitle("Часы и ноль");
        const bool have = snap_.haveStatus;
        if (BeginKV("##kvclk", S(150)))
        {
            KV("Часы прибора", have ? snap_.status.time + (snap_.status.rtc ? "  (DS3231)" : "  (без DS3231)") : "—");
            KV("Часы ПК", text::LocalTimeString(std::time(nullptr)));
            if (have)
            {
                const std::time_t dev = text::ParseLocalTime(snap_.status.time);
                const std::time_t pc = std::time(nullptr) - static_cast<std::time_t>((snap_.nowMs - snap_.statusAtMs) / 1000);
                if (dev > 0)
                {
                    const long long d = static_cast<long long>(dev - pc);
                    const ImVec4 c = std::llabs(d) <= 2 ? pal.ok : pal.warn;
                    KV("Расхождение", std::llabs(d) <= 2 ? "совпадают (±2 с)"
                                      : d < 0          ? "прибор отстаёт на " + text::Duration(static_cast<std::uint64_t>(-d))
                                                       : "прибор спешит на " + text::Duration(static_cast<std::uint64_t>(d)),
                       &c);
                }
            }
            EndKV();
        }
        if (Button("Синхронизировать время с ПК", BtnKind::Primary, ImVec2(0, 0), !have || set_.timeReq != nullptr))
            SyncTime();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("set time: часы прибора (и модуль DS3231) = время этого ПК");
        if (!set_.timeMsg.empty())
        {
            FontScope f(fontSmall);
            TextColored(set_.timeOk ? pal.ok : pal.err, "%s", set_.timeMsg.c_str());
        }
        ImGui::Separator();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Ноль датчиков");
        Hint("«Ноль» — текущие углы отвечающих датчиков принять за ноль; «Сбросить» — углы без вычета нуля. Ноль не "
             "сохраняется при выключении прибора — задаётся на каждом замере.");
        ImGui::SameLine(0, S(14));
        if (Button("Ноль", BtnKind::Normal, ImVec2(S(96), 0), !have || set_.zeroReq != nullptr))
            set_.zeroReq = link_->Send("zero");
        ImGui::SameLine(0, S(8));
        if (Button("Сбросить", BtnKind::Normal, ImVec2(S(110), 0), !have || set_.zeroReq != nullptr))
            set_.zeroReq = link_->Send("zero reset");
    }
    EndCard();
}

void App::SettingsAddrCard()
{
    if (BeginCard("##s_addr"))
    {
        CardTitle("Смена Modbus-адреса датчика");
        Banner(pal.warn, pal.warnBg, "⚠",
               "На шине RS485 оставьте ТОЛЬКО датчик, адрес которого меняется, — остальные отключите! Новый датчик "
               "обычно имеет адрес 1; прибор опрашивает адреса 2 (Д2) и 3 (Д3).");
        ImGui::Spacing();
        const bool busy = set_.addrPhase == 1 || set_.addrPhase == 2;
        if (busy)
            ImGui::BeginDisabled();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Текущий адрес");
        ImGui::SameLine(0, S(10));
        ImGui::SetNextItemWidth(S(110));
        ImGui::InputInt("##afrom", &set_.addrFrom, 1, 1);
        ImGui::SameLine(0, S(18));
        ImGui::TextUnformatted("Новый");
        ImGui::SameLine(0, S(8));
        ImGui::SetNextItemWidth(S(110));
        ImGui::InputInt("##ato", &set_.addrTo, 1, 1);
        set_.addrFrom = std::clamp(set_.addrFrom, 1, 247);
        set_.addrTo = std::clamp(set_.addrTo, 1, 247);
        ImGui::Checkbox("На шине только этот датчик", &set_.addrOnlyOne);
        if (busy)
            ImGui::EndDisabled();
        const bool same = set_.addrFrom == set_.addrTo;
        const char* why = !HaveStatus()       ? "Нет связи с прибором"
                          : Recording()       ? "Идёт запись замера — остановите её тумблером"
                          : same              ? "Адреса должны различаться"
                          : !set_.addrOnlyOne ? "Подтвердите, что на шине только этот датчик"
                          : busy              ? "Идёт смена адреса…"
                                              : nullptr;
        if (Button("Сменить адрес…", BtnKind::Primary, ImVec2(0, 0), why != nullptr, why))
            ImGui::OpenPopup("Сменить адрес датчика?");
        if (ImGui::BeginPopupModal("Сменить адрес датчика?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::Text("Датчик с адресом %d получит адрес %d и сохранит его в своей памяти.", set_.addrFrom, set_.addrTo);
            TextColored(pal.warn, "На шине должен быть только этот датчик!");
            ImGui::Spacing();
            if (Button("Сменить", BtnKind::Primary, ImVec2(S(140), 0)))
            {
                StartAddressChange(set_.addrFrom, set_.addrTo);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (Button("Отмена", BtnKind::Normal, ImVec2(S(120), 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (set_.addrPhase == 1 || set_.addrPhase == 2)
        {
            ImGui::SameLine(0, S(12));
            ImGui::AlignTextToFramePadding();
            static const char* spin[] = {"◐", "◓", "◑", "◒"};
            TextColored(pal.info, "%s смена адреса %d → %d…", spin[(link_->NowMs() / 150) % 4], set_.addrFrom, set_.addrTo);
        }
        else if (set_.addrPhase == 3)
            Banner(pal.ok, pal.okBg, "✓", set_.addrMsg.c_str());
        else if (set_.addrPhase == 4)
            Banner(pal.err, pal.errBg, "✗", set_.addrMsg.c_str());
    }
    EndCard();
}

void App::SettingsPcCard()
{
    if (BeginCard("##s_pc"))
    {
        CardTitle("Программа на ПК");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Файл настроек программы: %s", text::PathToUtf8(PcSettings::Dir() / L"krenomer.ini").c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Оформление");
        ImGui::SameLine(S(150));
        int theme = settings_.darkTheme ? 1 : 0;
        static const char* tl[] = {"Светлое", "Тёмное"};
        static const int tv[] = {0, 1};
        if (Segmented("theme", &theme, tl, tv, 2))
        {
            settings_.darkTheme = theme == 1;
            themeChanged_ = true;
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Углы каждые");
        Hint("Как часто прибор присылает углы для графиков (команда stream). Чаще — плавнее графики.");
        ImGui::SameLine(S(150));
        static const char* sl[] = {"50 мс", "100", "200", "500 мс"};
        static const int sv[] = {50, 100, 200, 500};
        if (Segmented("stream", &settings_.streamMs, sl, sv, 4))
            link_->SetStreamPeriod(settings_.streamMs);

    }
    EndCard();
}

} // namespace ui
