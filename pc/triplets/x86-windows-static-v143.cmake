# x86-windows-static, собранный тулсетом v143 (MSVC 14.44): 32-битный krenomer.exe для 32-битной Windows 7.
#
# Зачем: ПК на кафедре — Windows 7. Статическая STL тулсета v145 (MSVC 14.5x, VS 18)
# импортирует функции Windows 8+ (CreateFile2, GetSystemTimePreciseAsFileTime),
# и .exe на Windows 7 не запускается. v143 ещё поддерживает Windows 7. Зависимости
# должны быть собраны тем же тулсетом, что и программа (иначе не слинкуются).
set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_PLATFORM_TOOLSET v143)
set(VCPKG_PLATFORM_TOOLSET_VERSION 14.44)

# Заголовки Windows SDK — в режиме Windows 7: библиотеки не должны напрямую
# звать API новее Windows 7.
set(VCPKG_C_FLAGS "/D_WIN32_WINNT=0x0601 /DWINVER=0x0601")
set(VCPKG_CXX_FLAGS "/D_WIN32_WINNT=0x0601 /DWINVER=0x0601")
