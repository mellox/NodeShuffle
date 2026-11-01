# PINNEDSPAWN lint (2026-11-01, packet rwg-pinnedspawn-1; handoff _team/test-logs/2026-11-01-nodeshuffle-pinnedspawn-handoff.md).
# A pinned entry with no live actor at load reached EnsureNewNodeSpawned's occupancy gate, whose building branch refused the
# entry's OWN miner; after 8 strikes the give-up deactivated it and the node was gone (forensics Q6). This lint pins the fix.
# LOADS: NodeShuffleSubsystem.cpp (gate, ApplyLayout call site, relink census), NodeShufflePinnedHeal.cpp (heal),
#        NodeShuffleSubsystem.h (members), NodeShuffle.cpp (marker).
#   S1 (I1) the building branch exempts a PINNED entry (continue, counted), and only after that does the unchanged refusal run;
#      the resource-node branch carries no pinned test; the OVERLAPGATE-PINNED line is once per entry per session, ungated
#   S2 (I2) both refusing branches record kind/class/path before returning true; the primary spot's hit is copied right after
#      the primary call; the OVERLAPGATE line has its exact format and arguments, is ungated, prints the pre-nudge spot, and is
#      called on the relocated visit and after the strike increment, before the give-up test
#   S3 (I3) the heal: latch only with a non-empty Layout, predicate pinned and inactive, re-arm bActive, bRayCasted (TRUE) and the
#      strike counter, exact census and per-entry formats, ungated; called once in ApplyLayout after the adopt block and before
#      the apply loop and RwgSpawnPass; members declared in the header
#   S4 (I4) EXTRACTOR-RELINK: the four branch counters partition the loop, linked is read back after SetResourceNode, the census
#      has its exact format, is change-driven and sits between the extractor loop and the portable-miner loop
#   S5 marker rwg-pinnedspawn-1 once on its own line in NodeShuffle.cpp and nowhere in the packet sources
#   S6 every new field token appears exactly once in its file (no legend echoes a field in field=value form)
# Every forbidden needle is BUILT BY CONCATENATION. Mutants run in-file. Exit 0 = all hold; 1 = a rule or mutant fails; 2 = missing file.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    heal  = 'NodeShuffle\Private\NodeShufflePinnedHeal.cpp'
    hdr   = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
}
$text = [ordered]@{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = ((Get-Content -Raw -Encoding utf8 -Path $p) -replace "`r`n", "`n")
}
function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }
function Ordered([string]$t, [string[]]$needles) {
    $at = 0
    foreach ($n in $needles) { $i = $t.IndexOf($n, $at); if ($i -lt 0) { return $false }; $at = $i + $n.Length }
    return $true
}
function Span([string]$t, [string]$a, [string]$b) {
    if ((Count $t $a) -ne 1) { return '' }
    $i = $t.IndexOf($a); $j = $t.IndexOf($b, $i + $a.Length)
    if ($j -lt 0) { return '' }
    return $t.Substring($i, $j - $i)
}
$diagGate = 'AreDiagnostics' + 'Enabled'
function TokCount([string]$t, [string]$tok) { return ([regex]::Matches($t, '(?<![A-Za-z])' + [regex]::Escape($tok))).Count }

