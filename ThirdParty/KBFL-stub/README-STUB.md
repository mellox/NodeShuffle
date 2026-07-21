# KBFL — BUILD-TIME HEADER STUB ONLY

**This is NOT the real KBFL.** It exists so the NodeShuffle plugin's optional
`NodeShuffleVetoKBFL` module can *compile and link* against KBFL's
`UKBFLCDOCallRequirement` — the same idiom every SF mod uses to compile against the
closed FactoryGame headers.

What it contains:

- `Source/KBFL/Public/Subsystems/HelperClasses/KBFLCDOCallRequirement.h` — copied
  **VERBATIM** from the public KBFL source (same include path structure), so vtable
  and property layout match what the real KBFL DLL was built from.
- A stub `Private` cpp with **no-op bodies** whose only purpose is to make this
  module's build emit an import library (`KBFL.lib`). Consumers' DLLs then import
  `...KBFL-Win64-Shipping.dll` **by name** and resolve against the player's
  actually-installed KBFL at runtime.
- A minimal module implementation so the DLL links. It never runs in a shipped game.

Rules:

- **NEVER Alpakit / package / deploy this plugin.** It ships NOTHING. Deploying it
  would shadow the real KBFL (`SemVersion 9999.9.9` would satisfy every dependency)
  and break every KBFL-dependent mod.
- Do not add more KBFL classes here casually — the one-class surface is deliberate
  ABI minimization for the NodeShuffle veto module (everything else is reached via
  reflection at runtime). If real KBFL source is ever needed in this project,
  **replace this folder wholesale** with the actual KBFL repo instead of growing
  the stub.
- If the upstream `KBFLCDOCallRequirement.h` changes, re-copy it verbatim and
  rebuild consumers; NodeShuffle's runtime ABI guard
  (`GetPropertiesSize()` vs `sizeof`) will refuse to arm on drift either way.

Source of the copied header: the public KMods source clone (KBFL repo,
`Source/KBFL/Public/Subsystems/HelperClasses/KBFLCDOCallRequirement.h`).

**Source of truth / provenance:** the TRACKED copy of this stub lives in the
NodeShuffle mod repo at `C:\Claude\Projects\NodeShuffle\ThirdParty\KBFL-stub\`
(this engine repo gitignores `Mods/*`, so THIS folder is an untracked deployed
copy). If this folder is ever missing, restore it by copying
`ThirdParty\KBFL-stub\` from the NodeShuffle repo to
`<SatisfactoryModLoader>\Mods\KBFL\`. Make edits in the tracked copy first,
then re-deploy here — never let the two drift.
