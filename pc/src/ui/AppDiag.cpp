// Страница «Диагностика шины»: по датчикам — удачные ответы, таймауты, CRC, чужие кадры, задержка ответа
// (средняя, мин.…макс.); цикл опроса, предельная частота, пауза шины; суперцикл прибора; связь ПК ↔ прибор;
// «Сбросить статистику» (diag reset) и снимок diag текстом.
#include "App.hpp"

#include <algorithm>

#include <imgui.h>

#include "../core/TextUtil.hpp"
#include "Widgets.hpp"

namespace ui
{

namespace
{

std::string Ms(std::uint64_t us)
{
    return Fmt("%.2f", static_cast<double>(us) / 1000.0);
}

// Среднее (мин.…макс.), мс
std::string AvgRange(const proto::Stat5& s)
{
    return s.n ? Ms(s.avg) + "  (" + Ms(s.min) + "…" + Ms(s.max) + ")" : std::string("—");
}

std::string Num(std::uint64_t v)
{
    // 1234567 -> «1 234 567»
    std::string s = std::to_string(v), out;
    for (std::size_t i = 0; i < s.size(); i++)
    {
        if (i && (s.size() - i) % 3 == 0)
            out += "\xC2\xA0"; // неразрывный пробел
        out += s[i];
    }
    return out;
}

} // namespace

void App::TickDiag()
{
    if (diag_.diagReq && diag_.diagReq->done.load())
    {
        auto r = diag_.diagReq;
        diag_.diagReq.reset();
        diag_.diagText = r->lines;
        if (!r->ok)
            diag_.diagText.push_back(r->final);
        diag_.diagAt = text::LocalTimeString(std::time(nullptr));
        diag_.showText = true;
    }
    if (diag_.resetReq && diag_.resetReq->done.load())
    {
        auto r = diag_.resetReq;
        diag_.resetReq.reset();
        if (r->ok)
            Notify("Статистика шины обнулена", 1);
        else
            Notify("Не удалось обнулить статистику: " + r->final, 2);
    }
}

void App::PageDiag()
{
    if (!Connected())
    {
        NotConnectedPanel("Здесь — статистика шины RS485: ответы и ошибки датчиков, задержки, время цикла опроса.");
        return;
    }
    const bool have = snap_.haveStatus;
    const auto& s = snap_.status;

    // Кнопки
    if (Button("Сбросить статистику", BtnKind::Normal, ImVec2(0, 0), diag_.resetReq != nullptr))
        diag_.resetReq = link_->Send("diag reset");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("diag reset: обнулить счётчики ответов, таймаутов, CRC, задержки и время цикла");
    ImGui::SameLine(0, S(8));
    if (Button("Снимок diag (текстом)", BtnKind::Normal, ImVec2(0, 0), diag_.diagReq != nullptr))
        diag_.diagReq = link_->Send("diag", Origin::App, 4000);
    ImGui::SameLine(0, S(16));
    ImGui::AlignTextToFramePadding();
    {
        FontScope f(fontSmall);
        Muted("Счётчики шины — с включения прибора или с последнего сброса.");
    }
    ImGui::Dummy(ImVec2(0, S(2)));

    const float W = ImGui::GetContentRegionAvail().x;
    const float gap = S(12);
    const float leftW = std::floor((W - gap) * 0.56f);
    const float rightW = W - leftW - gap;

    // Левая колонка: датчики, связь
    ImGui::BeginGroup();
    if (BeginCard("##d_sens", ImVec2(leftW, 0)))
    {
        CardTitle("Датчики на шине RS485");
        if (ImGui::BeginTable("##st", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Показатель", ImGuiTableColumnFlags_WidthStretch, 1.45f);
            ImGui::TableSetupColumn("Д2", ImGuiTableColumnFlags_WidthStretch, 1.f);
            ImGui::TableSetupColumn("Д3", ImGuiTableColumnFlags_WidthStretch, 1.f);
            ImGui::TableHeadersRow();
            auto row = [&](const char* label, auto&& get, const char* hint = nullptr) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, pal.textDim);
                ImGui::TextUnformatted(label);
                ImGui::PopStyleColor();
                if (hint)
                    Hint(hint);
                for (int i = 0; i < 2; i++)
                {
                    ImGui::TableNextColumn();
                    const proto::SensorStatus* d = have ? s.Sensor(History::kAddr[i]) : nullptr;
                    if (!d)
                    {
                        Muted("—");
                        continue;
                    }
                    std::string v;
                    bool bad = false;
                    get(*d, v, bad);
                    if (bad)
                        TextColored(pal.warn, "%s", v.c_str());
                    else
                        ImGui::TextUnformatted(v.c_str());
                }
            };
            using SS = proto::SensorStatus;
            row("Состояние", [](const SS& d, std::string& v, bool& bad) {
                v = d.st == "OK" ? "отвечает" : d.st == "LOST" ? "нет связи" : "не отвечал";
                bad = d.st != "OK";
            });
            row("Удачных ответов", [](const SS& d, std::string& v, bool&) { v = Num(d.lat.n); },
                "С включения прибора или с «Сбросить статистику»");
            row("Таймаутов", [](const SS& d, std::string& v, bool& bad) {
                v = Num(d.to);
                bad = d.to > 0;
            }, "Датчик не ответил за время ожидания");
            row("Ошибок CRC", [](const SS& d, std::string& v, bool& bad) {
                v = Num(d.crc);
                bad = d.crc > 0;
            }, "Ответ с битой контрольной суммой — помехи на линии");
            row("Чужих / неполных кадров", [](const SS& d, std::string& v, bool& bad) {
                v = Num(d.bad);
                bad = d.bad > 0;
            }, "Ответ не того адреса или оборванный: два датчика на одном адресе, помехи, мала пауза шины");
            row("Задержка ответа, мс", [](const SS& d, std::string& v, bool&) { v = AvgRange(d.lat); },
                "Средняя (мин.…макс.): от конца запроса до первого байта ответа датчика");
            row("Последняя задержка, мс", [](const SS& d, std::string& v, bool&) { v = d.lat.n ? Ms(d.lat.last) : "—"; });
            row("Ответ целиком, мс", [](const SS& d, std::string& v, bool&) { v = AvgRange(d.done); },
                "Средняя (мин.…макс.): от конца запроса до последнего байта ответа");
            row("Таймаут опроса, мс", [](const SS& d, std::string& v, bool&) { v = Ms(d.tmoUs); });
            row("Ошибок опроса всего", [](const SS& d, std::string& v, bool& bad) {
                v = Num(d.err);
                bad = d.err > 0;
            }, "С включения прибора: таймауты и битые ответы");
            row("Сбойных ответов всего", [](const SS& d, std::string& v, bool& bad) {
                v = Num(d.garbled);
                bad = d.garbled > 0;
            }, "Битые и чужие кадры с включения, в т.ч. до первого удачного ответа");
            row("Последняя ошибка", [](const SS& d, std::string& v, bool& bad) {
                v = d.lastErr == "-" ? "нет" : d.lastErr;
                bad = d.lastErr != "-";
            });
            row("С последнего ответа", [](const SS& d, std::string& v, bool&) {
                v = d.ageMs ? (*d.ageMs < 2000 ? Fmt("%llu мс", static_cast<unsigned long long>(*d.ageMs))
                                               : text::Duration(*d.ageMs / 1000))
                            : "не отвечал";
            });
            ImGui::EndTable();
        }
    }
    EndCard();
    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
    if (BeginCard("##d_link", ImVec2(leftW, 0)))
    {
        CardTitle("Связь ПК ↔ прибор");
        if (BeginKV("##kvlink", S(230)))
        {
            KV("Порт", snap_.port.empty() ? "—" : snap_.port);
            KV("Поток углов", Fmt("%.1f строк/с (каждые %d мс)", snap_.streamHz, snap_.streamMs));
            KV("Строк принято / команд", Num(snap_.linesRx) + " / " + Num(snap_.cmdsTx));
            const ImVec4 tc = snap_.timeouts ? pal.warn : pal.text;
            KV("Команд без ответа", Num(snap_.timeouts), &tc);
            KV("Переподключений", Num(snap_.reconnects));
            KV("Нераспознанных строк", Num(snap_.badLines));
            EndKV();
        }
    }
    EndCard();
    ImGui::EndGroup();

