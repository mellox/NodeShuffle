#pragma once

// ns-scanfilter-1 (2026-11-14): per-resource SHOW/HIDE switches for MODDED resources in the resource scanner wheel.
// Handoff: _team/test-logs/2026-11-14-nodeshuffle-scanfilter-1-handoff.md. Route: ...-nodeshuffle-scanfilter-route.md.
//
// User decisions 2026-11-14 ("Scanner 1a, 2a and on means to show, 3a"): one switch per resource (on = shown); a new row
// starts HIDDEN for a resource under All Minable's mount and SHOWN for every other mod; a change applies in a running game.
// ns-scanfilter-2 (user decision 2026-11-14, "Yes, ON always shows"): an ON row also PUTS its resource in the scanner's
// list when the game's list lacks it. Still nothing is written to the save and no scanner unlock is made.
//
// THREE PARTS:
//   HOOK   FilterScannableResources -- the body of the SML hook on AFGResourceScanner::GetScannableResources (installed
//          in NodeShuffle.cpp). The original runs first; hidden MODDED classes are removed, everything else keeps its
//          original order, then ON rows the list lacks are appended in row order (classes resolved by Tick, held weakly).
//          Reads only the module-static snapshot below: no config walk, no class resolve, no disk, no config write.
//          No snapshot for the caller's world -> the list is returned untouched.
//   TICK   ANodeShuffleSubsystem::ScanFilterTick -> Tick: from RefreshTick (authority, every TickIntervalSeconds = 5 s),
//          adds rows for the union of (a) managed modded resources of the layout and (b) modded classes the hook saw
//          without a row, then rebuilds the snapshot from the LIVE config tree.
//   ROWS   ns-scanfilter-4: the switch is the 'Show in scanner' field of the protection rows (key ProtectedForeignResources);
//          the old ScannerResourceVisibility array is registered hidden, read once by the carry-over, then emptied.

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class AFGResourceScanner;
class UFGResourceDescriptor;
class UConfigPropertySection;

namespace NodeShuffleScanFilter
{
    // THE BASE GAME'S CONTENT ROOT. The same literal the Foreign classification (NodeShuffle.cpp) and the knowledge pass
    // (UnlockModdedScannerKnowledge) test; "/Game/" as the base game's content root is the engine mount convention (assumed,
    // same assumption those two rest on).
    extern const TCHAR* const BaseGameContentRoot;
    // The base game's own native module. A descriptor class defined in C++ by the game has a path under it, which is
    // outside /Game/ but is still base game, so it is never filterable either.
    extern const TCHAR* const BaseGameScriptRoot;
    // user decision 2026-11-14: a new row for a resource under this mount starts HIDDEN; every other mod starts SHOWN.
    extern const TCHAR* const AllMinableMountRoot;

    // Rows beyond this are not added; a resource with no row still gets the default rule in the hook.
    constexpr int32 MaxRows = 256;
    // Full SCANFILTER-CALL lines per session before the census takes over, and the census period.
    constexpr int32 FullCallLines = 20;
    constexpr double CallCensusPeriodSeconds = 5.0;

    // Modded = filterable. Predicate: the path is non-empty, does NOT start with BaseGameContentRoot, and does NOT start
    // with BaseGameScriptRoot. Everything else (vanilla, base-game native, empty) is never hidden and never gets a row.
    bool IsModdedPath(const FString& ClassPath);
    // The hook's rule for a modded resource with no row (ns-scanfilter-4: a NEW row's value is StartingShowValue, below): hidden under
    // AllMinableMountRoot, shown otherwise.
    bool DefaultShownForPath(const FString& ClassPath);

    // The hook body. InOutList holds the original's result on entry. Returns true when it removed or added anything.
    bool FilterScannableResources(const AFGResourceScanner* Scanner, TArray<TSubclassOf<UFGResourceDescriptor>>& InOutList);

    // ns-scanfilter-3 (2026-11-14): the scanner's closest-cluster pick asks the unlock subsystem's IsNodeScannable about each
    // cluster, and that searches the SAVED unlock list, which hook-added resources are not on. The GetClosetClusters hook
    // (NodeShuffleScanDiag.cpp) brackets the game's original call with BeginClosestPick / EndClosestPick; the IsNodeScannable
    // hook (NodeShuffle.cpp) asks ShouldAnswerScannable whether to turn the game's false into true. Game thread only.
    void BeginClosestPick(const AFGResourceScanner* Scanner);
    void EndClosestPick();
    // True only when: inside the pick (flag set), a snapshot exists for the pick's world, the class's OWN path is modded and its
    // row is ON, and the game said false. Never asked to turn true into false. Counts every question for SCANFILTER-PICK.
    bool ShouldAnswerScannable(UClass* ResourceClass, bool bGameAnswer);
    // One SCANFILTER-PICK line per pick, after the original returned.
    void EmitPickLine(const AFGResourceScanner* Scanner, int32 ClustersReturned);

    // The periodic pass (game thread, authority). ManagedModdedPaths = source (a). bModEnabled = the config's master
    // 'Enabled': false withdraws the snapshot, so the hook returns every list untouched while the mod is switched off.
    void Tick(UObject* WorldContext, const TArray<FString>& ManagedModdedPaths, bool bModEnabled);

    // Called from UNodeShuffleConfig::PostInitProperties (OTHER MODS group). ns-scanfilter-4: registers the RETIRED list (hidden,
    // emptied by the one-time carry-over) and the hidden carry-over flag; the live switches are a field of the protection rows.
    void RegisterConfigRows(UConfigPropertySection* Root, UClass* ArrayClass, UClass* SectionClass, UClass* StringClass,
        UClass* BoolClass);

    // ---- ns-scanfilter-4 (2026-11-14, user: "Merge anyway"): ONE list. The 'Show in scanner' switch is a field of the
    // ProtectedForeignResources rows (NodeShuffleConfig.cpp adds it to that row template through this function).
    void AddShowFieldToRowTemplate(UConfigPropertySection* RowTemplate, UClass* BoolClass);
    // The starting value of a NEW row's switch, the same whichever side creates the row: ON only when the path is modded, not
    // under AllMinableMountRoot, and (NodeShuffle shuffles it in this world, OR it is on the unlock subsystem's scannable list
    // as last read by Tick, OR the hook saw it in the scanner's list). False when Tick has not read this world yet.
    // OutBasis names the branch that decided (a literal, for the log).
    bool StartingShowValue(const UObject* WorldContext, const FString& Path, const TCHAR** OutBasis);
    // The in-game item name when the class is already in memory (ResolveClass never loads) and is a resource descriptor.
    bool InGameNameIfLoaded(const FString& Path, FString& OutName);
}
