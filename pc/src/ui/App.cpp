// Каркас окна: шапка, меню, уведомления, «О программе», пульт демо-режима; логика Tick().
#include "App.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#include <imgui.h>

#include "../comm/NullConnection.hpp"
#include "../comm/SerialConnection.hpp"
#include "../comm/SimConnection.hpp"
#include "../core/TextUtil.hpp"
#include "Widgets.hpp"

#ifdef KRENOMER_PROJECT_VERSION
namespace
{
// kAppVersion («1.6») — начало версии из CMakeLists.txt (project VERSION 1.6.0): сведения о файле и окно — одно.
// 1.6: новый вид — дашборд (тёмно-синие панель и шапка, карточки, вкладки; «Обработка»: показатели, графики,
// вкладки, «развернуть», делитель).
// 1.5.2: отчёт в печатной форме (лист «Протокол», картинки в книге, единая сетка, печать A4; сырые данные — не в печать).
// 1.5.1: обратный знак датчика (ось +X — к левому борту), плечо начального положения от ДП; опыт на модели M021.
constexpr bool VersionMatches(const char* project, const char* app)
{
    while (*app)
        if (*project++ != *app++)
            return false;
    return *project == '\0' || *project == '.';
}
static_assert(VersionMatches(KRENOMER_PROJECT_VERSION, ui::kAppVersion),
              "kAppVersion (App.hpp) не совпадает с project(VERSION) в CMakeLists.txt");
} // namespace
#endif