    // Правая колонка: цикл, прибор
    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    const float lw = S(226);
    if (BeginCard("##d_bus", ImVec2(rightW, 0)))
    {
        CardTitle("Цикл опроса");
        if (BeginKV("##kvbus", lw))
        {
            if (have)
            {
                KV("Частота: задана / факт.", Fmt("%d Гц / %.1f Гц", s.freq, s.rate));
                KV("Предел по шине", s.maxRate > 0 ? Fmt("%.1f Гц", s.maxRate) : "—", nullptr,
                   "1 / среднее время цикла: быстрее шина не успевает опросить оба датчика");
                KV("Время цикла, мс", AvgRange(s.busCyc), nullptr, "Среднее (мин.…макс.): паузы и обмен с обоими датчиками");
                KV("Последний цикл, мс", s.busCyc.n ? Ms(s.busCyc.last) : "—");
                KV("Пауза шины: задана, мс", Fmt("%d", s.gap), nullptr, "Тишина на линии перед каждым запросом");
                KV("Пауза шины: факт., мс", AvgRange(s.busGap), nullptr, "Измеренная пауза: средняя (мин.…макс.)");
                KV("Простой до цикла, мс", AvgRange(s.busIdle), nullptr,
                   "Задержка старта цикла сверх паузы — суперцикл прибора был занят");
                const ImVec4 fb = s.fallbacks ? pal.warn : pal.text;
                KV("Резервный таймер", Num(s.fallbacks) + " срабатываний", &fb, "Должно быть 0 (таймер TIM5 исправен)");
            }
            EndKV();
        }
    }
    EndCard();
    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
    if (BeginCard("##d_dev", ImVec2(rightW, 0)))
    {
        CardTitle("Прибор");
        if (BeginKV("##kvdev", lw))
        {
            if (have)
            {
                KV("Прошивка", snap_.haveVer ? "v" + snap_.ver.version : s.fw);
                if (snap_.haveVer && !snap_.ver.build.empty())
                    KV("Сборка", snap_.ver.build);
                KV("Время работы", text::Duration(s.upS));
                KV("Загрузка процессора", Fmt("%d %%", s.cpu));
                KV("Проходов суперцикла", Num(s.loops) + " в секунду");
                KV("Самый долгий проход", Fmt("%llu мс", static_cast<unsigned long long>(s.loopMax)));
                KV("Настройки во флеше", s.savePending ? "ждут сохранения" : "сохранены");
            }
            EndKV();
        }
    }
    EndCard();
    ImGui::EndGroup();

