#include "core/ProcessInspector.hpp"

#include <Windows.h>

#include <iostream>
#include <string>
#include <string_view>

int wmain(int argc, wchar_t* argv[]) {
    std::wcout << L"SlutopiaTrainer - process inspection bootstrap v0.1\n";
    std::wcout << L"Read-only diagnostics; no game values are modified.\n\n";

    if (argc == 2 && std::wstring_view(argv[1]) == L"--list") {
        const auto processes = trainer::EnumerateProcesses();
        if (processes.empty()) {
            std::wcerr << L"Unable to enumerate processes or none were found.\n";
            return 1;
        }
        for (const auto& entry : processes)
            std::wcout << entry.pid << L"\t" << entry.executable << L'\n';
        return 0;
    }

    if (argc == 3 && std::wstring_view(argv[1]) == L"--process") {
        const auto match = trainer::FindProcess(argv[2]);
        if (!match) {
            std::wcerr << L"Process not found: " << argv[2] << L'\n';
            return 2;
        }

        std::wcout << L"Process: " << match->executable << L"\n"
                   << L"PID: " << match->pid << L"\n";
        const std::wstring image = trainer::QueryImagePath(match->pid);
        if (image.empty()) {
            std::wcerr << L"Executable path unavailable (access denied or process exited).\n";
            return 3;
        }
        std::wcout << L"Image: " << image << L'\n';
        return 0;
    }

    std::wcout << L"Usage:\n"
               << L"  SlutopiaTrainer.exe --list\n"
               << L"  SlutopiaTrainer.exe --process ExactGameExecutable.exe\n";
    return argc == 1 ? 0 : 64;
}
