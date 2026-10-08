#pragma once
// Картинка из интерфейса без окна и без OpenGL — графики «Обработки» в PNG. Свой контекст ImGui и ImPlot со своим
// атласом шрифтов (крупнее экранного — для отчёта), светлая тема, белый фон; ImDrawData растеризуется программно
// (как в стенде снимков tests/ui_shots.cpp: треугольники с цветами вершин и текстурой атласа).
//
// Capture — в потоке интерфейса и не посреди кадра основного контекста (из Tick): переключает текущие контексты
// ImGui/ImPlot и глобальные палитру, масштаб и шрифты, потом возвращает их. Rasterize — в любом потоке (кадр —
// самостоятельная копия: вершины, индексы, атлас).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#include <imgui.h>
#include <implot.h>

#include "Fonts.hpp"
#include "Theme.hpp"

namespace ui::offscreen
{

struct Frame
{
    int w = 0, h = 0;          // пикселей
    ImVec4 bg{1, 1, 1, 1};
    std::vector<ImDrawVert> vtx;
    std::vector<std::uint32_t> idx;
    struct Cmd
    {
        ImVec4 clip;
        std::uint32_t idxOffset = 0, elemCount = 0;
    };
    std::vector<Cmd> cmds;
    std::vector<unsigned char> tex; // атлас RGBA
    int texW = 0, texH = 0;
};

// Кадр размером w×h логических точек при масштабе scale (картинка — w·scale × h·scale пикселей). draw рисует
// содержимое во всё окно; аргументы — доступная ширина и высота.
inline std::shared_ptr<Frame> Capture(int w, int h, float scale, const std::function<void(float, float)>& draw)
{
    // Сохранить состояние программы
    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ImPlotContext* prevPlot = ImPlot::GetCurrentContext();
    const Palette savedPal = pal;
    const bool savedDark = darkTheme;
    const float savedScale = uiScale;
    ImFont* const savedFonts[7] = {fontBody, fontSmall, fontBold, fontH2, fontBig, fontHuge, fontMono};

    auto frame = std::make_shared<Frame>();
    {
        ImFontAtlas atlas;
        ImGuiContext* ctx = ImGui::CreateContext(&atlas);
        ImGui::SetCurrentContext(ctx);
        ImPlotContext* plotCtx = ImPlot::CreateContext();
        ImPlot::SetCurrentContext(plotCtx);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = ImVec2(std::round(w * scale), std::round(h * scale));
        io.DeltaTime = 1.f / 60.f;
        ApplyTheme(false, scale);
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = pal.card;
        ImPlot::StyleColorsLight();

        // Шрифты как на экране (Fonts.hpp), в масштабе картинки
        const char* env = std::getenv("KRENOMER_EMBEDDED_FONTS");
        const bool sys = !(env && env[0] == '1');
        fontBody = AddUiFont(io, sys ? "segoeui.ttf" : nullptr, KRENOMER_FONT_REGULAR, (sys ? 17.f : 16.f) * scale,
                             TextRanges());
        if (!fontBody)
            fontBody = atlas.AddFontDefault();
        fontSmall = AddUiFont(io, sys ? "segoeui.ttf" : nullptr, KRENOMER_FONT_REGULAR, (sys ? 15.f : 13.5f) * scale,
                              TextRanges());
        fontBold = AddUiFont(io, sys ? "seguisb.ttf" : nullptr, KRENOMER_FONT_BOLD, (sys ? 17.f : 16.f) * scale,
                             TextRanges());
        fontH2 = AddUiFont(io, sys ? "seguisb.ttf" : nullptr, KRENOMER_FONT_BOLD, (sys ? 20.5f : 19.f) * scale,
                           TextRanges());
        for (ImFont** f : {&fontSmall, &fontBold, &fontH2})
            if (!*f)
                *f = fontBody;
        fontBig = fontHuge = fontH2;
        fontMono = fontBody;
        io.FontDefault = fontBody;
        unsigned char* px = nullptr;
        atlas.GetTexDataAsRGBA32(&px, &frame->texW, &frame->texH);
        atlas.SetTexID(static_cast<ImTextureID>(1));
        if (px)
            frame->tex.assign(px, px + static_cast<std::size_t>(frame->texW) * frame->texH * 4);

        // Два кадра: раскладка (размеры текста, легенды) устанавливается с первого
        for (int pass = 0; pass < 2; pass++)
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(14)));
            ImGui::Begin("##offscreen", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
            ImGui::PopStyleVar();
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            draw(avail.x, avail.y);
            ImGui::End();
            ImGui::Render();
        }

        // Копия ImDrawData: вершины подряд, индексы — сразу с поправкой на начало списка
        const ImDrawData* dd = ImGui::GetDrawData();
        frame->w = static_cast<int>(io.DisplaySize.x);
        frame->h = static_cast<int>(io.DisplaySize.y);
        frame->bg = pal.card;
        for (const ImDrawList* dl : dd->CmdLists)
        {
            const auto base = static_cast<std::uint32_t>(frame->vtx.size());
            frame->vtx.insert(frame->vtx.end(), dl->VtxBuffer.begin(), dl->VtxBuffer.end());
            for (const ImDrawCmd& cmd : dl->CmdBuffer)
            {
                if (cmd.UserCallback)
                    continue;
                Frame::Cmd c;
                c.clip = cmd.ClipRect;
                c.idxOffset = static_cast<std::uint32_t>(frame->idx.size());
                c.elemCount = cmd.ElemCount;
                for (unsigned int k = 0; k < cmd.ElemCount; k++)
                    frame->idx.push_back(static_cast<std::uint32_t>(dl->IdxBuffer[static_cast<int>(cmd.IdxOffset + k)]) +
                                         cmd.VtxOffset + base);
                frame->cmds.push_back(c);
            }
        }
        ImPlot::DestroyContext(plotCtx);
        ImGui::DestroyContext(ctx);
    }

    // Вернуть состояние программы
    ImGui::SetCurrentContext(prevCtx);
    ImPlot::SetCurrentContext(prevPlot);
    pal = savedPal;
    darkTheme = savedDark;
    uiScale = savedScale;
    fontBody = savedFonts[0];
    fontSmall = savedFonts[1];
    fontBold = savedFonts[2];
    fontH2 = savedFonts[3];
    fontBig = savedFonts[4];
    fontHuge = savedFonts[5];
    fontMono = savedFonts[6];
    return frame;
}

