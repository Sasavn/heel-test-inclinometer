// Каркас окна: шапка, меню, уведомления, «О программе», пульт демо-режима; логика Tick().
#include "App.hpp"

#include <algorithm>
#include <cstring>

#include <imgui.h>

#include "../comm/NullConnection.hpp"
#include "../comm/SerialConnection.hpp"
#include "../comm/SimConnection.hpp"
#include "../core/TextUtil.hpp"
#include "Widgets.hpp"

namespace ui
{

App::App(AppOptions opt) : opt_(opt), updater_(nullptr, nullptr)
{
    if (opt_.useSettingsFile)
        settings_.Load();
    set_.streamMs = settings_.streamMs;
    std::snprintf(files_.dir, sizeof(files_.dir), "%s", settings_.DownloadDirOrDefault().c_str());
    std::snprintf(fw_.path, sizeof(fw_.path), "%s", settings_.firmwarePath.c_str());
#ifdef _WIN32
    dfuReal_ = std::make_unique<fw::RealDfuBackend>();
#endif
    MakeLink(opt_.startDemo);
}

App::~App()
{
    if (opt_.useSettingsFile)
    {
        settings_.downloadDir = files_.dir == settings_.DefaultDownloadDir() ? "" : files_.dir;
        settings_.firmwarePath = fw_.path;
        settings_.Save();
    }
    updater_.Cancel();
    link_.reset();
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
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16), 0));
    ImGui::BeginChild("##top", ImVec2(0, h), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float W = ImGui::GetWindowWidth();
    dl->AddLine(ImVec2(wp.x, wp.y + h - 1), ImVec2(wp.x + W, wp.y + h - 1), U32(pal.border));

    // Название
    const float fh = ImGui::GetFrameHeight();
    ImGui::SetCursorPosY((h - fontH2->FontSize - fontSmall->FontSize - S(2)) * 0.5f);
    ImGui::BeginGroup();
    {
        FontScope f(fontH2);
        ImGui::TextUnformatted("Регистратор крена");
    }
    {
        FontScope f(fontSmall);
        Muted("программа для ПК %s", kAppVersion);
    }
    ImGui::EndGroup();

    // Справа: порт, подключение, демо (на узком окне уже)
    const bool narrow = W < S(1180);
    const float demoW = narrow ? S(136) : S(150), connW = narrow ? S(116) : S(128), portW = narrow ? S(180) : S(230);
    const float rightX = W - S(16) - demoW - connW - portW - 2 * S(8);

    // Состояние связи: полный текст, а если не влезает до кнопок — короткий
    ImGui::SameLine(0, S(28));
    ImGui::SetCursorPosY((h - fh) * 0.5f);
    std::string chip, chipShort;
    ImVec4 fg = pal.muted, bg = pal.button;
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
    if (snap_.demo)
    {
        ImGui::BeginDisabled();
        const char* items[] = {"ДЕМО (имитатор)"};
        int z = 0;
        ImGui::Combo("##port", &z, items, 1);
        ImGui::EndDisabled();
    }
    else
    {
        std::string preview = settings_.port.empty() ? (narrow ? "Порт: авто" : "Порт: автоматически")
                                                     : "Порт: " + settings_.port;
        if (ImGui::BeginCombo("##port", preview.c_str()))
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
    if (Button(off ? "Подключить" : "Отключить", BtnKind::Normal, ImVec2(connW, 0), updater_.Busy() || dl_.running,
               "Идёт скачивание или перепрошивка"))
        link_->SetEnabled(off);
    ImGui::SameLine(0, S(8));
    if (Button(snap_.demo ? "Выйти из демо" : "Демо-режим", snap_.demo ? BtnKind::Primary : BtnKind::Normal,
               ImVec2(demoW, 0), updater_.Busy() || dl_.running, "Идёт скачивание или перепрошивка"))
        StartDemo(!snap_.demo);
    if (ImGui::IsItemHovered() && !snap_.demo)
        ImGui::SetTooltip("Имитатор прибора: все страницы работают без железа");

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void App::RenderSidebar(float w, float h)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10), S(12)));
    ImGui::BeginChild("##nav", ImVec2(w, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        dl->AddLine(ImVec2(wp.x + w - 1, wp.y), ImVec2(wp.x + w - 1, wp.y + h), U32(pal.border));
    }
    struct Item
    {
        Page page;
        const char* icon;
        const char* label;
    };
    static const Item items[] = {
        {Page::Measure, "◉", "Измерение"},     {Page::Diag, "⚡", "Диагностика"},
        {Page::Settings, "⚙", "Настройки"},    {Page::Files, "▤", "Файлы на карте"},
        {Page::Firmware, "⇪", "Прошивка"},     {Page::Terminal, "⌨", "Терминал"},
    };
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, S(4)));
    for (const auto& it : items)
    {
        const bool sel = page_ == it.page;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float iw = ImGui::GetContentRegionAvail().x, ih = S(40);
        ImGui::PushID(static_cast<int>(it.page));
        if (ImGui::InvisibleButton("##nav", ImVec2(iw, ih)))
            page_ = it.page;
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (sel || hov)
            dl->AddRectFilled(p, ImVec2(p.x + iw, p.y + ih), U32(sel ? pal.navSel : pal.navHover), S(6));
        const ImU32 tc = U32(sel ? pal.accentText : pal.text);
        const float ty = p.y + (ih - fontBold->FontSize) * 0.5f;
        dl->AddText(fontBold, fontBold->FontSize, ImVec2(p.x + S(12), ty), tc, it.icon);
        dl->AddText(sel ? fontBold : fontBody, fontBody->FontSize, ImVec2(p.x + S(40), ty), tc, it.label);
        // Отметки на пунктах: идёт запись / скачивание / перепрошивка
        const char* badge = nullptr;
        ImVec4 bc = pal.warn;
        if (it.page == Page::Files && dl_.running)
            badge = "⇩";
        if (it.page == Page::Firmware && updater_.Busy())
            badge = "●";
        if (it.page == Page::Measure && Recording())
        {
            badge = "●";
            bc = pal.err;
        }
        if (badge)
            dl->AddText(fontBold, fontBold->FontSize, ImVec2(p.x + iw - S(18), ty), U32(sel ? pal.accentText : bc), badge);
    }
    ImGui::PopStyleVar();

    // Часы прибора и ПК
    ImGui::Dummy(ImVec2(0, S(10)));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, S(6)));
    {
        FontScope f(fontSmall);
        const std::string pc = text::LocalTimeString(std::time(nullptr));
        const std::string dev = HaveStatus() ? snap_.status.time : std::string("—");
        if (BeginKV("##clk", S(64)))
        {
            KV("Прибор", dev.size() > 11 ? dev.substr(11) : dev);
            KV("ПК", pc.substr(11));
            EndKV();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Часы прибора: %s\nЧасы ПК: %s", dev.c_str(), pc.c_str());
    }

    // Пульт демо-режима
    if (snap_.demo && sim_)
    {
        ImGui::Dummy(ImVec2(0, S(6)));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, S(4)));
        RenderDemoPanel();
    }

    // О программе — внизу
    const float bh = ImGui::GetFrameHeight();
    if (ImGui::GetCursorPosY() < h - bh - S(14))
        ImGui::SetCursorPosY(h - bh - S(12));
    if (Button("О программе", BtnKind::Normal, ImVec2(-1, 0)))
        aboutOpen_ = true;
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void App::RenderDemoPanel()
{
    FontScope f(fontSmall);
    ImGui::PushStyleColor(ImGuiCol_Text, pal.info);
    // Раскрыт, если по высоте помещается (на экранах 1024×768 — свёрнут)
    ImGui::SetNextItemOpen(ImGui::GetMainViewport()->WorkSize.y >= S(760), ImGuiCond_Once);
    const bool open = ImGui::TreeNodeEx("Пульт имитатора", ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::PopStyleColor();
    if (!open)
        return;
    auto c = sim_->GetControls();
    bool changed = false;
    changed |= ui::Checkbox("Тумблер записи", &c.recSwitch);
    changed |= ui::Checkbox("Датчик Д2 на шине", &c.sensorOn[0]);
    changed |= ui::Checkbox("Датчик Д3 на шине", &c.sensorOn[1]);
    changed |= ui::Checkbox("Карта вставлена", &c.card);
    changed |= ui::Checkbox("АКБ разряжена", &c.batteryLow);
    const char* motions[] = {"Качка: штиль", "Качка: слабая", "Качка: сильная"};
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::Combo("##motion", &c.motion, motions, 3);
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
