cmake_minimum_required(VERSION 3.20)
project(SlutopiaTrainer VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "SlutopiaTrainer is a Windows-only application")
endif()

add_executable(SlutopiaTrainer
    src/main.cpp
    src/core/ProcessInspector.cpp
    src/core/ProcessInspector.hpp
)

target_compile_features(SlutopiaTrainer PRIVATE cxx_std_20)
target_compile_definitions(SlutopiaTrainer PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    target_compile_options(SlutopiaTrainer PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(SlutopiaTrainer PRIVATE -Wall -Wextra -Wpedantic)
endif()