namespace ui
{

App::App(AppOptions opt) : opt_(opt), updater_(nullptr, nullptr)
{
    if (opt_.useSettingsFile)
        settings_.Load();
    set_.streamMs = settings_.streamMs;
    std::snprintf(files_.dir, sizeof(files_.dir), "%s", settings_.DownloadDirOrDefault().c_str());
    std::snprintf(fw_.path, sizeof(fw_.path), "%s", settings_.firmwarePath.c_str());
    std::snprintf(pcrec_.dir, sizeof(pcrec_.dir), "%s", settings_.RecordDirOrDefault().c_str());
#ifdef _WIN32
    dfuReal_ = std::make_unique<fw::RealDfuBackend>();
#endif
    MakeLink(opt_.startDemo);
}

App::~App()
{
    StopPcRecording(); // дописать файлы записи на ПК (и .xlsx — деструктор Recorder дождётся)
    SaveSettingsNow();
    updater_.Cancel();
    link_.reset();
}

std::string App::Shown(const std::string& s) const
{
    std::string out = s;
    for (const auto& [from, to] : shownPaths_)
    {
        if (from.empty())
            continue;
        std::string alt = from; // тот же путь с «/»
        std::replace(alt.begin(), alt.end(), '\\', '/');
        for (const std::string* f : {static_cast<const std::string*>(&from), static_cast<const std::string*>(&alt)})
            for (std::size_t pos = out.find(*f); pos != std::string::npos; pos = out.find(*f, pos + to.size()))
                out.replace(pos, f->size(), to);
    }
    return out;
}

bool App::PathInput(const char* id, char* buf, std::size_t size, int flags)
{
    if (shownPaths_.empty())
        return ImGui::InputText(id, buf, size, flags);
    char shown[1024];
    std::snprintf(shown, sizeof(shown), "%s", Shown(buf).c_str());
    ImGui::InputText(id, shown, sizeof(shown), ImGuiInputTextFlags_ReadOnly);
    return false;
}

void App::SaveSettingsNow()
{
    settingsSaveAt_ = 0;
    if (!opt_.useSettingsFile)
        return;
    settings_.downloadDir = files_.dir == settings_.DefaultDownloadDir() ? "" : files_.dir;
    settings_.firmwarePath = fw_.path;
    settings_.recordDir = pcrec_.dir == settings_.DownloadDirOrDefault() ? "" : pcrec_.dir;
    settings_.Save();
}

namespace
{
std::int64_t SteadyMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

// Выбор, который должен пережить закрытие программы (ось, D, P, …), — в файл через ~1 с, а не только при выходе:
// программу могут закрыть снятием задачи или выключением ПК.
void App::SaveSettingsSoon()
{
    settingsSaveAt_ = SteadyMs() + 1000;
}

void App::MakeLink(bool demo)
{
    link_.reset(); // сначала закрыть прежний порт / имитатор
    if (demo)
    {
        sim_ = std::make_shared<SimDevice>(opt_.simSeed, opt_.simStart);
        link_ = std::make_unique<DeviceLink>(std::make_unique<SimConnection>(sim_), opt_.threaded);
        dfuDemo_ = std::make_unique<fw::DemoDfuBackend>(sim_, [this] { return link_ ? link_->NowMs() : 0; });
        updater_.SetBackend(link_.get(), dfuDemo_.get());
    }
    else
    {
        sim_.reset();
        dfuDemo_.reset();
        if (opt_.noHardware)
            link_ = std::make_unique<DeviceLink>(std::make_unique<NullConnection>(), opt_.threaded);
        else
        {
            auto conn = std::make_unique<SerialConnection>();
            conn->SetPortChoice(settings_.port);
            link_ = std::make_unique<DeviceLink>(std::move(conn), opt_.threaded);
        }
        updater_.SetBackend(link_.get(), dfuReal_.get());
    }
    link_->SetStreamPeriod(settings_.streamMs);
    snap_ = link_->Snapshot();
    lastConnectSeq_ = 0;
    files_.list.clear();
    files_.listed = false;
    files_.note.clear();
    files_.noteKind.clear();
    term_.lines.clear();
    term_.lastSeq = 0;
    toasts_.clear(); // у нового канала своё время (NowMs с нуля)
    for (auto& f : set_.f)
        f.edited = false;
    diag_.diagText.clear();
}

void App::StartDemo(bool on)
{
    if (updater_.Busy() || dl_.running)
    {
        Notify("Дождитесь окончания скачивания / перепрошивки", 2);
        return;
    }
    if (PcRecording())
    {
        Notify("Идёт запись на ПК — сначала остановите её", 2);
        return;
    }
    MakeLink(on);
    Notify(on ? "Демо-режим: подключён имитатор прибора" : "Демо-режим выключен — поиск прибора на USB", 0);
}

std::string App::WindowTitle() const
{
    return std::string("Регистратор крена — программа для ПК ") + kAppVersion;
}

void App::Notify(const std::string& text, int kind, int ms)
{
    toasts_.push_back({text, kind, link_->NowMs() + ms});
    if (toasts_.size() > 4)
        toasts_.erase(toasts_.begin());
}

void App::Tick()
{
    const std::int64_t now = link_->NowMs();
    snap_ = link_->Snapshot();
    if (snap_.connectSeq != lastConnectSeq_ && snap_.state == LinkState::Connected)
    {
        lastConnectSeq_ = snap_.connectSeq;
        if (!snap_.demo)
            Notify("Прибор подключён: " + snap_.port +
                       (snap_.ver.version.empty() ? std::string() : ", прошивка v" + snap_.ver.version),
                   1);
        files_.listed = false;
    }
    updater_.Tick(now);
    TickFiles();
    TickSettings();
    TickDiag();
    TickRecord();
    TickProcess();
    if (settingsSaveAt_ && SteadyMs() >= settingsSaveAt_)
        SaveSettingsNow();
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [&](const Toast& t) { return now > t.until; }),
                  toasts_.end());
}

// ---------------------------------------------------------------------------------------------------------------
// Кадр
// ---------------------------------------------------------------------------------------------------------------

void App::Render()
{
    snap_ = link_->Snapshot();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Begin("##app", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    const float topH = S(58);
    const float navW = S(212);
    RenderTopBar(topH);
    RenderSidebar(navW, vp->WorkSize.y - topH);
    ImGui::SameLine(0, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16), S(14)));
    ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    RenderPage();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
    RenderAbout();
    RenderToasts();
}

