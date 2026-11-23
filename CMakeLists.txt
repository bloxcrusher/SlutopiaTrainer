cmake_minimum_required(VERSION 3.20)
project(SlutopiaTrainer VERSION 0.2.0 LANGUAGES CXX)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
include(CTest)
include(FetchContent)
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
FetchContent_MakeAvailable(json)
add_library(trainer_common
    src/core/ConfigManager.cpp
    src/core/Logger.cpp
    src/core/FeatureManager.cpp
    src/core/CopyTransaction.cpp
    src/core/NrbfReader.cpp
    src/core/SaveManager.cpp)
target_compile_features(trainer_common PUBLIC cxx_std_20)
target_include_directories(trainer_common PUBLIC src)
target_link_libraries(trainer_common PUBLIC nlohmann_json::nlohmann_json)
if(BUILD_TESTING)
    add_executable(common_tests tests/CommonTests.cpp)
    target_link_libraries(common_tests PRIVATE trainer_common)
    target_compile_options(common_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    add_executable(nrbf_audit_tests tests/NrbfAuditTests.cpp)
    target_link_libraries(nrbf_audit_tests PRIVATE trainer_common)
    add_test(NAME nrbf_audit COMMAND nrbf_audit_tests)
    set_tests_properties(nrbf_audit PROPERTIES TIMEOUT 60)
    add_test(NAME common COMMAND common_tests)
    set_tests_properties(common PROPERTIES TIMEOUT 15)
endif()
if(NOT WIN32)
    message(STATUS "Windows UI unavailable: configuring portable configuration/feature tests only")
    return()
endif()
target_sources(trainer_common PRIVATE src/core/ProcessInspector.cpp src/core/GameDetector.cpp)
target_link_libraries(trainer_common PUBLIC shell32 ole32 version advapi32)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)
add_library(imgui_dx11 STATIC ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui_dx11 PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_dx11 PUBLIC d3d11 dxgi d3dcompiler dwmapi)
add_executable(SlutopiaTrainer
    src/main.cpp
    src/core/ProcessManager.cpp
    src/core/MemoryInspector.cpp
    src/core/TrainerManager.cpp
    src/ui/UserInterface.cpp)
target_link_libraries(SlutopiaTrainer PRIVATE trainer_common imgui_dx11 shell32 ole32 version)
target_compile_definitions(trainer_common PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
if(MSVC)
    target_compile_options(trainer_common PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(SlutopiaTrainer PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()
if(BUILD_TESTING)
    add_executable(windows_integration_tests tests/WindowsIntegrationTests.cpp src/core/TrainerManager.cpp src/core/ProcessManager.cpp)
    target_link_libraries(windows_integration_tests PRIVATE trainer_common)
    target_compile_options(windows_integration_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    add_test(NAME installation_and_trainer COMMAND windows_integration_tests)
    set_tests_properties(installation_and_trainer PROPERTIES TIMEOUT 15)
    add_test(NAME process_lifecycle COMMAND SlutopiaTrainer --self-test)
    add_test(NAME gui_no_game COMMAND SlutopiaTrainer --smoke-gui)
    set_tests_properties(process_lifecycle gui_no_game PROPERTIES TIMEOUT 30)
endif()
