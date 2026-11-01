// Packet rwg-pinnedspawn-1 (2026-11-01), invariant I3: THE PINNED-ENTRY HEAL.
//
// WHAT IT REPAIRS. A pinned layout entry (a player built a miner on its node) that has no live actor at
// load is spawned by EnsureNewNodeSpawned when a player comes within the spawn radius. Before this packet
// that path's occupancy gate refused any non-foundation building within 6 m, with no pinned exemption, so
// the entry's OWN miner tripped it; after MaxOverlapNudges (8) refusals the give-up wrote bActive=false and
// bRayCasted=false and the node was gone for every later session (measured 2026-11-01, forensics Q6:
// iron entry 05E3DB0F... and a SAM entry). I1 stops new drops; this function returns the dropped ones.
//
// WHICH SITES CAN PRODUCE bPinned && !bActive (premise P1, checked by reading every bActive=false write):
// only the two spawn-path gates in EnsureNewNodeSpawned -- the overlap give-up and the INSIDEGATE refusal
// (the latter reachable only while NodeShuffle.InsideGateRefuse is set). Every roll/deal site skips
// pinned entries or forces them active, so no legitimate inactive pinned entry exists to protect.
//
// WHAT IT RESTORES, AND IN WHICH DIRECTION. Both gates stamp bActive=false and bRayCasted=false. Both are
// reached only after the settle block has set bRayCasted=true, and both pin sites (ApplyLayout's occupancy
// pin and the adopt path) write bRayCasted=true with Location locked to the node. So the heal writes
// bActive=true, bRayCasted=TRUE and OverlapNudges=0: the entry spawns at its locked Location without a
// re-settle that could move it off its miner. Location and every other field are left as they are.
//
// ONCE PER LOAD. Latched only after it has seen a non-empty Layout, so a pass that runs before the save
// data arrives does not consume it. A pinned entry that a gate drops again in the same session (the node
// branch or enclosure gate can still strike a pinned entry; see the handoff) stays dropped until the next
// load, which bounds any refuse/heal cycle to one per load.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffle.h"

void ANodeShuffleSubsystem::HealDeactivatedPinnedEntriesOnce()
{
    if (bPinnedHealDone || Layout.Num() == 0)
    {
        return;
    }
    bPinnedHealDone = true;

    int32 PinnedTotal = 0;
    int32 PinnedInactiveBefore = 0;
    int32 Reactivated = 0;
    int32 ActivePinnedStrikesCleared = 0; // pinned entries already active that carried a nonzero strike count
    for (FNodeShuffleEntry& E : Layout)
    {
        if (!E.bPinned)
        {
            continue;
        }
        ++PinnedTotal;
        if (E.bActive)
        {
            // A strike count left on an active pinned entry by an earlier build would let a single later
            // node-branch or enclosure refusal reach the give-up at once; clear it (counted, not logged per entry).
            if (E.OverlapNudges > 0)
            {
                E.OverlapNudges = 0;
                ++ActivePinnedStrikesCleared;
            }
            continue;
        }
        ++PinnedInactiveBefore;
        const int32 StrikesBefore = E.OverlapNudges;
        const int32 RayCastedBefore = E.bRayCasted ? 1 : 0;
        E.bActive = true;
        E.bRayCasted = true;
        E.OverlapNudges = 0;
        ++Reactivated;
        UE_LOG(LogNodeShuffle, Display,
            TEXT("PINNEDHEAL-ENTRY guid=%s res=%s at=%s newNode=%d strikesBefore=%d rayCastedBefore=%d"),
            *E.EntryGuid.ToString(), *E.AssignedResourceClassPath, *E.Location.ToCompactString(),
            E.bIsNewNode ? 1 : 0, StrikesBefore, RayCastedBefore);
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("PINNEDHEAL reactivated=%d pinnedTotal=%d pinnedInactiveBefore=%d activePinnedStrikesCleared=%d layoutEntries=%d"),
        Reactivated, PinnedTotal, PinnedInactiveBefore, ActivePinnedStrikesCleared, Layout.Num());
}
