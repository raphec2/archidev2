# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.16)
project(archidev2-qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)

# Ubuntu's qtermwidget5.pc is unusable (it Requires a libutf8proc.pc the distro
# does not ship), so use the CMake config package instead of pkg-config.
find_package(QTermWidget5 REQUIRED)

# Same packaging bug, second symptom: the imported target propagates the *dev*
# symlink /usr/lib/.../libutf8proc.so, which ships only in libutf8proc-dev.
# libqtermwidget5.so already links libutf8proc.so.3 itself, so nothing of ours
# needs it. Drop any propagated absolute .so path that does not exist rather
# than making the build depend on an extra -dev package.
# (The alternative, if you prefer: sudo apt install libutf8proc-dev)
get_target_property(_qtw_link qtermwidget5 INTERFACE_LINK_LIBRARIES)
if(_qtw_link)
    set(_qtw_kept "")
    foreach(_lib IN LISTS _qtw_link)
        if(_lib MATCHES "^/" AND NOT EXISTS "${_lib}")
            message(STATUS "Dropping missing propagated link path: ${_lib}")
        else()
            list(APPEND _qtw_kept "${_lib}")
        endif()
    endforeach()
    set_target_properties(qtermwidget5 PROPERTIES INTERFACE_LINK_LIBRARIES "${_qtw_kept}")
endif()

add_executable(archidev2-qt main.cpp)
target_link_libraries(archidev2-qt PRIVATE Qt5::Widgets qtermwidget5)
target_compile_options(archidev2-qt PRIVATE -Wall -Wextra -Wpedantic)