# ---- anchors (unique context) ----
$lambdaHead  = '        auto OverlapsAt = [&](const FVector& At) -> bool'
$nodeReject  = "                    ++NodeGateCallOccupiedRejected; // ns-t35-gatereach (resource-node half)`n                    GateHitKind = TEXT(""resourceNode""); // rwg-pinnedspawn-1 I2`n                    GateHitClass = Other->GetClass()->GetName();`n                    GateHitPath = Other->GetPathName();`n                    return true;"
$bldPred     = '                        if (HitActor && HitActor->IsA<AFGBuildable>() && !HitActor->IsA<AFGBuildableFoundation>())'
$pinExempt   = "                            if (Entry.bPinned)`n                            {`n                                if (PinnedBuildingHitsIgnored == 0)"
$pinCount    = "                                ++PinnedBuildingHitsIgnored;`n                                continue;`n                            }"
$bldReject   = "                            ++NodeGateCallOccupiedRejected; // ns-t35-gatereach (buildable half)`n                            GateHitKind = TEXT(""buildable""); // rwg-pinnedspawn-1 I2`n                            GateHitClass = HitActor->GetClass()->GetName();`n                            GateHitPath = HitActor->GetPathName();`n                            return true; // a machine/wall/belt"
$primCall    = '        const bool bSpotOccupied = OverlapsAt(Entry.Location);'
$primPair    = "        if (bSpotOccupied) { ++NodeGatePrimaryOccupiedRejected; }`n        else { ++NodeGatePrimaryEnclosedReached; }"
$primCopy    = '        const FString PrimaryHitKind = bSpotOccupied ? FString(GateHitKind) : FString(TEXT("none"));'
$pinLogGuard = '        if (Entry.bPinned && PinnedBuildingHitsIgnored > 0 && !PinnedBuildingSkipLogged.Contains(Entry.EntryGuid))'
$pinLogFmt   = 'TEXT("OVERLAPGATE-PINNED entry=%s res=%s at=%s buildingOverlapHitsIgnored=%d firstBuildingClass=%s firstBuildingPath=%s occupiedAfterExemption=%d"),'
$refusedAt   = '            const FVector RefusedAt = Entry.Location; // rwg-pinnedspawn-1 I2: the refused spot, before a nudge moves it'
$spiralLoop  = '            for (int32 i = 1; i <= LandRelocationTries && !bRelocated; i++)'
$gateFmt     = 'TEXT("OVERLAPGATE refused entry=%s res=%s at=%s pinned=%d occupied=%d enclosed=%d overlapKind=%s overlapClass=%s overlapPath=%s relocated=%d strike=%d/%d"),'
$gateArgs    = "                    *Entry.EntryGuid.ToString(), *ResourceClass->GetName(), *RefusedAt.ToCompactString(),`n                    Entry.bPinned ? 1 : 0, bSpotOccupied ? 1 : 0, bSpotEnclosed ? 1 : 0,`n                    *PrimaryHitKind, *PrimaryHitClass, *PrimaryHitPath,`n                    bRelocated ? 1 : 0, static_cast<int32>(Entry.OverlapNudges), static_cast<int32>(MaxOverlapNudges));"
$lambdaLog   = '            auto LogGateRefusal = [&]()'
$relocCall   = '            if (bRelocated) { LogGateRefusal(); }'
$strikeCall  = "                Entry.OverlapNudges++;`n                LogGateRefusal(); // rwg-pinnedspawn-1 I2: after the strike, before any give-up`n                if (Entry.OverlapNudges >= MaxOverlapNudges)"
$giveUp      = 'TEXT("Overlap guard: resolved %s node at %s after %d failed nudges'
# S3
$healSig     = 'void ANodeShuffleSubsystem::HealDeactivatedPinnedEntriesOnce()'
$healGuard   = "    if (bPinnedHealDone || Layout.Num() == 0)`n    {`n        return;`n    }`n    bPinnedHealDone = true;"
$healSkip    = "        if (!E.bPinned)`n        {`n            continue;`n        }"
$healActive  = "        if (E.bActive)`n        {"
$healRearm   = "        E.bActive = true;`n        E.bRayCasted = true;`n        E.OverlapNudges = 0;`n        ++Reactivated;"
$healEntry   = 'TEXT("PINNEDHEAL-ENTRY guid=%s res=%s at=%s newNode=%d strikesBefore=%d rayCastedBefore=%d"),'
$healFmt     = 'TEXT("PINNEDHEAL reactivated=%d pinnedTotal=%d pinnedInactiveBefore=%d activePinnedStrikesCleared=%d layoutEntries=%d"),'
$healArgs    = '        Reactivated, PinnedTotal, PinnedInactiveBefore, ActivePinnedStrikesCleared, Layout.Num());'
$adoptBlock  = "        AdoptRestoredSpawnedNodes();`n        bAdoptedRestoredNodes = true;`n    }"
$healCall    = '    HealDeactivatedPinnedEntriesOnce();'
$applyLoop   = '    for (FNodeShuffleEntry& Entry : Layout)'
$rwgSpawn    = 'RwgSpawnPass(SpawnRadiusCm, bChangedWorld);'
$hdrFlag     = '    bool bPinnedHealDone = false;'
$hdrDecl     = '    void HealDeactivatedPinnedEntriesOnce();'
$hdrSkipSet  = '    TSet<FGuid> PinnedBuildingSkipLogged;'
$hdrKey      = '    FString RelinkCensusLastKey;'
# S4
$relinkSig   = 'void ANodeShuffleSubsystem::ReassociateOrphanedExtractors()'
$relinkInit  = '    int32 RelinkExtractors = 0, RelinkLinked = 0, RelinkLinkNoEffect = 0, RelinkNoNode = 0, RelinkAlreadyLinked = 0;'
$extLoop     = '    for (TActorIterator<AFGBuildableResourceExtractorBase> It(GetWorld()); It; ++It)'
$extCount    = '        ++RelinkExtractors;'
$boundCount  = "        if (BoundObj != nullptr)`n        {`n            ++RelinkAlreadyLinked; // rwg-pinnedspawn-1 I4"
$setter      = '            Extractor->SetResourceNode(Orphan);'
$readBack    = "            const bool bBoundAfter = Extractor->GetExtractableResource().GetObject() == Orphan;`n            if (bBoundAfter) { ++RelinkLinked; } else { ++RelinkLinkNoEffect; }"
$linkFmt     = 'TEXT("EXTRACTOR-RELINK-LINK extractor=%s node=%s at=%s nodeDistM=%.1f boundAfter=%d"),'
$noNodeCount = "        else`n        {`n            // rwg-pinnedspawn-1 I4: no binding and no spawned node within ExtractorSnapDistance. Named once per`n            // extractor per session (capped), so a miner standing without a node is visible in the log.`n            ++RelinkNoNode;"
$noNodeFmt   = 'TEXT("EXTRACTOR-UNBOUND-NOSPAWNEDNODE extractor=%s class=%s at=%s spawnedNodes=%d snapRadiusM=%.0f"),'
$relinkKey   = "        const FString RelinkKey = FString::Printf(TEXT(""%d|%d|%d|%d|%d""),`n            RelinkLinked, RelinkNoNode, RelinkAlreadyLinked, RelinkLinkNoEffect, RelinkExtractors);`n        if (RelinkKey != RelinkCensusLastKey)`n        {`n            RelinkCensusLastKey = RelinkKey;"
$relinkFmt   = 'TEXT("EXTRACTOR-RELINK linked=%d unboundNoSpawnedNodeInSnap=%d bindingPresent=%d linkNoEffect=%d extractors=%d spawnedNodes=%d"),'
$relinkArgs  = '                RelinkLinked, RelinkNoNode, RelinkAlreadyLinked, RelinkLinkNoEffect, RelinkExtractors, SpawnedNodes.Num());'
$portable    = '    for (TActorIterator<AFGPortableMiner> It(GetWorld()); It; ++It)'
$marker      = '2026-11-01-rwg-' + 'pinnedspawn-1'

