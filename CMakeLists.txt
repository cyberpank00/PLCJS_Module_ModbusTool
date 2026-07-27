cmake_minimum_required(VERSION 3.19)

project(module_tool VERSION 0.1.0 LANGUAGES CXX)

# Enable the Windows resource compiler so src/app.rc (exe icon) can be built.
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Emit compile_commands.json so clangd/IDE tooling can resolve Qt include paths.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# qt_add_executable (instead of plain add_executable) so that, when linked
# against a *static* Qt, the required plugins (platform, style, ...) are
# imported automatically and the result is a single self-contained .exe.
qt_add_executable(module_tool
    src/main.cpp

    # Window/title-bar icon (embedded via Qt resource) + Windows exe icon (.rc).
    src/resources.qrc
    $<$<BOOL:${WIN32}>:${CMAKE_CURRENT_SOURCE_DIR}/src/app.rc>

    src/MainWindow.h
    src/MainWindow.cpp

    src/modbus/Crc32.h
    src/modbus/Crc32.cpp
    src/modbus/ModbusTcpClient.h
    src/modbus/ModbusTcpClient.cpp

    src/protocol/BootloaderProtocol.h
    src/protocol/BootloaderProtocol.cpp

    src/maps/ModuleMaps.h
    src/maps/ModuleMaps.cpp

    src/tabs/OnlineTab.h
    src/tabs/OnlineTab.cpp
    src/tabs/SettingsTab.h
    src/tabs/SettingsTab.cpp
    src/tabs/FwUpdateTab.h
    src/tabs/FwUpdateTab.cpp
    src/tabs/FwWorker.h
    src/tabs/FwWorker.cpp
)

target_include_directories(module_tool PRIVATE src)

target_link_libraries(module_tool PRIVATE Qt6::Widgets Qt6::Network)

# Static single-file build: shrink the exe by letting the linker drop unused
# code/data sections and by stripping symbols. Applied to the size-oriented
# configs (Release, MinSizeRel) only for the static Qt build, so the normal
# (shared) developer build is untouched. Use MinSizeRel (-Os) for the smallest
# binary.
#
# NOTE: -flto is intentionally not used — the bundled MinGW GCC 13.1.0 hits an
# internal compiler error (binds_to_current_def_p, symtab.cc:2497) when linking
# this project with LTO enabled.
if(NOT QT6_IS_SHARED_LIBS_BUILD)
    set(_size_cfg "$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>")
    target_compile_options(module_tool PRIVATE
        "$<${_size_cfg}:-ffunction-sections;-fdata-sections>")
    target_link_options(module_tool PRIVATE
        "$<${_size_cfg}:-Wl,--gc-sections;-s>")
endif()

# When building against a static Qt, make sure the native Windows style plugin
# is linked in (qt_add_executable imports the platform plugin automatically, but
# not the style — without it the UI falls back to the plain default style).
if(WIN32 AND NOT QT6_IS_SHARED_LIBS_BUILD)
    if(TARGET Qt6::QModernWindowsStylePlugin)
        qt_import_plugins(module_tool INCLUDE Qt6::QModernWindowsStylePlugin)
    endif()
endif()

# Compress the final static executable with UPX (https://upx.github.io/).
# Shrinks the static build (~20 MB) down to ~6 MB. UPX is free software (GPLv2+)
# whose license explicitly permits compressing commercial executables; the exe
# is self-decompressing (a few ms of extra startup time, no other cost).
# Enabled automatically for the static release build when 'upx' is found on PATH
# (or in C:/Qt/Tools/upx); disable with -DMODULE_TOOL_UPX=OFF.
if(NOT QT6_IS_SHARED_LIBS_BUILD AND CMAKE_BUILD_TYPE MATCHES "^(Release|MinSizeRel)$")
    option(MODULE_TOOL_UPX "Compress the static executable with UPX" ON)
    if(MODULE_TOOL_UPX)
        find_program(UPX_EXECUTABLE NAMES upx HINTS "C:/Qt/Tools/upx")
        if(UPX_EXECUTABLE)
            add_custom_command(TARGET module_tool POST_BUILD
                COMMAND "${UPX_EXECUTABLE}" --best --lzma "$<TARGET_FILE:module_tool>"
                COMMENT "Compressing executable with UPX"
                VERBATIM)
        else()
            message(STATUS "UPX not found; skipping compression (set -DMODULE_TOOL_UPX=OFF to silence)")
        endif()
    endif()
endif()

# On Windows, make it a GUI (no console) app for Release builds.
if(WIN32)
    set_target_properties(module_tool PROPERTIES WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)

    # For a *shared* Qt, deploy the Qt runtime (DLLs + plugins + MinGW runtime)
    # next to the executable so it can be launched standalone. Not needed for a
    # static build, which produces a single self-contained .exe.
    if(QT6_IS_SHARED_LIBS_BUILD)
        get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
        if(WINDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET module_tool POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations
                        "$<TARGET_FILE:module_tool>"
                COMMENT "Deploying Qt runtime with windeployqt"
                VERBATIM)
        endif()
    endif()
endif()
