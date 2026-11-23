# Game evidence and technical findings

Analysis date: 2026-11-23. Full analysis executed on local installation.

| Technical Parameter | Confirmed Value / Evidence |
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
| Active Player ID | `6b03543d59f73e521168d4b7` (save file size: 173,635 bytes) |

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

### 2. Confirmed Local Parameters
The following parameters are stored in `SavedData` Mementos and can be inspected/modified offline:
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

### 3. Server-Authoritative Boundaries
Code analysis in `ClientServerCore\Energy` and `Saves\LocalServerSaveMerger` reveals:
- **Energy**: Managed strictly server-side by `ClientServerCore.Energy.EnergyServerService`.
  `SavedData.ClearDummies()` explicitly purges energy mementos (`IsIgnoredEnergyMemento`),
  preventing local offline energy manipulation.
- **Save Synchronization**: On network connect, `LocalServerSaveMerger.Merge()` reconciles
  local currencies against the server `userState`. If server provides `currency["Normal"]`,
  wallet currencies are overwritten with server authoritative values. Offline play operates
  directly on the local save file.

### 4. Implementation Details
The C++20 trainer implements:
- Automatic detection of `Slutopia.exe` and Steam library installation.
- Process connection and live module diagnostics.
- `SaveManager` for direct, verified offline reading and reversible editing of local currencies and player level.
- Automatic creation of `.bak` backups before any modification.
