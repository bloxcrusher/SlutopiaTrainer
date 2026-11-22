# Codex project instructions

Project: SlutopiaTrainer (Windows / C++20 / CMake).

Current state: v0.1 process-inspection bootstrap, not a functional trainer.
Do not claim that any cheat, unlock, currency edit, or timer edit works until it has
been tested on local game state and confirmed.

## Goals
- Build a maintainable external desktop tool with a polished Dear ImGui + DX11 UI.
- Identify the actual game executable, engine, architecture, and local game-state
  layout from files the project owner supplies; do not assume Unity or invent offsets.
- Prefer out-of-process diagnostics. Keep offsets/version signatures in one
  documented configuration file once verified.
- Keep game-dependent features disabled unless the required evidence exists.
- Support local offline-state inspection and reversible modifications where possible.
- Do not attempt in-app-purchase bypass, server-side currency changes,
  account authentication bypass, or anti-cheat evasion.
- Add explicit errors and diagnostics. No simulated successes or placeholder
  feature implementations presented as working code.

## Engineering
- C++20; CMake 3.20+; Windows 10/11; x64 MSVC build.
- Separate process inspection, version detection, game adapters, feature logic,
  persistence, and UI.
- RAII for Windows handles, least-privilege process access, check all errors.
- Include working build/test commands and update README for changes.
- Use .github/workflows/windows-build.yml as a compile smoke test.
- Do not commit game files, saves, tokens, or private information.

## Next validated steps
1. Obtain the exact executable filename and engine evidence from the user's
   installed game directory. Record game version and architecture.
2. Build the UI shell with process status and diagnostics.
3. Implement only locally verified features, one at a time.
