#pragma once

// rwg-seedgas-1 (2026-10-31, amended A1 the same day): the MODDED VANILLA-COUNT TABLE on RWG worlds.
// Brief + amendment A1: _team/test-logs/2026-10-31-nodeshuffle-seedgas-brief.md. Scope: ...-seed-lithium-scope.md.
//
// One config table (master toggle ModdedVanillaCountsOnRwg, default ON; rows ModdedVanillaCountRows =
// {ResourcePath, NodeClassPath (may be empty), VanillaCount}). Per row, on an RWG map only:
//   target     = round(VanillaCount * vanillaOreScale)
//   vanillaOreScale = (captured /Game/ SOLID-form originals this roll) / RwgBaseSolidNodes (429), clamped [0.25, 4.0].
//   MODQUOTA prints it as vanillaOreScale, next to its numerator vanillaOreOriginals and the constant baseMapSolidNodes.
//   Legend: this world's RWG-planned vanilla solid-ore originals divided by the base map's 429, which includes the player's
//   RWG amount settings as well as land. (Fix round 1, orchestrator: the old name landFactor claimed a land measure; with
//   SolidNodeAmount 0.25x it is about 3.4x the land ratio times 0.25. Behaviour unchanged: modded rows follow the same
//   amount setting the vanilla ores do.)
//   captured originals of the row resource > 0  -> OVERRIDE: its count in the quota pipeline becomes target
//   captured originals of the row resource == 0 -> SEED, only if the descriptor is RF_GAS AND an own node class resolves
//                                                 (loaded, AFGResourceNode child, not an AFGResourceDeposit child, not
//                                                 abstract, not session-refused, and the spawn site's own-class test
//                                                 IsOwnNodeClassForResource: own mount AND CDO resource == the row
//                                                 resource); registry first, then the configured class path
//   anything else                               -> skip, with the reason printed
// Both actions go through the SAME quota pipeline as captured originals: the count is written into
// VanillaResourceCounts/VanillaCount before ResourceKinds is generated and RESTORED right after the trim loop, so share,
// floors, trim and max(quota, pinned) apply exactly as for every resource while the registry block, the REPCLASS census
// and EmitRollCensus still print captured originals. The gas deck drop then skips SEEDED kinds only. Captured originals
// (hidden RWG nodes, their records, the layout dump's originals) are never touched.

#include "CoreMinimal.h"
#include "Resources/FGResourceNode.h"

struct FNodeShuffleEntry;
class UConfigPropertySection;
namespace NodeShuffleRwgDeal { struct FPurityDecks; }

namespace NodeShuffleRwgSeedGas
{
    extern const TCHAR* const DefaultLeadResource;
    constexpr int32 DefaultLeadVanillaCount = 6;
    extern const TCHAR* const DefaultLithiumResource;
    extern const TCHAR* const DefaultLithiumNodeClass;
    constexpr int32 DefaultLithiumVanillaCount = 23;
    extern const TCHAR* const DefaultThoriumResource;
    constexpr int32 DefaultThoriumVanillaCount = 45;
    constexpr int32 RwgBaseSolidNodes = 429;  // RWG's documented base-map solid-node count at 1x (amendment A1)
    constexpr float VanillaOreScaleMin = 0.25f;
    constexpr float VanillaOreScaleMax = 4.0f;
    constexpr int32 MaxRowCount = 1000;       // boundary validation of a hand-edited VanillaCount

    struct FRow
    {
        FString Resource;
        FString NodeClassPath;
        int32 VanillaCount = 0;
    };

    struct FRowResult
    {
        FRow Row;
        bool bHasRow = true;                      // false: a captured modded resource with no table row (census only)
        FString Form = TEXT("unread");
        FString ResolvedClass;
        const TCHAR* ResolvedBy = TEXT("n/a");    // config | registry | none | n/a (not a seed candidate)
        const TCHAR* Action = TEXT("skip");       // override | seed | skip
        const TCHAR* Skipped = TEXT("none");      // none | disabled | notRwg | hasNoRow | badRow | vanillaRow | duplicate | descUnloaded | notGas | noClass | emptyPool
        int32 CapturedOriginals = 0;
        int32 Target = 0;
        // gate-4 measurements, printed by MODQUOTA-CLASS
        bool bCfgLoaded = false, bCfgNode = false, bCfgAbstract = false, bCfgRefused = false, bCfgOwnMount = false;
        FString CfgCdoResource = TEXT("none");
        FString RegistryMatched = TEXT("none");
        int32 RegistryMountFiltered = 0;
    };

