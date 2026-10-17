// RWG P1 (2026-10-14) -- map identity and OBSERVE-ONLY readiness on non-vanilla maps.
// Spec: _team/nodeshuffle-rwg-fullshuffle-spec.md §A1, §B1-B4, §G (R1-only lines), §H (P1).
// RWG P2a (2026-10-17, _team/nodeshuffle-rwg-p2-spec.md §1/§3): readiness v2 -- ready only when the
// generator's EPWProgressStage reads Ready, the count has held 12 ticks counted from the first Ready tick,
// and the floor holds. Unreadable stage = never ready (no quiescence fallback). Still observe-only.
//
// WHAT THIS FILE MAY CHANGE. On vanilla: nothing but one MAPMODE line at BeginPlay -- RefreshTick only
// enters RwgObserveTick when GetMapMode() != Vanilla, and every other function here is reached only from
// there. Off vanilla: it counts nodes, reads the generator by reflection, traces and logs. It never rolls,
// hides, spawns, adopts or writes a file. Nothing gates on its predicate except IsWorldReadyForReroll's
// off-vanilla AND (spec B4), which P1 never reaches because RefreshTick returns before the re-roll branch.
// Every log line reports what it MEASURED (counts, names, predicate branches); none states a cause.
#include "NodeShuffleMapMode.h"
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleNodeComponent.h"
#include "NodeShuffleResourceNode.h"
#include "EngineUtils.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "Resources/FGResourceNodeBase.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceDeposit.h"
#include "FGWaterVolume.h"

namespace
{
    const TCHAR* const GeneratorClassPath = TEXT("/Script/ProceduralWorld.PWWorldGenerator");

    const TCHAR* MapModeName(ENodeShuffleMapMode M)
    {
        switch (M)
        {
        case ENodeShuffleMapMode::Vanilla: return TEXT("vanilla");
        case ENodeShuffleMapMode::Rwg:     return TEXT("rwg");
        case ENodeShuffleMapMode::Unknown: return TEXT("unknown");
        default:                           return TEXT("unlatched");
        }
    }

    bool IsEngineOwned(const UStruct* S)
    {
        const FString Pkg = S->GetOutermost()->GetName();
        return Pkg.StartsWith(TEXT("/Script/Engine")) || Pkg.StartsWith(TEXT("/Script/CoreUObject"));
    }