void App::RenderTopBar(float h)
{
    // Шапка — тёмно-синяя, как левая панель (одна «рамка» вокруг светлой рабочей области)
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.nav);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), 0));
    ImGui::BeginChild("##top", ImVec2(0, h), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const float W = ImGui::GetWindowWidth();
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        dl->AddLine(ImVec2(wp.x, wp.y + h - 1), ImVec2(wp.x + W, wp.y + h - 1), U32(pal.navLine));
    }

    // Название: значок и две строки
    const float fh = ImGui::GetFrameHeight();
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float sz = S(30);
        const float y0 = ImGui::GetWindowPos().y + (h - sz) * 0.5f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(p.x, y0), ImVec2(p.x + sz, y0 + sz), U32(pal.orange), S(7));
        // уровень и «крен» — наклонная черта на горизонте
        dl->AddLine(ImVec2(p.x + S(7), y0 + S(19)), ImVec2(p.x + sz - S(7), y0 + S(19)), U32(ImVec4(1, 1, 1, 0.55f)), S(2));
        dl->AddLine(ImVec2(p.x + S(8), y0 + S(22)), ImVec2(p.x + sz - S(8), y0 + S(10)), U32(ImVec4(1, 1, 1, 1)), S(2.4f));
        ImGui::SetCursorScreenPos(ImVec2(p.x + sz + S(12), p.y));
    }
    ImGui::SetCursorPosY((h - fontH2->FontSize - fontSmall->FontSize - S(2)) * 0.5f);
    ImGui::BeginGroup();
    {
        FontScope f(fontH2);
        TextColored(pal.navText, "Регистратор крена");
    }
    {
        FontScope f(fontSmall);
        TextColored(pal.navMuted, "программа для ПК %s", kAppVersion);
    }
    ImGui::EndGroup();

    // Справа: порт, подключение, демо (на узком окне уже)
    const bool narrow = W < S(1180);
    const float demoW = narrow ? S(136) : S(150), connW = narrow ? S(116) : S(128), portW = narrow ? S(180) : S(230);
    const float rightX = W - S(18) - demoW - connW - portW - 2 * S(8);

    // Состояние связи: полный текст, а если не влезает до кнопок — короткий
    ImGui::SameLine(0, S(28));
    ImGui::SetCursorPosY((h - fh) * 0.5f);
    std::string chip, chipShort;
    ImVec4 fg = pal.navText, bg = pal.navFrame;
    if (updater_.Busy())
    {
        chip = "Обновление прошивки…";
        chipShort = "Прошивка…";
        fg = pal.info;
        bg = pal.infoBg;
    }
    else
        switch (snap_.state)
        {
        case LinkState::Connected:
            if (snap_.demo)
            {
                chip = "Демо-режим · имитатор прибора";
                chipShort = "Демо-режим";
                fg = pal.info;
                bg = pal.infoBg;
            }
            else
            {
                chip = "На связи · " + snap_.port +
                       (snap_.ver.version.empty() ? std::string() : " · прошивка v" + snap_.ver.version);
                chipShort = "На связи · " + snap_.port;
                fg = pal.ok;
                bg = pal.okBg;
            }
            break;
        case LinkState::Probing:
            chip = "Проверка прибора · " + snap_.port + "…";
            chipShort = "Проверка…";
            fg = pal.warn;
            bg = pal.warnBg;
            break;
        case LinkState::Searching:
            chip = snap_.demo ? "Имитатор перезагружается…" : "Нет связи · поиск прибора…";
            chipShort = "Нет связи";
            fg = pal.err;
            bg = pal.errBg;
            break;
        case LinkState::Off:
            chip = chipShort = "Отключено";
            break;
        }
    const float room = rightX - ImGui::GetCursorPosX() - S(12);
    if (ImGui::CalcTextSize(chip.c_str()).x + S(34) > room)
        chip = chipShort;
    Chip(chip.c_str(), fg, bg);
    if (ImGui::IsItemHovered() && snap_.state != LinkState::Connected && !snap_.problem.empty())
        ImGui::SetTooltip("%s", snap_.problem.c_str());

    ImGui::SetCursorPos(ImVec2(rightX, (h - fh) * 0.5f));
    ImGui::SetNextItemWidth(portW);
    // Поле порта на синем: полупрозрачная светлая заливка, светлый текст
    ImGui::PushStyleColor(ImGuiCol_FrameBg, pal.navFrame);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Alpha(pal.navText, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_Button, pal.navFrame);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Alpha(pal.navText, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_Border, pal.navLine);
    ImGui::PushStyleColor(ImGuiCol_Text, pal.navText);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, pal.navMuted);
    if (snap_.demo)
    {
        ImGui::BeginDisabled();
        const char* items[] = {"ДЕМО (имитатор)"};
        int z = 0;
        ImGui::Combo("##port", &z, items, 1);
        ImGui::EndDisabled();
        ImGui::PopStyleColor(7);
    }
    else
    {
        std::string preview = settings_.port.empty() ? (narrow ? "Порт: авто" : "Порт: автоматически")
                                                     : "Порт: " + settings_.port;
        const bool open = ImGui::BeginCombo("##port", preview.c_str());
        ImGui::PopStyleColor(7);
        if (open)
        {
            if (ImGui::Selectable("Автоматически (VID:PID 0483:5740)", settings_.port.empty()))
            {
                settings_.port.clear();
                link_->SetPortChoice("");
            }
            for (const auto& p : snap_.ports)
            {
                const std::string label = p.name + " — " + (p.description.empty() ? p.hwid : p.description) +
                                          (p.ours ? "  ✓ регистратор?" : "");
                if (ImGui::Selectable(label.c_str(), settings_.port == p.name))
                {
                    settings_.port = p.name;
                    link_->SetPortChoice(p.name);
                }
            }
            if (snap_.ports.empty())
                Muted("COM-портов нет");
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Обычно — «автоматически»: программа сама находит прибор (ST Virtual COM Port 0483:5740).\n"
                              "Выберите порт вручную, если в системе несколько таких устройств.");
    }
    ImGui::SameLine(0, S(8));
    const bool off = snap_.state == LinkState::Off;
    const bool busy = updater_.Busy() || dl_.running || PcRecording();
    const char* busyWhy = PcRecording() ? "Идёт запись на ПК — сначала остановите её" : "Идёт скачивание или перепрошивка";
    if (Button(off ? "Подключить" : "Отключить", BtnKind::Normal, ImVec2(connW, 0), busy, busyWhy))
        link_->SetEnabled(off);
    ImGui::SameLine(0, S(8));
    if (Button(snap_.demo ? "Выйти из демо" : "Демо-режим", snap_.demo ? BtnKind::Warn : BtnKind::Normal,
               ImVec2(demoW, 0), busy, busyWhy))
        StartDemo(!snap_.demo);
    if (ImGui::IsItemHovered() && !snap_.demo)
        ImGui::SetTooltip("Имитатор прибора: все страницы работают без железа");

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// Левая панель: тёмно-синяя, пункты — группами с заголовками капсом (раскрываются / сворачиваются): «Опыт
// кренования» (Измерение, Обработка) — первой, «Устройство» (Настройки, Диагностика, Файлы, Прошивка, Терминал);
// в демо-режиме — блок «Имитатор» (пульт); внизу — связь, часы, версия и «О программе».
void App::RenderSidebar(float w, float h)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.nav);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10), S(10)));
    ImGui::BeginChild("##nav", ImVec2(w, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    struct Item
    {
        Page page;
        const char* icon;
        const char* label;
    };
    struct Group
    {
        const char* title;
        std::vector<Item> items;
    };
    static const Group groups[] = {
        {"ОПЫТ КРЕНОВАНИЯ", {{Page::Measure, "◉", "Измерение"}, {Page::Process, "∑", "Обработка"}}},
        {"УСТРОЙСТВО",
         {{Page::Settings, "⚙", "Настройки"},
          {Page::Diag, "⚡", "Диагностика"},
          {Page::Files, "▤", "Файлы на карте"},
          {Page::Firmware, "⇪", "Прошивка"},
          {Page::Terminal, "⌨", "Терминал"}}},
    };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float iw = ImGui::GetContentRegionAvail().x;
    // Заголовок группы: капсом, мелко, ▾ / ▸ справа; щелчок — свернуть / раскрыть
    auto header = [&](const char* title, bool& open, int id) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float hh = S(28);
        ImGui::PushID(id);
        if (ImGui::InvisibleButton("##grp", ImVec2(iw, hh)))
            open = !open;
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        FontScope f(fontSmall);
        const float ty = p.y + (hh - fontSmall->FontSize) * 0.5f;
        dl->AddText(fontSmall, fontSmall->FontSize, ImVec2(p.x + S(10), ty), U32(hov ? pal.navText : pal.navMuted), title);
        dl->AddText(fontSmall, fontSmall->FontSize, ImVec2(p.x + iw - S(16), ty), U32(pal.navMuted), open ? "▼" : "▶");
    };
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, S(2)));
    int gi = 0;
    for (const Group& g : groups)
    {
        bool& open = navOpen_[gi];
        if (gi > 0)
            ImGui::Dummy(ImVec2(0, S(6)));
        header(g.title, open, gi++);
        if (!open)
            continue;
        for (const auto& it : g.items)
        {
            const bool sel = page_ == it.page;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float ih = S(36);
            ImGui::PushID(static_cast<int>(it.page));
            if (ImGui::InvisibleButton("##nav", ImVec2(iw, ih)))
                page_ = it.page;
            const bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            if (sel || hov)
                dl->AddRectFilled(p, ImVec2(p.x + iw, p.y + ih), U32(sel ? pal.navSel : pal.navHover), S(7));
            if (sel)
                dl->AddRectFilled(ImVec2(p.x, p.y + S(8)), ImVec2(p.x + S(3), p.y + ih - S(8)), U32(pal.orange), S(1.5f));
            const ImU32 tc = U32(sel ? ImVec4(1, 1, 1, 1) : pal.navText);
            const float ty = p.y + (ih - fontBody->FontSize) * 0.5f;
            dl->AddText(fontBold, fontBold->FontSize, ImVec2(p.x + S(14), p.y + (ih - fontBold->FontSize) * 0.5f),
                        U32(sel ? ImVec4(1, 1, 1, 1) : pal.navMuted), it.icon);
            dl->AddText(sel ? fontBold : fontBody, fontBody->FontSize, ImVec2(p.x + S(42), ty), tc, it.label);
            // Отметки на пунктах: идёт запись / скачивание / перепрошивка
            const char* badge = nullptr;
            ImVec4 bc = pal.orange;
            if (it.page == Page::Files && dl_.running)
                badge = "⇩";
            if (it.page == Page::Firmware && updater_.Busy())
                badge = "●";
            if (it.page == Page::Measure && (Recording() || PcRecording()))
            {
                badge = "●";
                bc = ImVec4(1.f, 0.42f, 0.40f, 1.f);
            }
            if (badge)
                dl->AddText(fontBold, fontBold->FontSize, ImVec2(p.x + iw - S(20), p.y + (ih - fontBold->FontSize) * 0.5f),
                            U32(bc), badge);
        }
    }
    // Пульт имитатора — своим блоком
    if (snap_.demo && sim_)
    {
        ImGui::Dummy(ImVec2(0, S(6)));
        header("ИМИТАТОР ПРИБОРА", navOpen_[2], 2);
        if (navOpen_[2])
            RenderDemoPanel();
    }
    ImGui::PopStyleVar();

    // Внизу: связь, часы прибора и ПК, «О программе»
    const float bh = ImGui::GetFrameHeight();
    const float lineH = fontSmall->FontSize + S(3);
    const float bottomH = bh + 3 * lineH + S(22);
    if (ImGui::GetCursorPosY() < h - bottomH)
        ImGui::SetCursorPosY(h - bottomH);
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + iw, p.y), U32(pal.navLine));
        ImGui::Dummy(ImVec2(0, S(8)));
        FontScope f(fontSmall);
        const bool on = snap_.state == LinkState::Connected;
        const ImVec4 dot = on ? (snap_.demo ? pal.info : ImVec4(0.40f, 0.85f, 0.45f, 1)) : ImVec4(1.f, 0.45f, 0.42f, 1);
        const ImVec2 q = ImGui::GetCursorScreenPos();
        dl->AddCircleFilled(ImVec2(q.x + S(14), q.y + lineH * 0.5f), S(4), U32(dot));
        ImGui::SetCursorScreenPos(ImVec2(q.x + S(24), q.y));
        TextColored(pal.navText, "%s", on ? (snap_.demo ? "Имитатор на связи" : "Прибор на связи") : "Прибор не подключён");
        const std::string pc = text::LocalTimeString(std::time(nullptr));
        const std::string dev = HaveStatus() ? snap_.status.time : std::string("—");
        ImGui::SetCursorScreenPos(ImVec2(q.x + S(24), ImGui::GetCursorScreenPos().y));
        TextColored(pal.navMuted, "Прибор  %s", (dev.size() > 11 ? dev.substr(11) : dev).c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Часы прибора: %s\nЧасы ПК: %s", dev.c_str(), pc.c_str());
        ImGui::SetCursorScreenPos(ImVec2(q.x + S(24), ImGui::GetCursorScreenPos().y));
        TextColored(pal.navMuted, "ПК        %s", pc.substr(11).c_str());
    }
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::PushStyleColor(ImGuiCol_Button, pal.navFrame);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Alpha(pal.navText, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Alpha(pal.navText, 0.24f));
    ImGui::PushStyleColor(ImGuiCol_Text, pal.navText);
    ImGui::PushStyleColor(ImGuiCol_Border, pal.navLine);
    if (ImGui::Button("О программе", ImVec2(-1, 0)))
        aboutOpen_ = true;
    ImGui::PopStyleColor(5);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// Пульт имитатора (демо-режим) в левой панели: светлый текст и полупрозрачные поля на синем.
