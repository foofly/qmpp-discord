cmake_minimum_required(VERSION 3.16)
project(libdiscordrichpresence)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(Qt6 OPTIONAL_COMPONENTS WebSockets)

# --- QMMP / QMMPUI location ---
# Prefer manually-specified paths (useful for Flatpak builds).
# Fall back to pkg-config when building against system packages.
if(DEFINED QMMP_INCLUDE_DIR AND DEFINED QMMP_LIB_DIR)
    message(STATUS "Using manual QMMP paths: headers=${QMMP_INCLUDE_DIR}  libs=${QMMP_LIB_DIR}")
    set(QMMP_INCLUDE_DIRS "${QMMP_INCLUDE_DIR}")
    set(QMMPUI_INCLUDE_DIRS "${QMMP_INCLUDE_DIR}")
    set(QMMP_LIBRARIES   "${QMMP_LIB_DIR}/libqmmp.so")
    set(QMMPUI_LIBRARIES "${QMMP_LIB_DIR}/libqmmpui.so")
    if(NOT DEFINED PLUGIN_DIR)
        set(PLUGIN_DIR "${QMMP_LIB_DIR}/qmmp-2.3")
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(QMMP   REQUIRED qmmp)
    pkg_check_modules(QMMPUI REQUIRED qmmpui)
    if(NOT DEFINED PLUGIN_DIR)
        execute_process(
            COMMAND pkg-config --variable=plugindir qmmp
            OUTPUT_VARIABLE PLUGIN_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(NOT PLUGIN_DIR)
            set(PLUGIN_DIR "/usr/lib64/qmmp")
        endif()
    endif()
endif()

# The default Application ID is baked into the binary at build time.
# Set this to your own Discord Application ID before publishing.
# cmake -DDISCORD_APP_ID=1234567890123456789 ..
set(DISCORD_APP_ID "1501338231696920737" CACHE STRING "Default Discord Application ID compiled into the plugin")
if(DISCORD_APP_ID STREQUAL "")
    message(WARNING "DISCORD_APP_ID is not set — users will need to enter one in the plugin settings.")
endif()

set(SOURCES
    discordipc.cpp
    discordrichpresence.cpp
    discordrichpresencefactory.cpp
    settingsdialog.cpp
)

add_library(discordrichpresence MODULE ${SOURCES})
target_compile_definitions(discordrichpresence PRIVATE
    DEFAULT_DISCORD_APP_ID="${DISCORD_APP_ID}"
)

target_include_directories(discordrichpresence PRIVATE
    ${QMMP_INCLUDE_DIRS}
    ${QMMPUI_INCLUDE_DIRS}
)

target_link_libraries(discordrichpresence PRIVATE
    Qt6::Widgets
    Qt6::Network
    ${QMMP_LIBRARIES}
    ${QMMPUI_LIBRARIES}
)
if(Qt6WebSockets_FOUND)
    message(STATUS "Building with WebSocket fallback support")
    target_link_libraries(discordrichpresence PRIVATE Qt6::WebSockets)
    target_compile_definitions(discordrichpresence PRIVATE HAVE_WEBSOCKETS)
else()
    message(STATUS "Qt6WebSockets not found — WebSocket fallback disabled (install qt6-qtwebsockets-devel to enable)")
endif()

install(TARGETS discordrichpresence DESTINATION ${PLUGIN_DIR}/General)
