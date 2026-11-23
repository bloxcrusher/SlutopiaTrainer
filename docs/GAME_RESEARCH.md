# Game evidence and technical findings

Analysis date reported by `4903c5c`: 2026-11-23. The following findings were
reported from Gemini's local installation. This review has no corresponding game
binaries, NRBF fixtures or decompiled evidence, so it cannot independently verify
them. See [ANTIGRAVITY_REVIEW.md](ANTIGRAVITY_REVIEW.md) for defects and current limits.

| Technical Parameter | Reported Value / Evidence (not independently verified) |
| --- | --- |
| Steam App ID | `4612690` (confirmed via `appmanifest_4612690.acf` and Steam shortcut) |
| Steam Build ID | `26839990` |
| Executable | `Slutopia.exe` |
| Architecture | `x64` (IMAGE_FILE_MACHINE_AMD64, 64-bit PE) |
| Engine Version | Unity `6000.0.60f1` (File version: `6000.0.60.6414259`) |
| Scripting Runtime | Mono (`MonoBleedingEdge\EmbedRuntime\mono-2.0-bdwgc.dll`) |
| Install Directory | `C:\Program Files (x86)\Steam\steamapps\common\Slutopia` |
| Local Save Path | `%USERPROFILE%\AppData\LocalLow\MobiStar Games\Slutopia\<PlayerID>` |
| Save Serialization | .NET `BinaryFormatter` (`Saves.SavedData` root object with `List<Data.Memento>`) |

## Reverse engineering breakdown

### 1. Persistence & Save Architecture
Decompilation of `Slutopia_Data\Managed\Assembly-CSharp.dll` reveals:
- Local save files are handled by `Data.FileSerializer`:
  `Application.persistentDataPath + "/" + this.user.PlayerID`
- Saves are serialized as `Saves.SavedData`, containing `UpdatedAt` and a `List<Data.Memento>`.
- Individual systems inherit `ISavableState` and produce corresponding `Memento` instances:
  - `StripClub.Model.Data.SimpleCurrency`: saves instances of `SimpleCurrency.CurrencyMemento`
    with `UniqueKey = "currency_" + currencyType + identificator` (e.g. `currency_Soft[0];`).
  - `_Scripts.PlayerLeveling.PlayerLeveler`: saves `PlayerLevelerMemento` with `rewardedLevel`.
  - `Merge.DynamicOrders.PresetMemento`, `PatternMemento`, `SmilesControllerMemento`.

### 2. Reported Stored Parameters
Gemini reports the following memento keys. Their presence in local storage does not
prove offline authority, safe editability, units, or field offsets:
- `currency_Soft[0];`: Soft Currency (Coins / Cash)
- `currency_Hard[0];`: Hard Currency (Diamonds / Gems)
- `currency_GirlCrystal[0];`: Girl Crystals (character ascension)
- `currency_Star[0];`: Star Currency (StarShop progression)
- `currency_StandardTickets[0];`: Standard Gacha Tickets
- `currency_Contracts[0];`: Contracts
- `currency_BP[0];`: BattlePass Currency
- `currency_Jewel[0];`: Jewels
- `currency_Smiles[0];`: Smiles
- `PlayerLeveler` / `rewardedLevel`: Player Level

### 3. Reported Server Boundaries
Code analysis in `ClientServerCore\Energy` and `Saves\LocalServerSaveMerger` reveals:
- **Energy**: Managed strictly server-side by `ClientServerCore.Energy.EnergyServerService`.
  `SavedData.ClearDummies()` explicitly purges energy mementos (`IsIgnoredEnergyMemento`),
  preventing local offline energy manipulation.
- **Save Synchronization**: On network connect, `LocalServerSaveMerger.Merge()` reconciles
  local currencies against the server `userState`. If server provides `currency["Normal"]`,
  wallet currencies are overwritten with server authoritative values. Offline play operates
  directly on the local save file.

### 4. Current implementation after review

Installation and save metadata inspection are available. Save headers identify candidates
only, not active player identity or trusted parameters. No version-matched NRBF schema
reader exists. Resource/progression extraction, mutation and backup restoration are
disabled; attempts return explicit errors and preserve all files. The previous scanner
and destructive fallback writer were removed after review. See the review document
for required fixtures, exact-build evidence and in-game acceptance checks.
