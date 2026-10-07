# Проверка после сборки: .exe должен запускаться на Windows 7.
#
# Загрузчик Windows разрешает все статические импорты ДО main(): одна функция
# Windows 8+ в таблице импорта — и программа на Windows 7 не стартует вовсе
# («Точка входа в процедуру ... не найдена в библиотеке DLL KERNEL32.dll»).
#
# Запуск: cmake -DEXE=<путь к exe> -DDUMPBIN=<путь к dumpbin.exe> -P CheckWin7Imports.cmake
# При нарушении .exe удаляется (чтобы не попал на ПК с Windows 7) и сборка падает.
# (Скрипт взят из ShagomerPCModule без изменений логики.)

if(NOT EXE OR NOT EXISTS "${EXE}")
    message(FATAL_ERROR "CheckWin7Imports: не найден EXE='${EXE}'")
endif()
if(NOT DUMPBIN OR NOT EXISTS "${DUMPBIN}")
    message(FATAL_ERROR "CheckWin7Imports: не найден dumpbin ('${DUMPBIN}')")
endif()

execute_process(
    COMMAND "${DUMPBIN}" /nologo /imports "${EXE}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "CheckWin7Imports: dumpbin завершился с кодом ${rc}: ${err}")
endif()

# Функции, которых нет в Windows 7 SP1 (появились в Windows 8 / 8.1 / 10).
set(win8plus_functions
    # KERNEL32 / KernelBase
    CreateFile2 GetSystemTimePreciseAsFileTime
    WaitOnAddress WakeByAddressSingle WakeByAddressAll
    GetCurrentThreadStackLimits SetThreadInformation GetThreadInformation
    GetProcessInformation SetProcessInformation PrefetchVirtualMemory
    CreateFileMappingFromApp MapViewOfFileFromApp VirtualProtectFromApp OpenFileMappingFromApp
    GetOverlappedResultEx CopyFile2
    InitializeSynchronizationBarrier EnterSynchronizationBarrier DeleteSynchronizationBarrier
    GetFirmwareType GetCurrentPackageId GetCurrentPackageFullName GetCurrentPackageFamilyName
    GetCurrentPackagePath GetPackageFamilyName GetPackageFullName
    SetThreadDescription GetThreadDescription
    GetSystemCpuSetInformation SetThreadSelectedCpuSets GetThreadSelectedCpuSets
    SetProcessDefaultCpuSets GetProcessDefaultCpuSets
    GetProcessMitigationPolicy SetProcessMitigationPolicy
    VirtualAlloc2 MapViewOfFile3 IsWow64Process2 GetMachineTypeAttributes
    GetSystemTimeAdjustmentPrecise QueryInterruptTime QueryInterruptTimePrecise
    QueryUnbiasedInterruptTimePrecise CreatePseudoConsole ResizePseudoConsole ClosePseudoConsole
    AppPolicyGetProcessTerminationMethod AppPolicyGetThreadInitializationType
    AppPolicyGetShowDeveloperDiagnostic AppPolicyGetWindowingModel
    GetIntegratedDisplaySize GetOsSafeBootMode DiscardVirtualMemory OfferVirtualMemory ReclaimVirtualMemory
    # USER32
    GetDpiForWindow GetDpiForSystem GetSystemMetricsForDpi AdjustWindowRectExForDpi
    SystemParametersInfoForDpi EnableNonClientDpiScaling
    SetProcessDpiAwarenessContext SetThreadDpiAwarenessContext GetThreadDpiAwarenessContext
    GetWindowDpiAwarenessContext AreDpiAwarenessContextsEqual GetAwarenessFromDpiAwarenessContext
    IsValidDpiAwarenessContext GetDpiFromDpiAwarenessContext
    EnableMouseInPointer GetPointerType GetPointerInfo GetPointerFrameInfo GetPointerTouchInfo
    GetPointerPenInfo GetPointerDevice GetPointerDevices RegisterPointerDeviceNotifications
    SkipPointerFrameMessages InitializeTouchInjection InjectTouchInput IsImmersiveProcess
    GetDisplayAutoRotationPreferences SetDisplayAutoRotationPreferences
    # SHCORE (Windows 8.1+)
    SetProcessDpiAwareness GetProcessDpiAwareness GetDpiForMonitor GetScaleFactorForMonitor)

# DLL, которых в Windows 7 нет (или которые не должны быть: CRT линкуется статически).
set(forbidden_dll_regex "^(api-ms-win-|ext-ms-|shcore\\.dll$|vcruntime|msvcp|ucrtbase|concrt)")

string(REPLACE "\r" "" out "${out}")
string(REPLACE "\n" ";" lines "${out}")

set(dll "")
set(dlls "")
set(problems "")
set(count 0)
foreach(line IN LISTS lines)
    # Имя DLL: строка из 4 пробелов и имени, оканчивающегося на .dll.
    if(line MATCHES "^    ([A-Za-z0-9_.-]+\\.[dD][lL][lL])$")
        set(dll "${CMAKE_MATCH_1}")
        string(TOLOWER "${dll}" dll_lower)
        list(APPEND dlls "${dll}")
        if(dll_lower MATCHES "${forbidden_dll_regex}")
            list(APPEND problems "DLL ${dll}")
        endif()
    # Функция: "<hint в hex> <имя>".
    elseif(dll AND line MATCHES "^ +[0-9A-Fa-f]+ ([A-Za-z_][A-Za-z0-9_@?$]*)$")
        set(fn "${CMAKE_MATCH_1}")
        math(EXPR count "${count} + 1")
        if(fn IN_LIST win8plus_functions)
            list(APPEND problems "${dll}!${fn}")
        endif()
    endif()
endforeach()

if(count EQUAL 0)
    message(FATAL_ERROR "CheckWin7Imports: не удалось разобрать вывод dumpbin для ${EXE}")
endif()

if(problems)
    file(REMOVE "${EXE}")
    list(JOIN problems "\n  " problem_text)
    message(FATAL_ERROR
        "Windows 7: ${EXE} импортирует то, чего нет в Windows 7:\n  ${problem_text}\n"
        "Соберите тулсетом v143 (VsDevCmd -vcvars_ver=14.44, пресет default / build-win7.cmd). "
        ".exe удалён.")
endif()

list(JOIN dlls ", " dll_text)
message(STATUS "Windows 7: импорты ${EXE} в порядке (${count} функций из: ${dll_text})")