void App::RenderDemoPanel()
{
    FontScope f(fontSmall);
    ImGui::PushStyleColor(ImGuiCol_Text, pal.navText);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, pal.navFrame);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Alpha(pal.navText, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_Button, pal.navFrame);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Alpha(pal.navText, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_Border, pal.navLine);
    ImGui::Indent(S(8));
    auto c = sim_->GetControls();
    bool changed = false;
    const ImVec4 keepFrame = pal.frame, keepHover = pal.frameHover, keepBorder = pal.border;
    pal.frame = pal.navFrame; // чекбоксы — светлые рамки на синем
    pal.frameHover = Alpha(pal.navText, 0.16f);
    pal.border = pal.navMuted;
    changed |= ui::Checkbox("Тумблер записи", &c.recSwitch);
    changed |= ui::Checkbox("Датчик Д2 на шине", &c.sensorOn[0]);
    changed |= ui::Checkbox("Датчик Д3 на шине", &c.sensorOn[1]);
    changed |= ui::Checkbox("Карта вставлена", &c.card);
    changed |= ui::Checkbox("АКБ разряжена", &c.batteryLow);
    pal.frame = keepFrame;
    pal.frameHover = keepHover;
    pal.border = keepBorder;
    const char* motions[] = {"Качка: штиль", "Качка: слабая", "Качка: сильная"};
    ImGui::SetNextItemWidth(-S(2));
    changed |= ImGui::Combo("##motion", &c.motion, motions, 3);
    ImGui::Unindent(S(8));
    ImGui::PopStyleColor(6);
    if (changed)
        sim_->SetControls(c);
}

