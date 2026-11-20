// Packet rwg-thoriumclass-1 (2026-11-01): the two REPCLASS diagnostics this packet adds. Both REPORT what they measured
// and assert no cause; every field is named by the predicate that produced it.
//
//   REPCLASS-CLEARANCE  one Display line per (resource, role, class) at roll time, emitted from the REPCLASS census in
//                       RollLayout: the chosen role for the class the roll spawns, the originals and registry roles when the
//                       originals' class or the registry candidate differs from it, and the control role once per roll for the
//                       vanilla solid representative (the class on which Mk1 binds were measured 2026-11-01). Prints the
//                       class CDO's clearance boxes (the private AFGResourceNodeBase::mClearanceData, reached through the
//                       Config/AccessTransformers.ini friend grant -- hence a subsystem member), the native box component's
//                       extent / collision profile / collision mode, and every primitive component template the Blueprint
//                       (and its Blueprint parents) adds through its SimpleConstructionScript, since those never sit on the
//                       CDO. Never loads a class: an unloaded class prints a zero resident flag and nothing else.
//
//   REPCLASS-STALE      once per load, from PostLoadGame_Implementation: how many active spawned entries carry a stamped
//                       spawn class (RepClassSpawnPath / SpawnNodeClassPath / NodeClassPath, read through the same resolver
//                       the spawner uses) that differs from the class the rwg-thoriumclass-1 rule would now choose. The rule
//                       is reconstructed from the saved Layout alone: every entry that records an original (VanillaNodePath
//                       non-empty) still carries that original's class in NodeClassPath (the restamp never writes it; lint
//                       check_repclass R8 trap i), latched per resource with the roll's own ranking. A resource whose
//                       originals' class is absent or unspawnable (tested on the RESIDENT class, as the roll does; a class not
//                       resident at load counts as unspawnable here) would consult the asset registry at roll time; that is
//                       not run at load, so such entries are counted as unresolved, not as stale. COUNT ONLY -- no entry is
//                       restamped here; the saved classes change only when a re-roll recomputes them.
//
// Packet rwg-thoriumclass-2 (2026-11-01):
//   DRESS-NATIVE        once per entry per session, from the spawn and adopt native-visual branches: a modded-origin node that
//                       keeps a mesh of its own class while that class is not the assigned resource's own (the own-class branch
//                       runs first and never reaches here). Names the site, the node, the assigned resource, the node's class and
//                       the mesh it shows (NodeHasOwnVisual's search: the first static-mesh component with a mesh, our rock
//                       excluded, else the linked mesh actor). Measured 2026-11-01: 22 lithium nodes wearing lead_C / Res_*2_C
//                       meshes printed only at Verbose before this line existed.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffle.h"
#include "NodeShuffleRepClass.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceNodeBase.h"
#include "FGClearanceData.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Misc/PackageName.h"

static FString NsRepClassVec(const FVector& V)
{
    return FString::Printf(TEXT("(%.0f,%.0f,%.0f)"), V.X, V.Y, V.Z);
}

static FString NsRepClassBox(const FBox& B)
{
    if (!B.IsValid) { return TEXT("invalid"); }
    return NsRepClassVec(B.Min) + TEXT("..") + NsRepClassVec(B.Max);
}

