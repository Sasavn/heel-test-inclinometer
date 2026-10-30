cmake_minimum_required(VERSION 3.24)

# Статическая линковка MSVC CRT — один .exe без DLL рядом (как ShagomerPCModule).
# Должно быть задано до project(). Активируется со статическим триплетом vcpkg
# (x64-windows-static и наш x64-windows-static-v143 из triplets/).
if(WIN32 AND VCPKG_TARGET_TRIPLET MATCHES "-static(-|$)")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(Krenomer VERSION 1.8.5 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Исходники — UTF-8 без BOM; без /utf-8 MSVC читал бы их в кодовой странице сборочной машины.
if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/utf-8>" "$<$<COMPILE_LANGUAGE:CXX>:/W4>"
                        "$<$<COMPILE_LANGUAGE:CXX>:/wd4100>" "$<$<COMPILE_LANGUAGE:CXX>:/wd4505>")
endif()

option(KRENOMER_BUILD_TESTS "Юнит-тесты протокола и связи, стенд снимков экрана (tests/)" OFF)
option(KRENOMER_WIN7 "Windows: .exe должен запускаться на Windows 7 (тулсет v143, проверка импортов)" ON)

# ПК на кафедре — Windows 7. Статическая STL MSVC 14.50+ (тулсет v145, VS 18) импортирует функции Windows 8+ —
# такой .exe на Windows 7 не стартует. Нужен тулсет v143 (MSVC 14.3x/14.4x): VsDevCmd.bat -arch=x64
# -vcvars_ver=14.44 (build-win7.cmd, пресет default).
if(WIN32 AND MSVC AND KRENOMER_WIN7 AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.50)
    message(FATAL_ERROR
        "Компилятор MSVC ${CMAKE_CXX_COMPILER_VERSION} (тулсет v145+) даёт .exe, который не запускается на Windows 7. "
        "Соберите из VsDevCmd.bat -arch=x64 -vcvars_ver=14.44 (build-win7.cmd) или -DKRENOMER_WIN7=OFF.")
endif()