void App::RenderPage()
{
    switch (page_)
    {
    case Page::Measure: PageMeasure(); break;
    case Page::Diag: PageDiag(); break;
    case Page::Settings: PageSettings(); break;
    case Page::Files: PageFiles(); break;
    case Page::Firmware: PageFirmware(); break;
    case Page::Terminal: PageTerminal(); break;
    case Page::Process: PageProcess(); break;
    default: break;
    }
}

// Нет связи: что делать (на страницах, где без прибора нечего показать).
void App::NotConnectedPanel(const char* what)
{
    if (BeginCard("##nc", ImVec2(0, 0)))
    {
        CardTitle(snap_.state == LinkState::Probing ? "Проверка прибора…" : "Прибор не подключён");
        ImGui::TextWrapped("%s", what);
        ImGui::Spacing();
        if (snap_.state == LinkState::Off)
            ImGui::TextWrapped("Связь выключена кнопкой «Отключить» — нажмите «Подключить» вверху справа.");
        else
            ImGui::TextWrapped("Подключите регистратор кабелем USB к ПК — программа найдёт его сама (порт с VID:PID "
                               "0483:5740) и подключится.");
        if (!snap_.problem.empty() && !snap_.demo)
        {
            ImGui::Spacing();
            Muted("Сейчас: %s", snap_.problem.c_str());
        }
        ImGui::Spacing();
        SmallMuted("Windows 7: для COM-порта нужен драйвер «STM32 Virtual COM Port» (STSW-STM32102), в Windows 10 и новее "
                   "драйвер не нужен. Посмотреть программу без прибора можно в демо-режиме.");
        ImGui::Spacing();
        if (!snap_.demo && Button("Демо-режим (без прибора)", BtnKind::Normal))
            StartDemo(true);
    }
    EndCard();
}

