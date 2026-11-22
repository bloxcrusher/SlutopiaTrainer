# Game evidence and next analysis

Research date: 2026-11-23. No Slutopia installation, executable, save, memory dump or
Steam appmanifest is present in this workspace. The repository initially contained
only a generic process inspector. No game-specific address or data layout is confirmed.

| Question | Evidence and conclusion |
| --- | --- |
| Steam identity | Public listings identify Slutopia as app 4612690 and its demo as 4612700. Confirm the installed appmanifest and build ID before selecting an adapter. |
| Engine | [SteamDB demo configuration](https://steamdb.info/app/4612700/config/) reports Unity/Mono. This is evidence for the **demo**, not proof of the installed full game's engine or scripting backend. Runtime detection reports observed UnityPlayer/Mono modules only. |
| Executable | The same demo listing names `Slutopia Demo.exe`. The full game's executable remains unverified. Nothing is preconfigured as a guessed target. |
| x86/x64 | Unverified for the game. The trainer is built x64; it queries a selected process with IsWow64Process2, including x86 and ARM64 reporting. Module enumeration can fail across architectures or permissions and reports that error. |
| Local progress location/format | Unknown. Unity's usual LocalLow/PlayerPrefs locations are investigation leads, not confirmed save paths. |
| Local resources, counters, timers, speed, progress | No data structures, units, ownership or persistence mechanisms verified. No values displayed and no edits implemented. |
| Server-dependent parameters | Unknown. Single-player classification and local files do not prove that a value is locally authoritative. Steam Cloud synchronization is also distinct from server-side gameplay validation. |

The full game's SteamDB configuration could not be retrieved during this research;
the Steam store request reached a sign-in/age-check page. A third-party save guide
surfaced in search, but its generic filenames and paths were not corroborated by
actual files, so none are used in code. Do not infer server boundaries from a trainer
site or from the absence of a network connection alone.

## Required diagnostic data

Start with metadata, not a full game upload:

1. `steamapps/appmanifest_4612690.acf` (or `appmanifest_4612700.acf` for demo):
   app ID, install directory and build ID; redact account information.
2. Recursive relative file listing of the actual install directory, sizes and dates.
   SHA-256 and PE machine of the executable actually launched; its file/product version.
3. Runtime `--process ActualName.exe` output, and the MISC module table (including
   UnityPlayer.dll, mono-2.0-bdwgc.dll or GameAssembly.dll if present).
4. If Unity is observed: file/version/hash evidence for `UnityPlayer.dll`,
   `<actual-exe-stem>_Data/globalgamemanagers`, `_Data/boot.config`;
   list `_Data/Managed/*.dll`, or `_Data/il2cpp_data/Metadata/global-metadata.dat`
   and GameAssembly.dll if present. Only request those binaries for local inspection
   once metadata shows they are needed; do not commit or redistribute them.
5. A redacted Player.log from the location the game actually creates. It may identify
   engine version, company/product names and persistence paths. Review it for tokens,
   usernames and private paths before sharing.
6. Save-file and registry access observations using Microsoft Sysinternals Process Monitor:
   filter **Process Name is the actual executable**; capture CreateFile/WriteFile and
   RegSetValue while making one ordinary local progress change, then close the game.
   Export only relevant rows with paths/times/operations. This establishes candidates;
   it does not establish authority or schema.
7. For the observed save location, keep private backups A/B (game closed before each copy),
   where B differs by one known legitimate action and its displayed before/after values.
   Record whether Steam Cloud was enabled. Provide filenames, hashes, sizes and timestamps
   first. If binary analysis is necessary, use sanitized offline test saves locally.

Run the metadata collector in PowerShell (it copies no game or save contents):

```powershell
powershell -NoProfile -File .\tools\Collect-Diagnostics.ps1 `
  -GameDirectory 'D:\SteamLibrary\steamapps\common\ACTUAL_FOLDER' `
  -OutputPath "$env:TEMP\slutopia-diagnostics.json"
# Optional, only after observing the real save location:
# -SaveDirectory 'C:\actual\observed\save-folder'
```

Also launch the game normally through Steam and run:

```powershell
.\build\Release\SlutopiaTrainer.exe --list
.\build\Release\SlutopiaTrainer.exe --process 'ActualName.exe'
```

The report includes local paths and names: review/redact before sharing. The tool never
uploads a report. Do not supply credentials, authentication tokens, payment data,
complete memory dumps or other players' data.

## Adapter acceptance criteria

A future adapter needs an exact game/build identity and file hashes, documented
local ownership, a reproducible storage schema/signature, type/units/range validation,
process-session identity, failure behavior and a backup/rollback procedure. Every
feature stays disabled until tested against that build. Observed module base addresses
are diagnostics, never resource offsets. The current MemoryInspector is a bounded,
read-only primitive; it is not called for game values. No mutation API is exposed.

In-app purchases, paid unlocks, server validation/authentication bypass and anti-cheat
evasion are outside this project. No server-bound feature will be enabled.
