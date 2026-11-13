// Полный ручной редактор протокола; никаких обращений к прибору или запуска вложений.
#include "App.hpp"
#include "Dialogs.hpp"
#include "Widgets.hpp"
#include <algorithm>
#include <cctype>
#include <imgui.h>

namespace ui
{
namespace
{
constexpr const char* kSections[] = {
    "Титульный лист", "1. Общие положения", "2. Исходные данные и корпус судна",
    "3. Организация и условия кренования", "4. Подготовка к кренованию", "5. Кренование",
    "6. Обработка результатов кренования", "7. Оценка качества кренования", "8. Заключение",
    "9. Документация", "А. Акт №1: надводный борт и осадки", "Б. Акт №2: недостающие грузы",
    "В. Акт №3: цистерны и жидкие грузы", "Г. Акт №4: излишние грузы",
    "Д. Акт №5: крен-балласт", "Е. Акт №6: показания весков", "Ж. Сертификаты и поверки"
};
// std::string растёт вместе с вводом; фиксированный буфер не обрезает паспорт, ячейки или длинный путь.
int ResizeText(ImGuiInputTextCallbackData* d)
{
    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
        auto& s = *static_cast<std::string*>(d->UserData);
        s.resize(static_cast<std::size_t>(d->BufTextLen));
        d->Buf = s.data();
    }
    return 0;
}
bool TextValue(const char* id, const std::string& label, std::string& value, bool multiline = true)
{
    ImGui::TextWrapped("%s", label.c_str());
    ImGui::SetNextItemWidth(-1);
    const auto flags = ImGuiInputTextFlags_CallbackResize;
    return multiline ? ImGui::InputTextMultiline(id, value.data(), value.capacity() + 1,
                                                ImVec2(-1, ImGui::GetTextLineHeight() * 3 + ImGui::GetStyle().FramePadding.y * 2),
                                                flags, ResizeText, &value)
                     : ImGui::InputText(id, value.data(), value.capacity() + 1, flags, ResizeText, &value);
}
bool AttachmentType(const std::string& path)
{
    auto ext = text::PathToUtf8(text::PathFromUtf8(path).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".pdf";
}
} // namespace

void App::ProcFormalProtocol()
{
    if (!proc_.formalOpen)
        return;
    auto* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * .02f, vp->WorkPos.y + vp->WorkSize.y * .02f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * .96f, vp->WorkSize.y * .96f), ImGuiCond_Always);
    if (!ImGui::Begin("Данные протокола…", &proc_.formalOpen, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Полный шаблон: титульный лист, девять разделов и приложения А–Ж. "
                       "Название судна и документа задаются в начале шаблона. Данные сохраняются в проекте опыта.");
    ImGui::TextWrapped("Незаполненные сведения помечены в черновике. Введённые подписи и одобрения требуют проверки ответственными лицами.");
    ImGui::SetNextItemWidth(-1);
    const bool changedSection = ImGui::Combo("##formal_section", &proc_.formalSection, kSections, 17);
    ImGui::Separator();
    // Нижняя строка остаётся видимой; вся форма прокручивается по вертикали, включая последнюю строку таблицы.
    if (ImGui::BeginChild("##formal_content", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2), ImGuiChildFlags_Borders))
    {
        if (changedSection) ImGui::SetScrollY(0);
        bool changed = false;
        for (const auto& f : heel::formal::FieldSpecs())
        {
            if (f.section != proc_.formalSection) continue;
            ImGui::PushID(f.id.c_str());
            auto& value = proc_.formal.fields[f.id];
            if (f.id == "center_mode")
            {
                const char* modes[] = {"Не выбрано", "Ручной ввод ЦТ", "По гидростатике"};
                int mode = value == "manual" ? 1 : value == "hydro" ? 2 : 0;
                ImGui::TextWrapped("Способ определения центра тяжести");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##value", &mode, modes, 3))
                { value = mode == 1 ? "manual" : mode == 2 ? "hydro" : ""; changed = true; }
            }
            else if (f.id == "excess_inventory_complete" || f.id == "missing_inventory_complete")
            {
                const char* states[] = {"Опись не подтверждена", "Опись проверена"};
                int state = value == "yes" ? 1 : 0;
                ImGui::TextWrapped("%s", f.id == "excess_inventory_complete" ? "Опись излишних грузов" : "Опись недостающих грузов");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##value", &state, states, 2))
                { value = state == 1 ? "yes" : ""; changed = true; }
            }
            else
                changed |= TextValue("##value", f.label, value);
            ImGui::PopID();
        }
        for (const auto& t : heel::formal::TableSpecs())
        {
            if (t.section != proc_.formalSection) continue;
            ImGui::PushID(t.id.c_str());
            ImGui::Separator();
            ImGui::TextWrapped("%s", t.title.c_str());
            auto& rows = proc_.formal.tables[t.id];
            if (ImGui::Button("Добавить строку"))
            {
                rows.emplace_back(t.columns.size());
                changed = true;
            }
            ImGui::TextWrapped("Строк: %zu. Каждая строка редактируется вертикально; продолжение таблицы не ограничено.", rows.size());
            for (std::size_t r = 0; r < rows.size();)
            {
                ImGui::PushID(static_cast<int>(r));
                const std::string title = "Строка " + std::to_string(r + 1);
                const bool open = ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                bool remove = false;
                if (open)
                {
                    if (rows[r].size() < t.columns.size()) rows[r].resize(t.columns.size());
                    for (std::size_t c = 0; c < rows[r].size(); ++c)
                    {
                        ImGui::PushID(static_cast<int>(c));
                        changed |= TextValue("##cell", c < t.columns.size() ? t.columns[c] : "Дополнительная ячейка " + std::to_string(c + 1), rows[r][c]);
                        ImGui::PopID();
                    }
                    remove = ImGui::Button("Удалить строку");
                }
                ImGui::PopID();
                if (remove) { rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(r)); changed = true; break; }
                else ++r;
            }
            ImGui::PopID();
        }
        // Вложения доступны из любого раздела: одинаковый список, несколько страниц на одно место.
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Вложения: изображения, акты, страницы поверки", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextWrapped("Укажите место в протоколе и подпись. Составной файл прикрепляйте целиком; отдельные страницы одного сертификата — вложениями с тем же местом. "
                               "Допустимы PNG, JPG и PDF. Файлы здесь не запускаются.");
            if (ImGui::Button("Добавить вложение"))
            {
                proc_.formal.attachments.push_back({"scales_verification", "", ""});
                changed = true;
            }
            for (std::size_t i = 0; i < proc_.formal.attachments.size();)
            {
                ImGui::PushID(static_cast<int>(i));
                auto& a = proc_.formal.attachments[i];
                ImGui::Separator();
                ImGui::Text("Вложение %zu", i + 1);
                const char* slots[] = {"mooring_plan", "tape_calibration", "scales_verification", "hydrometer_passport", "organization_logo"};
                const char* places[] = {"Рисунок 1. Схема швартовки", "Ж. Сертификат калибровки рулетки",
                                       "Ж. Сведения о поверке весов", "Ж. Паспорт и поверка ареометра", "Титульный лист. Логотип организации"};
                int selected = -1;
                for (int k = 0; k < 5; ++k) if (a.slot == slots[k]) selected = k;
                ImGui::TextWrapped("Место в протоколе");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##place", &selected, places, 5))
                { a.slot = slots[selected]; changed = true; }
                if (ImGui::TreeNode("Дополнительно"))
                {
                    changed |= TextValue("##slot", "Код места для расширенного шаблона", a.slot, false);
                    changed |= TextValue("##path", "Путь к PNG / JPG / PDF", a.path, false);
                    ImGui::TreePop();
                }
                changed |= TextValue("##caption", "Подпись / номер страницы", a.caption);
                ImGui::TextWrapped("Файл: %s", a.path.empty() ? "не выбран" : text::PathToUtf8(text::PathFromUtf8(a.path).filename()).c_str());
                if (ImGui::Button("Выбрать файл…"))
                {
                    std::string path = a.path;
                    if (dialogs::PickFile(path, L"Изображения и PDF\0*.png;*.jpg;*.jpeg;*.pdf\0", L"Вложение протокола") && AttachmentType(path))
                    { a.path = std::move(path); changed = true; }
                }
                if (!a.path.empty() && !AttachmentType(a.path))
                    ImGui::TextWrapped("Укажите PNG, JPG или PDF. Этот путь не является поддерживаемым вложением.");
                const bool remove = ImGui::Button("Удалить вложение");
                ImGui::PopID();
                if (remove) { proc_.formal.attachments.erase(proc_.formal.attachments.begin() + static_cast<std::ptrdiff_t>(i)); changed = true; break; }
                else ++i;
            }
        }
        if (changed) proc_.projCheck = true;
    }
    ImGui::EndChild();
    const bool canSave = proc_.files && !proc_.load.valid() && !ProcProjectTarget().empty();
    ImGui::BeginDisabled(!canSave);
    if (ImGui::Button("Сохранить проект")) ProcessSaveProject();
    ImGui::EndDisabled();
    if (ImGui::Button("Готово")) proc_.formalOpen = false;
    if (!canSave) ImGui::TextWrapped("Для сохранения откройте записи нужного опыта. Новый опыт начинает чистый шаблон.");
    ImGui::End();
}
} // namespace ui
