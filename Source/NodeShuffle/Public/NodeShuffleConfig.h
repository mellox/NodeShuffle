#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "Configuration/ConfigManager.h"
#include "NodeShuffleConfig.generated.h"

// T60 (ns-t60-protect-checkboxes, 2026-08-10): ONE row of the dynamically-populated per-resource
// opt-out list. Mirrors the two properties of the config array's element section, BY NAME.
//
// THIS STRUCT IS NOT OPTIONAL AND ITS UPROPERTY FLAGS ARE NOT COSMETIC. UConfigPropertyArray::
// FillConfigStruct_Implementation does `check(NewElementIndex >= 0)` on the value returned by
// FReflectedObjectState_Array::AddNewArrayElement, and that function returns -1 whenever the mirror
// field is absent OR lacks CPF_BlueprintVisible (SML BlueprintReflectedObject.cpp:21-29). Since
// FillConfigurationStruct runs on every ApplyLayout pass, a missing/incorrectly-flagged mirror field
// is a hard assert in normal play, not a silent no-op. BlueprintReadWrite sets that flag.
USTRUCT(BlueprintType)
struct NODESHUFFLE_API FNodeShuffleForeignResourceRow
{
    GENERATED_BODY()

    // THE ROW IDENTITY: the resource descriptor class PATH, e.g.
    // "/AlkaLib/.../Desc_OreLithium.Desc_OreLithium_C". Matched exactly; never re-derived from the label.
    UPROPERTY(BlueprintReadWrite)
    FString Resource;

    // TRUE = NodeShuffle's T58 veto keeps this resource's nodes alive at the KBFL sweep.
    // FALSE = the player handed this resource back, and the sweeping mod removes those nodes as it
    // would with NodeShuffle absent. Default TRUE — an unlisted or untouched resource is protected.
    UPROPERTY(BlueprintReadWrite)
    bool Protected{true};
};

