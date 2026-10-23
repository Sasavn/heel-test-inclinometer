# Project override: all consumers use one generated ff.h/ffconf.h ABI.
# Upstream SDK files remain untouched; source pins fail closed on an SDK change.
idf_build_get_property(target IDF_TARGET)
idf_build_get_property(python PYTHON)
file(TO_CMAKE_PATH "$ENV{IDF_PATH}/components/fatfs" upstream)
idf_build_get_property(build_dir BUILD_DIR)
set(generated "${build_dir}/tab5-fatfs-generated")
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
execute_process(COMMAND "${python}" "${CMAKE_CURRENT_LIST_DIR}/prepare.py"
                --idf-fatfs "${upstream}" --output "${generated}"
                RESULT_VARIABLE prepare_result ERROR_VARIABLE prepare_error)
if(NOT prepare_result EQUAL 0)
    message(FATAL_ERROR "Pinned FatFS overlay failed: ${prepare_error}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/prepare.py" "${CMAKE_CURRENT_LIST_DIR}/upstream.json"
    "${upstream}/src/ff.c" "${upstream}/src/ff.h" "${upstream}/src/ffconf.h"
    "${upstream}/src/ffunicode.c" "${upstream}/src/diskio.h"
    "${upstream}/CMakeLists.txt" "${upstream}/Kconfig")
endif()
set(srcs "${upstream}/diskio/diskio.c" "${upstream}/diskio/diskio_rawflash.c"
    "${upstream}/diskio/diskio_wl.c" "${generated}/ff.c" "${generated}/ffunicode.c"
    "config_guard.c")
set(include_dirs "${generated}" "${upstream}/diskio")
set(requires "wear_levelling")
if(${target} STREQUAL "linux")
    list(APPEND srcs "${upstream}/port/linux/ffsystem.c")
else()
    list(APPEND srcs "${upstream}/port/freertos/ffsystem.c"
        "${upstream}/diskio/diskio_sdmmc.c" "${upstream}/vfs/vfs_fat.c"
        "${upstream}/vfs/vfs_fat_sdmmc.c" "${upstream}/vfs/vfs_fat_spiflash.c")
    list(APPEND include_dirs "${upstream}/vfs")
    list(APPEND requires "sdmmc" "esp_driver_sdmmc" "esp_driver_sdspi")
    list(APPEND priv_requires "vfs" "esp_driver_gpio")
endif()
idf_component_register(SRCS ${srcs} INCLUDE_DIRS ${include_dirs}
    REQUIRES ${requires} PRIV_REQUIRES ${priv_requires})
