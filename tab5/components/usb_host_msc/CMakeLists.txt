# Keep installed MSC 1.3.0 sources unchanged. Compile its real VFS definition
# with a small accessor, rather than guessing an opaque handle's ABI in main.
set(upstream "${CMAKE_CURRENT_LIST_DIR}/upstream")
if(NOT "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}" VERSION_EQUAL "5.5.2")
    message(FATAL_ERROR "MSC pdrv accessor requires audited ESP-IDF 5.5.2")
endif()
file(READ "${CMAKE_CURRENT_LIST_DIR}/upstream.json" pins)
string(JSON count LENGTH "${pins}" files)
math(EXPR last "${count}-1")
foreach(index RANGE 0 ${last})
    string(JSON name MEMBER "${pins}" files ${index})
    string(JSON expected GET "${pins}" files "${name}")
    if(NOT EXISTS "${upstream}/${name}")
        message(FATAL_ERROR "Pinned MSC source missing: ${upstream}/${name}")
    endif()
    file(SHA256 "${upstream}/${name}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "MSC 1.3.0 source pin mismatch: ${name}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${upstream}/${name}")
endforeach()
idf_component_register(
    SRCS "${upstream}/src/msc_scsi_bot.c" "${upstream}/src/msc_host.c"
         "${upstream}/src/diskio_usb.c" "msc_vfs_owned.c"
    INCLUDE_DIRS "include" "${upstream}/include" "${upstream}/include/usb"
    PRIV_INCLUDE_DIRS "${upstream}/private_include" "${upstream}/include/esp_private"
    REQUIRES fatfs usb PRIV_REQUIRES heap)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TAB5_MSC_VFS_SOURCE="${upstream}/src/msc_host_vfs.c")
