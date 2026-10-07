// krenomer.exe — программа для ПК к регистратору крена (STM32F411 + BWM427): показания, качка, графики,
// диагностика шины, настройки, файлы с карты и перепрошивка по USB. Ключ --demo — сразу демо-режим.
#include <cstdint>
#include <cstring>
#include <string>

#include "ui/Renderer.hpp"

#ifdef _WIN32
#include <objbase.h>
#include <windows.h>

#include "core/TextUtil.hpp"

namespace
{

// Один экземпляр: COM-порт прибора может открыть только одна программа.
bool AnotherInstanceRunning()
{
    const HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\Krenomer.PC.SingleInstance");
    return mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS;
}

} // namespace
#endif

int main(int argc, char** argv)
{
    bool demo = false;
    for (int i = 1; i < argc; i++)
        if (std::strcmp(argv[i], "--demo") == 0)
            demo = true;
#ifdef _WIN32
    if (AnotherInstanceRunning())
    {
        MessageBoxW(nullptr,
                    text::Widen("Программа уже запущена — переключитесь на её окно.\n\nCOM-порт прибора может "
                                "открыть только одна программа.")
                        .c_str(),
                    text::Widen("Регистратор крена").c_str(), MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    // Окна выбора папки и файла (IFileOpenDialog) — COM в однопоточном апартаменте.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
#endif
    RunUi(demo);
#ifdef _WIN32
    CoUninitialize();
#endif
    return 0;
}