    FString EnumValueName(const UEnum* E, int64 V)
    {
        FString N = E->GetNameStringByValue(V);
        if (N.IsEmpty()) { return FString::Printf(TEXT("<value %lld>"), V); }
        const int32 Sep = N.Find(TEXT("::"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        return Sep == INDEX_NONE ? N : N.Mid(Sep + 2);
    }

    // Type-checked enum read (spec B2: chosen by TYPE, never by property name). False = not enum-typed.
    bool ReadEnumProperty(const FProperty* Prop, const void* ValuePtr, const UEnum*& OutEnum, int64& OutValue)
    {
        if (const FEnumProperty* EP = CastField<FEnumProperty>(Prop))
        {
            const FNumericProperty* Under = EP->GetUnderlyingProperty();
            if (!EP->GetEnum() || !Under) { return false; }
            OutEnum = EP->GetEnum();
            OutValue = Under->GetSignedIntPropertyValue(ValuePtr);
            return true;
        }
        if (const FByteProperty* BP = CastField<FByteProperty>(Prop))
        {
            if (!BP->Enum) { return false; }
            OutEnum = BP->Enum.Get();
            OutValue = BP->GetPropertyValue(ValuePtr);
            return true;
        }
        return false;
    }

    // Walks Obj's own (non-engine) class chain for an EPWGeneratorState-typed property and a PWProgressState
    // struct holding an EPWProgressStage. A missing type = an empty out-string, never a crash.
    // P2a: returns STAGE readability. P1 returned the EPWGeneratorState hit, and RWG 0.1.7 has no such
    // UPROPERTY (R1 RWGPROBE: 41 props, none of that enum), so P1's readiness never evaluated the stage.
    bool FindGeneratorFields(UObject* Obj, FString& OutGen, FString& OutStage)
    {
        bool bGen = false;
        for (UClass* C = Obj->GetClass(); C && !IsEngineOwned(C); C = C->GetSuperClass())
        {
            for (TFieldIterator<FProperty> It(C, EFieldIteratorFlags::ExcludeSuper); It; ++It)
            {
                FProperty* P = *It;
                void* Ptr = P->ContainerPtrToValuePtr<void>(Obj);
                const UEnum* E = nullptr;
                int64 V = 0;
                if (ReadEnumProperty(P, Ptr, E, V))
                {
                    if (!bGen && E->GetName() == TEXT("EPWGeneratorState")) { OutGen = EnumValueName(E, V); bGen = true; }
                    continue;
                }
                const FStructProperty* SP = CastField<FStructProperty>(P);
                if (!SP || !SP->Struct || !OutStage.IsEmpty() || SP->Struct->GetName() != TEXT("PWProgressState")) { continue; }
                for (TFieldIterator<FProperty> In(SP->Struct.Get()); In; ++In)
                {
                    const UEnum* IE = nullptr;
                    int64 IV = 0;
                    if (ReadEnumProperty(*In, In->ContainerPtrToValuePtr<void>(Ptr), IE, IV)
                        && IE->GetName() == TEXT("EPWProgressStage"))
                    {
                        OutStage = EnumValueName(IE, IV);
                        break;
                    }
                }
            }
        }
        (void)bGen; // OutGen stays a print-only field (RWGWAIT prints 'absent' when it is empty)
        return !OutStage.IsEmpty();
    }
}

// Shared with NodeShuffleMapModeR1.cpp (declared in NodeShuffleMapMode.h).
APawn* NodeShuffleMapMode::FirstPlayerPawn(UWorld* World)
{
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* Pc = It->Get();
        if (APawn* Pawn = Pc ? Pc->GetPawn() : nullptr) { return Pawn; }
    }
    return nullptr;
}

bool NodeShuffleMapMode::IsOursForRwg(AFGResourceNodeBase* Node)
{
    // Same "ours" test as NodeShuffleLateNodes.cpp's capture scan and NodeShuffleIsOurNode
    // (NodeShuffleSubsystem.cpp, file-static): managed registry, our component, legacy class.
    return FNodeShuffleModule::IsManagedSpawnedNode(Node)
        || UNodeShuffleNodeComponent::Find(Node) || Node->IsA<ANodeShuffleResourceNode>();
}

ENodeShuffleMapMode ANodeShuffleSubsystem::GetMapMode() const
{
    if (MapMode != ENodeShuffleMapMode::Unlatched) { return MapMode; }
    const UWorld* World = GetWorld();
    const ULevel* Level = World ? World->PersistentLevel.Get() : nullptr;
    const UPackage* Pkg = Level ? Level->GetOutermost() : nullptr;
    if (!Pkg) { return ENodeShuffleMapMode::Unknown; } // not latched: resolved on a later call
    MapPackageName = Pkg->GetName();
    // Exact (case-insensitive) equality only. No fuzzy match: a miss must be visible in MAPMODE.
    if (MapPackageName.Equals(NodeShuffleMapMode::VanillaPackage(), ESearchCase::IgnoreCase))
    {
        MapMode = ENodeShuffleMapMode::Vanilla;
    }
    else if (MapPackageName.Equals(NodeShuffleMapMode::RwgPackage(), ESearchCase::IgnoreCase))
    {
        MapMode = ENodeShuffleMapMode::Rwg;
    }
    else
    {
        MapMode = ENodeShuffleMapMode::Unknown;
    }
    return MapMode;
}

void ANodeShuffleSubsystem::LogMapModeOnce()
{
    const ENodeShuffleMapMode Mode = GetMapMode();
    const bool bVanilla = Mode == ENodeShuffleMapMode::Vanilla;
    // The generator lookup runs only off vanilla, so vanilla executes nothing new but this line.
    const UClass* GenClass = bVanilla ? nullptr : FindObject<UClass>(nullptr, GeneratorClassPath);
    UE_LOG(LogNodeShuffle, Display,
        TEXT("MAPMODE: package='%s' mode=%s storeBackend=%s generatorClass='%s' rollOffVanillaEnabled=%d"),
        *MapPackageName, MapModeName(Mode), bVanilla ? TEXT("files") : TEXT("session"),
        bVanilla ? TEXT("not looked up on vanilla") : (GenClass ? *GenClass->GetPathName() : TEXT("<not found>")),
        NodeShuffleMapMode::bRwgRollEnabled ? 1 : 0);
}

void ANodeShuffleSubsystem::RwgCountNodes(int32& OutNodes, int32& OutFracking, int32& OutDeposits) const
{
    OutNodes = OutFracking = OutDeposits = 0;
    for (TActorIterator<AFGResourceNodeBase> It(GetWorld()); It; ++It)
    {
        AFGResourceNodeBase* Node = *It;
        if (!IsValid(Node) || NodeShuffleMapMode::IsOursForRwg(Node)) { continue; }
        ++OutNodes;
        if (IsFrackingActor(Node)) { ++OutFracking; }
        if (Node->IsA<AFGResourceDeposit>()) { ++OutDeposits; }
    }
}

void ANodeShuffleSubsystem::RwgDiscoverGenerator()
{
    if (RwgGenerator.IsValid()) { return; }
    UWorld* World = GetWorld();
    if (!World) { return; }
    UObject* Found = nullptr;
    UObject* FirstAny = nullptr;
    int32 Instances = 0;
    const TCHAR* Route = TEXT("classLookup");
    UClass* GenClass = FindObject<UClass>(nullptr, GeneratorClassPath);
    if (GenClass)
    {
        TArray<UObject*> Objs;
        GetObjectsOfClass(GenClass, Objs, true, RF_ClassDefaultObject | RF_ArchetypeObject);
        for (UObject* O : Objs)
        {
            if (!IsValid(O)) { continue; }
            ++Instances;
            if (O->GetWorld() == World) { Found = O; break; }
            if (!FirstAny && O->GetWorld() == nullptr) { FirstAny = O; }
        }
        if (!Found) { Found = FirstAny; }
    }
    int32 PrefixClasses = 0;
    if (!Found && RwgFallbackScans < 10 && (RwgObserveTicks % NodeShuffleMapMode::RwgWaitEveryTicks) == 1)
    {
        // Spec B2 fallback: one actor scan, each distinct class's superclass chain checked for an RWG prefix;
        // the first such actor with a readable EPWProgressStage (P2a; P1 keyed this on the absent
        // EPWGeneratorState, so the scan could never take an actor) is taken.
        ++RwgFallbackScans;
        Route = TEXT("actorScan");
        TSet<UClass*> Checked;
        for (TActorIterator<AActor> It(World); It && !Found; ++It)
        {
            UClass* C = It->GetClass();
            if (Checked.Contains(C)) { continue; }
            Checked.Add(C);
            bool bPrefix = false;
            for (UClass* K = C; K && !bPrefix; K = K->GetSuperClass())
            {
                const FString P = K->GetPathName();
                bPrefix = P.StartsWith(TEXT("/Script/ProceduralWorld.")) || P.StartsWith(TEXT("/RandomWorldGenerator/"));
            }
            if (!bPrefix) { continue; }
            ++PrefixClasses;
            FString G, S;
            if (FindGeneratorFields(*It, G, S)) { Found = *It; }
        }
        if (!Found)
        {
            UE_LOG(LogNodeShuffle, Display,
                TEXT("RWGGEN: no generator object found; classLookupHit=%d instancesOfClass=%d fallbackActorScan=%d of 10 distinctClassesScanned=%d rwgPrefixClasses=%d"),
                GenClass ? 1 : 0, Instances, RwgFallbackScans, Checked.Num(), PrefixClasses);
        }
    }
    if (!Found) { return; }
    RwgGenerator = Found;
    RwgGeneratorClassPath = Found->GetClass()->GetPathName();
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGGEN: generator object found by %s: class='%s' object='%s' instancesOfClass=%d sameWorld=%d"),
        Route, *RwgGeneratorClassPath, *Found->GetName(), Instances, Found->GetWorld() == World ? 1 : 0);
    if (!bRwgProbeLogged) { RwgLogProbe(); }
}

