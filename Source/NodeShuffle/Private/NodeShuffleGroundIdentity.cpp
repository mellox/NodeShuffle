// ns-t45-verticaldiag: implementation of the shared emitters. See NodeShuffleGroundIdentity.h for
// why they exist. Display-only; nothing here traces, decides, gates or writes.

#include "NodeShuffleGroundIdentity.h"

#include "NodeShuffle.h"
#include "NodeShuffleSubsystem.h" // FNodeShuffleCaveCellReading

#include "Engine/HitResult.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

void LogGroundTraceHitIdentity(const TCHAR* Prefix, const FString& Tag, const FVector& ProbedFrom,
                               bool bTraceHit, const FHitResult& Hit)
{
    if (!bTraceHit)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("%s: GROUND-TRACE HIT IDENTITY for %s -- the long downward ground trace from %s reported ")
            TEXT("no blocking hit, so there is no actor, no component and no distance to name for it. This ")
            TEXT("line reports that the trace returned nothing; it does not say why."),
            Prefix, *Tag, *ProbedFrom.ToCompactString());
        return;
    }

    const AActor* HitActor = Hit.GetActor();
    const UPrimitiveComponent* HitComp = Hit.GetComponent();
    const FString ActorName = HitActor ? HitActor->GetName()
                                       : FString(TEXT("<none: the hit carried no actor>"));
    const FString ActorClass = HitActor ? HitActor->GetClass()->GetName()
                                        : FString(TEXT("<none: the hit carried no actor>"));
    const FString CompName = HitComp ? HitComp->GetName()
                                     : FString(TEXT("<none: the hit carried no component>"));
    const FString CompClass = HitComp ? HitComp->GetClass()->GetName()
                                      : FString(TEXT("<none: the hit carried no component>"));

    UE_LOG(LogNodeShuffle, Display,
        TEXT("%s: GROUND-TRACE HIT IDENTITY for %s -- the long downward ground trace from %s ended on ")
        TEXT("actor '%s' of class '%s', component '%s' of class '%s'. Its impact point is %s, which is ")
        TEXT("%.0f cm from the point the trace was run from and %+.0f m from that point in Z. WHY THIS ")
        TEXT("LINE EXISTS: the landing point of this trace has repeatedly sat metres above the point ")
        TEXT("probed, and a gap whose owner is not named is the same reading whether the surface above ")
        TEXT("is a cavern roof, an overhanging rock or a landscape section. NAMING ONLY: this reports ")
        TEXT("which actor and component that one blocking hit belonged to and how far away it was. It ")
        TEXT("makes no claim about what kind of place this is and states no cause; any reading of the ")
        TEXT("class names above is the reader's and not this line's."),
        Prefix, *Tag, *ProbedFrom.ToCompactString(),
        *ActorName, *ActorClass, *CompName, *CompClass,
        *Hit.ImpactPoint.ToCompactString(),
        FVector::Dist(ProbedFrom, Hit.ImpactPoint),
        (Hit.ImpactPoint.Z - ProbedFrom.Z) / 100.0);
}

