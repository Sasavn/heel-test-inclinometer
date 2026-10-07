// Страница «Прошивка»: текущая версия, выбор .bin, проверка образа, поиск dfu-util, обновление по шагам
// (boot -> DFU -> запись -> чтение и сравнение -> запуск -> прибор снова на связи) и журнал dfu-util.
#include "App.hpp"

#include <filesystem>
#include <system_error>

#include <imgui.h>

#include "../core/TextUtil.hpp"
#include "Dialogs.hpp"
#include "Widgets.hpp"

namespace ui
{

namespace fs = std::filesystem;

void App::SetFirmwarePath(const std::string& path)
{
    std::snprintf(fw_.path, sizeof(fw_.path), "%s", path.c_str());
    fw_.checkedPath.clear();
}

bool App::StartFirmwareUpdate(bool force)
{
    settings_.firmwarePath = fw_.path;
    const bool ok = updater_.Start(fw_.path, force, link_->NowMs());
    if (!ok)
        Notify("Обновление не начато: " + updater_.Error(), 2, 6000);
    return ok;
}

void App::PageFirmware()
{
    const std::int64_t now = link_->NowMs();
    fw::IDfuBackend* backend = snap_.demo ? dfuDemo_.get() : dfuReal_.get();
    if (!backend)
    {
        Muted("Перепрошивка доступна только в Windows.");
        return;
    }
    // Образ: перечитать при смене пути или файла
    {
        std::error_code ec;
        const fs::path p = text::PathFromUtf8(fw_.path);
        std::int64_t mt = 0;
        if (fw_.path[0] && fs::is_regular_file(p, ec))
            mt = static_cast<std::int64_t>(fs::last_write_time(p, ec).time_since_epoch().count());
        if (fw_.checkedPath != fw_.path || mt != fw_.checkedMtime)
        {
            fw_.checkedPath = fw_.path;
            fw_.checkedMtime = mt;
            fw_.imageRead = false;
            std::vector<std::uint8_t> d;
            if (fw_.path[0] && fw::ReadFile(p, d))
            {
                fw_.image = fw::CheckImage(d);
                fw_.imageRead = true;
            }
        }
    }
    // dfu-util и устройство DFU — раз в 2 с
    if (now - fw_.dfuCheckedAt > 2000 && !updater_.Busy())
    {
        fw_.dfuCheckedAt = now;
        fw_.dfuSearched.clear();
        fw_.dfuPath = backend->FindDfuUtil(&fw_.dfuSearched);
        fw_.dfuPresent = backend->DfuDevicePresent(&fw_.dfuDriver);
    }

    const float W = ImGui::GetContentRegionAvail().x;
    const float gap = S(12);
    const float colW = std::floor((W - gap) * 0.5f);

    ImGui::BeginGroup();
    // Текущая прошивка
    if (BeginCard("##fw_cur", ImVec2(colW, 0)))
    {
        CardTitle("Прошивка прибора сейчас");
        if (BeginKV("##kvcur", S(110)))
        {
            if (Connected() && snap_.haveVer)
            {
                KV("Версия", snap_.ver.version.empty() ? "без номера (старая прошивка)" : "v" + snap_.ver.version);
                KV("Сборка", snap_.ver.build.empty() ? "—" : snap_.ver.build);
                KV("Кристалл", snap_.ver.details.empty() ? "—" : snap_.ver.details);
            }
            else if (updater_.Busy())
            {
                const ImVec4 c = pal.info;
                KV("Состояние", "идёт обновление прошивки", &c);
            }
            else if (fw_.dfuPresent)
            {
                const ImVec4 c = pal.warn;
                KV("Состояние", "в загрузчике (USB DFU 0483:DF11)", &c);
            }
            else
                KV("Состояние", "прибор не подключён");
            EndKV();
        }
    }
    EndCard();
    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));

    // Образ
    if (BeginCard("##fw_img", ImVec2(colW, 0)))
    {
        CardTitle("Новая прошивка", "Файл .bin из сборки прошивки (make): build\\BWM427_Inclinometer.bin");
        if (updater_.Busy())
            ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(colW - S(28) - S(130) - S(8));
        ImGui::InputText("##fwpath", fw_.path, sizeof(fw_.path));
        ImGui::SameLine(0, S(8));
        if (Button("Выбрать…", BtnKind::Normal, ImVec2(S(130), 0)))
        {
            std::string p = fw_.path;
            if (dialogs::PickFile(p, L"Прошивка (*.bin)\0*.bin\0Все файлы\0*.*\0", L"Файл прошивки"))
                SetFirmwarePath(p);
        }
        if (updater_.Busy())
            ImGui::EndDisabled();
        if (!fw_.path[0])
            Muted("Файл не выбран.");
        else if (!fw_.imageRead)
            TextColored(pal.err, "✗ Файл не найден или не читается.");
        else if (!fw_.image.ok)
            TextColored(pal.err, "✗ %s", fw_.image.error.c_str());
        else
        {
            TextColored(pal.ok, "✓ Прошивка регистратора крена, версия %s",
                        fw_.image.version.empty() ? "?" : fw_.image.version.c_str());
            FontScope f(fontSmall);
            Muted("сборка %s · %s · стек 0x%08X · сброс 0x%08X", fw_.image.build.c_str(), text::Bytes(fw_.image.size).c_str(),
                  fw_.image.sp, fw_.image.reset);
        }
    }
    EndCard();
    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));

    // dfu-util
    if (BeginCard("##fw_dfu", ImVec2(colW, 0)))
    {
        CardTitle("Программа dfu-util");
        if (!fw_.dfuPath.empty())
            TextColored(pal.ok, "✓ %s", fw_.dfuPath.c_str());
        else
        {
            TextColored(pal.err, "✗ dfu-util.exe не найден");
            ImGui::TextWrapped("Положите dfu-util.exe (0.9 или новее, сборка для Windows) рядом с krenomer.exe — или в "
                               "папку dfu-util рядом с ним. Подойдёт и копия из Arduino IDE.");
        }
        if (ImGui::TreeNode("Где искали"))
        {
            FontScope f(fontSmall);
            for (const auto& s : fw_.dfuSearched)
                Muted("%s", s.c_str());
            ImGui::TreePop();
        }
    }
    EndCard();
    ImGui::EndGroup();

    // Обновление
    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    if (BeginCard("##fw_go", ImVec2(W - colW - gap, 0)))
    {
        CardTitle("Обновление по USB");
        static const char* steps[] = {"Перевод прибора в загрузчик (boot)", "Ожидание USB DFU 0483:DF11",
                                      "Запись образа (dfu-util)", "Проверка: чтение обратно и сравнение",
                                      "Запуск новой прошивки", "Прибор снова на связи"};
        int cur = -1;
        switch (updater_.GetStep())
        {
        case fw::Updater::Step::Boot: cur = 0; break;
        case fw::Updater::Step::WaitDfu: cur = 1; break;
        case fw::Updater::Step::Download: cur = 2; break;
        case fw::Updater::Step::Readback: cur = 3; break;
        case fw::Updater::Step::Leave: cur = 4; break;
        case fw::Updater::Step::WaitDevice: cur = 5; break;
        case fw::Updater::Step::Done: cur = 6; break;
        case fw::Updater::Step::Failed: cur = -2; break;
        default: break;
        }
        // Шаг, на котором остановилась ошибка — последний начатый
        static int lastStarted = -1;
        if (cur >= 0)
            lastStarted = cur;
        else if (cur == -1)
            lastStarted = -1;
        for (int i = 0; i < 6; i++)
        {
            const char* icon = "○";
            ImVec4 c = pal.muted;
            if (cur == 6 || (cur >= 0 && i < cur) || (cur == -2 && i < lastStarted))
                icon = "✓", c = pal.ok;
            else if (cur == i)
            {
                static const char* spin[] = {"◐", "◓", "◑", "◒"};
                icon = spin[(now / 150) % 4];
                c = pal.info;
            }
            else if (cur == -2 && i == lastStarted)
                icon = "✗", c = pal.err;
            {
                FontScope f(fontBold);
                TextColored(c, "%s", icon);
            }
            ImGui::SameLine(0, S(10));
            if (cur == i)
            {
                FontScope f(fontBold);
                ImGui::TextUnformatted(steps[i]);
            }
            else
                TextColored(cur == -1 ? pal.textDim : pal.text, "%s", steps[i]);
        }
        ImGui::Spacing();
        if (cur == 2 || cur == 3)
            Progress(static_cast<float>(updater_.Progress()), Fmt("%.0f %%", updater_.Progress() * 100).c_str());
        if (updater_.GetStep() == fw::Updater::Step::Done)
            Banner(pal.ok, pal.okBg, "✓",
                   ("Прошивка обновлена" + (updater_.NewVersion().empty() ? std::string() : " до v" + updater_.NewVersion()) +
                    ", проверена и запущена. Настройки прибора сохранены.")
                       .c_str());
        else if (updater_.GetStep() == fw::Updater::Step::Failed)
            Banner(pal.err, pal.errBg, "✗", updater_.Error().c_str());
        ImGui::Spacing();

        const bool imageOk = fw_.imageRead && fw_.image.ok;
        const bool deviceOk = Connected() || fw_.dfuPresent;
        const char* why = !imageOk           ? "Выберите правильный файл прошивки .bin"
                          : fw_.dfuPath.empty() ? "Не найден dfu-util.exe"
                          : !deviceOk        ? "Прибор не подключён"
                          : dl_.running      ? "Идёт скачивание файлов"
                                             : nullptr;
        if (!updater_.Busy())
        {
            if (Button("Обновить прошивку…", BtnKind::Primary, ImVec2(S(230), S(36)), why != nullptr, why))
            {
                fw_.confirmForce = false;
                ImGui::OpenPopup("Обновить прошивку?");
            }
            if (updater_.NeedsForce())
            {
                ImGui::SameLine(0, S(8));
                if (Button("Прервать запись и обновить…", BtnKind::Warn, ImVec2(0, S(36)), why != nullptr, why))
                {
                    fw_.confirmForce = true;
                    ImGui::OpenPopup("Обновить прошивку?");
                }
            }
        }
        else if (Button("Отменить", BtnKind::Danger, ImVec2(S(160), S(36))))
            updater_.Cancel();

        if (ImGui::BeginPopupModal("Обновить прошивку?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::Text("Прошивка прибора: %s  →  %s",
                        snap_.haveVer && !snap_.ver.version.empty() ? ("v" + snap_.ver.version).c_str() : "?",
                        fw_.image.version.empty() ? "?" : ("v" + fw_.image.version).c_str());
            ImGui::TextUnformatted("Прибор перезапустится в загрузчик, примерно на минуту пропадёт связь.");
            ImGui::TextUnformatted("Не отключайте USB до конца. Настройки прибора сохранятся.");
            if (fw_.confirmForce)
                TextColored(pal.warn, "Идущая запись замера будет остановлена (файлы закроются).");
            ImGui::Spacing();
            if (Button("Обновить", BtnKind::Primary, ImVec2(S(150), 0)))
            {
                StartFirmwareUpdate(fw_.confirmForce);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (Button("Отмена", BtnKind::Normal, ImVec2(S(120), 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Журнал
        ImGui::Spacing();
        FontScope f(fontMono);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.cardAlt);
        ImGui::BeginChild("##fwlog", ImVec2(0, S(150)), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        if (updater_.Log().empty())
            Muted("Здесь будет журнал обновления и вывод dfu-util.");
        for (const auto& l : updater_.Log())
        {
            const ImVec4 c = l.kind == 'e' ? pal.err : l.kind == 'k' ? pal.ok : l.kind == 'o' ? pal.textDim : pal.text;
            TextColored(c, "%s", l.text.c_str());
        }
        if (updater_.Busy())
            ImGui::SetScrollHereY(1.f);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    EndCard();
    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
    if (BeginCard("##fw_help", ImVec2(W - colW - gap, 0)))
    {
        CardTitle("Если не получается");
        FontScope f(fontSmall);
        static const char* tips[] = {
            "Загрузчик STM32 на USB — устройство «DFU in FS Mode» (0483:DF11). Для dfu-util ему нужен драйвер WinUSB: "
            "один раз установите его утилитой Zadig (Options → List All Devices → «STM32 BOOTLOADER» → WinUSB → "
            "Install Driver).",
            "Прибор не уходит в загрузчик по команде — вручную: удерживая BOOT0, нажать и отпустить NRST, затем "
            "«Обновить прошивку».",
            "Проверка не прошла — прибор остаётся в загрузчике: просто повторите обновление.",
            "Windows 7: для COM-порта прибора нужен драйвер ST Virtual COM Port (STSW-STM32102).",
        };
        for (const char* t : tips)
        {
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", t);
        }
    }
    EndCard();
    ImGui::EndGroup();
}

} // namespace ui
