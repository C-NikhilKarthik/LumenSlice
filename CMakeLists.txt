cmake_minimum_required(VERSION 3.24)
project(LumenSlice LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC reports __cplusplus as 199711L regardless of /std unless this flag is set.
# DCMTK's osconfig.h keys its C++11 feature check off __cplusplus, so without it
# the headers wrongly conclude the compiler lacks C++11 and #error out.
if(MSVC)
    add_compile_options(/Zc:__cplusplus)
endif()

# The Windows front-end deliberately links the same bridge/core sources used by
# the Swift app. DCMTK is supplied by vcpkg in CI (and can also be installed
# system-wide by setting DCMTK_ROOT).
file(GLOB_RECURSE LUMEN_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(LumenCore STATIC ${LUMEN_CORE_SOURCES})
target_include_directories(LumenCore PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bridge/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

find_path(DCMTK_INCLUDE_DIR dcmtk/dcmdata/dctk.h
    HINTS "${DCMTK_ROOT}/include")
find_library(DCMTK_DCMDATA dcmdata HINTS "${DCMTK_ROOT}/lib")
find_library(DCMTK_DCMIMGLE dcmimgle HINTS "${DCMTK_ROOT}/lib")
find_library(DCMTK_DCMIMAGE dcmimage HINTS "${DCMTK_ROOT}/lib")
find_library(DCMTK_DCMJPEG dcmjpeg HINTS "${DCMTK_ROOT}/lib")
find_library(DCMTK_DCMJPLS dcmjpls HINTS "${DCMTK_ROOT}/lib")
find_library(DCMTK_OFSTD ofstd HINTS "${DCMTK_ROOT}/lib")
find_library(DCMTK_OFLOG oflog HINTS "${DCMTK_ROOT}/lib")
find_package(ZLIB REQUIRED)
if(NOT DCMTK_INCLUDE_DIR OR NOT DCMTK_DCMDATA OR NOT DCMTK_OFSTD OR NOT DCMTK_OFLOG)
    message(FATAL_ERROR "DCMTK was not found. Install it with vcpkg or set DCMTK_ROOT.")
endif()
target_include_directories(LumenCore PUBLIC "${DCMTK_INCLUDE_DIR}")
target_link_libraries(LumenCore PUBLIC ${DCMTK_DCMJPLS} ${DCMTK_DCMJPEG}
    ${DCMTK_DCMIMAGE} ${DCMTK_DCMIMGLE} ${DCMTK_DCMDATA} ${DCMTK_OFLOG}
    ${DCMTK_OFSTD} ZLIB::ZLIB)
if(WIN32)
    target_link_libraries(LumenCore PUBLIC ws2_32)
endif()

if(WIN32)
    # Windows front-end: a Qt 6 Widgets GUI (gui/win) over the shared C++ core
    # (LumenCore), at feature parity with the macOS SwiftUI app. It replaces the
    # earlier minimal Win32 shell (windows/LumenSliceWin.cpp), which is kept in the
    # tree for reference but no longer built.
    #
    # Qt is consumed as PREBUILT binaries (aqtinstall locally, install-qt-action in
    # CI) rather than from vcpkg — building qtbase from source is impractical. Point
    # CMake at the Qt install with, e.g.:
    #   -DCMAKE_PREFIX_PATH="C:/Qt/6.8.1/msvc2022_64"
    set(CMAKE_AUTOMOC ON)
    find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Concurrent)

    add_executable(LumenSlice WIN32
        gui/win/main.cpp
        gui/win/MainWindow.cpp
        gui/win/SliceView.cpp
        gui/win/MeshView.cpp)
    target_link_libraries(LumenSlice PRIVATE
        LumenCore
        Qt6::Widgets Qt6::OpenGLWidgets Qt6::Concurrent
        comctl32 shell32 ole32)
    target_compile_definitions(LumenSlice PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

    # Keep the DICOM dictionary beside the executable in packaged builds.
    add_custom_command(TARGET LumenSlice POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LumenSlice>/resources")
endif()