void LogCaveStoreReading(const TCHAR* Prefix, const FString& Tag,
                         const FNodeShuffleCaveCellReading& Cave)
{
    // ONE state word per reading, and it appears in this file exactly once each -- as its own value,
    // never inside the legend prose. docs/TECH-DEBT.md: a legend that spells its own field's value made
    // a bare grep match every line regardless of value, three times in this project already.
    const TCHAR* StateWord = TEXT("LOOKUP-NOT-RUN");
    if (Cave.bRan)
    {
        if (!Cave.bCellPresent) { StateWord = TEXT("CELL-ABSENT"); }
        else if (Cave.CellState == 1) { StateWord = TEXT("CELL-PRESENT-FRONTIER"); }
        else if (Cave.CellState == 2) { StateWord = TEXT("CELL-PRESENT-EXPANDED"); }
        else if (Cave.CellState == 4) { StateWord = TEXT("CELL-PRESENT-MOUTH"); }
        else { StateWord = TEXT("CELL-PRESENT-OTHERSTATE"); }
    }

    FString CellDetail;
    if (!Cave.bRan)
    {
        CellDetail = TEXT("No key was computed and no lookup was made, so nothing below describes a cell.");
    }
    else if (Cave.bCellPresent)
    {
        const FString Ceiling = (Cave.CellCeilingCm < 0.0)
            ? FString::Printf(TEXT("a stored ceiling clearance of %.0f cm, which is the store's own ")
                              TEXT("unknown-value sentinel and is treated by its own placement pick as ")
                              TEXT("tall"), Cave.CellCeilingCm)
            : FString::Printf(TEXT("a stored ceiling clearance of %.0f cm above that floor"),
                              Cave.CellCeilingCm);
        CellDetail = FString::Printf(
            TEXT("The cell held at that key carries the raw state value %d, a recorded floor at Z=%.0f ")
            TEXT("with the probed point standing %+.0f m above that floor, and %s."),
            static_cast<int32>(Cave.CellState), Cave.CellFloorZ,
            Cave.PointAboveCellFloorCm / 100.0, *Ceiling);
    }
    else
    {
        CellDetail = TEXT("No cell is held at that key, so the store records no floor, no state value and ")
                     TEXT("no ceiling clearance for this point.");
    }

    UE_LOG(LogNodeShuffle, Display,
        TEXT("%s: CAVE-STORE READING for %s: %s. The probed point %s falls in cave-store cell %d,%d ")
        TEXT("(cells are %.0f cm square in XY; that cell's centre in XY is %s). %s The store held %d ")
        TEXT("cell(s) and %d underground seed(s) when this reading was taken, and it %s already loaded ")
        TEXT("before this command asked for it. WHAT THIS CAN AND CANNOT SAY: it reports what the mod's ")
        TEXT("OWN store HOLDS at this key and nothing more. That store is grown by the mod's own ")
        TEXT("flood-fill outward from proven seeds, so a key it does not hold is a key the fill has not ")
        TEXT("reached -- which this reading cannot tell apart from a point that is in no cavern at all, ")
        TEXT("and a zero total above means it holds nothing anywhere yet. The state word after the tag ")
        TEXT("is one of six: three name the stored state of a cell that IS held at this key (an ")
        TEXT("expandable one, a fully expanded one, or one at a cavern's walkable edge), one covers a ")
        TEXT("cell held at some other raw state value, one says the store holds no cell at this key, ")
        TEXT("and one says the lookup was not made. NOTHING PLACES OR REFUSES ANYTHING ON THIS READING: ")
        TEXT("it is computed for this log line and no code reads it. It states no cause."),
        Prefix, *Tag, StateWord,
        *Cave.Point.ToCompactString(), Cave.CellX, Cave.CellY,
        Cave.CellSizeCm, *Cave.CellCentre.ToCompactString(),
        *CellDetail, Cave.StoreCellsTotal, Cave.StoreSeedCount,
        Cave.bStoreLoadedBeforeThisCall ? TEXT("WAS") : TEXT("was NOT"));
}

void LogCaveStoreGroupSummary(const TCHAR* Prefix, const FString& GroupTag, int32 Probed,
                              int32 Present, int32 Absent, int32 StoreTotal)
{
    UE_LOG(LogNodeShuffle, Display,
        TEXT("%s: group '%s' cave-store summary -- of the %d point(s) probed, the mod's own cave store ")
        TEXT("holds a cell at %d of them and holds none at %d. The store held %d cell(s) in total when ")
        TEXT("the last lookup of this run read it back. THIS IS A LOOKUP, NOT A TEST: the store is grown ")
        TEXT("by the mod's own flood-fill outward from proven seeds, so a point the store holds nothing ")
        TEXT("for is a point the fill has not reached, which this summary cannot tell apart from a point ")
        TEXT("that is in no cavern. A total of 0 means the store holds nothing anywhere and both columns ")
        TEXT("are then vacuous; a total of -1 means no lookup ran at all on this run. NO PLACEMENT ")
        TEXT("BEHAVIOUR READS EITHER COLUMN. This line counts lookups; it states no cause."),
        Prefix, *GroupTag, Probed, Present, Absent, StoreTotal);
}