# Зависимости — vcpkg в манифестном режиме (vcpkg.json рядом), cmake --preset default.
find_package(imgui CONFIG REQUIRED)   # фичи freetype, glfw-binding, opengl2/3-binding
find_package(implot CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(CSerialPort CONFIG REQUIRED)

set(KRENOMER_WIN_DEFS NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601 WINVER=0x0601 _CRT_SECURE_NO_WARNINGS)

# Ресурсы: шрифты DejaVu (RCDATA), значок и сведения о версии.
set(KRENOMER_FONT_REGULAR "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/DejaVuSans.ttf")
set(KRENOMER_FONT_BOLD "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/DejaVuSans-Bold.ttf")
set(KRENOMER_FONT_MONO "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/DejaVuSansMono.ttf")
set(KRENOMER_ICON "${CMAKE_CURRENT_SOURCE_DIR}/src/app.ico")
set(KRENOMER_VERSION "${PROJECT_VERSION}")
string(REPLACE "." "," KRENOMER_VERSION_COMMA "${PROJECT_VERSION}.0")
set(KRENOMER_VERSION_DOTS "${PROJECT_VERSION}.0") # 1.4.0.0 — в сведениях о файле и в манифесте
configure_file(src/fonts.rc.in "${CMAKE_CURRENT_BINARY_DIR}/fonts.rc" @ONLY)
configure_file(src/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
configure_file(src/app.manifest.in "${CMAKE_CURRENT_BINARY_DIR}/krenomer.manifest" @ONLY)

# Ядро без интерфейса: связь, протокол, разбор (тесты линкуются с ним).
add_library(krenomer_core STATIC src/comm/DeviceLink.cpp)
target_include_directories(krenomer_core PUBLIC src)
if(WIN32)
    target_compile_definitions(krenomer_core PUBLIC ${KRENOMER_WIN_DEFS})
endif()

# Интерфейс (страницы): программа и стенд снимков.
add_library(krenomer_ui STATIC
    src/ui/App.cpp
    src/ui/AppMeasure.cpp
    src/ui/AppRecord.cpp
    src/ui/AppDiag.cpp
    src/ui/AppSettings.cpp
    src/ui/AppFiles.cpp
    src/ui/AppProcess.cpp
    src/ui/AppRules.cpp
    src/ui/AppProcDash.cpp
    src/ui/AppProject.cpp
    src/ui/AppSteps.cpp
    src/ui/AppHelp.cpp
    src/ui/AppFirmware.cpp
    src/ui/AppTerminal.cpp)
target_include_directories(krenomer_ui PUBLIC src ${CSerialPort_INCLUDE_DIR})
target_compile_definitions(krenomer_ui PRIVATE KRENOMER_PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(krenomer_ui PUBLIC krenomer_core imgui::imgui implot::implot ${CSerialPort_LIBRARY})
if(WIN32)
    target_link_libraries(krenomer_ui PUBLIC ws2_32 bcrypt setupapi ole32 shell32 comdlg32 uuid advapi32)
endif()

add_executable(krenomer
    src/main.cpp
    src/ui/Renderer.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/fonts.rc"
    "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
if(MSVC)
    # Манифест: asInvoker, supportedOS Windows 7–11, DPI (src/app.manifest.in) — вместо UAC-фрагмента компоновщика
    target_sources(krenomer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/krenomer.manifest")
    target_link_options(krenomer PRIVATE "/MANIFESTUAC:NO")
endif()
target_link_libraries(krenomer PRIVATE krenomer_ui glfw OpenGL::GL)
set_target_properties(krenomer PROPERTIES OUTPUT_NAME krenomer-esp32)

if(WIN32)
    # GUI-приложение без консоли, точка входа остаётся main().
    set_target_properties(krenomer PROPERTIES WIN32_EXECUTABLE ON)
    if(MSVC)
        target_link_options(krenomer PRIVATE "/ENTRY:mainCRTStartup")
    endif()

    # После сборки: в таблице импорта не должно быть функций Windows 8+ (иначе на Windows 7 программа не стартует).
    # Нарушение — ошибка сборки, .exe удаляется. (cmake/CheckWin7Imports.cmake — из ShagomerPCModule.)
    if(MSVC AND KRENOMER_WIN7)
        get_filename_component(_krenomer_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        find_program(KRENOMER_DUMPBIN dumpbin HINTS "${_krenomer_cl_dir}" NO_DEFAULT_PATH)
        find_program(KRENOMER_DUMPBIN dumpbin)
        if(NOT KRENOMER_DUMPBIN)
            message(FATAL_ERROR "Не найден dumpbin.exe для проверки импортов (Windows 7)")
        endif()
        add_custom_command(TARGET krenomer POST_BUILD
            COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:krenomer>" "-DDUMPBIN=${KRENOMER_DUMPBIN}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckWin7Imports.cmake"
            COMMENT "Проверка импортов: совместимость с Windows 7"
            VERBATIM)
    endif()
endif()

# Тесты: протокол (разбор потока, JSON status, base64/CRC, файлы), связь с прибором-имитатором, перепрошивка
# (демо dfu-util), обработка опыта кренования (process_test), стенд снимков экрана без окна и видеокарты (ui_shots).
if(KRENOMER_BUILD_TESTS)
    enable_testing()
    foreach(_t clock_sync_test protocol_test link_test firmware_test process_test esp32_test esprom_test recovery_test export_target_test report_grid_test reverse_regression_test plateau_detector_test)
        add_executable(${_t} tests/${_t}.cpp)
        target_include_directories(${_t} PRIVATE src tests)
        target_link_libraries(${_t} PRIVATE krenomer_core)
        target_compile_definitions(${_t} PRIVATE KRENOMER_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
        if(WIN32)
            target_link_libraries(${_t} PRIVATE setupapi bcrypt ws2_32)
        endif()
        if(_t STREQUAL "plateau_detector_test")
            add_test(NAME ${_t} COMMAND ${_t} "${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
        else()
            add_test(NAME ${_t} COMMAND ${_t})
        endif()
    endforeach()
    if(WIN32)
        target_link_libraries(clock_sync_test PRIVATE ws2_32)
    endif()
    add_test(NAME report_grid_full_test COMMAND report_grid_test --full)
    if(WIN32)
        target_link_libraries(export_target_test PRIVATE advapi32)
    endif()

    add_executable(ui_shots tests/ui_shots.cpp "${CMAKE_CURRENT_BINARY_DIR}/fonts.rc")
    target_include_directories(ui_shots PRIVATE src tests)
    target_link_libraries(ui_shots PRIVATE krenomer_ui)
    target_compile_definitions(ui_shots PRIVATE KRENOMER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
                                                KRENOMER_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME ui_shots COMMAND ui_shots --out "${CMAKE_CURRENT_BINARY_DIR}/shots")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shots-work")
    set_tests_properties(ui_shots PROPERTIES ENVIRONMENT "KRENOMER_SHOTS_DIR=${CMAKE_CURRENT_BINARY_DIR}/shots-work")
endif()