void ANodeShuffleSubsystem::EmitRepClassClearanceLine(const FString& ResourcePath, const TCHAR* RoleName, const FString& ClassPath) const
{
    const UClass* Cls = FindObject<UClass>(nullptr, *ClassPath);
    const AFGResourceNodeBase* Cdo = (Cls && Cls->IsChildOf(AFGResourceNodeBase::StaticClass()))
        ? Cls->GetDefaultObject<AFGResourceNodeBase>() : nullptr;

    int32 ClearN = 0;
    FString Clear = TEXT("[]");
    FString BoxExtent = TEXT("none");
    FString BoxProfile = TEXT("none");
    int32 BoxCollision = -1; // ECollisionEnabled::Type of the native box component; -1 when the CDO has none
    if (Cdo)
    {
        ClearN = Cdo->mClearanceData.Num();
        TArray<FString> Parts;
        for (const FFGClearanceData& D : Cdo->mClearanceData)
        {
            Parts.Add(FString::Printf(TEXT("T%d:%s@%s"), (int32)D.Type, *NsRepClassBox(D.ClearanceBox), *NsRepClassVec(D.RelativeTransform.GetLocation())));
        }
        Clear = TEXT("[") + FString::Join(Parts, TEXT(";")) + TEXT("]");
        if (const UBoxComponent* Box = Cdo->mBoxComponent.Get())
        {
            BoxExtent = NsRepClassVec(Box->GetUnscaledBoxExtent());
            BoxProfile = Box->GetCollisionProfileName().ToString();
            BoxCollision = (int32)Box->GetCollisionEnabled();
        }
    }

    // Blueprint-added primitive templates (this class and its Blueprint parents): Class:Profile:CollisionEnabled, plus the
    // unscaled extent for a box. These are instanced at spawn and are absent from the CDO, so the CDO read above misses them.
    int32 ScsPrims = 0;
    TArray<FString> ScsParts;
    for (const UClass* C = Cls; C; C = C->GetSuperClass())
    {
        const UBlueprintGeneratedClass* Bpgc = Cast<UBlueprintGeneratedClass>(C);
        if (!Bpgc || !Bpgc->SimpleConstructionScript) { continue; }
        for (const USCS_Node* N : Bpgc->SimpleConstructionScript->GetAllNodes())
        {
            const UPrimitiveComponent* P = N ? Cast<UPrimitiveComponent>(N->ComponentTemplate.Get()) : nullptr;
            if (!P) { continue; }
            ++ScsPrims;
            FString Extra;
            if (const UBoxComponent* B = Cast<UBoxComponent>(P)) { Extra = TEXT("x") + NsRepClassVec(B->GetUnscaledBoxExtent()); }
            ScsParts.Add(FString::Printf(TEXT("%s:%s:%d%s"), *P->GetClass()->GetName(), *P->GetCollisionProfileName().ToString(), (int32)P->GetCollisionEnabled(), *Extra));
        }
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("REPCLASS-CLEARANCE res=%s role=%s class=%s resident=%d nodeBase=%d clearN=%d clear=%s boxExtent=%s boxProfile=%s boxCollision=%d scsPrims=%d scs=[%s]"),
        *FPackageName::ObjectPathToObjectName(ResourcePath), RoleName, *FPackageName::ObjectPathToObjectName(ClassPath),
        Cls ? 1 : 0, Cdo ? 1 : 0, ClearN, *Clear, *BoxExtent, *BoxProfile, BoxCollision, ScsPrims, *FString::Join(ScsParts, TEXT(";")));
}