// Программная растеризация кадра -> RGB (w·h·3, сверху вниз). (Shot() из ShagomerPCModule/tests/ui_smoke.)
inline std::vector<unsigned char> Rasterize(const Frame& f)
{
    const int W = f.w, H = f.h;
    if (W <= 0 || H <= 0 || f.tex.empty())
        return {};
    std::vector<float> img(static_cast<std::size_t>(W) * H * 3);
    for (std::size_t i = 0; i < img.size(); i += 3)
    {
        img[i] = f.bg.x;
        img[i + 1] = f.bg.y;
        img[i + 2] = f.bg.z;
    }
    for (const Frame::Cmd& cmd : f.cmds)
    {
        const int cx0 = std::max(0, static_cast<int>(cmd.clip.x)), cy0 = std::max(0, static_cast<int>(cmd.clip.y));
        const int cx1 = std::min(W, static_cast<int>(std::ceil(cmd.clip.z)));
        const int cy1 = std::min(H, static_cast<int>(std::ceil(cmd.clip.w)));
        for (std::uint32_t k = 0; k + 2 < cmd.elemCount; k += 3)
        {
            const ImDrawVert* v[3];
            bool bad = false;
            for (int j = 0; j < 3; j++)
            {
                const std::uint32_t i = f.idx[cmd.idxOffset + k + static_cast<std::uint32_t>(j)];
                if (i >= f.vtx.size())
                {
                    bad = true;
                    break;
                }
                v[j] = &f.vtx[i];
            }
            if (bad)
                continue;
            // В double и с точкой выборки чуть правее и ниже центра пикселя (по x и y — на разное, чтобы и диагональ
            // прямоугольника из двух треугольников не проходила через неё): края линий ImGui и границы текселей атласа
            // часто лежат ровно на центрах пикселей — во float такие пиксели то попадают, то нет
            const double x0 = v[0]->pos.x, y0 = v[0]->pos.y, x1 = v[1]->pos.x, y1 = v[1]->pos.y, x2 = v[2]->pos.x,
                         y2 = v[2]->pos.y;
            const double area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
            if (std::abs(area) < 1e-9)
                continue;
            const int bx0 = std::max(cx0, static_cast<int>(std::floor(std::min({x0, x1, x2}))));
            const int by0 = std::max(cy0, static_cast<int>(std::floor(std::min({y0, y1, y2}))));
            const int bx1 = std::min(cx1, static_cast<int>(std::ceil(std::max({x0, x1, x2}))));
            const int by1 = std::min(cy1, static_cast<int>(std::ceil(std::max({y0, y1, y2}))));
            for (int py = by0; py < by1; py++)
                for (int px = bx0; px < bx1; px++)
                {
                    const double sx = px + 0.5 + 1.0 / 509, sy = py + 0.5 + 1.0 / 1021;
                    const double w0 = ((x1 - sx) * (y2 - sy) - (x2 - sx) * (y1 - sy)) / area;
                    const double w1 = ((x2 - sx) * (y0 - sy) - (x0 - sx) * (y2 - sy)) / area;
                    const double w2 = 1.0 - w0 - w1;
                    if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0)
                        continue;
                    const double u = w0 * v[0]->uv.x + w1 * v[1]->uv.x + w2 * v[2]->uv.x;
                    const double t = w0 * v[0]->uv.y + w1 * v[1]->uv.y + w2 * v[2]->uv.y;
                    const int tx = std::clamp(static_cast<int>(u * f.texW + 1e-3), 0, f.texW - 1);
                    const int ty = std::clamp(static_cast<int>(t * f.texH + 1e-3), 0, f.texH - 1);
                    const unsigned char* texel = f.tex.data() + (static_cast<std::size_t>(ty) * f.texW + tx) * 4;
                    float c[4];
                    for (int ch = 0; ch < 4; ch++)
                    {
                        const float a = static_cast<float>((v[0]->col >> (ch * 8)) & 0xFF);
                        const float b = static_cast<float>((v[1]->col >> (ch * 8)) & 0xFF);
                        const float d = static_cast<float>((v[2]->col >> (ch * 8)) & 0xFF);
                        c[ch] = static_cast<float>((w0 * a + w1 * b + w2 * d) / 255.0) * (texel[ch] / 255.f);
                    }
                    float* dst = &img[(static_cast<std::size_t>(py) * W + px) * 3];
                    for (int ch = 0; ch < 3; ch++)
                        dst[ch] = c[ch] * c[3] + dst[ch] * (1.f - c[3]);
                }
        }
    }
    std::vector<unsigned char> rgb(img.size());
    for (std::size_t i = 0; i < img.size(); i++)
        rgb[i] = static_cast<unsigned char>(std::clamp(img[i], 0.f, 1.f) * 255.f + 0.5f);
    return rgb;
}

} // namespace ui::offscreen
