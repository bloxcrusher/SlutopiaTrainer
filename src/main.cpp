#include "core/TrainerManager.hpp"
#include "core/MemoryInspector.hpp"
#include "ui/UserInterface.hpp"
#include <ShlObj.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
namespace {
std::filesystem::path DataDirectory() {
    PWSTR raw = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw);
    if (FAILED(result)) throw std::runtime_error("LocalAppData directory unavailable");
    const std::filesystem::path path(raw); CoTaskMemFree(raw);
    return path / L"SlutopiaTrainer";
}
int SelfTest() {
    using namespace trainer;
    std::string error;
    const auto list = EnumerateProcesses(&error);
    if (!error.empty() || list.empty()) return 10;
    if (QueryImagePath(GetCurrentProcessId()).empty() || QueryArchitecture(GetCurrentProcess()) == "Unknown") return 11;
    if (EnumerateModules(GetCurrentProcessId(), &error).empty() || !error.empty()) return 12;
    const std::uint32_t value = 0x12345678;
    const auto memory = MemoryInspector::Read(GetCurrentProcessId(), reinterpret_cast<std::uintptr_t>(&value), sizeof(value), error);
    if (!memory || memory->size() != sizeof(value) || std::memcmp(memory->data(), &value, sizeof(value)) != 0) return 13;
    if (MemoryInspector::Read(GetCurrentProcessId(), 1, sizeof(value), error)) return 14;
    const auto image = QueryImagePath(GetCurrentProcessId());
    // A uniquely named copy avoids matching the test parent during process discovery.
    const auto dir = std::filesystem::temp_directory_path() / (L"SlutopiaTrainer-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(dir);
    const auto childFile = dir / L"InspectorLifecycleTarget.exe";
    std::filesystem::copy_file(image, childFile, std::filesystem::copy_options::overwrite_existing);
    int result = 0;
    {
        Logger logger(dir / "test.log"); ProcessManager manager(logger);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION info{};
        std::wstring command = L"\"" + childFile.wstring() + L"\" --test-target";
        if (!CreateProcessW(childFile.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)) return 15;
        UniqueHandle child(info.hProcess), thread(info.hThread);
        manager.Configure(childFile.filename().wstring(),true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        while (!manager.Status().connected && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(30));
        if (!manager.Status().connected || manager.Status().process.pid != info.dwProcessId) result = 16;
        if (WaitForSingleObject(child.Get(),8000) != WAIT_OBJECT_0) result = 17;
        const auto exitDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (manager.Status().connected && std::chrono::steady_clock::now() < exitDeadline) std::this_thread::sleep_for(std::chrono::milliseconds(30));
        if (manager.Status().connected || !manager.Status().modules.empty()) result = 18;
        manager.Configure(L"SlutopiaTrainer-missing-9d18d19d.exe",true);
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        if (manager.Status().connected) result = 19;
    }
    std::filesystem::remove_all(dir);
    std::cout << "Process lifecycle and read-only memory checks: " << (result == 0 ? "PASS" : "FAIL") << '\n';
    return result;
}
}
int wmain(int argc, wchar_t* argv[]) {
    using namespace trainer;
    try {
        if (argc == 2 && std::wstring_view(argv[1]) == L"--test-target") { Sleep(6500); return 0; }
        if (argc == 2 && std::wstring_view(argv[1]) == L"--self-test") return SelfTest();
        if (argc == 2 && std::wstring_view(argv[1]) == L"--list") {
            std::string error;
            const auto list = EnumerateProcesses(&error);
            for (const auto& p : list) std::wcout << p.pid << L'\t' << p.executable << L'\n';
            if (!error.empty()) { std::cerr << error << '\n'; return 1; }
            return 0;
        }
        if (argc == 3 && std::wstring_view(argv[1]) == L"--process") {
            std::string error;
            const auto p = FindProcess(argv[2], &error);
            if (!error.empty()) { std::cerr << error << '\n'; return 1; }
            if (!p) { std::wcerr << L"Process not found\n"; return 2; }
            UniqueHandle handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,p->pid));
            if (!handle.Valid()) { std::cerr << WinError("OpenProcess failed") << '\n'; return 3; }
            const auto path = QueryImagePath(handle.Get(), &error);
            const auto arch = QueryArchitecture(handle.Get(), &error);
            const auto modules = EnumerateModules(p->pid, &error);
            std::wcout << L"PID: " << p->pid << L"\nImage: " << path << L'\n';
            std::cout << "Architecture: " << arch << "\nModules: " << modules.size() << '\n';
            if (!error.empty()) { std::cerr << error << '\n'; return 3; }
            return 0;
        }
        const bool smoke = argc == 2 && std::wstring_view(argv[1]) == L"--smoke-gui";
        if (argc != 1 && !smoke) {
            std::cout << "Usage: SlutopiaTrainer.exe [--list | --process Name.exe | --self-test | --smoke-gui]\n"; return 64;
        }
        auto directory = smoke ? std::filesystem::temp_directory_path() / (L"SlutopiaTrainer-smoke-" + std::to_wstring(GetCurrentProcessId())) : DataDirectory();
        int result;
        {
            TrainerManager trainer(directory);
            if (smoke) trainer.processes.Configure(L"SlutopiaTrainer-no-game-9d18d19d.exe",true);
            else {
                DWORD consoleProcesses[2]{};
                if (GetConsoleProcessList(consoleProcesses,2) == 1)
                    if (const auto console = GetConsoleWindow()) ShowWindow(console,SW_HIDE);
            }
            result = RunUserInterface(trainer,smoke);
        }
        if (smoke) std::filesystem::remove_all(directory);
        return result;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (argc == 1) MessageBoxA(nullptr,error.what(),"SLUTOPIA TRAINER - Error",MB_OK | MB_ICONERROR);
        return 1;
    }
}