void App::RenderToasts()
{
    if (toasts_.empty())
        return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = vp->WorkPos.y + vp->WorkSize.y - S(16);
    int i = 0;
    for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it, ++i)
    {
        const ImVec4 fg = it->kind == 1 ? pal.ok : it->kind == 2 ? pal.err : pal.info;
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - S(16), y), ImGuiCond_Always, ImVec2(1, 1));
        ImGui::SetNextWindowBgAlpha(1.f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, it->kind == 1 ? pal.okBg : it->kind == 2 ? pal.errBg : pal.infoBg);
        ImGui::PushStyleColor(ImGuiCol_Border, fg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(8));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(14), S(10)));
        char id[32];
        std::snprintf(id, sizeof(id), "##toast%d", i);
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(S(460), FLT_MAX));
        ImGui::Begin(id, nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
        ImGui::PushTextWrapPos(S(430));
        ImGui::PushStyleColor(ImGuiCol_Text, pal.text);
        ImGui::TextUnformatted(it->text.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        y -= ImGui::GetWindowHeight() + S(8);
        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }
}

void App::RenderAbout()
{
    if (aboutOpen_)
    {
        ImGui::OpenPopup("О программе");
        aboutOpen_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(S(600), 0));
    if (ImGui::BeginPopupModal("О программе", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        {
            FontScope f(fontH2);
            ImGui::Text("Регистратор крена — программа для ПК %s", kAppVersion);
        }
        ImGui::TextWrapped("Показания, качка, диагностика шины RS485, настройки, файлы замеров с карты и обновление "
                           "прошивки регистратора крена (STM32F411 + два инклинометра BWM427) по USB.");
        ImGui::Spacing();
        MutedWrapped("Связь: USB CDC (виртуальный COM-порт 0483:5740), команды прибора — README проекта.");
        ImGui::Spacing();
        CardTitle("Сторонние компоненты");
        {
            FontScope f(fontSmall);
            static const char* items[] = {
                "Dear ImGui (MIT) — интерфейс; ImPlot (MIT) — графики",
                "GLFW (zlib/libpng) — окно и OpenGL; FreeType (FTL) — отрисовка шрифтов",
                "CSerialPort (LGPL-3.0 с исключением для статической линковки) — COM-порт",
                "Шрифты DejaVu Sans 2.35 (лицензия Bitstream Vera / DejaVu, свободная)",
                "dfu-util (GPL-2.0) — отдельная программа, запускается для перепрошивки",
            };
            for (const char* t : items)
            {
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("%s", t);
            }
        }
        ImGui::Spacing();
        if (Button("Закрыть", BtnKind::Primary, ImVec2(S(120), 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace ui
