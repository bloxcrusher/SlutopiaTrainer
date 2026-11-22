# SlutopiaTrainer

External Windows C++20 application with Dear ImGui, DirectX 11 and Win32.
**Diagnostic foundation, not yet a working game trainer.** No game values, offsets,
save schema or local/server ownership boundaries have been confirmed. Game features
remain visibly disabled; no simulated values or successful edits are shown.

## Implemented

- Dark rounded interface titled **SLUTOPIA TRAINER**, with RESOURCES, PROGRESSION,
  MISC and SETTINGS tabs, animated switches and reduced-motion setting.
- Exact executable-name selection (case-insensitive), PID, image path, architecture
  via IsWow64Process2, loaded module names/paths/base/size and engine module evidence.
- Background polling every 750 ms, automatic reconnection, zero-timeout exit checks,
  cache invalidation on exit, manual disconnect and refresh. Module list is a snapshot
  at connection; Refresh obtains a new snapshot.
- Query-only process connection using RAII handles; no injection, hooks or memory writes.
  A separate bounded read-only MemoryInspector is reserved for validated adapters.
- JSON configuration and rotating diagnostics under `%LOCALAPPDATA%\SlutopiaTrainer`.
  Missing config uses defaults; corrupt/oversized/unsupported config reports an error.
  Saves replace the previous file atomically. Logs keep 200 on-screen entries and
  rotate the file at approximately 1 MiB (one `.old` file).
- Focused-window hotkeys: F5 refresh; F6 connect/disconnect. No global hooks.
  Settings changes are persisted with **Save settings**; saving applies auto-connect.
  Manual Connect starts monitoring/reconnecting until Disconnect or another settings save.
- Existing `--list` and `--process <name.exe>` command-line modes retained.

## Build and run on Windows

Requires Windows 10 version 1511+ / Windows 11, CMake 3.20+, Git, Visual Studio 2022
with Desktop development with C++, and internet for the pinned ImGui and JSON dependencies.
The trainer target is Windows-only; portable tests also build on Linux.

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure --timeout 30
.\build\Release\SlutopiaTrainer.exe
.\build\Release\SlutopiaTrainer.exe --list
.\build\Release\SlutopiaTrainer.exe --process 'ActualName.exe'
```

No target executable is guessed. In SETTINGS enter the filename observed in Task
Manager, then Connect. A process-name match is **not** a verified game identity.
Access denied, architecture restrictions, missing modules and process exits are reported;
there is no automatic elevation. Logs and process paths can contain private information.

The executable retains a console entry point for diagnostics and hides its own console
when opening the GUI. DX11 falls back to WARP if hardware initialization fails.
Closing the window stops and joins the worker without waiting for the game to close.

## Components

| Component | Responsibility |
| --- | --- |
| `ProcessInspector` | Existing discovery foundation, extended with errors, architecture and modules |
| `ProcessManager` | Worker, RAII process session, status snapshots, reconnect and exit handling |
| `GameDetector` | Evidence from loaded modules; does not assert a game build |
| `MemoryInspector` | Checked, bounded read-only primitive; no game reads wired up |
| `TrainerManager` | Coordinates configuration, connection and diagnostics |
| `FeatureManager` | Explicit unsupported feature descriptors and reasons; no execution adapter |
| `ConfigManager` | Validated versioned JSON and atomic replacement |
| `Logger` | Thread-safe bounded history and rotating file |
| `UserInterface` | Win32 window, ImGui/DX11 rendering, tabs, switches and focused hotkeys |

## Verification

[Windows Actions](https://github.com/bloxcrusher/SlutopiaTrainer/actions/workflows/windows-build.yml)
configures and builds MSVC x64, runs CTest, enumerates processes and uploads the executable
and test logs. Tests cover config validation/overwrite, disabled feature descriptors,
bounded logs, real process discovery/modules/architecture, safe memory-read rejection,
child process connection/exit/cache clearing and a 60-frame GUI run across all four tabs with a nonexistent
game target. Each Windows test has a 30-second timeout.

Linux verification (portable components only):

```sh
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

A green CI smoke test does not establish game compatibility or verify visual quality on
all DPI/display configurations. No installed-game test has been possible in this workspace.

## Game research and next data

See [docs/GAME_RESEARCH.md](docs/GAME_RESEARCH.md) for sources, uncertainty, the exact
file/metadata list and the procedure to identify the actual progress storage.
[tools/Collect-Diagnostics.ps1](tools/Collect-Diagnostics.ps1) collects install file
metadata, executable PE architecture/version and hashes, with optional observed save
metadata. It neither copies game/save contents nor uploads anything.

Public evidence points to Unity/Mono and `Slutopia Demo.exe` for **the demo** (4612700).
The full game (4612690), its architecture, actual executable, save layout and server
boundaries require installed-build evidence. Demo information is not applied to the
full game as a verified profile.

Unsupported: local resource/counter display or edits, timer edits, offline process speed
and progress edits. Each has a visible reason in its tab. No in-app purchase bypass,
paid unlocks, server validation/authentication bypass or anti-cheat evasion is implemented.
Do not commit game binaries, saves, diagnostic reports, account data or secrets.
