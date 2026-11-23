# Game evidence and technical findings

Analysis date: 2026-11-23. Verified directly against the local Steam installation
and a read-only copy of a local save file (user's live save was never modified).

## 1. Verified Build Profile & Binary Hashes

| Technical Parameter | Verified Value / Evidence |
| --- | --- |
| Steam App ID | `4612690` (`appmanifest_4612690.acf`) |
| Steam Build ID | `26839990` |
| Executable | `Slutopia.exe` (672,256 bytes, x64 PE `IMAGE_FILE_MACHINE_AMD64`) |
| `Slutopia.exe` SHA-256 | `EFAB05E682151729DF2CA3DCBB0932A9F924EBD30A6704482DBBF9C9CA54FB1A` |
| `UnityPlayer.dll` SHA-256 | `42001CFF5851F6CBC4CDA91713EF008A286FF13CA8EB1072F02F797394CC07D5` (33,731,496 bytes) |
| `mono-2.0-bdwgc.dll` SHA-256 | `9040706179AD5DB4A26744FBA5E353685C2EB22371D873B4B22F80D235768D21` (7,830,952 bytes) |
| `Assembly-CSharp.dll` SHA-256 | `538893C80A851A57D493C35A9809BA88F640ED701412A36782C5A86CD3B5A618` (6,589,440 bytes) |
| Engine / Product Version | Unity `6000.0.60f1` (FileVersion `6000.0.60.6414259`, `boot.config` build-guid `ffbfb2237b564b8f9aec82eac0a5b9fb`) |
| Scripting Runtime | Mono (`MonoBleedingEdge\EmbedRuntime\mono-2.0-bdwgc.dll`) |
| Local Save Directory | `%LOCALAPPDATA%Low\MobiStar Games\Slutopia` (`app.info`: `MobiStar Games` / `Slutopia`) |
| Save Serialization | MS-NRBF (.NET `BinaryFormatter`), root class `Saves.SavedData` |

## 2. Verified MS-NRBF Save Graph Structure

Parsed completely from byte `0` to `MessageEnd` using `trainer::nrbf::Parse` (0 unconsumed bytes):
- Sample size: `173,868` bytes (`10,059` NRBF records, `2` libraries, `3,250` objects, `155` arrays, `3,977` strings).
- Root object (`rootId = 1`): `Saves.SavedData`
  - `UpdatedAt` (`Int64`)
  - `Data` (`System.Collections.Generic.List<Data.Memento>`, `_size = 1,737`, array capacity `2,048`, `58` distinct `Memento` classes).
- `StripClub.Model.Data.SimpleCurrency+CurrencyMemento` (`34` instances):
  - Fields: `Count` (`Int32`), `Identificator` (`Types.CompositeIdentificator`), `IsMigrated98aTo98fe` (`Boolean`), `Memento+<UniqueKey>k__BackingField` (`String`), `Memento+<Version>k__BackingField` (`String`), `Memento+<FirstSavedInThisSession>k__BackingField` (`Boolean`).
  - Contains both legacy keys (e.g., `currency_Soft`, `currency_Hard`, `currency_GirlCrystal`) and current `[0];`-suffixed keys (e.g., `currency_Soft[0];`, `currency_Hard[0];`, `currency_GirlCrystal[0];`).
- `_Scripts.PlayerLeveling.PlayerLevelerMemento` (`1` instance):
  - Fields: `rewardedLevel` (`Int32`), `UniqueKey = "PlayerLeveler"`, `Version`.
- Notable additional mementos observed in the save graph:
  - `AntiCheat.CheatEngineSearchService+Memento` (`1x`)
  - `Cheats.JewelsObserverTemp+JewelsObserverMemento` (`1x`)
  - `MergeCore.GeneralData+GeneralDataMemento` (`1x`), `MergeCore.CollectionController+CollectionMemento` (`1x`)
  - `StarShop.StarShopItem+Memento` (`116x`), `Stories.Story+StoryMemento` (`116x`), `Relationships.Models.Relationship+RelationShipMemento` (`13x`)

## 3. Confirmed Facts vs. Assumptions (Server Authority)

### Confirmed from `Assembly-CSharp.dll`
- **Energy is strictly server-authoritative**: handled by `ClientServerCore.Energy.EnergyServerService` (`ServerEnergyProjection`, `ServerConfirmedEnergyOutcome`). `SavedData.ClearDummies()` explicitly strips legacy `Energy` / `EventEnergy` mementos on deserialization.
- **Main wallet currencies are server-merged**: `Saves.LocalServerSaveMerger.MergeMainWalletCurrency` calls `TryMainWalletResourceKey` -> `SelectorTools.GetResourceNameValueByType`, which maps `Soft`, `Hard`, `Star`, `BP`, `Jewel`, `Contracts`, `Present1..4`, `AuctionTicket`, `StandardTickets`, `EventTickets`, `Smiles`, `GirlCrystal`, `StoryEventResource`, and `StoryEventUpgrade` to server `userState.currency.Normal` keys and overwrites local `Count` on merge.
- **Player level is server-merged**: `LocalServerSaveMerger.MergePlayerLevel` reconciles `PlayerLevelerMemento.rewardedLevel` from server `userState`, and `PlayerLeveler.ClaimServerRewards` uses `clientServerApi.ClaimPlayerLevelRewards`.
- **Additional server-merged types in `LocalServerSaveMerger.MergeOne`**: `StarShopItem.Memento`, `CharacterSettings.ThisMemento`, `CharacterProgressMemento`, `CharacterManagerState.Memento`, `BannerMemento`, `RouletteRealMemento`, `PlayerProfileMemento`, `ProfileDecorationStorageMemento`, `LegacyRestorableIntMemento` (`dungeon_key_*`), `NovelMemento`, `DateMemento`.

### Unverified / Remaining Assumptions
- Whether purely local mementos not referenced by `LocalServerSaveMerger` (such as `MergeCore.GeneralData+GeneralDataMemento` or `Merge.DynamicOrders.*`) survive server reconciliation without server-side rejection has **not** yet been tested in-game.
- Therefore, direct save mutation on the user's live profile remains **disabled** in `SaveManager::ModifyParameter` and `RestoreBackup`. Only read-only NRBF inspection is enabled in the UI, and integrity-checked patching (`SaveManager::PatchCopyFileVerified`) is restricted to separate test copies.

## 4. Current Implementation Status

- **Bounded MS-NRBF Parser (`src/core/NrbfReader.*`)**: full non-executing MS-NRBF reader with strict bounds, recursion, and record-count limits, verified against both synthetic fixtures and a real 173,868-byte save copy.
- **Read-Only Save Inspection (`src/core/SaveManager.*`)**: parses `Saves.SavedData` and displays verified `CurrencyMemento` and `PlayerLevelerMemento` values in the Dear ImGui UI with explicit `Server-merged` authority labels and exact NRBF field offsets.
- **Verified Copy-Only Patcher (`SaveManager::PatchCopyFileVerified`)**: refuses in-place edits, checks expected old value and bounds, re-parses the full patched NRBF stream before and after disk write, and uses atomic `MoveFileExW` / `rename` with no destructive `trunc` fallback.