function Invoke-PinnedSpawnChecks($T) {
    $f = @()
    $s = $T.sub; $h = $T.heal
    # S1
    if ((Count $s $bldPred) -ne 1 -or (Count $s $pinExempt) -ne 1 -or (Count $s $pinCount) -ne 1 -or (Count $s $bldReject) -ne 1) { $f += '(S1) building predicate, pinned exemption, its counted continue or the unchanged refusal is missing' }
    if (-not (Ordered $s @($lambdaHead, $bldPred, $pinExempt, $pinCount, $bldReject))) { $f += '(S1) order: building predicate -> pinned exemption (continue) -> refusal' }
    $bb = Span $s $bldPred 'return true; // a machine/wall/belt'
    if (-not $bb -or (Count $bb 'bPinned') -ne 1 -or (Count $bb ('!' + 'Entry.bPinned')) -ne 0) { $f += '(S1) the building branch must test Entry.bPinned exactly once, positively' }
    $nb = Span $s $lambdaHead $bldPred
    if (-not $nb -or (Count $nb 'bPinned') -ne 0) { $f += '(S1) the resource-node branch must carry no pinned test' }
    if ((Count $s $pinLogGuard) -ne 1 -or (Count $s '            PinnedBuildingSkipLogged.Add(Entry.EntryGuid);') -ne 1 -or (Count $s $pinLogFmt) -ne 1) { $f += '(S1) OVERLAPGATE-PINNED guard, latch or format missing' }
    $pl = Span $s $pinLogGuard '        const bool bSpotEnclosed = '
    if (-not $pl -or $pl.Contains($diagGate)) { $f += '(S1) OVERLAPGATE-PINNED must be ungated' }
    if ((Count $s '        if (Entry.bPinned && bSpotOccupied) { return; } // R1: spawn held this visit; no nudge, no strike, no drop') -ne 1 -or (Count $s '        const bool bSpotEnclosed = bSpotEnclosedRaw && !Entry.bPinned;') -ne 1) { $f += '(S1) R1: a pinned entry must hold on an occupied spot and never apply the enclosure verdict' }
    # S2
    if ((Count $s ($primCall + "`n" + $primPair + "`n")) -ne 1) { $f += '(S2) the primary census if/else pair must stay adjacent (an inserted block captures its else)' }
    if ((Count $s $bldReject) -ne 1) { $f += '(S2) the buildable refusal must record kind, class and path before returning' }
    if ((Count $s $nodeReject) -ne 1) { $f += '(S2) the resource-node refusal must record kind, class and path before returning' }
    if ((Count $s $primCall) -ne 1 -or (Count $s $primCopy) -ne 1 -or -not (Ordered $s @($primCall, $primCopy, $pinLogGuard, $refusedAt, $spiralLoop, $lambdaLog, $relocCall, $strikeCall, $giveUp))) { $f += '(S2) order: primary call -> hit copy -> pinned line -> refused spot -> spiral -> log lambda -> relocated call -> strike call -> give-up' }
    if ((Count $s $gateFmt) -ne 1 -or (Count $s ($gateFmt + "`n" + $gateArgs)) -ne 1) { $f += '(S2) OVERLAPGATE format or argument list changed' }
    if ((Count $s $relocCall) -ne 1 -or (Count $s $strikeCall) -ne 1) { $f += '(S2) OVERLAPGATE must be logged on the relocated visit and after the strike' }
    $ll = Span $s $lambdaLog $relocCall
    if (-not $ll -or $ll.Contains($diagGate) -or -not $ll.Contains('UE_LOG(LogNodeShuffle, Display,')) { $f += '(S2) OVERLAPGATE must be an ungated Display line' }
    # S3
    if ((Count $h $healSig) -ne 1 -or (Count $h $healGuard) -ne 1 -or (Count $h $healSkip) -ne 1 -or (Count $h $healRearm) -ne 1) { $f += '(S3) heal signature, non-empty latch, pinned predicate or the re-arm triple is missing' }
    if (-not (Ordered $h @($healSig, $healGuard, $healSkip, $healActive, '            continue;', '        ++PinnedInactiveBefore;', $healRearm, $healEntry, $healFmt))) { $f += '(S3) order: latch -> pinned skip -> active skip -> count -> re-arm -> entry line -> census' }
    if ((Count $h ('E.bRayCasted = ' + 'false')) -ne 0 -or (Count $h 'E.Location =') -ne 0) { $f += '(S3) the heal must not unsettle or move an entry' }
    if ((Count $h $healEntry) -ne 1 -or (Count $h $healFmt) -ne 1 -or (Count $h ($healFmt + "`n" + $healArgs)) -ne 1) { $f += '(S3) PINNEDHEAL census or entry format changed' }
    if ($h.Contains($diagGate)) { $f += '(S3) the heal lines must be ungated' }
    if ((Count $s $healCall) -ne 1 -or -not (Ordered $s @($adoptBlock, $healCall, $applyLoop, $rwgSpawn))) { $f += '(S3) one call, after the adopt block and before the apply loop and RwgSpawnPass' }
    if ((Count $T.hdr $hdrFlag) -ne 1 -or (Count $T.hdr $hdrDecl) -ne 1 -or (Count $T.hdr $hdrSkipSet) -ne 1 -or (Count $T.hdr $hdrKey) -ne 1) { $f += '(S3) header members missing' }
    $healSite = Span $s $adoptBlock $healCall
    if (-not $healSite -or $healSite.Contains($diagGate) -or $healSite.Contains('if (')) { $f += '(S3) the heal call must be unconditional, directly after the adopt block' }
    if ((Count $h ("                E.OverlapNudges = 0;`n                ++ActivePinnedStrikesCleared;")) -ne 1) { $f += '(S3) an active pinned entry must have its strike count cleared' }
    # S4
    $rb = Span $s $relinkSig 'void ANodeShuffleSubsystem::RefreshScannersAndRadarTowers()'
    if (-not $rb) { $f += '(S4) relink function not found' }
    else {
        if (-not (Ordered $rb @($relinkInit, $extLoop, $extCount, $boundCount, $setter, $readBack, $linkFmt, $noNodeCount, $noNodeFmt, $relinkKey, $relinkFmt, $portable))) { $f += '(S4) order: counters -> loop -> per-branch counts -> readback -> census -> portable loop' }
        if ((Count $rb ('++Relink' + 'Linked;')) -ne 1 -or (Count $rb $readBack) -ne 1) { $f += '(S4) linked must be counted only from the readback' }
        if ((Count $rb $relinkFmt) -ne 1 -or (Count $rb ($relinkFmt + "`n" + $relinkArgs)) -ne 1) { $f += '(S4) EXTRACTOR-RELINK format or argument list changed' }
        if ((Count $rb $relinkKey) -ne 1) { $f += '(S4) census must be change-driven on the full tuple' }
        if ((Count $rb '++RelinkNoNode;') -ne 1 -or (Count $rb '++RelinkAlreadyLinked;') -ne 1 -or (Count $rb '++RelinkExtractors;') -ne 1) { $f += '(S4) branch counters missing' }
    }
    # S5
    if ((Count $T.nsmod ('    UE_LOG(LogNodeShuffle, Display, TEXT("===== NodeShuffle packet marker (' + $marker + ')====="));')) -ne 1 -or (Count $T.nsmod $marker) -ne 1) { $f += '(S5) marker missing or not on its own line' }
    if ((Count $s $marker) -ne 0 -or (Count $h $marker) -ne 0) { $f += '(S5) marker must not appear in the packet sources' }
    # S6 (anchored on a non-letter before the token, so a legend's "(tok=" or " tok=" both count and "alreadyLinked=" is not "linked=")
    foreach ($tok in @('overlapKind=', 'overlapClass=', 'overlapPath=', 'strike=', 'buildingOverlapHitsIgnored=', 'unboundNoSpawnedNodeInSnap=', 'bindingPresent=', 'linkNoEffect=', 'boundAfter=', 'linked=')) {
        if ((TokCount $s $tok) -ne 1) { $f += "(S6) field token echoed or missing in the subsystem: $tok" }
    }
    foreach ($tok in @('reactivated=', 'pinnedTotal=', 'pinnedInactiveBefore=', 'strikesBefore=')) {
        if ((TokCount $h $tok) -ne 1) { $f += "(S6) field token echoed or missing in the heal: $tok" }
    }
    return ,$f
}

