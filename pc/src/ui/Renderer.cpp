// Окно GLFW + OpenGL 3 (или OpenGL 2 на старых встроенных видеокартах и в удалённом рабочем столе), цикл кадров.
// (По образцу Renderer.cpp из ShagomerPCModule.)
#include "Renderer.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl2.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>
#include <GLFW/glfw3.h>

#include "App.hpp"
#include "Fonts.hpp"
#include "Theme.hpp"
#include "../core/TextUtil.hpp"

#ifdef _WIN32
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace
{

std::string lastGlfwError;

void GlfwErrorCallback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
    lastGlfwError = description ? description : "";
}

// Программа без консоли: ошибку старта показываем окном.
void ShowFatalError(const char* utf8Message)
{
    std::string text = utf8Message;
    if (!lastGlfwError.empty())
        text += "\n\nGLFW: " + lastGlfwError;
#ifdef _WIN32
    MessageBoxW(nullptr, text::Widen(text).c_str(), text::Widen("Регистратор крена").c_str(), MB_OK | MB_ICONERROR);
#else
    std::fprintf(stderr, "%s\n", text.c_str());
#endif
}

GLFWwindow* CreateMainWindow(int width, int height, bool legacy, const std::string& title)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    if (!legacy)
    {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    }
    return glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
}

#ifdef _WIN32
// Значок окна из ресурсов .exe (app.rc, ресурс 1)
void SetWindowIcon(GLFWwindow* window)
{
    HWND hwnd = glfwGetWin32Window(window);
    HICON iconBig = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, 0));
    HICON iconSmall = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0));
    if (hwnd && iconBig)
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(iconBig));
    if (hwnd && iconSmall)
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconSmall));
}
#endif

} // namespace

void RunUi(bool startDemo)
{
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit())
    {
        ShowFatalError("Не удалось запустить графическую подсистему (GLFW).");
        return;
    }
    const char* glslVersion = "#version 130";

    int workX = 0, workY = 0, workW = 1280, workH = 800;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
        glfwGetMonitorWorkarea(monitor, &workX, &workY, &workW, &workH);
    float scale = 1.f;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
    {
        float sx = 1.f, sy = 1.f;
        glfwGetMonitorContentScale(monitor, &sx, &sy);
        scale = std::clamp(sx, 1.f, 2.5f);
    }
    // Окно под рабочую область; на экранах до 1366×768 — развернуть.
    const int width = std::min(static_cast<int>(1320 * scale), workW - 20);
    const int height = std::min(static_cast<int>(840 * scale), workH - 50);
    bool maximize = workW <= static_cast<int>(1366 * scale) || workH <= static_cast<int>(800 * scale);

    ui::App app(ui::AppOptions{true, startDemo});

    bool legacyGl = false;
    GLFWwindow* window = CreateMainWindow(width, height, false, app.WindowTitle());
    if (!window)
    {
        legacyGl = true;
        window = CreateMainWindow(width, height, true, app.WindowTitle());
    }
    if (!window)
    {
        ShowFatalError("Не удалось создать окно OpenGL.\nОбновите драйвер видеокарты (нужен OpenGL 1.1 или новее).");
        glfwTerminate();
        return;
    }
    glfwSetWindowSizeLimits(window, static_cast<int>(1000 * scale), static_cast<int>(620 * scale), GLFW_DONT_CARE,
                            GLFW_DONT_CARE);
    glfwSetWindowPos(window, workX + (workW - width) / 2, workY + (workH - height) / 2);
    glfwShowWindow(window);
    if (maximize)
        glfwMaximizeWindow(window);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
#ifdef _WIN32
    SetWindowIcon(window);
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ui::ApplyTheme(app.DarkTheme(), scale);
    if (app.DarkTheme())
        ImPlot::StyleColorsDark();
    else
        ImPlot::StyleColorsLight();
    ui::SetupFonts(io, scale);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    if (!legacyGl && !ImGui_ImplOpenGL3_Init(glslVersion))
    {
        ImGui_ImplOpenGL3_Shutdown();
        legacyGl = true;
    }
    if (legacyGl && !ImGui_ImplOpenGL2_Init())
    {
        ShowFatalError("Не удалось инициализировать отрисовку OpenGL.\nОбновите драйвер видеокарты.");
        ImGui_ImplGlfw_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return;
    }

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        app.Tick();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
        {
            ImGui_ImplGlfw_Sleep(30); // свёрнуто: не рисуем, но скачивание и перепрошивка идут
            continue;
        }
        if (app.ThemeChanged())
        {
            ui::ApplyTheme(app.DarkTheme(), scale);
            if (app.DarkTheme())
                ImPlot::StyleColorsDark();
            else
                ImPlot::StyleColorsLight();
        }
        if (legacyGl)
            ImGui_ImplOpenGL2_NewFrame();
        else
            ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        app.Render();
        ImGui::Render();
        int displayW, displayH;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(ui::pal.bg.x, ui::pal.bg.y, ui::pal.bg.z, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (legacyGl)
            ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        else
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    if (legacyGl)
        ImGui_ImplOpenGL2_Shutdown();
    else
        ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
}
