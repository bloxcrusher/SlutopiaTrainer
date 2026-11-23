# Review of antigravity-analysis / 4903c5c

Reviewed 2026-11-23 against its parent `82aa6f9` (PR #1 foundation).
The original commit's Windows CI was green. That run tests a synthetic byte pattern,
not an NRBF object graph, real game persistence or server ownership.
No installed game, decompiled assembly or sanitized save fixtures are available in
this review environment. Gemini's local-analysis notes are retained as reported
findings, not independently verified compatibility evidence.

## Findings and fixes

| Severity | Original problem | Resolution |
| --- | --- | --- |
| P1 | `ParseParameters` decrements unsigned `size_t` through zero. A near-start key with no matching marker can wrap, hang or read outside the buffer. The test covers only a hand-built matching marker. | Remove the unvalidated byte scanner; regression inputs at offsets zero/eight return no trusted values promptly. |
| P1 | NRBF object ID `0x22`, nearby strings and `key - 11` are treated as field offsets. IDs depend on serialization order. No root/type/member graph or exact build is validated. All currencies are labelled locally authoritative, including reportedly synchronized ones. | Parameter extraction and edits fail closed until a real schema/build/ownership adapter exists. No premium or synchronized currency editor remains exposed. |
| P1 | Cached offsets are reused after rereading a potentially changed file. No process/exclusive-access or concurrent-save check exists. Windows rename over an existing target can fail, triggering direct truncation; write/flush failures are ignored. Backup restore also overwrites directly. | Remove the destructive paths entirely. Both mutation APIs return explicit errors without touching saves/backups/temp files. A safe atomic writer alone would not repair the incorrect schema. |
| P1 | UI holds references into `info.parameters`, then calls ModifyParameter -> Load which replaces that vector, and continues using `p`. This is use-after-invalidation. Increment buttons also ignore failures and can overflow signed values. | Remove the unsafe editing views and stale static edit caches. Show read-only candidate metadata and visible inspection errors. |
| P2 | First matching filesystem entry becomes the active player, arbitrary-size files are read in full, errors may leave stale parameters and Unicode paths are narrowed via `.string()`. | Bounded metadata inspection (512 entries, 16 MiB maximum candidate size, 17-byte header reads), ambiguity rejection, state clearing, UTF-8 UI paths, no auto-selected player identity. |
| P2 | Manifest lookup stops in `steamapps/common`, one directory too early. Engine/version/x64 are fabricated from module names or from finding any executable. Registry path is fixed-size and Steam libraries are not read. | Read the manifest from `steamapps`, validate app/directory/build fields, reject duplicates, use actual file-version resources, leave unknown evidence unknown, read registry paths with checked types and enumerate modern libraryfolders paths. |
| P2 | Default target detection turns auto-connect on despite the saved preference. GUI smoke tests inspect the real profile and Steam installation. | Preserve preferences; explicit isolated construction for smoke/integration tests avoids profile discovery. |
| P2 | Windows sources/libraries were added to the portable common target unconditionally; Linux configure succeeds but compilation fails on ShlObj.h. | Separate conditional Windows sources/libraries and retain portable configuration/save-diagnostic tests. |

## Resulting behavior

The Windows app retains all four tabs, process discovery/connection, runtime modules,
settings, hotkeys and diagnostics. Installation candidates, observed file versions,
Steam build metadata and NRBF-header save candidates can be inspected. These findings
do not enable game edits. `IsValid()` remains false without a validated adapter.
Both `ModifyParameter` and `RestoreBackup` preserve their API but explicitly reject
operations. No game values are fabricated or presented as confirmed.

## Verification

- Portable GCC/CMake/CTest: configuration round trips and invalid settings, bounded
  logs, rejection of heuristic signatures and near-start keys, missing/truncated/
  oversized/multiple save candidates, Unicode directories, backup/temp exclusion,
  unchanged save/backup bytes after rejected operations, and stale-state clearing.
- Windows Release/MSVC CI: the portable suite plus deterministic installation/
  manifest/unknown-version/trainer-isolation tests, real process lifecycle/read-only
  memory checks, four-tab GUI smoke test without game/profile discovery, CLI enumeration
  and the metadata collector. See the fix PR's checks for results.
- No installed-game test is claimed.

## Required before enabling persistence

Supply build ID and hashes for executable/Assembly-CSharp.dll, redacted decompiled
member/type evidence, sanitized NRBF fixtures representing at least two legitimate
before/after states and differing object-ID order, and evidence of local authority.
Implement a bounded non-executing NRBF schema reader with explicit type/member identity,
not BinaryFormatter deserialization or pattern matching. Establish backup/session
identity, exclusive access, external-change detection, atomic replacement with no
truncate fallback, and recovery on failures. Then verify an ordinary local-only
parameter survives save/load/restart in the exact installed build. Server reconciliation,
premium entitlements, purchases and server-bound values remain outside edit support.