bool ANodeShuffleSubsystem::RwgReadGeneratorState(FString& OutGenState, FString& OutProgressStage) const
{
    OutGenState.Reset();
    OutProgressStage.Reset();
    UObject* Gen = RwgGenerator.Get();
    if (!IsValid(Gen)) { return false; }
    // P2a: readable == the progress-stage string was read (FindGeneratorFields returns exactly that).
    return FindGeneratorFields(Gen, OutGenState, OutProgressStage) && !OutProgressStage.IsEmpty();
}

void ANodeShuffleSubsystem::RwgLogProbe()
{
    bRwgProbeLogged = true;
    UObject* Gen = RwgGenerator.Get();
    if (!IsValid(Gen))
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RWGPROBE: class='<none found>' props=0 [] classLookup='%s' fallbackActorScans=%d"),
            GeneratorClassPath, RwgFallbackScans);
        return;
    }
    FString Dump;
    int32 Props = 0;
    int32 Listed = 0;
    for (UClass* C = Gen->GetClass(); C && !IsEngineOwned(C); C = C->GetSuperClass())
    {
        for (TFieldIterator<FProperty> It(C, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            ++Props;
            if (Listed >= 60) { continue; }
            FProperty* P = *It;
            void* Ptr = P->ContainerPtrToValuePtr<void>(Gen);
            FString Val = TEXT("?");
            const UEnum* E = nullptr;
            int64 V = 0;
            if (ReadEnumProperty(P, Ptr, E, V)) { Val = E->GetName() + TEXT("::") + EnumValueName(E, V); }
            else if (const FBoolProperty* B = CastField<FBoolProperty>(P)) { Val = B->GetPropertyValue(Ptr) ? TEXT("true") : TEXT("false"); }
            else if (const FNumericProperty* N = CastField<FNumericProperty>(P)) { Val = N->GetNumericPropertyValueToString(Ptr); }
            else if (const FStructProperty* SP = CastField<FStructProperty>(P)) { Val = SP->Struct ? SP->Struct->GetName() : FString(TEXT("struct")); }
            Dump += (Listed > 0 ? TEXT("; ") : TEXT("")) + P->GetName() + TEXT(":") + P->GetClass()->GetName() + TEXT("=") + Val;
            ++Listed;
        }
    }
    UE_LOG(LogNodeShuffle, Display, TEXT("RWGPROBE: class='%s' props=%d listed=%d [%s]"),
        *RwgGeneratorClassPath, Props, Listed, *Dump);
}