    // Снимок diag — окно поверх страницы
    if (diag_.showText)
    {
        ImGui::OpenPopup("Снимок diag");
        diag_.showText = false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(vp->WorkSize.x - S(80), S(980)), vp->WorkSize.y * 0.8f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Снимок diag", nullptr, ImGuiWindowFlags_NoSavedSettings))
    {
        Muted("Ответ прибора на команду diag, получен %s", diag_.diagAt.c_str());
        const float bh = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.cardAlt);
        ImGui::BeginChild("##diagtext", ImVec2(0, ImGui::GetContentRegionAvail().y - bh), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar);
        {
            FontScope f(fontMono);
            for (const auto& l : diag_.diagText)
                ImGui::TextUnformatted(l.c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        if (Button("Копировать", BtnKind::Normal, ImVec2(S(140), 0)))
        {
            std::string all;
            for (const auto& l : diag_.diagText)
                all += l + "\n";
            ImGui::SetClipboardText(all.c_str());
            Notify("Текст diag скопирован", 1, 2000);
        }
        ImGui::SameLine();
        if (Button("Закрыть", BtnKind::Primary, ImVec2(S(140), 0)) || diag_.closeText)
            ImGui::CloseCurrentPopup();
        diag_.closeText = false;
        ImGui::EndPopup();
    }
}

} // namespace ui