    struct FState
    {
        bool bEnabled = false;
        bool bMapIsRwg = false;
        bool bTreeFound = false;
        int32 RowsMalformed = 0;
        int32 VanillaOreOriginals = 0;
        float VanillaOreScale = 1.0f;
        int32 VanillaCountBefore = 0;
        bool bApplied = false;                    // DecideAndApply changed the pipeline maps; Restore pending
        TMap<FString, int32> SavedCounts;         // resource -> captured count before override (absent key = seeded)
        TArray<FRowResult> Rows;
        TSet<FString> SeededKinds;
        // fix round 1 F2 (user decision D3): the purity source of SEEDED kinds = Impure/Normal/Pure of every captured original
        // (not a new node) whose resource path starts /Game/ and is FormSolid -- the vanillaOreScale numerator's set. Counted on
        // every roll (no RNG); used only when a kind was seeded.
        int32 PuritySrc = 0;
        int32 PuritySrcBins[3] = { 0, 0, 0 };        // Impure, Normal, Pure
        TSet<FString> PurityFromGameSolid;           // seeded kinds given that source (absent: map-wide deck, purityDeck=mapwide)
    };

    // Config panel: registers ModdedVanillaCountRows on Root with the lead + lithium rows as BOTH the runtime value and the
    // reset default. Called once from UNodeShuffleConfig::PostInitProperties.
    void RegisterConfigRows(UConfigPropertySection* Root, UClass* ArrayClass, UClass* SectionClass, UClass* StringClass,
        UClass* IntegerClass);

    // Reads the rows from the LIVE config tree (T60's proven route), never from the struct mirror.
    bool ReadRows(UObject* WorldContext, TArray<FRow>& Out, int32& OutMalformed);

    // RollLayout call 1 (before ResourceKinds is generated).
    void DecideAndApply(UObject* WorldContext, bool bEnabled, bool bMapIsRwg, const TMap<FString, FString>& RefusedClasses,
        TMap<FString, int32>& VanillaResourceCounts, int32& VanillaCount, TMap<FString, uint8>& FormByResource, uint8 FormSolid,
        TMap<FString, FString>& NodeClassByResource, FState& Out);

    // RollLayout call 2 (right after the trim loop): puts VanillaResourceCounts/VanillaCount back to the captured values.
    void Restore(FState& S, TMap<FString, int32>& VanillaResourceCounts, int32& VanillaCount);

    bool IsSeededKind(const FState& S, const FString& Kind);

    // RollLayout call 2b (right after BuildPurityDecks, before LogRoll): counts the /Game/ solid originals' purities into S, then,
    // ONLY when a kind was seeded and that set is non-empty, gives each seeded kind (sorted order) its own shuffled copy in
    // Purity.ByClass. No seeded kind -> no RNG use. Empty set -> seeded kinds keep the map-wide deck. Returns the set size.
    int32 AddSeededPuritySources(FState& S, const TArray<FNodeShuffleEntry>& Layout, const TMap<FString, uint8>& FormByResource,
        uint8 FormSolid, FRandomStream& Rng, NodeShuffleRwgDeal::FPurityDecks& Purity);

    // RollLayout call 3 (after BuildPurityDecks): MODQUOTA-CFG once, MODQUOTA once per row and per captured modded resource
    // without a row, MODQUOTA-CLASS for every seed candidate that reached the class gate.
    void LogRoll(const FState& S, const TMap<FString, int32>& Quota, const TMap<FString, int32>& PinnedPerResource,
        const TArray<FString>& SolidDeck, const TArray<FString>& LiquidDeck, const NodeShuffleRwgDeal::FPurityDecks& Purity);

    // ApplyLayout, after the RWG spawn pass: SEEDGASSPAWN per GAS-form row resource, change-driven. A seeded entry :=
    // new-slot, active, Assigned == row resource, Original != row resource.
    void EmitSpawnCensus(UObject* WorldContext, bool bMapIsRwg, const TArray<FNodeShuffleEntry>& Layout,
        const TMap<FGuid, AFGResourceNode*>& SpawnedNodes, const TMap<FGuid, int32>& SpawnFailCounts,
        const TSet<FGuid>& SpawnParked);
}