void ANodeShuffleSubsystem::RwgObserveTick()
{
    UWorld* World = GetWorld();
    if (!World) { return; }
    if (GetMapMode() == ENodeShuffleMapMode::Unknown)
    {
        // Spec decision 2: an unrecognised map idles -- loudly, once, naming both strings. Fail CLOSED by design.
        if (MapMode == ENodeShuffleMapMode::Unknown && !bRwgUnknownWarned)
        {
            bRwgUnknownWarned = true;
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("MAPMODE: map package '%s' equals neither '%s' nor '%s'; this session NodeShuffle runs no roll and no layout apply on this map and reads and writes neither NodeShuffle_WaterGrid.json nor NodeShuffle_CaveFloors.json; saved layout entries %d, hide records %d"),
                *MapPackageName, NodeShuffleMapMode::VanillaPackage(), NodeShuffleMapMode::RwgPackage(),
                Layout.Num(), OriginalNodeRecord.Num());
        }
        return;
    }
    ++RwgObserveTicks;
    const double T = World->GetTimeSeconds();
    const double RealT = World->GetRealTimeSeconds();
    if (bLayoutGenerated && !bRwgLayoutNoticeLogged)
    {
        bRwgLayoutNoticeLogged = true;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RWGLAYOUT: save holds a layout; entries=%d hideRecords=%d rwgLayoutVersion=%d mapRwg=%d; it is applied only after an apply clause latches and RWGREADY prints"),
            Layout.Num(), OriginalNodeRecord.Num(), RwgLayoutVersion, GetMapMode() == ENodeShuffleMapMode::Rwg ? 1 : 0);
    }
    APawn* Pawn = NodeShuffleMapMode::FirstPlayerPawn(World);
    if (Pawn && !bRwgPawnFirstSet)
    {
        bRwgPawnFirstSet = true;
        RwgPawnFirst = Pawn->GetActorLocation();
    }

    // P2a: discovery, the stage and the content fields run EVERY tick (pre- and post-latch) so RWGSTAGE
    // times each change; RwgDiscoverGenerator returns at once when the generator is already held.
    RwgDiscoverGenerator();
    FString Gen, Stage;
    const bool bStageReadable = RwgReadGeneratorState(Gen, Stage);
    int32 ContentSpawned = -1, FillPct = -1;
    RwgReadContentFields(ContentSpawned, FillPct);
    RwgNoteStage(Stage, ContentSpawned, FillPct, T, RealT);
    // genState stays in the lines (greps keep working); RWG 0.1.7 has no such UPROPERTY, so it prints 'absent'.
    const FString GenShown = Gen.IsEmpty() ? FString(TEXT("absent")) : Gen;
    const FString StageShown = Stage.IsEmpty() ? FString(TEXT("<unreadable>")) : Stage;

    if (!bRwgReady)
    {
        int32 Nodes = 0, Fracking = 0, Deposits = 0;
        RwgCountNodes(Nodes, Fracking, Deposits);
        const int32 Delta = RwgPrevNodeCount < 0 ? Nodes : Nodes - RwgPrevNodeCount;
        RwgPrevNodeCount = Nodes;
        // RWG P2b apply-on-load clause B: consecutive unchanged-count ticks at ANY stage, counted only on a save
        // that holds a layout (a new game uses clause A alone).
        RwgStableAnyStage = bLayoutGenerated ? ((Delta == 0) ? RwgStableAnyStage + 1 : 0) : 0;
        const bool bStageReady = bStageReadable && Stage == TEXT("Ready");
        if (bStageReady && !bRwgStageReadySeen)
        {
            // First tick that reads Ready: it starts the stability count at 0 and does not count itself.
            bRwgStageReadySeen = true;
            RwgFirstReadyT = T;
            RwgStableSinceReady = 0;
        }
        else if (bStageReady)
        {
            RwgStableSinceReady = (Delta == 0) ? RwgStableSinceReady + 1 : 0;
        }
        else if (bRwgStageReadySeen)
        {
            // A non-Ready or unreadable read after a Ready read, before the latch: the count starts over.
            bRwgStageReadySeen = false;
            RwgFirstReadyT = -1.0;
            RwgStableSinceReady = 0;
            if (!bRwgStageRegressLogged)
            {
                bRwgStageRegressLogged = true;
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("RWGWAIT: stageRegressed=1 t=%.0fs progressStage='%s' nodes=%d; firstReadyT and stableSinceReady reset to none and 0"),
                    T, *StageShown, Nodes);
            }
        }
        RwgUnreadableTicks = bStageReadable ? 0 : RwgUnreadableTicks + 1;
        if (!bRwgUnreadableWarned && RwgUnreadableTicks >= NodeShuffleMapMode::RwgUnreadableWarnTicks)
        {
            bRwgUnreadableWarned = true;
            UE_LOG(LogNodeShuffle, Warning,
                TEXT("RWGWAIT: progress stage unreadable for %d ticks; generatorFound=%d; NodeShuffle does not roll on this map while the stage stays unreadable"),
                RwgUnreadableTicks, RwgGenerator.IsValid() ? 1 : 0);
        }
        const int32 Strict = RwgCountStrictNodes();
        // Floor: a new game uses IsWorldReadyForRoll (strict /Game/ solid count, the vanilla gate, called as
        // is); a save that already holds a layout uses the any-node count mirror (brief §1).
        bRwgFloorByAnyNode = bLayoutGenerated;
        const bool bFloor = bLayoutGenerated ? RwgAnyLoadedNodesAtFloor() : IsWorldReadyForRoll();
        const bool bStable = RwgStableSinceReady >= NodeShuffleMapMode::RwgStableTicksRequired;
        const TCHAR* GatedBy = !bStageReadable ? TEXT("unreadable") : !bStageReady ? TEXT("stageNotReady")
            : !bStable ? TEXT("unstable") : !bFloor ? TEXT("floor") : TEXT("none");
        // READINESS V2 (clause A). Every clause needs the stage read; there is no quiescence-only path for a NEW game.
        const bool bClauseA = bStageReadable && bStageReady && bStable && bFloor;
        // RWG P2b clause B (apply a SAVED layout only, never a roll): layout exists AND count unchanged for
        // RwgApplyStableTicks ticks at any stage AND count >= RwgMinNodes. Brief "R2 measurements" (reload unmeasured).
        const bool bClauseB = bLayoutGenerated && RwgStableAnyStage >= NodeShuffleMapMode::RwgApplyStableTicks
            && Nodes >= NodeShuffleMapMode::RwgMinNodes;
        const bool bReadyNow = bClauseA || bClauseB;
        if (!bReadyNow)
        {
            if ((RwgObserveTicks % NodeShuffleMapMode::RwgWaitEveryTicks) == 1)
            {
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("RWGWAIT: t=%.0fs realT=%.0fs nodes=%d strict=%d fracking=%d deposits=%d delta=%d stageReady=%d firstReadyT=%.0f stableSinceReady=%d/%d genState='%s' progressStage='%s' contentSpawned=%d fillPct=%d floorBy=%s gatedBy=%s savedLayout=%d stableAnyStage=%d/%d"),
                    T, RealT, Nodes, Strict, Fracking, Deposits, Delta, bStageReady ? 1 : 0, RwgFirstReadyT,
                    RwgStableSinceReady, NodeShuffleMapMode::RwgStableTicksRequired, *GenShown, *StageShown,
                    ContentSpawned, FillPct, bLayoutGenerated ? TEXT("anyNode") : TEXT("strict"), GatedBy,
                    bLayoutGenerated ? 1 : 0, RwgStableAnyStage, NodeShuffleMapMode::RwgApplyStableTicks);
            }
            if (!bRwgWaitWarned && T >= NodeShuffleMapMode::RwgWaitWarnSeconds)
            {
                bRwgWaitWarned = true;
                UE_LOG(LogNodeShuffle, Warning,
                    TEXT("RWGWAIT: readiness predicate still false at t=%.0fs; nodes=%d strict=%d stableSinceReady=%d/%d progressStage='%s' gatedBy=%s; no roll and no layout apply on this map while it stays false"),
                    T, Nodes, Strict, RwgStableSinceReady, NodeShuffleMapMode::RwgStableTicksRequired, *StageShown, GatedBy);
            }
            return;
        }

        bRwgReady = true;
        RwgReadyBy = bClauseA ? TEXT("stage") : TEXT("savedLayoutStable");
        RwgReadyTick = RwgObserveTicks;
        RwgReadyNodeCount = Nodes;
        // Extent for the P2b trace window / deal box (brief §1 extent fix): 2nd-98th percentile box, Z range and
        // mean over STRICT nodes only (/Game/ solid, the IsWorldReadyForRoll predicate); the box over every
        // non-ours node is printed beside it for comparison (brief §4: a >2 % difference decides a P2b filter).
        TArray<float> Xs, Ys, AXs, AYs;
        double ZSum = 0.0;
        RwgZMin = TNumericLimits<float>::Max();
        RwgZMax = TNumericLimits<float>::Lowest();
        for (TActorIterator<AFGResourceNodeBase> It(World); It; ++It)
        {
            AFGResourceNodeBase* Node = *It;
            if (!IsValid(Node) || NodeShuffleMapMode::IsOursForRwg(Node)) { continue; }
            const FVector L = Node->GetActorLocation();
            AXs.Add(L.X);
            AYs.Add(L.Y);
            const AFGResourceNode* Rn = Cast<AFGResourceNode>(Node);
            if (!Rn || !IsEligibleVanillaNode(Rn, false, false)) { continue; }
            Xs.Add(L.X);
            Ys.Add(L.Y);
            ZSum += L.Z;
            RwgZMin = FMath::Min(RwgZMin, static_cast<float>(L.Z));
            RwgZMax = FMath::Max(RwgZMax, static_cast<float>(L.Z));
        }
        if (Xs.Num() == 0) { RwgZMin = RwgZMax = 0.f; }
        RwgMeanZ = Xs.Num() > 0 ? static_cast<float>(ZSum / Xs.Num()) : 0.f;
        Xs.Sort();
        Ys.Sort();
        AXs.Sort();
        AYs.Sort();
        const auto Pct = [](const TArray<float>& S, float P) -> float
        {
            return S.Num() == 0 ? 0.f : S[FMath::Clamp(FMath::RoundToInt(P * (S.Num() - 1)), 0, S.Num() - 1)];
        };
        const auto BoxKm = [&Pct](const TArray<float>& S) { return (Pct(S, 0.98f) - Pct(S, 0.02f)) / 100000.f; };
        const float MovedM = (Pawn && bRwgPawnFirstSet)
            ? static_cast<float>(FVector::Dist(Pawn->GetActorLocation(), RwgPawnFirst) / 100.0) : -1.f;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RWGREADY: by=%s t=%.1fs realT=%.1fs firstReadyT=%.1fs nodes=%d strict=%d fracking=%d deposits=%d stableSinceReady=%d genState='%s' progressStage='%s' contentSpawned=%d fillPct=%d floorBy=%s zMin=%.0f zMax=%.0f meanZ=%.0f strictInExtent=%d boxKmStrict=%.1fx%.1f boxKmAll=%.1fx%.1f allInBox=%d pawnMovedSinceFirstTickM=%.0f rollOffVanillaEnabled=%d"),
            *RwgReadyBy, T, RealT, RwgFirstReadyT, Nodes, Strict, Fracking, Deposits, RwgStableSinceReady, *GenShown, *StageShown,
            ContentSpawned, FillPct, bLayoutGenerated ? TEXT("anyNode") : TEXT("strict"),
            RwgZMin, RwgZMax, RwgMeanZ, Xs.Num(), BoxKm(Xs), BoxKm(Ys), BoxKm(AXs), BoxKm(AYs), AXs.Num(), MovedM,
            NodeShuffleMapMode::bRwgRollEnabled ? 1 : 0);
        RwgLogReadyDiagnostics();
        return;
    }

    // Post-ready, observe-only: a 60 s census counted from the latch, plus the §3 re-probes for 10 min.
    const int32 SinceReady = RwgObserveTicks - RwgReadyTick;
    if ((SinceReady % NodeShuffleMapMode::RwgObserveEveryTicks) != 0) { return; }
    int32 Nodes = 0, Fracking = 0, Deposits = 0;
    RwgCountNodes(Nodes, Fracking, Deposits);
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGOBSERVE: t=%.0fs realT=%.0fs nodes=%d fracking=%d deposits=%d changeSinceReady=%d genState='%s' progressStage='%s' contentSpawned=%d fillPct=%d sessionWaterCells=%d sessionCaveCells=%d"),
        T, RealT, Nodes, Fracking, Deposits, Nodes - RwgReadyNodeCount, *GenShown, *StageShown,
        ContentSpawned, FillPct, WaterGrid.Num(), CaveFloors.Num());
    RwgLogCollision(/*bEmitWater=*/false);
    if (SinceReady > 0 && SinceReady <= NodeShuffleMapMode::RwgReprobeWindowTicks) { RwgLogReprobes(/*bFull=*/false); }
}
