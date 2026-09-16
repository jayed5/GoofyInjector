cmake_minimum_required(VERSION 3.20)
project(GoofyInjector LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(GoofyInjector WIN32
    src/main.cpp
    src/gui.cpp
    src/injector.cpp
    src/i18n.cpp
    src/settings.cpp
    src/cli.cpp
    src/version.rc
)

target_compile_definitions(GoofyInjector PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

if (MSVC)
    target_compile_options(GoofyInjector PRIVATE /utf-8 /W4)
elseif (MINGW)
    # wmain entry point with MinGW
    target_compile_options(GoofyInjector PRIVATE -municode)
    target_link_options(GoofyInjector PRIVATE -municode)
endif()
