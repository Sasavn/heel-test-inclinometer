#pragma once
// Отчёт «Обработки» — книга Excel Отчет_Кренование.xlsx (как у BWM427_Analyzer):
//  - «Результаты»: строка на замер и пост (файл, пост, замер, плечо l, средний крен, СКО, точек, h, АКБ, примечание),
//    итоговое среднее h, оценка по МНК, исходные данные (D, P, L, окно, ось, порог);
//  - «Сырые_данные»: все строки файлов замеров в расчёте + замер и пост, числа — числами.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "HeelData.hpp"
#include "Xlsx.hpp"

namespace heel
{

inline constexpr const char* kReportName = "Отчет_Кренование.xlsx";
inline constexpr const char* kPlotName[2] = {"График_Нос.png", "График_Корма.png"};
// Строк в «Сырые_данные» не больше (у Excel предел 1 048 576; книга собирается в памяти — у 32-битной программы
// на сотни тысяч строк её может не хватить).
inline constexpr std::size_t kRawMaxRows = 300000;

struct ReportInfo
{
    std::string folder;  // папка с файлами
    std::string program; // «Регистратор крена — программа для ПК 1.1»
    std::string when;    // время обработки
};

inline const char* AxisName(int axis)
{
    return axis == 0 ? "X (CalcX)" : "Y (CalcY)";
}

namespace detail
{

inline std::size_t Utf8Len(const std::string& s)
{
    std::size_t n = 0;
    for (const char c : s)
        n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    return n;
}

inline std::string Num(double v, int dec)
{
    if (!std::isfinite(v))
        return "—";
    char b[48];
    std::snprintf(b, sizeof(b), "%.*f", dec, v);
    return b;
}

// Ширины столбцов по содержимому (как в старой программе: длина + 2), от 6 до 60 знаков.
inline void AutoWidths(xlsx::Sheet& sh)
{
    sh.widths.clear();
    for (const auto& row : sh.rows)
        for (std::size_t c = 0; c < row.size(); c++)
        {
            const xlsx::Cell& cell = row[c];
            std::size_t len = 0;
            if (cell.kind == xlsx::Cell::Kind::Text)
                len = Utf8Len(cell.text);
            else if (cell.kind == xlsx::Cell::Kind::Number)
                len = Num(cell.num, cell.decimals < 0 ? 3 : cell.decimals).size();
            if (sh.widths.size() <= c)
                sh.widths.resize(c + 1, 6.0);
            sh.widths[c] = std::min(60.0, std::max(sh.widths[c], static_cast<double>(len) + 2.0));
        }
}

} // namespace detail

// Текст про МНК для отчёта и интерфейса.
inline std::string FitText(const Fit& f)
{
    if (f.n == 0 || !std::isfinite(f.k))
        return "нет учтённых точек";
    char b[256];
    if (f.throughOrigin)
        std::snprintf(b, sizeof(b), "tg θ = k·(P·l/D), прямая через ноль (у всех точек одно плечо): k = %.4f, точек %d",
                      f.k, f.n);
    else
        std::snprintf(b, sizeof(b), "tg θ = k·(P·l/D) + b: k = %.4f, b = %.5f (начальный крен %.3f°), R² = %.4f, точек %d",
                      f.k, f.b, std::atan(f.b) * 180.0 / kPi, f.r2, f.n);
    return b;
}

inline std::vector<xlsx::Sheet> BuildReport(const Settings& s, const std::vector<File>& files,
                                            const std::vector<Measurement>& ms, const Summary& sum,
                                            const ReportInfo& info, std::string* warning = nullptr)
{
    using xlsx::Num;
    using xlsx::Text;
    xlsx::Sheet res("Результаты");
    res.AddRow({Text("Файл"), Text("Пост"), Text("Замер"), Text("Плечо l (м)"), Text("Средний крен (°)"), Text("СКО (°)"),
                Text("Точек"), Text("h (м)"), Text("АКБ (В)"), Text("Примечание")});
    for (const Measurement& m : ms)
    {
        if (!m.use)
            continue;
        for (int p = 0; p < 2; p++)
        {
            const PostData& pd = m.post[p];
            if (pd.file < 0)
                continue;
            const File& f = files[static_cast<std::size_t>(pd.file)];
            std::string note = pd.res.counted ? std::string() : pd.res.note;
            if (pd.st.whole)
                note += std::string(note.empty() ? "" : "; ") + "файл короче окна — среднее по всему файлу";
            res.AddRow({Text(f.name), Text(kPostName[p]), m.number > kPcRunBase ? Text(m.label) : Num(m.number, 0),
                        Num(std::isfinite(m.arm) ? std::fabs(m.arm) : s.L, 3), Num(pd.st.mean, 3), Num(pd.st.sd, 3),
                        Num(pd.st.n, 0), pd.res.counted ? Num(pd.res.h, 3) : xlsx::Empty(), Num(pd.batV, 1), Text(note)});
        }
    }
    res.AddRow(std::vector<xlsx::Cell>{});
    res.AddRow({Text("ИТОГОВОЕ СРЕДНЕЕ ЗНАЧЕНИЕ:"), Text("—"), Text("—"), xlsx::Empty(), xlsx::Empty(), xlsx::Empty(),
                Num(sum.n, 0), Num(sum.h, 3), xlsx::Empty(),
                Text(sum.n ? "среднее h по " + std::to_string(sum.n) + " значениям (замер × пост)" +
                                 (std::isfinite(sum.sd) ? ", СКО " + detail::Num(sum.sd, 3) + " м" : std::string())
                           : std::string("нет учтённых значений"))});
    res.AddRow({Text("По МНК:"), Text("—"), Text("—"), xlsx::Empty(), xlsx::Empty(), xlsx::Empty(), Num(sum.ls.n, 0),
                Num(sum.ls.h, 3), xlsx::Empty(), Text(FitText(sum.ls))});
    detail::AutoWidths(res); // по таблице; подписи исходных данных ниже — в широкий столбец A
    res.AddRow(std::vector<xlsx::Cell>{});
    res.AddRow({Text("Исходные данные")});
    res.AddRow({Text("Водоизмещение D (т)"), Num(s.D, 1)});
    res.AddRow({Text("Масса кренящего груза P (т)"), Num(s.P, 2)});
    res.AddRow({Text("Плечо переноса L по умолчанию (м)"), Num(s.L, 3)});
    res.AddRow({Text("Крен — среднее за последние (с)"), Num(s.windowS, 0)});
    res.AddRow({Text("Ось угла"), Text(AxisName(s.axis))});
    res.AddRow({Text("Порог крена |θ| (°)"), Num(s.thresholdDeg, 2)});
    res.AddRow({Text("Формула"), Text("h = P·l / (D·tg|θ|); |θ| не больше порога — не учитывается")});
    if (!info.folder.empty())
        res.AddRow({Text("Папка"), Text(info.folder)});
    if (!info.when.empty())
        res.AddRow({Text("Обработано"), Text(info.when)});
    if (!info.program.empty())
        res.AddRow({Text("Программа"), Text(info.program)});
    res.widths[0] = std::max(res.widths[0], 34.0);

    xlsx::Sheet raw("Сырые_данные");
    raw.AddRow({Text("Замер"), Text("Пост"), Text("Датчик"), Text("Date"), Text("Time"), Text("t (с)"), Text("RawX"),
                Text("RawY"), Text("OffsetX"), Text("OffsetY"), Text("CalcX"), Text("CalcY"), Text("BatV")});
    raw.widths = {8, 8, 9, 12, 10, 10, 9, 9, 9, 9, 9, 9, 7};
    std::size_t skipped = 0;
    for (const Measurement& m : ms)
    {
        if (!m.use)
            continue;
        for (int p = 0; p < 2; p++)
        {
            const PostData& pd = m.post[p];
            if (pd.file < 0)
                continue;
            const File& f = files[static_cast<std::size_t>(pd.file)];
            const std::string sensor = SensorName(f.sensor);
            for (const csvlog::Row& r : f.log.rows)
            {
                if (raw.rows.size() > kRawMaxRows)
                {
                    skipped++;
                    continue;
                }
                raw.AddRow({m.number > kPcRunBase ? Text(m.label) : Num(m.number, 0), Text(kPostName[p]), Text(sensor),
                            r.day >= 0 ? Text(csvlog::DateText(r.day)) : xlsx::Empty(),
                            r.sec >= 0 ? Text(csvlog::ClockText(r.sec)) : xlsx::Empty(), Num(r.t, 3), Num(r.rawX, 2), Num(r.rawY, 2), Num(r.offX, 3),
                            Num(r.offY, 3), Num(r.calcX, 3), Num(r.calcY, 3), Num(r.batV, 1)});
            }
        }
    }
    if (skipped && warning)
        *warning = "в листе «Сырые_данные» не поместилось строк: " + std::to_string(skipped) + " (предел программы — " +
                   std::to_string(kRawMaxRows) + ")";
    return {std::move(res), std::move(raw)};
}

} // namespace heel
