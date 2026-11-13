// Native DOCX regression test. --out <directory> retains reviewable artifacts;
// no Office/Python at runtime.
#include "VodnikData.hpp"
#include "core/HeelProtocol.hpp"
#include "core/IncliningProtocolDocx.hpp"
#include "core/Png.hpp"
#include "core/XlsxRead.hpp"
#include <fstream>
#include <iostream>
#include <set>

namespace fs = std::filesystem;
using namespace heel::formal;
namespace
{
int checks = 0;
void Check(bool b, const std::string &what)
{
    ++checks;
    if (!b)
        throw std::runtime_error(what);
}
std::string Read(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    return {(std::istreambuf_iterator<char>(f)), {}};
}
void Write(const fs::path &p, const std::string &v)
{
    std::ofstream f(p, std::ios::binary);
    f << v;
    Check(bool(f), "write fixture");
}
std::string Part(const fs::path &p, const std::string &name = "word/document.xml")
{
    std::string out;
    Check(xlsxread::detail::Extract(Read(p), name, out), "extract " + name);
    return out;
}
void Has(const std::string &xml, const std::string &text)
{
    Check(xml.find(text) != std::string::npos, "missing: " + text);
}
void Structure(const fs::path &p)
{
    const auto xml = Part(p);
    for (std::size_t i = 1; i < SectionNames().size(); ++i)
        Has(xml, SectionNames()[i]);
    for (const auto &t : TableSpecs())
    {
        Has(xml, t.title);
        if (t.id.find("readings") == std::string::npos)
            for (const auto &column : t.columns)
                Has(xml, xlsx::detail::Escaped(column));
        else
        {
            // The printed matrices pivot the normalized input columns into rows.
            Has(xml, "Показания / номер наклонения");
            Has(xml, "Среднее отклонение, мм");
            Has(xml, "Разность отклонений, мм");
            Has(xml, "Приращение угла, рад");
        }
    }
    for (const auto &f : FieldSpecs())
        if (f.section != 0)
            Has(xml, xlsx::detail::Escaped(f.label));
    for (const char *role : {"Разработал", "Проверил", "Нач. бюро", "Н. контр.", "Утвердил"})
        Has(xml, role);
    for (const char *token : {"<w:tbl>", "<w:tblHeader/>", "<m:f>", "<m:rad>", "<m:sSub>", "<w:pgSz", "<w:pgBorders",
                              "СОДЕРЖАНИЕ", "ЧЕРНОВИК", "Акт №6"})
        Has(xml, token);
    Has(Part(p, "word/footer1.xml"), "NUMPAGES");
}
heel::RsCase Example(heel::Settings &s)
{
    heel::RsCase k;
    k.in.D = s.units.ToTonnes(s.D);
    k.in.P = s.units.ToTonnes(s.P);
    k.in.theta = {0, 1, 2};
    k.in.arm = {0, 1, 2};
    k.in.post = {{0, 1, 2}, {0, 1.01, 2.02}, {0, .99, 1.98}, {0, 1.02, 2.04}};
    k.postIds = {0, 1, 2, 3};
    k.label = {"0", "POS_A", "POS_FINAL"};
    k.r = rs::Evaluate(k.in);
    k.hMain = k.r.hFinal;
    k.dh = k.in.dh;
    k.checks = rs::Checks(k.r, k.c);
    return k;
}

void ControlFitRegression(const fs::path &out)
{
    for (bool model : {false, true})
    {
        heel::Settings s;
        s.units.model = model;
        s.D = model ? 100000 : 100;
        heel::RsCase k;
        k.in.D = 100;
        k.in.theta = {0, 1, 2};
        // Same physical line: tan(theta) rises by 0.5 per 100 t*m.
        // Its height is 2 m. Inverse slope alone is 200 t*m / 2e8 kg*mm.
        k.M = model ? std::vector<double>{0, 1e8, 2e8} : std::vector<double>{0, 100, 200};
        k.tanT = {0, .5, 1};
        k.fit = heel::FitLine(k.M, k.tanT);
        Check(std::abs(k.fit.k - (model ? 5e-9 : .005)) < 1e-12, "control slope has moment units");
        k.hCurrent = 1.234567;
        k.hMain = 1.9;
        k.dh = .1;
        std::string err;
        const auto path = out / (model ? "control-fit-model.docx" : "control-fit-ship.docx");
        Check(WriteDocx(path, DefaultData(), s, k, {}, {}, &err), err);
        const auto doc = Part(path);
        Has(doc, "МНК контрольного графика: 2,000000 м");
        Has(doc, "без поправки Δh");
        Has(doc, "От начального положения, без поправки: 1,234567 м");
        Has(doc, "От начального положения, с поправкой Δh: 1,134567 м");
        Has(doc, "итог=1,900000 м");
    }
}

void D0Regression(const fs::path &out)
{
    heel::Settings s;
    s.D = 100;
    s.rsMain = 1;
    heel::RsCase k;
    k.in.D = 100;
    k.in.P = 1;
    k.in.Dworst = 120;
    k.in.hInfo = 1;
    k.in.lMax = .5;
    k.in.dh = .1;
    for (int i = 0; i <= 8; ++i)
        k.in.theta.push_back(i * .01 * 180 / heel::kPi);
    k.in.dM.assign(8, 2); // h_i=2/(100*0.01)=2 m
    Data d = DefaultData();
    d.fields["center_mode"] = "manual";
    d.fields["center_source"] = "Synthetic known center";
    d.fields["center_x_m"] = "1";
    d.fields["center_y_m"] = "0";
    d.fields["center_z_m"] = "2";
    d.fields["excess_inventory_complete"] = "yes";
    d.fields["missing_inventory_complete"] = "yes";
    for (int mode = 0; mode < 3; ++mode)
    {
        k.in.D0 = mode == 0 ? 99 : mode == 1 ? 100 : heel::kNaN;
        k.r = rs::Evaluate(k.in);
        k.hMain = k.r.hFinal;
        k.dh = k.in.dh;
        const auto ratio = k.r.Dratio, error = k.r.err663;
        const auto quality = k.r.ok663;
        const auto path = out / (mode == 0 ? "d0-mismatch.docx" : mode == 1 ? "d0-match.docx" : "d0-missing.docx");
        std::string err;
        Check(WriteDocx(path, d, s, k, {}, {}, &err), err);
        const auto doc = Part(path);
        Has(doc, "D₀ баланса грузов = 100,000000 т");
        if (mode == 0)
        {
            Has(doc, "D₀ в ранее рассчитанной проверке качества = 99,000000 т");
            Has(doc, "ПРЕДУПРЕЖДЕНИЕ: D₀ баланса отличается от D₀ проверки качества");
            Has(doc, "Отношение Δ₀/Δ₁=0,825000");
        }
        else if (mode == 1)
        {
            Has(doc, "D₀ баланса и проверки качества согласованы");
            Check(doc.find("ПРЕДУПРЕЖДЕНИЕ: D₀ баланса отличается") == std::string::npos,
                  "equal D0 has no mismatch warning");
        }
        else
            Has(doc, "D₀ проверки качества не задано");
        Check(k.r.Dratio == ratio && k.r.err663 == error && k.r.ok663 == quality,
              "D0 provenance never recalculates quality");
    }
    // Independent counterexample: only reconciling D0 flips 6.6.3 pass -> fail.
    k.in.theta = {0};
    k.in.dM.assign(8, 1);
    k.in.D0 = 90;
    k.in.Dworst = 100;
    k.in.hInfo = .8;
    k.in.lMax = .4;
    const double deviation = .04 * std::sqrt(7.0) / 5.4;
    for (int i = 0; i < 8; ++i)
    {
        const double hi = 1 + (i % 2 ? deviation : -deviation);
        k.in.theta.push_back(k.in.theta.back() + 1 / (100 * hi) * 180 / heel::kPi);
    }
    k.r = rs::Evaluate(k.in);
    k.hMain = k.r.hFinal;
    auto reconciled = k.in;
    reconciled.D0 = 110;
    Check(k.r.ok663 == 1 && rs::Evaluate(reconciled).ok663 == 0, "90 vs 110 tonnes flips quality condition");
    d.tables["excess_loads"] = {{"Extra", "synthetic", "10", "0", "0", "0"}};
    d.tables["missing_loads"] = {{"Missing", "synthetic", "20", "0", "0", "0"}};
    k.checks = rs::Checks(k.r, k.c);
    std::string err;
    Check(WriteDocx(out / "d0-blocking-context.docx", d, s, k, {}, {}, &err), err);
    const auto doc = Part(out / "d0-blocking-context.docx");
    Has(doc, "D₀ баланса грузов = 110,000000 т");
    Has(doc, "Оценка качества текущего баланса заблокирована для итогового заключения");
    Has(doc, "Приведённые ниже статусы относятся к ранее рассчитанным исходным данным");
    Has(doc, "e·Δ₀/Δ₁=0,036000");
    Check(k.r.ok663 == 1 && std::abs(k.r.Dratio - .9) < 1e-12, "blocking context preserves original result");
}

void QualityBoundaryRegression(const fs::path &out)
{
    heel::Settings s;
    s.D = 100;
    heel::RsCase k;
    k.in.D = 100;
    k.in.theta = {0};
    k.in.dM.assign(9, 1);
    for (int i = 0; i < 9; ++i)
        k.in.theta.push_back(k.in.theta.back() + .01 / (i == 8 ? 1 + 1e-11 : 1) * 180 / heel::kPi);
    k.r = rs::Evaluate(k.in);
    Check(k.r.ok661 && k.r.excludedCount == 0 && k.r.n == 9, "core retains nearly identical heights");
    Check(std::abs(k.r.m.back().h - k.r.hk) > 2 * k.r.sigma, "fixture crosses strict 2sigma only");
    std::string err;
    Check(WriteDocx(out / "quality-rounding-boundary.docx", DefaultData(), s, k, {}, {}, &err), err);
    const auto doc = Part(out / "quality-rounding-boundary.docx");
    const auto begin = doc.find("Таблица 11 –"), end = doc.find("8. ЗАКЛЮЧЕНИЕ", begin);
    Check(begin != std::string::npos && end != std::string::npos, "quality table boundaries");
    const auto table = doc.substr(begin, end - begin);
    Check(table.find("за пределом 2σ") == std::string::npos, "display follows core rounding tolerance");
    std::size_t at = 0;
    int retained = 0;
    while ((at = table.find("в пределах 2σ", at)) != std::string::npos)
    {
        ++retained;
        ++at;
    }
    Check(retained == 9, "all nine quality labels agree with core");
}

void BranchLabelsRegression(const fs::path &out)
{
    heel::Settings s;
    s.D = 100;
    s.rsInstrument = 3;
    s.postNames = {"НОС", "КОРМА", "МИДЕЛЬ", "ЗАПАСНОЙ"};
    heel::RsCase k;
    k.in.D = 100;
    k.in.P = 1;
    k.in.theta = {0, 1};
    k.in.arm = {0, 1};
    k.in.post = {{0, 1}, {0, 1.02}};
    k.postIds = {2, 0};
    k.dh = .2;
    k.in.dh = .2;
    k.hCurrent = 1.5;
    k.hMain = 1.3;
    for (int instrument : {-1, 0, 1, 2, -2})
    {
        k.in.instrument = instrument;
        k.r = rs::Evaluate(k.in);
        const auto before = k.r.m.front().dTheta;
        const auto path = out / ("branch-labels-" + std::to_string(instrument) + ".docx");
        std::string err;
        Check(WriteDocx(path, DefaultData(), s, k, {}, {}, &err), err);
        const auto doc = Part(path);
        const std::string expected = instrument == 0   ? "Пост «МИДЕЛЬ»"
                                     : instrument == 1 ? "Пост «НОС»"
                                                       : "Среднее пригодных приращений постов";
        Has(doc, "Источник приращения угла таблицы 9: " + expected);
        Has(doc, "От начального положения, без поправки: 1,500000 м");
        Has(doc, "От начального положения, с поправкой Δh: 1,300000 м");
        Has(doc, "Уменьшение по 6.6.5 не применено");
        Check(k.r.m.front().dTheta == before, "labels never change selected angle");
    }
    k.in.post.clear();
    k.postIds.clear();
    k.in.instrument = 0;
    k.r = rs::Evaluate(k.in);
    std::string err;
    Check(WriteDocx(out / "branch-labels-single.docx", DefaultData(), s, k, {}, {}, &err), err);
    Has(Part(out / "branch-labels-single.docx"),
        "Источник приращения угла таблицы 9: Приращения единого расчётного ряда");
    k.in.theta = {0};
    k.in.arm.clear();
    k.in.dM.assign(8, 1);
    for (int i = 0; i < 8; ++i)
        k.in.theta.push_back(k.in.theta.back() + .01 / (i % 2 ? 1.1 : .9) * 180 / heel::kPi);
    k.in.hInfo = .8;
    k.in.lMax = .4;
    k.in.D0 = 100;
    k.in.Dworst = 100;
    k.r = rs::Evaluate(k.in);
    Check(k.r.reduced, "fixture applies 6.6.5");
    Check(std::abs(k.r.hFinalRaw - (k.r.hk - k.r.err)) < 1e-12, "core raw height includes probability error once");
    s.rsMain = 1;
    k.hMain = k.r.hFinal;
    Check(WriteDocx(out / "branch-labels-reduced.docx", DefaultData(), s, k, {}, {}, &err), err);
    Has(Part(out / "branch-labels-reduced.docx"), "Уменьшение по 6.6.5 применено");
    Has(Part(out / "branch-labels-reduced.docx"),
        "Итог без поправки на свободные поверхности уже уменьшен на вероятную ошибку");
}

void MovesReferenceRegression(const fs::path &out)
{
    heel::Settings s;
    s.rsGroups = {{"G1", 1}, {"G2", 2}};
    for (int i = 0; i < 10; ++i)
        s.rsMoves.push_back({double(i + 1), -double(i + 1)});
    Data d = DefaultData();
    for (int i = 0; i < 10; ++i)
        d.tables["moves"].push_back(
            {"MANUAL_" + std::to_string(i + 1), "G1", "right", "center", "left", "17", "3", "987654.3"});
    d.tables["moves"].back().push_back("MANUAL_EXTRA_ONLY");
    d.tables["act_moves"] = d.tables["moves"];
    std::string err;
    Check(WriteDocx(out / "moves-reference.docx", d, s, {}, {}, {}, &err), err);
    const auto doc = Part(out / "moves-reference.docx");
    auto count = [](const std::string &text, const std::string &token) {
        std::size_t at = 0;
        int n = 0;
        while ((at = text.find(token, at)) != std::string::npos)
        {
            ++n;
            at += token.size();
        }
        return n;
    };
    auto section = [&](const std::string &begin, const std::string &end) {
        const auto start = doc.find(begin);
        Check(start != std::string::npos, "moves section starts");
        const auto finish = doc.find(end, start + begin.size());
        Check(finish != std::string::npos, "moves section ends");
        return doc.substr(start, finish - start);
    };
    for (const auto &block : std::vector<std::string>{section("Таблица 4 –", "Таблица 5 –"),
                                                      section("Акт №5. Таблица 2 –", "ПРИЛОЖЕНИЕ Е")})
    {
        const auto split = block.find("Справочные ручные сведения");
        Check(split != std::string::npos, "manual reference is separate");
        const auto canonical = block.substr(0, split), reference = block.substr(split);
        Has(canonical, "Расчётный блок из настроек обработки: 20 строк");
        Check(count(canonical, "<w:tr>") == 42,
              "exactly 20 canonical rows plus repeated header in each of two column blocks");
        Check(canonical.find("MANUAL_") == std::string::npos && canonical.find("987654.3") == std::string::npos,
              "manual rows excluded from canonical movements");
        Has(reference, "не являются дополнительными расчётными перемещениями и не входят в сумму ΔM");
        Check(count(reference, "MANUAL_EXTRA_ONLY") == 1, "manual extra column retained once per form");
        for (int i = 1; i <= 10; ++i)
            Check(count(reference, "MANUAL_" + std::to_string(i) + "</w:t>") == 2,
                  "manual row retained once in each horizontal continuation");
    }
    d.tables["act_moves"] = {{"ACT_DISTINCT", "custom", "", "", "", "", "", "", "ACT_EXTRA"}};
    Check(WriteDocx(out / "moves-reference-distinct.docx", d, s, {}, {}, {}, &err), err);
    const auto distinct = Part(out / "moves-reference-distinct.docx");
    Has(distinct, "ACT_DISTINCT");
    Has(distinct, "ACT_EXTRA");
    Has(distinct, "MANUAL_EXTRA_ONLY");
}
} // namespace
int main(int argc, char **argv)
{
    try
    {
        fs::path out = fs::current_path() / "inclining_protocol_output";
        std::string regression;
        for (int i = 1; i + 1 < argc; ++i)
            if (std::string(argv[i]) == "--out")
                out = text::PathFromUtf8(argv[++i]);
            else if (std::string(argv[i]) == "--regression")
                regression = argv[++i];
        fs::create_directories(out);
        if (!regression.empty())
        {
            if (regression == "fit")
                ControlFitRegression(out);
            else if (regression == "d0")
                D0Regression(out);
            else if (regression == "2sigma")
                QualityBoundaryRegression(out);
            else if (regression == "labels")
                BranchLabelsRegression(out);
            else if (regression == "moves")
                MovesReferenceRegression(out);
            else
                throw std::runtime_error("unknown regression");
            std::cout << "regression " << regression << ": " << checks << " checks passed\n";
            return 0;
        }
        ControlFitRegression(out);
        D0Regression(out);
        QualityBoundaryRegression(out);
        BranchLabelsRegression(out);
        MovesReferenceRegression(out);
        std::string err;
        heel::Settings s;
        heel::ReportInfo info;
        heel::RsCase empty;
        Check(WriteDocx(out / "empty.docx", DefaultData(), s, empty, info, {}, &err), err);
        Structure(out / "empty.docx");
        Has(Part(out / "empty.docx"), "нет данных / не заполнено");
        std::set<std::string> ids;
        for (const auto &f : FieldSpecs())
            Check(ids.insert(f.id).second, "duplicate field id");
        ids.clear();
        for (const auto &t : TableSpecs())
            Check(ids.insert(t.id).second, "duplicate table id");

        Data d = DefaultData();
        for (const char *role : {"developer", "checker", "bureau_head", "norm_controller", "approver"})
            d.fields[std::string(role) + "_date"] = std::string("DATE_") + role;
        d.fields["vessel_name"] = "Synthetic <&\" vessel";
        d.fields["center_mode"] = "manual";
        d.fields["center_source"] = "Synthetic fixture: known center";
        d.fields["center_x_m"] = "2";
        d.fields["center_y_m"] = "-1";
        d.fields["center_z_m"] = "3";
        d.fields["excess_inventory_complete"] = "yes";
        d.fields["missing_inventory_complete"] = "yes";
        d.fields["future_field"] = "\n\t\"\\ 😀";
        d.tables["excess_loads"] = {{"EXTRA", "synthetic", "4", "1", "-2", "5", "9999", "9999", "9999"}};
        d.tables["missing_loads"] = {{"MISSING", "synthetic", "1", "-2", "1", "4"}};
        d.tables["rolling"] = {{"Manual", "1", "3", "15", "bad supplied period", "bad supplied average"}};
        d.tables["freeboard"] = {{"1", "station", "1", "3", "2", "4", "2.5", "7.5"}};
        d.tables["draft_marks"] = {{"bow", "2", "4", "3", "4", "6", "5", "4"}};
        d.tables["future_table"] = {{"retained extra table", "tail cell"}};
        json::Value parsed;
        Data copy;
        Check(json::Parse(ToJson(d), parsed, &err), err);
        Check(FromJson(parsed, copy, &err), err);
        Check(ToJson(d) == ToJson(copy), "JSON exact round trip including Unicode/unknown fields");
        auto unchanged = copy;
        for (const char *bad :
             {"{\"fields\":[]}", "{\"tables\":{\"bad\":[[2]]}}", "{\"attachments\":[{}]}", "{\"extra\":1e999}"})
        {
            Check(json::Parse(bad, parsed, &err), "test bad JSON parsed");
            Check(!FromJson(parsed, copy, &err), "invalid type/nonfinite rejected");
            Check(ToJson(copy) == ToJson(unchanged), "failed import atomic");
        }
        s.units.model = true;
        s.D = 100000;
        s.P = 1000;
        s.L = 1000;
        s.rsGroups = {{"GROUP_LAST", 1000}, {"GROUP_OTHER", 2000}};
        s.rsMoves = {{1000, -2000}, {-1000, 2000}};
        s.postNames = {"POST_BOW", "POST_STERN", "POST_THREE", "POST_FOUR"};
        auto k = Example(s);
        k.checks.push_back({"Synthetic decimal rendering", "6.6.2", "0.1250 ≤ 0.50 м; 6.6.2", "decimal_check", 1});
        const std::string pdf = "%PDF-1.4\n% synthetic external attachment, not a "
                                "signed document\n%%EOF\n";
        Write(out / "synthetic.pdf", pdf);
        d.attachments.push_back({"extra_pdf", text::PathToUtf8(out / "synthetic.pdf"), "Synthetic external PDF"});
        Check(png::WriteRgb(out / "synthetic.png", 3, 2, std::vector<unsigned char>(18, 160)), "native PNG fixture");
        auto bytes = Read(out / "synthetic.png");
        for (auto &a : d.attachments)
            if (a.slot == "mooring_plan")
                a.path = text::PathToUtf8(out / "synthetic.png");
        std::vector<heel::ReportImage> images{
            {"PLOT_FINAL", std::vector<unsigned char>(bytes.begin(), bytes.end()), 3, 2}};
        xlsx::Sheet legacy("Synthetic");
        legacy.AddRow({xlsx::Text("legacy workbook retained")});
        Check(xlsx::Write(out / text::PathFromUtf8(heel::kReportName), {legacy}, &err), err);
        Check(WriteDocx(out / "filled.docx", d, s, k, info, images, &err), err);
        Structure(out / "filled.docx");
        auto document = Part(out / "filled.docx");
        Has(document, "Способ определения ЦТ: Ручной ввод");
        Has(document, "Опись излишних грузов: Опись проверена");
        Has(document, "Опись недостающих грузов: Опись проверена");
        Check(document.find("center_mode") == std::string::npos, "missing-data notes contain no persistence keys");
        Check(d.fields.at("center_mode") == "manual" && d.fields.at("excess_inventory_complete") == "yes",
              "display leaves persisted values unchanged");
        for (const char *role : {"developer", "checker", "bureau_head", "norm_controller", "approver"})
            Has(document, std::string("DATE_") + role);
        Has(document, "0,1250 ≤ 0,50 м; 6.6.2");
        Check(document.find("nan м") == std::string::npos && document.find("+nan°") == std::string::npos,
              "nonfinite check descriptions are explicit missing data");
        Check(document.find("Поле NUMPAGES") == std::string::npos, "title contains human-readable sheet reference");
        for (const char *token :
             {"97,000000", "194,000000", "-91,000000", "284,000000", "2,927835", "-0,938144", "-8,000000", "20,000000",
              "5,000000", "Synthetic &lt;&amp;&quot; vessel", "POST_BOW", "POST_STERN", "POST_THREE", "POST_FOUR",
              "GROUP_LAST", "POS_FINAL", "PLOT_FINAL", "Дополнительная таблица future_table"})
            Has(document, token);
        Check(document.find("9999") == std::string::npos, "recomputed mass moments replace supplied errors");
        Has(Part(out / "filled.docx", "word/_rels/document.xml.rels"), "media/image");
        Check(!Part(out / "filled.docx", "word/media/image1.png").empty(), "embedded PNG");
        bool copied = false, book = false;
        for (const auto &dir : fs::directory_iterator(out))
            if (dir.is_directory() &&
                document.find(text::PathToUtf8(dir.path().filename()) + "/manifest.txt") != std::string::npos)
                for (const auto &f : fs::directory_iterator(dir))
                {
                    if (f.path().extension() == ".pdf")
                    {
                        Check(Read(f.path()) == pdf, "PDF byte-exact copy");
                        copied = true;
                    }
                    if (f.path().extension() == ".xlsx")
                    {
                        Check(Read(f.path()) == Read(out / text::PathFromUtf8(heel::kReportName)),
                              "legacy XLSX byte-exact");
                        book = true;
                    }
                }
        Check(copied && book, "external PDF and XLSX in complete bundle");
        Data duplicate = d;
        duplicate.tables["tanks"] = {{"EXTRA", "4", "1", "-2", "5", "0", "0", "0", "excess"}};
        duplicate.tables["missing_loads"][0].push_back("98.1"); // optional force, never a mass
        Check(WriteDocx(out / "duplicate-tank.docx", duplicate, s, k, info, {}, &err), err);
        const auto duplicateXml = Part(out / "duplicate-tank.docx");
        Has(duplicateXml, "учтена один раз");
        Has(duplicateXml, "97,000000");
        Has(duplicateXml, "98.1");
        Check(duplicateXml.find("93,000000") == std::string::npos, "duplicated liquid not subtracted twice");

        // The input directory may contain stale exports; only the fresh target
        // directory can provide an implicit legacy supplement.
        fs::path fresh = out / "freshness";
        for (int n = 1; fs::exists(fresh); ++n)
            fresh = out / ("freshness_" + std::to_string(n));
        fs::create_directories(fresh / "source");
        fs::create_directories(fresh / "export");
        Write(fresh / "source" / text::PathFromUtf8(heel::kReportName), "STALE_XLSX");
        Write(fresh / "source" / text::PathFromUtf8("График_старый.png"), "OLD_PNG");
        heel::ReportInfo staleInfo;
        staleInfo.folder = text::PathToUtf8(fresh / "source");
        Check(WriteDocx(fresh / "export" / "without-excel.docx", DefaultData(), s, k, staleInfo, {}, &err), err);
        bool missingBook = false;
        for (const auto &directory : fs::directory_iterator(fresh / "export"))
            if (directory.is_directory())
            {
                const auto manifest = Read(directory.path() / "manifest.txt");
                Has(manifest, "ОТСУТСТВУЕТ");
                Has(manifest, "исходный отчёт XLSX");
                missingBook = true;
                for (const auto &f : fs::directory_iterator(directory))
                {
                    Check(Read(f.path()).find("STALE_XLSX") == std::string::npos, "stale workbook never imported");
                    Check(Read(f.path()).find("OLD_PNG") == std::string::npos, "old input plot never imported");
                }
            }
        Check(missingBook, "missing current workbook reported");
        Write(fresh / "export" / text::PathFromUtf8(heel::kReportName), "NEW_XLSX");
        Check(WriteDocx(fresh / "export" / "with-excel.docx", DefaultData(), s, k, staleInfo, {}, &err), err);
        bool newBook = false;
        for (const auto &directory : fs::directory_iterator(fresh / "export"))
            if (directory.is_directory())
                for (const auto &f : fs::directory_iterator(directory))
                    if (f.path().extension() == ".xlsx")
                    {
                        Check(Read(f.path()) == "NEW_XLSX", "fresh target workbook copied");
                        newBook = true;
                    }
        Check(newBook, "fresh workbook included");
        Data composite = DefaultData();
        composite.attachments.push_back({"tape_calibration+tape_calibration_back",
                                         text::PathToUtf8(out / "synthetic.pdf"),
                                         "Synthetic tape calibration, both logical parts"});
        Check(WriteDocx(fresh / "export" / "composite.docx", composite, s, k, info, {}, &err), err);
        const auto compositeXml = Part(fresh / "export" / "composite.docx");
        Has(compositeXml, "состав задан оператором: tape_calibration+tape_calibration_back");
        for (const auto &directory : fs::directory_iterator(fresh / "export"))
            if (directory.is_directory() &&
                compositeXml.find(text::PathToUtf8(directory.path().filename()) + "/manifest.txt") != std::string::npos)
            {
                const auto manifest = Read(directory.path() / "manifest.txt");
                Check(manifest.find("\ntape_calibration | ") == std::string::npos,
                      "composite front not marked missing");
                Check(manifest.find("\ntape_calibration_back | ") == std::string::npos,
                      "composite back not marked missing");
            }
        const auto saved = Read(out / "filled.docx");
        Data failure = DefaultData();
        Write(out / "zero-attachment.bin", "");
        failure.attachments.push_back(
            {"zero", text::PathToUtf8(out / "zero-attachment.bin"), "Invalid zero-length attachment"});
        Check(!WriteDocx(out / "filled.docx", failure, s, k, info, {}, &err), "empty attachment rejected");
        Check(Read(out / "filled.docx") == saved, "failure preserves existing report");
        Check(!WriteDocx(fresh / "export", DefaultData(), s, k, info, {}, &err),
              "directory cannot be replaced as a DOCX");

        d.fields["center_x_m"] = "nan";
        d.fields["center_y_m"] = "inf";
        d.fields["center_z_m"] = "1e999";
        Check(WriteDocx(out / "invalid-input.docx", d, s, k, info, {}, &err), err);
        Has(Part(out / "invalid-input.docx"), "ЦТ при опыте: Xg=нет данных");
        d.fields["center_mode"] = "hydro";
        d.fields["hydro_source"] = "synthetic geometry";
        d.fields["coordinate_system"] = "explicit synthetic axes";
        d.fields["hydro_xc_m"] = "-1";
        d.fields["hydro_zc_m"] = "2";
        d.fields["hydro_r_m"] = "3";
        d.fields["trim_deg"] = "-2";
        d.fields["initial_heel_deg"] = "-1";
        d.fields["hydro_x_sign"] = "+";
        k.hMain = 2;
        Check(WriteDocx(out / "hydro.docx", d, s, k, info, {}, &err), err);
        Has(Part(out / "hydro.docx"), "знак X введён явно: +");
        Has(Part(out / "hydro.docx"), "Способ определения ЦТ: По заданной гидростатике");
        for (const char *value : {"-1,034899", "2,999391", "-0,034910"})
            Has(Part(out / "hydro.docx"), value);
        Data unknown = d;
        unknown.fields["center_mode"] = "unrecognized_mode";
        unknown.fields["missing_inventory_complete"] = "maybe";
        Check(WriteDocx(out / "unknown-mode.docx", unknown, s, k, info, {}, &err), err);
        const auto unknownXml = Part(out / "unknown-mode.docx");
        Has(unknownXml, "Способ не определён / требуется выбор");
        Has(unknownXml, "Статус не определён / требуется проверка описи");
        Check(unknownXml.find("center_mode") == std::string::npos &&
                  unknownXml.find("unrecognized_mode") == std::string::npos,
              "unknown choices remain human-readable");

        Data partial = DefaultData();
        partial.tables["readings_1"] = {
            {"MEAN_ONLY", "0", "", "", "", "41.25", "", "", ""},
            {"PARTIAL", "0", "1", "вправо", "17.5", "", "", "", ""},
            {"PARTIAL", "0", "14", "влево", "33.5", "", "", "", ""},
            {"PARTIAL", "0", "custom", "особый отсчёт", "12.5", "", "", "", "", "KEEP_EXTRA"}};
        partial.tables["readings_2"] = {{"SECOND_MEAN", "0", "", "", "", "22.75", "", "", ""}};
        Check(WriteDocx(out / "partial-manual.docx", partial, s, empty, info, {}, &err), err);
        const auto partialXml = Part(out / "partial-manual.docx");
        auto block = [&](const std::string &begin, const std::string &end) {
            const auto from = partialXml.find(begin);
            Check(from != std::string::npos, "partial matrix begins: " + begin);
            const auto to = partialXml.find(end, from + begin.size());
            Check(to != std::string::npos, "partial matrix ends: " + end);
            return partialXml.substr(from, to - from);
        };
        const auto first = block("Таблица 6 –", "Таблица 7 –");
        const auto second = block("Таблица 7 –", "Таблица 8 –");
        const auto actFirst = block("Акт №6 – Запись показаний первого", "Акт №6 – Запись показаний второго");
        for (const auto &pair : std::vector<std::pair<std::string, std::string>>{{first, "MEAN_ONLY"},
                                                                                 {first, "PARTIAL"},
                                                                                 {second, "SECOND_MEAN"},
                                                                                 {actFirst, "MEAN_ONLY"},
                                                                                 {actFirst, "PARTIAL"}})
        {
            for (int number = 1; number <= 10; ++number)
                Has(pair.first,
                    pair.second + " — Замер " + std::to_string(number) + (number % 2 ? " вправо, мм" : " влево, мм"));
            for (const char *result : {"Среднее отклонение, мм", "Разность отклонений, мм", "Приращение угла, рад"})
                Has(pair.first, pair.second + " — " + result);
        }
        for (const char *value :
             {"41.25", "17.5", "33.5", "12.5", "KEEP_EXTRA", "Замер 14 влево", "Замер custom особый отсчёт"})
            Has(first, value);
        Has(second, "22.75");

        // Direct table-level reproduction of the existing repository fixture. No
        // invented CSV files.
        heel::Settings vs;
        vs.D = vodnik::kD;
        heel::RsCase vk;
        vk.in.D = vodnik::kD;
        vk.in.D0 = vodnik::kD0;
        vk.in.Dworst = vodnik::kDworst;
        vk.in.hInfo = vodnik::kH;
        vk.in.lMax = vodnik::kLmax;
        vk.in.dh = vodnik::kDh;
        vk.postIds = {0, 1};
        vk.in.post.resize(2);
        for (int i = 0; i < vodnik::kPositions; ++i)
        {
            vk.in.theta.push_back(vodnik::ThetaDeg(0, i));
            for (int p = 0; p < 2; ++p)
                vk.in.post[p].push_back(vodnik::ThetaDeg(p, i));
        }
        for (int i = 0; i < 10; ++i)
            vk.in.dM.push_back(vodnik::kMass[vodnik::kGroup[i]] * vodnik::kArm[i]);
        vk.r = rs::Evaluate(vk.in);
        vk.hMain = vk.r.hFinal;
        vk.dh = vodnik::kDh;
        Check(std::abs(vk.r.hk - 1.154) < .001 && std::abs(vk.hMain - 1.123) < .001, "existing fixture h values");
        Check(std::abs(vk.r.eps - .0554) < 1e-9, "epsilon lower bound correctly applied");
        Data vd = DefaultData();
        vd.fields["measurement_mode"] = "manual";
        vd.fields["purpose"] = "Воспроизведение образца по существующей расчётной фикстуре. Не новый "
                               "опыт. Не архивная электронная запись.";
        vd.fields["quality_comment"] = "Реконструкции существующей фикстуры: начало первого веска 368,5 "
                                       "вместо 418,9 мм; второе плечо 2,40 вместо 2,42 м. Противоречия "
                                       "гидростатики не разрешены.";
        Check(WriteDocx(out / "reproduction.docx", vd, vs, vk, info, {}, &err), err);
        Has(Part(out / "reproduction.docx"), "0,055400");

        Data longData = DefaultData();
        longData.fields["purpose"] = std::string(6000, 'Q') + "END_LONG_FIELD";
        for (int n = 0; n < 47; ++n)
        {
            longData.tables["readings_1"].push_back(
                {"MATRIX_POST", std::to_string(n), "1", "вправо", std::to_string(n + 10), "", "", "", ""});
            longData.tables["documents"].push_back({std::to_string(n),
                                                    std::string(250, 'A') + "END_ROW_" + std::to_string(n), "code",
                                                    "date", "source", "EXTRA_COL_" + std::to_string(n)});
        }
        auto stress = k;
        auto stressSettings = s;
        stressSettings.rsGroups.clear();
        stressSettings.rsMoves.clear();
        for (int g = 0; g < 5; ++g)
            stressSettings.rsGroups.push_back({"STRESS_GROUP_" + std::to_string(g), double((g + 1) * 1000)});
        for (int move = 0; move < 26; ++move)
            stressSettings.rsMoves.push_back({double(move * 100 + 10), -20, 30, -40, 50});
        stress.in.post.resize(9);
        stress.postIds.clear();
        for (std::size_t p = 0; p < stress.in.post.size(); ++p)
            for (int pos = 0; pos < 81; ++pos)
            {
                if (pos == 0)
                    stress.in.post[p].clear();
                stress.in.post[p].push_back(10 * double(p) + 0.01 * pos);
            }
        stress.label.clear();
        for (int n = 0; n < 81; ++n)
            stress.label.push_back("STRESS_POSITION_" + std::to_string(n));
        Check(WriteDocx(out / "long-stress.docx", longData, stressSettings, stress, info, images, &err), err);
        auto longXml = Part(out / "long-stress.docx");
        for (const char *token :
             {"END_LONG_FIELD", "END_ROW_46", "EXTRA_COL_46", "STRESS_POSITION_80", "Пост 9", "продолжение"})
            Has(longXml, token);
        for (int p = 0; p < 9; ++p)
            Has(longXml, p < 4 ? s.PostName(p) : "Пост " + std::to_string(p + 1));
        for (int move = 1; move <= 26; ++move)
            Has(longXml, "Перемещение " + std::to_string(move));
        for (int group = 0; group < 5; ++group)
            Has(longXml, "STRESS_GROUP_" + std::to_string(group));
        for (int post = 0; post < 9; ++post)
            for (int pos = 0; pos < 81; ++pos)
            {
                char number[48];
                std::snprintf(number, sizeof(number), "%.6f", 10 * post + 0.01 * pos);
                std::string display = number;
                std::replace(display.begin(), display.end(), '.', ',');
                Has(longXml, display);
            }
        std::cout << "inclining_protocol_test: " << checks << " checks passed; artifacts: " << text::PathToUtf8(out)
                  << "\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "inclining_protocol_test failed: " << e.what() << "\n";
        return 1;
    }
}