$base = Invoke-PinnedSpawnChecks $text
if ($base.Count -gt 0) { $base | ForEach-Object { Write-Host "FAIL$_" }; Write-Host "PINNEDSPAWN lint: FAIL -- baseline $($base.Count) problem(s)"; exit 1 }
Write-Host 'BASELINE GREEN: S1..S6 hold across 4 loaded files'

$mutants = @(
    @{ Name = 'X1 exempt only when NOT pinned (sign flip)'; Key = 'sub'; Expect = 'S1'; Find = $pinExempt; Repl = $pinExempt.Replace('if (Entry.bPinned)', 'if (' + '!Entry.bPinned)') }
    @{ Name = 'X2 exemption removed'; Key = 'sub'; Expect = 'S1'; Find = $pinCount; Repl = "                                ++PinnedBuildingHitsIgnored;`n                            }" }
    @{ Name = 'X3 pinned exemption added to the resource-node branch'; Key = 'sub'; Expect = 'S1'; Find = '                if (FVector::DistSquared(Other->GetActorLocation(), At) < RejSq)'; Repl = '                if (!Entry.bPinned && FVector::DistSquared(Other->GetActorLocation(), At) < RejSq)' }
    @{ Name = 'X4 non-pinned refusal dropped (everyone exempt)'; Key = 'sub'; Expect = 'S1'; Find = '                            if (Entry.bPinned)' + "`n"; Repl = '                            if (true)' + "`n" }
    @{ Name = 'X5 pinned line gated'; Key = 'sub'; Expect = 'S1'; Find = $pinLogGuard; Repl = $pinLogGuard.Replace('if (Entry.bPinned', 'if (FNodeShuffleModule::' + $diagGate + '() && Entry.bPinned') }
    @{ Name = 'X28 block inserted between the primary census if and its else'; Key = 'sub'; Expect = 'S2'; Find = $primPair; Repl = "        if (bSpotOccupied) { ++NodeGatePrimaryOccupiedRejected; }`n        if (Entry.bPinned) { }`n        else { ++NodeGatePrimaryEnclosedReached; }" }
    @{ Name = 'X29 pinned entry applies the enclosure verdict (R1 dropped)'; Key = 'sub'; Expect = 'S1'; Find = '        const bool bSpotEnclosed = bSpotEnclosedRaw && !Entry.bPinned;'; Repl = '        const bool bSpotEnclosed = bSpotEnclosedRaw;' }
    @{ Name = 'X6 buildable hit not recorded'; Key = 'sub'; Expect = 'S2'; Find = "                            GateHitPath = HitActor->GetPathName();`n"; Repl = '' }
    @{ Name = 'X7 resource-node hit not recorded'; Key = 'sub'; Expect = 'S2'; Find = "                    GateHitPath = Other->GetPathName();`n"; Repl = '' }
    @{ Name = 'X8 OVERLAPGATE drops overlapPath'; Key = 'sub'; Expect = 'S2'; Find = '*PrimaryHitKind, *PrimaryHitClass, *PrimaryHitPath,'; Repl = '*PrimaryHitKind, *PrimaryHitClass, TEXT("-"),' }
    @{ Name = 'X9 strike visit not logged'; Key = 'sub'; Expect = 'S2'; Find = "                LogGateRefusal(); // rwg-pinnedspawn-1 I2: after the strike, before any give-up`n"; Repl = '' }
    @{ Name = 'X10 OVERLAPGATE gated'; Key = 'sub'; Expect = 'S2'; Find = '            if (bRelocated) { LogGateRefusal(); }'; Repl = ('            if (bRelocated && FNodeShuffleModule::' + $diagGate + '()) { LogGateRefusal(); }') }
    @{ Name = 'X11 OVERLAPGATE prints the nudged spot'; Key = 'sub'; Expect = 'S2'; Find = '*ResourceClass->GetName(), *RefusedAt.ToCompactString(),'; Repl = '*ResourceClass->GetName(), *Entry.Location.ToCompactString(),' }
    @{ Name = 'X12 heal but forget to reset the strike counter'; Key = 'heal'; Expect = 'S3'; Find = "        E.bRayCasted = true;`n        E.OverlapNudges = 0;`n        ++Reactivated;"; Repl = "        E.bRayCasted = true;`n        ++Reactivated;" }
    @{ Name = 'X13 heal unsettles the entry (bRayCasted false)'; Key = 'heal'; Expect = 'S3'; Find = "        E.bActive = true;`n        E.bRayCasted = true;"; Repl = ("        E.bActive = true;`n        E.bRayCasted = " + 'false;') }
    @{ Name = 'X14 heal forgets bActive'; Key = 'heal'; Expect = 'S3'; Find = "        E.bActive = true;`n        E.bRayCasted = true;"; Repl = "        E.bRayCasted = true;" }
    @{ Name = 'X15 latch before the Layout is loaded'; Key = 'heal'; Expect = 'S3'; Find = '    if (bPinnedHealDone || Layout.Num() == 0)'; Repl = '    if (bPinnedHealDone)' }
    @{ Name = 'X16 heal reaches unpinned entries'; Key = 'heal'; Expect = 'S3'; Find = $healSkip; Repl = "        if (false)`n        {`n            continue;`n        }" }
    @{ Name = 'X17 heal called after RwgSpawnPass'; Key = 'sub'; Expect = 'S3'; Find = ($healCall + "`n"); Repl = ''
       Find2 = '    SettleNewNodesNearPlayers();' + "`n" + '    ReassociateOrphanedExtractors();'; Repl2 = ($healCall + "`n    SettleNewNodesNearPlayers();`n    ReassociateOrphanedExtractors();") }
    @{ Name = 'X18 heal census gated'; Key = 'heal'; Expect = 'S3'; Find = '    UE_LOG(LogNodeShuffle, Display,' + "`n" + '        TEXT("PINNEDHEAL reactivated'; Repl = ('    if (FNodeShuffleModule::' + $diagGate + "())`n    UE_LOG(LogNodeShuffle, Display,`n        TEXT(""PINNEDHEAL reactivated") }
    @{ Name = 'X19 heal census field dropped'; Key = 'heal'; Expect = 'S3'; Find = ' pinnedInactiveBefore=%d'; Repl = '' }
    @{ Name = 'X20 linked counted without the readback'; Key = 'sub'; Expect = 'S4'; Find = '            if (bBoundAfter) { ++RelinkLinked; } else { ++RelinkLinkNoEffect; }'; Repl = '            ++RelinkLinked; (void)bBoundAfter;' }
    @{ Name = 'X21 census printed every pass'; Key = 'sub'; Expect = 'S4'; Find = '        if (RelinkKey != RelinkCensusLastKey)'; Repl = '        if (true)' }
    @{ Name = 'X22 no-node extractors not counted'; Key = 'sub'; Expect = 'S4'; Find = "            ++RelinkNoNode;`n"; Repl = '' }
    @{ Name = 'X23 already-linked not counted'; Key = 'sub'; Expect = 'S4'; Find = '            ++RelinkAlreadyLinked; // rwg-pinnedspawn-1 I4'; Repl = '            // counter removed' }
    @{ Name = 'X24 census field dropped'; Key = 'sub'; Expect = 'S4'; Find = ' unboundNoSpawnedNodeInSnap=%d'; Repl = '' }
    @{ Name = 'X25 marker reverted'; Key = 'nsmod'; Expect = 'S5'; Find = $marker; Repl = ('2026-11-01-rwg-' + 'pinnedspawn-0') }
    @{ Name = 'X26 legend echoes a gate field'; Key = 'sub'; Expect = 'S6'; Find = '            // the entry''s nudge counter after this visit;'; Repl = ('            // the entry''s nudge counter after this visit (' + 'strike=' + 'N/8);') }
    @{ Name = 'X27 legend echoes a heal field'; Key = 'heal'; Expect = 'S6'; Find = '// ONCE PER LOAD.'; Repl = ('// ONCE PER LOAD (' + 'reactivated=' + 'N).') }
)
$bad = 0
foreach ($m in $mutants) {
    $t2 = [ordered]@{}; foreach ($k in $text.Keys) { $t2[$k] = $text[$k] }
    if ((Count $t2[$m.Key] $m.Find) -ne 1) { Write-Host "MUTANT-SETUP FAIL $($m.Name): find text not unique in $($m.Key)"; $bad++; continue }
    $t2[$m.Key] = $t2[$m.Key].Replace($m.Find, $m.Repl)
    if ($m.ContainsKey('Find2')) {
        if ((Count $t2[$m.Key] $m.Find2) -ne 1) { Write-Host "MUTANT-SETUP FAIL $($m.Name): find2 text not unique in $($m.Key)"; $bad++; continue }
        $t2[$m.Key] = $t2[$m.Key].Replace($m.Find2, $m.Repl2)
    }
    $r = Invoke-PinnedSpawnChecks $t2
    $hits = @($r | Where-Object { $_.StartsWith("($($m.Expect))") })
    if ($hits.Count -gt 0) { Write-Host "RED  (ok) $($m.Name)" }
    else { Write-Host "GREEN (BAD) $($m.Name): expected $($m.Expect), got: $($r -join ' | ')"; $bad++ }
}
if ($bad -gt 0) { Write-Host "FAIL: $bad mutant(s) not caught"; exit 1 }
Write-Host "PASS: PINNEDSPAWN lint -- S1..S6 hold across 4 loaded files; mutants caught $($mutants.Count)/$($mutants.Count)."
exit 0