// Plain struct mirror of the configuration. Field names MUST match the
// section property keys registered in UNodeShuffleConfig exactly —
// UConfigManager::FillConfigurationStruct maps them by name.
//
// T68 (2026-08-11) REMOVED FIVE FIELDS: AllowVanillaDisappear (hard-wired to its shipped default ON),
// RerollRelocatedWells (hard-wired ON), CommitWellsAtRoll (feature deleted outright),
// ShowCompatibilityNotices (hard-wired ON, both call sites pass true), EnableExperimentalFeatures
// (dead -- it had zero consumers). A key left over in an existing NodeShuffle.cfg is ignored on load
// and dropped on the next save (SML ConfigPropertySection.cpp:18-42), and the 1.4.0 version bump
// forces that save on first load (SML ConfigManager.cpp:103-117). Nothing in the save game mirrors a
// config value, so no removal can touch save data. Grading table: docs/TECH-DEBT.md T68.
//
// T71 (2026-08-12) REMOVED TWO MORE: ShuffleResourceWells and RelocateResourceWells, both hard-wired ON
// (see the block on the constants below). Same removal mechanics as T68 -- orphan keys ignored then
// dropped, no save-game mirror. Grading table: docs/TECH-DEBT.md T71.
USTRUCT(BlueprintType)
struct NODESHUFFLE_API FNodeShuffleConfigStruct
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    bool Enabled{true};

    // 0 = roll a random seed at first generation; non-zero = use this seed.
    UPROPERTY(BlueprintReadWrite)
    int32 SeedOverride{0};

    // User-facing reliable re-roll trigger. When true at load and the world is
    // ready, the layout re-rolls once regardless of seed equality, then the mod
    // clears this flag back to false and saves the config so it fires only once.
    UPROPERTY(BlueprintReadWrite)
    bool RerollNow{false};

    // Percent of the total node pool (vanilla + new) that is active.
    UPROPERTY(BlueprintReadWrite)
    int32 ActivePercent{70};

    // How many new candidate node locations to generate (generation time only).
    UPROPERTY(BlueprintReadWrite)
    int32 NewNodeCount{100};

    // Minimum active nodes per resource type — the completability floor.
    UPROPERTY(BlueprintReadWrite)
    int32 MinNodesPerResource{5};

    // Minimum active nodes per MODDED resource type (resources added by other
    // mods, i.e. outside /Game/). 0 = no minimum (the old behavior, where modded
    // types could collapse to a single location). Default 2 keeps the
    // proportional feel while preventing singletons.
    UPROPERTY(BlueprintReadWrite)
    int32 MinNodesPerModdedResource{2};

    UPROPERTY(BlueprintReadWrite)
    bool RandomizePurity{true};

    // Shuffle mod-added nodes (AllMinable items, modded ores) too. When on, modded
    // SOLID nodes also RELOCATE (move to new locations) on a re-roll, like vanilla
    // nodes, instead of shuffling in place. The completability floor never applies to
    // non-vanilla resources.
    UPROPERTY(BlueprintReadWrite)
    bool IncludeModdedNodes{true};

    // Spawn-on-discovery radius (metres). A new node only materializes (spawns
    // its actor + visual) once a player is within this distance of it AND the
    // terrain there has streamed in. Far, undiscovered nodes stay as data until
    // you explore to them — this is what makes them settle correctly on the
    // ground instead of floating in unloaded regions.
    UPROPERTY(BlueprintReadWrite)
    int32 SpawnRadiusMeters{600};

    // knowledge-1: register shuffled MODDED resources with the game's scanner-unlock list (SaveGame,
    // replicated) so scanners — and mods that gate extractors on scanner knowledge (SF+ Modular
    // Miner via KLib HasInformationAboutOre) — recognize them even when their own unlock schematics
    // never ran in this save. Vanilla resources are never touched.
    UPROPERTY(BlueprintReadWrite)
    bool UnlockModdedKnowledge{true};

    // Diagnostics gate. OFF by default so normal users get NO extra log output and
    // no overhead. When ON, the mod writes verbose HOLOGRAMHOOK / placement / node
    // diagnostics to FactoryGame.log for troubleshooting (e.g. Miner snap issues).
    // The actual fixes (e.g. the Mk1 accept-hook) are always active regardless.
    UPROPERTY(BlueprintReadWrite)
    bool EnableDiagnostics{false};

    // redesign-1 (Hide & Replace): on a BRAND-NEW game's first roll only, place a
    // starter set of nodes (2 Iron, 2 Limestone, 1 Copper, PURE) within
    // StarterNodeRadiusMeters of the player's real spawn point, so the early game is
    // playable no matter how the shuffle relocated the world's nodes. Drawn from the
    // relocated pool when possible (preserving counts), spawned additionally if the
    // pool can't supply a type. NEVER placed on an existing save.
    UPROPERTY(BlueprintReadWrite)
    bool EnableStarterNodes{true};

    // redesign-1: radius (metres) around the player's captured spawn location within
    // which the starter nodes are placed. Default 200 m.
    UPROPERTY(BlueprintReadWrite)
    int32 StarterNodeRadiusMeters{200};

    // ================================================================================================
    // T71 (ns-t71-wells-always-on, 2026-08-12): THE TWO WELL TOGGLES ARE GONE. WELLS ARE PART OF THE
    // SHUFFLE. Author's decision 2026-08-12 ("make them a natural part of the shuffle"), overriding the
    // T68 audit's keep-both recommendation (#7). These are NOT UPROPERTYs and are NOT reflected: they
    // are the hard-wired replacements for the deleted `ShuffleResourceWells` (Packet H1, in-place
    // retype) and `RelocateResourceWells` (Packet H2, rigid group relocation) config fields, kept as
    // NAMED constants rather than inlined `true` so that every gate site still reads a symbol that says
    // WHICH promise it is gating, and so a revert is one grep away. Same shape as T68's
    // `static constexpr bool bRerollRelocated = true` (NodeShuffleWellRelocateRoll.cpp).
    //
    // WHAT THE DELETED OFF PATHS GUARANTEED, AND WHERE THAT GUARANTEE WENT (full table: TECH-DEBT T71):
    //   * ShuffleResourceWells OFF = "no well ever changes what it yields". GONE -- every unpinned,
    //     unbuilt-on well is now retyped at roll time. A well with a pressurizer or any fracking
    //     extractor on it is STILL never touched; that is the surviving guarantee and it is unchanged.
    //   * RelocateResourceWells OFF = "wells never move". GONE -- and this is the one that carried the
    //     UNBOUNDED-ABSENCE warning: a well is removed from its old site the moment it is dealt a
    //     destination and only rebuilt when you travel to the new one, so it is in NEITHER place for as
    //     long as you do not go there. That text does not vanish with the toggle: it now lives in the
    //     mod Description (NodeShuffle.uplugin), README.md's Resource Wells section, and the CHANGELOG
    //     1.4.0 entry, because it is now a property of the mod rather than of an opt-in.
    //   * Two edges stay unverified and are now ALWAYS in scope rather than opt-in: desert-biome well
    //     mesh names (TECH-DEBT T2) and relocated-well snap-box overlap with ordinary nodes inside
    //     ~15 m (TECH-DEBT T3).
    // A key left over in an existing NodeShuffle.cfg is ignored on load and dropped on the next save
    // (SML ConfigPropertySection.cpp:18-42); the 1.4.0 version bump T68 already made forces that save
    // (SML ConfigManager.cpp:103-117), and it is version-diff-triggered, so no further bump is needed.
    static constexpr bool bWellShuffleHardWiredOn = true;
    static constexpr bool bWellRelocationHardWiredOn = true;

    // ---- T68 (ns-t68-release-config, 2026-08-11): the protection master switch, promoted from the
    // console variable NodeShuffle.DestroyerVeto to a panel checkbox. DEFAULT TRUE, which is a
    // BEHAVIOUR CHANGE for every existing install: the CVar defaulted to 0, so protection shipped off.
    //
    // PRECEDENCE (one resolver, two inputs -- FNodeShuffleModule::ResolveDestroyerVetoRequested in
    // NodeShuffle.cpp): this field is the PERSISTED source of truth; the CVar is a session-scoped
    // override that wins only when someone actually set it. Read ONCE per world init by the veto's arm
    // pass, exactly as the CVar was, so a mid-session flip can never split one KBFL sweep across two
    // policies.
    UPROPERTY(BlueprintReadWrite)
    bool ProtectOtherModsNodes{true};

    // T60: the per-resource opt-out list. PRESENT BECAUSE THE SCHEMA HAS THE MATCHING ARRAY PROPERTY —
    // see the check() note on FNodeShuffleForeignResourceRow. NOTHING IN THE MOD READS THIS FIELD:
    // both the world-init latch and the population pass walk the LIVE config tree
    // (UConfigManager::GetConfigurationRootSection) instead, because that path is the one the research
    // verified end-to-end and it removes array-of-struct reflection from the critical path entirely.
    // If a future consumer wants to read it, prove it fills correctly first.
    UPROPERTY(BlueprintReadWrite)
    TArray<FNodeShuffleForeignResourceRow> ProtectedForeignResources;

    static FNodeShuffleConfigStruct GetActiveConfig(UObject* WorldContext);
};

// C++-defined SML mod configuration. Registered by the root game instance
// module; SML generates the in-game settings UI (on the mod's page in the
// Mods menu) and persists values to FactoryGame/Configs/NodeShuffle.cfg.
UCLASS()
class NODESHUFFLE_API UNodeShuffleConfig : public UModConfiguration
{
    GENERATED_BODY()

public:
    UNodeShuffleConfig();

    // The property tree is built here rather than in the constructor so each
    // property can be outer'd to the root section — SML's dirty/save chain
    // walks property Outers and silently never saves otherwise.
    virtual void PostInitProperties() override;
};
