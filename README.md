# SlutopiaTrainer

Windows C++20 application foundation for exploring the local, offline state of *Slutopia*. **Currently a read-only process inspector, not a working trainer.** No game executable name, memory offsets, or features have been verified.

## What works now

- Windows process enumeration with `--list`.
- Exact executable-name lookup with `--process <name.exe>`.
- Process ID and executable-path inspection using limited query rights.
- CMake-based Windows x64 build; a GitHub Actions Windows build/smoke-test workflow.

## Build

Prerequisites: Windows 10/11, CMake 3.20+, Visual Studio 2022 with the Desktop development with C++ workload.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
.\build\Release\SlutopiaTrainer.exe --list
.\build\Release\SlutopiaTrainer.exe --process Example.exe
```

Replace `Example.exe` with the *actual* executable name shown by Task Manager. Running the executable without arguments prints usage and exits successfully.

## Project layout

- `src/main.cpp`: command-line entry point.
- `src/core/ProcessInspector.hpp` / `.cpp`: read-only Windows process discovery.
- `AGENTS.md`: instructions for Codex work in this repository.
- `.github/workflows/windows-build.yml`: Windows compile + smoke-test CI.

## Next stages

1. Confirm the game executable name, exact version, architecture, engine and offline-state boundaries using the owner's game installation.
2. Create a DX11/ImGui UI with clear connection state and diagnostics.
3. Add only game-dependent functionality that can be validated against the installed build. Keep unknown offsets and unsupported features disabled.

No server balance editing, microtransaction bypass, authentication bypass, or anti-cheat bypass is implemented.

## Automation notes

Use Codex against this repository. Review `AGENTS.md` first, make changes in small commits, and check that GitHub Actions completes successfully before describing the application as build-tested.