void ANodeShuffleSubsystem::EmitRepClassStaleCensusOnce()
{
    if (bRepClassStaleCensusDone || !bLayoutGenerated || Layout.Num() == 0)
    {
        return;
    }
    bRepClassStaleCensusDone = true;

    // 1. The originals' class per modded resource, from the saved entries themselves, latched with the roll's ranking.
    TMap<FString, FString> OriginalsByResource;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (E.VanillaNodePath.IsEmpty() || E.OriginalResourceClassPath.IsEmpty() || E.NodeClassPath.IsEmpty()) { continue; }
        if (NodeShuffleRepClass::IsGenericNodeClassPath(E.OriginalResourceClassPath)) { continue; }
        if (FString* Existing = OriginalsByResource.Find(E.OriginalResourceClassPath))
        {
            if (NodeShuffleRepClass::RepClassShouldReplace(*Existing, E.NodeClassPath, E.OriginalResourceClassPath)) { *Existing = E.NodeClassPath; }
        }
        else
        {
            OriginalsByResource.Add(E.OriginalResourceClassPath, E.NodeClassPath);
        }
    }

    // 2. Count. Pinned stale entries are counted apart: a re-roll carries them unchanged, so they never recompute.
    int32 Spawned = 0, Modded = 0, Stale = 0, StalePinned = 0, Unresolved = 0;
    TMap<FString, int32> ByClass;
    TMap<FString, bool> SpawnableCache;
    for (const FNodeShuffleEntry& E : Layout)
    {
        if (!E.bIsNewNode || !E.bActive) { continue; }
        ++Spawned;
        if (NodeShuffleRepClass::IsGenericNodeClassPath(E.AssignedResourceClassPath)) { continue; }
        ++Modded;
        const FString* Orig = OriginalsByResource.Find(E.AssignedResourceClassPath);
        bool bSpawnable = false;
        if (Orig)
        {
            if (const bool* Cached = SpawnableCache.Find(*Orig)) { bSpawnable = *Cached; }
            else { bSpawnable = NodeShuffleRepClass::IsSpawnableNodeClass(FindObject<UClass>(nullptr, **Orig)); SpawnableCache.Add(*Orig, bSpawnable); }
        }
        if (!Orig || !bSpawnable) { ++Unresolved; continue; } // the rule would consult the registry here; not run at load
        const FString Stamped = ResolveSpawnNodeClassPath(E);
        if (Stamped == *Orig) { continue; }
        ++Stale;
        if (E.bPinned) { ++StalePinned; }
        ByClass.FindOrAdd(FPackageName::ObjectPathToObjectName(Stamped) + TEXT(">") + FPackageName::ObjectPathToObjectName(*Orig))++;
    }
    TArray<FString> Keys;
    ByClass.GetKeys(Keys);
    Keys.Sort();
    TArray<FString> Parts;
    for (const FString& K : Keys) { Parts.Add(FString::Printf(TEXT("%s:%d"), *K, ByClass.FindChecked(K))); }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("REPCLASS-STALE count=%d pinned=%d spawned=%d modded=%d unresolved=%d byClass=[%s]"),
        Stale, StalePinned, Spawned, Modded, Unresolved, *FString::Join(Parts, TEXT(";")));
}

// rwg-thoriumclass-2 (2026-11-01): DRESS-NATIVE. Diagnostics-gated like its DRESS-SKIP siblings; once per entry (NativeDressLogged)
// because the adopt branch re-runs every pass. The mesh name mirrors NodeHasOwnVisual's search so the line names what that
// predicate accepted.
void ANodeShuffleSubsystem::EmitDressNativeOnce(const TCHAR* Site, const FNodeShuffleEntry& Entry, AActor* Node, UStaticMeshComponent* ExcludeRock)
{
    if (!FNodeShuffleModule::AreDiagnosticsEnabled() || !IsValid(Node)) { return; }
    bool bAlreadyLogged = false;
    NativeDressLogged.Add(Entry.EntryGuid, &bAlreadyLogged);
    if (bAlreadyLogged) { return; }
    FString MeshName = TEXT("none");
    TInlineComponentArray<UStaticMeshComponent*> Meshes(Node);
    for (UStaticMeshComponent* MC : Meshes)
    {
        if (!IsValid(MC) || MC == ExcludeRock || MC->GetStaticMesh() == nullptr) { continue; }
        MeshName = MC->GetStaticMesh()->GetName();
        break;
    }
    if (MeshName == TEXT("none"))
    {
        if (const AFGResourceNodeBase* Base = Cast<AFGResourceNodeBase>(Node))
        {
            if (const AActor* MA = Base->mMeshActor.Get()) { if (IsValid(MA) && !MA->IsHidden()) { MeshName = FString(TEXT("actor:")) + MA->GetName(); } }
        }
    }
    UE_LOG(LogNodeShuffle, Display, TEXT("DRESS-NATIVE site=%s node=%s resource=%s nodeClass=%s nativeMesh=%s"),
        Site, *Node->GetName(), *FPackageName::ObjectPathToObjectName(Entry.AssignedResourceClassPath), *Node->GetClass()->GetName(), *MeshName);
}
