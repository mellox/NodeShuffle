# LEADRESTORE lint (2026-11-11, packet ns-lead-restore-1; handoff _team/test-logs/2026-11-11-nodeshuffle-lead-restore-handoff.md).
# The settled restore spawns, once per load and at any pawn distance, the active new-node entries the RWG provisional pass refuses by
# bPinned or bRayCasted; every refusing exit of EnsureNewNodeSpawned HOLDS for that caller (no Layout or bookkeeping write).
# LOADS: NodeShuffleSubsystem.cpp (call site, hold sites, adopt predicate, gate radius), NodeShuffleSubsystem.h (declarations),
#        NodeShuffleSettledRestore.cpp/.h (the pass, ADOPTWORLD, census), NodeShuffleMapModeProvisionalScan.cpp (the refusals the
#        population is derived from), NodeShuffleMapModeSpawnPass.cpp (attempt-loop exclusions, existing callers), NodeShuffle.cpp and
#        NodeShuffleBuildMarkers.h (marker).
#   L1 order: adopt -> pinned heal -> RunSettledRestoreOnce -> RwgSpawnPass -> ReassociateOrphanedExtractors, one call, off vanilla
#   L2 latch: runtime bool, armed only with adopt done and a non-empty Layout, before ADOPTWORLD and before any spawn
#   L3 population: exact predicate lines, no distance or budget term, and the scan/attempt-loop source lines it is derived from
#   L4 hold sites: slot, classNull, notSettled, pinned R1, occupied/enclosed (before spiral, strike, give-up), inside gate (before its
#      bActive/bRayCasted writes), null spawn (before fail count, park, eviction), both pinned once-latches skipped for the far caller
#   L5 the switch is a defaulted pointer PARAMETER; no member of that name; existing callers unchanged; only the pass passes it
#   L6 the census is printed on the zero-candidate path (one return in the pass: the latch)
#   L7 exact formats of ADOPTWORLD, SETTLEDRESTORE, -SPAWNED, -HELD; ungated
#   L8 mirrors: adopt radius 300, gate radius 800, gate deposit skip, adopt pass-2 tests in order (both sides pinned)
#   L9 marker 2026-11-11-lead-restore-1 newest in the header, once in NodeShuffle.cpp, absent from the packet files
#   L10 every new field token once in its file; legend is prose without an = sign
#   L11 bRwgProvisional cleared only in the spawned branch
#   L12 the pass file writes no other entry field and no session bookkeeping
# Every forbidden needle is BUILT BY CONCATENATION. Mutants run in-file. Exit 0 = all hold; 1 = a rule or mutant fails; 2 = missing file.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    hdr   = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    sr    = 'NodeShuffle\Private\NodeShuffleSettledRestore.cpp'
    srh   = 'NodeShuffle\Private\NodeShuffleSettledRestore.h'
    scan  = 'NodeShuffle\Private\NodeShuffleMapModeProvisionalScan.cpp'
    spass = 'NodeShuffle\Private\NodeShuffleMapModeSpawnPass.cpp'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
    mk    = 'NodeShuffle\Private\NodeShuffleBuildMarkers.h'
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
$marker = '2026-11-11-' + 'lead-restore-1'
$N = "`n"

# ---- anchors (unique context) ----
$callRestore = '    if (GetMapMode() != ENodeShuffleMapMode::Vanilla) { RunSettledRestoreOnce(SpawnRadiusCm, bChangedWorld); }'
$callSpawn   = '    if (GetMapMode() != ENodeShuffleMapMode::Vanilla) { RwgSpawnPass(SpawnRadiusCm, bChangedWorld); }'
$adoptCall   = '        AdoptRestoredSpawnedNodes();' + $N + '        bAdoptedRestoredNodes = true;'
$healCall    = '    HealDeactivatedPinnedEntriesOnce();'
$relinkCall  = '    ReassociateOrphanedExtractors();'
$latch       = '    if (bSettledRestoreDone || !bAdoptedRestoredNodes || Layout.Num() == 0) { return; }' + $N + '    bSettledRestoreDone = true;' + $N + '    EmitAdoptWorldCensus();'
$farCall     = '        EnsureNewNodeSpawned(E, bLocalChanged, &Far);'
$popLines = @(
    '        if (!E.bIsNewNode || !E.bActive) { continue; }',
    '        if (Slot && IsValid(*Slot)) { continue; }',
    '        if (DormantThisSession.Contains(E.EntryGuid)) { ++SkipDormant; continue; }',
    '        if (SpawnParkedThisSession.Contains(E.EntryGuid)) { ++SkipParked; continue; }',
    '        if (Slot) { ++SkipDeadSlot; continue; }',
    '        if (!E.bPinned && RwgProvRefused.Contains(E.EntryGuid)) { ++SkipProvRefused; continue; }',
    '        if (!E.bPinned && !E.bRayCasted) { ++SkipUnpinnedUnsettled; continue; }',
    '        (E.bPinned ? Pinned : Rest).Add(I);')
$scanLines = @(
    '        if (!E.bIsNewNode || !E.bActive || E.bPinned) { return false; }',
    '        if (DormantThisSession.Num() > 0 && DormantThisSession.Contains(E.EntryGuid)) { return false; }',
    '        if (SpawnParkedThisSession.Num() > 0 && SpawnParkedThisSession.Contains(E.EntryGuid)) { return false; }',
    '        if (RwgProvRefused.Contains(E.EntryGuid)) { return false; }',
    '        if (E.bRayCasted) { ++RayCastedNoActor; return false; }')
$loopLines = @(
    '        if (!E.bIsNewNode || !E.bActive) { continue; }',
    '        if (DormantThisSession.Num() > 0 && DormantThisSession.Contains(E.EntryGuid)) { continue; }',
    '        if (SpawnParkedThisSession.Num() > 0 && SpawnParkedThisSession.Contains(E.EntryGuid)) { continue; }')
$hSlot     = '    if (FarRestore && Existing) { FarRestore->HeldGate = IsValid(*Existing) ? TEXT("slotAlive") : TEXT("slotDead"); return; }'
$deadSlot  = '    if (Existing && !IsValid(*Existing))'
$hClass    = '        if (FarRestore) { FarRestore->HeldGate = TEXT("classNull"); } // ns-lead-restore-1: the same return; no write on either path' + $N + '        return;'
$hSettle   = '    if (FarRestore && !Entry.bRayCasted) { FarRestore->HeldGate = TEXT("notSettled"); return; }'
$settleBlk = '    if (!Entry.bRayCasted)' + $N + '    {'
$hPinR1    = '        if (FarRestore && Entry.bPinned && bSpotOccupied) { FarHold(TEXT("pinnedOccupied")); return; }'
$r1        = '        if (Entry.bPinned && bSpotOccupied) { return; } // R1: spawn held this visit; no nudge, no strike, no drop'
$hOcc      = '        if (FarRestore && (bSpotOccupied || bSpotEnclosed))' + $N + '        {' + $N + '            FarHold(bSpotEnclosed ? TEXT("enclosed") : (PrimaryHitKind == TEXT("buildable")) ? TEXT("occupiedBuilding") : TEXT("occupiedNode"));' + $N + '            return;' + $N + '        }'
$spiral    = '            for (int32 i = 1; i <= LandRelocationTries && !bRelocated; i++)'
$nudgeWr   = '                    Entry.Location = TryLoc;'
$strike    = '                Entry.OverlapNudges++;'
$giveUp    = '                    Entry.bActive = false;' + $N + '                    Entry.bRayCasted = false;' + $N + '                    UE_LOG(LogNodeShuffle, Display,' + $N + '                        TEXT("Overlap guard: resolved'
$hInside   = '        else if (bGateRefuses && FarRestore)' + $N + '        {' + $N + '            FarRestore->HeldGate = TEXT("insideGate"); // ns-lead-restore-1 HOLD: no bActive/bRayCasted write' + $N + '            return;' + $N + '        }'
$insideWr  = '        else if (bGateRefuses)' + $N + '        {' + $N + '            // ns-t54 cold review F11'
$insideDrop = '            Entry.bActive = false;' + $N + '            Entry.bRayCasted = false;' + $N + '            UE_LOG(LogNodeShuffle, Display,' + $N + '                TEXT("INSIDEGATE: resolved'
$hNull     = '        if (FarRestore) { FarRestore->HeldGate = TEXT("spawnActorNull"); return; }'
$failWr    = '        int32& Fails = SpawnFailCounts.FindOrAdd(Entry.EntryGuid);'
$parkWr    = '            SpawnParkedThisSession.Add(Entry.EntryGuid);'
$evict     = '            EvictSpawnRefusingClass(ResolvedNodeClassPath, Entry);'
$latchPin1 = '        if (!FarRestore && Entry.bPinned && PinnedBuildingHitsIgnored > 0 && !PinnedBuildingSkipLogged.Contains(Entry.EntryGuid))'
$latchPin2 = '        if (!FarRestore && Entry.bPinned && (bSpotOccupied || bSpotEnclosedRaw) && !PinnedGateHoldLogged.Contains(Entry.EntryGuid))'
$decl      = '    void EnsureNewNodeSpawned(FNodeShuffleEntry& Entry, bool& bOutChangedWorld, FNodeShuffleFarRestore* FarRestore = nullptr);'
$defn      = 'void ANodeShuffleSubsystem::EnsureNewNodeSpawned(FNodeShuffleEntry& Entry, bool& bOutChangedWorld, FNodeShuffleFarRestore* FarRestore)'
$fmtWorld  = 'TEXT("ADOPTWORLD class=%s worldActors=%d boundToEntry=%d unboundLegacyClass=%d unboundNetStartup=%d unboundFracking=%d unboundOurComponent=%d unboundWouldBindNow=%d unboundResourceDiffers=%d unboundEntryHasSlot=%d unboundNoActiveEntryIn300cm=%d netStartupAll=%d nullResourceAll=%d"),'
$fmtCensus = 'TEXT("SETTLEDRESTORE candidates=%d spawned=%d held=%d heldSlotAlive=%d heldSlotDead=%d heldClassNull=%d heldNotSettled=%d heldPinnedOccupied=%d heldOccupiedNode=%d heldOccupiedBuilding=%d heldEnclosed=%d heldInsideGate=%d heldSpawnActorNull=%d heldNoGateRecorded=%d heldStateDiffs=%d xtabPinRayProv=111:%d/110:%d/101:%d/100:%d/011:%d/010:%d/001:%d/000:%d candNoPawn=%d candBeyondAttemptRadius=%d candWithinAttemptRadius=%d actorlessActiveNew=%d skipDormant=%d skipParked=%d skipDeadSlot=%d skipUnpinnedProvRefused=%d skipUnpinnedUnsettled=%d provFlagClearedOnSpawn=%d spawnedLinesCut=%d heldLinesCut=%d attemptRadiusM=%.0f pawns=%d"),'
$fmtSpawn  = 'TEXT("SETTLEDRESTORE-SPAWNED guid=%s res=%s spawnClass=%s at=%s nearestPawnDist2DM=%.0f gateNodesWithin8mAfterSpawn=%d pinned=%d pinnedBuildingHitsIgnored=%d"),'
$fmtHeld   = 'TEXT("SETTLEDRESTORE-HELD guid=%s res=%s at=%s heldGate=%s hitKind=%s hitClass=%s hitPath=%s hitDistCm=%.0f pinned=%d nearestPawnDist2DM=%.0f entryStateDiffs=%d"),'
$adoptPred = @(
    '        if (Node->IsA<ANodeShuffleResourceNode>()) { continue; }  // handled in pass 1',
    '        if (Node->IsNetStartupActor()) { continue; }              // level-placed original, not ours',
    '        if (IsFrackingActor(Node)) { continue; }',
    '        if (UNodeShuffleNodeComponent::Find(Node)) { continue; }  // already ours this session',
    '            if (!E.bActive) { continue; }',
    '            if (SpawnedNodes.Contains(E.EntryGuid)) { continue; } // entry already has a live node',
    '            if (DSq >= BestSq) { continue; }',
    '                && NodeRes != E.AssignedResourceClassPath) { continue; }')
$mirrorPred = @(
    '        if (BoundActors.Contains(Node)) { ++Row->Bound; continue; }',
    '        if (Node->IsA<ANodeShuffleResourceNode>()) { ++Row->Legacy; continue; }',
    '        if (Node->IsNetStartupActor()) { ++Row->NetStartup; continue; }',
    '        if (IsFrackingActor(Node)) { ++Row->Fracking; continue; }',
    '        if (UNodeShuffleNodeComponent::Find(Node)) { ++Row->Ours; continue; }',
    '            if (!E.bIsNewNode || !E.bActive) { continue; }',
    '            if (DSq >= MatchSq) { continue; }',
    '            const bool bResOk = NodeRes.IsEmpty() || E.AssignedResourceClassPath.IsEmpty() || NodeRes == E.AssignedResourceClassPath;',
    '            if (SpawnedNodes.Contains(E.EntryGuid)) { bTaken |= bResOk; continue; }',
    '            if (!bResOk) { bResDiff = true; continue; }')

function Invoke-Checks($T) {
    $f = @()
    $s = $T.sub; $sr = $T.sr
    # L1
    $apply = Span $s 'void ANodeShuffleSubsystem::ApplyLayout()' 'void ANodeShuffleSubsystem::SweepMismatchedDeposits()'
    if ($apply -eq '' -or -not (Ordered $apply @($adoptCall, $healCall, $callRestore, $callSpawn, $relinkCall))) { $f += '(L1) order: adopt -> pinned heal -> settled restore -> RwgSpawnPass -> relink' }
    if ((Count $s 'RunSettledRestoreOnce(') -ne 1 -or (Count $s $callRestore) -ne 1 -or (Count $T.spass 'RunSettledRestoreOnce(') -ne 0) { $f += '(L1) exactly one call, at the ApplyLayout site' }
    # L2
    $pass = Span $sr 'void ANodeShuffleSubsystem::RunSettledRestoreOnce(float SpawnRadiusCm, bool& bChangedWorld)' '    // Legend (prose only).'
    if ($pass -eq '' -or -not (Ordered $pass @($latch, $farCall))) { $f += '(L2) latch (adopt done, non-empty Layout) -> ADOPTWORLD -> far call' }
    if ((Count $T.hdr '    bool bSettledRestoreDone = false;') -ne 1 -or (Count $T.hdr ('UPROPERTY()' + $N + '    bool bSettledRestoreDone')) -ne 0) { $f += '(L2) latch is a plain runtime member' }
    # L3
    $pop = Span $sr '    TArray<int32> Pinned, Rest;' '    TArray<int32> Cands = MoveTemp(Pinned);'
    if ($pop -eq '' -or -not (Ordered $pop $popLines)) { $f += '(L3) population predicate lines (order: new+active, actor, dormant, parked, dead slot, prov refused, unsettled, pinned split)' }
    foreach ($bad in @(('IsLocationNear' + 'AnyPlayer'), ('RwgMaxSpawn' + 'AttemptsPerPass'), ('RwgNearest' + 'PawnDist2D'))) { if ($pop.Contains($bad)) { $f += "(L3) the population must carry no distance or budget term ('$bad')" } }
    if (-not (Ordered $T.scan $scanLines)) { $f += '(L3) the provisional scan refusals the predicate is derived from changed (re-derive the population)' }
    if (-not (Ordered $T.spass $loopLines)) { $f += '(L3) the attempt-loop exclusions the predicate honours changed' }
    # L4
    $ens = Span $s $defn 'void ANodeShuffleSubsystem::SettleNewNodesNearPlayers()'
    if ($ens -eq '') { $f += '(L4) EnsureNewNodeSpawned span missing'; return $f }
    if (-not (Ordered $ens @($hSlot, $deadSlot))) { $f += '(L4) slot hold before the external-destroy backoff' }
    if ((Count $ens $hClass) -ne 1) { $f += '(L4) classNull recorded on the unchanged return' }
    if (-not (Ordered $ens @($hSettle, $settleBlk, '        ++NodeGateSettleReached;'))) { $f += '(L4) notSettled hold before the settle block' }
    if (-not (Ordered $ens @($hPinR1, $r1))) { $f += '(L4) pinned R1 hold recorded before R1' }
    if (-not (Ordered $ens @($hOcc, $spiral, $nudgeWr, $strike, $giveUp))) { $f += '(L4) occupied/enclosed hold before nudge, strike and give-up' }
    if (-not (Ordered $ens @($hInside, $insideWr, $insideDrop))) { $f += '(L4) inside-gate hold before its bActive/bRayCasted drop' }
    if (-not (Ordered $ens @($hNull, $failWr, $parkWr, $evict))) { $f += '(L4) null-spawn hold before fail count, park and eviction' }
    if ((Count $ens $latchPin1) -ne 1 -or (Count $ens $latchPin2) -ne 1) { $f += '(L4) both pinned once-latches skipped for the far caller' }
    # L5
    if ((Count $T.hdr $decl) -ne 1 -or (Count $s $defn) -ne 1) { $f += '(L5) the switch is a defaulted pointer parameter (declaration and definition)' }
    $memberRx = '(?m)^\s+(bool|FNodeShuffleFarRestore\*?|TOptional<[^>]*>)\s+b?FarRestore\w*\s*(=|;)'
    foreach ($k in @('hdr', 'sub', 'sr')) { if ([regex]::IsMatch($T[$k], $memberRx) -or $T[$k].Contains('b' + 'FarRestore')) { $f += "(L5) no member or flag named FarRestore ($k)" } }
    if ((Count $T.spass 'EnsureNewNodeSpawned(E, bChangedWorld);') -ne 2 -or (Count $s 'EnsureNewNodeSpawned(Entry, bChangedWorld);') -ne 1 -or (Count $T.spass ('EnsureNewNodeSpawned(E, bChangedWorld, ' + '&')) -ne 0) { $f += '(L5) existing callers pass no far argument' }
    if ((Count $sr 'EnsureNewNodeSpawned(') -ne 1 -or (Count $sr $farCall) -ne 1) { $f += '(L5) the pass is the only far caller, once' }
    if ($T.srh.Contains('USTR' + 'UCT') -or $T.srh.Contains('UPROP' + 'ERTY')) { $f += '(L5) the far-restore struct is not reflected' }
    # L6
    if ((Count $pass 'return;') -ne 1 -or $pass.Contains('Cands.Num() ' + '==') -or $pass.Contains('Cands.Num() ' + '>') -or $pass.Contains('Cands.' + 'IsEmpty()')) { $f += '(L6) the census prints on the zero-candidate path (only the latch returns)' }
    $tail = Span $sr '    // Legend (prose only).' 'AttemptRadiusCm / 100.f, Pawns.Num());'
    if ($tail -eq '' -or (Count $tail $fmtCensus) -ne 1) { $f += '(L6) the census follows the candidate loop' }
    # L7
    foreach ($fmt in @($fmtWorld, $fmtCensus, $fmtSpawn, $fmtHeld)) { if ((Count $sr ('UE_LOG(LogNodeShuffle, Display, ' + $fmt)) -ne 1) { $f += "(L7) format missing or changed: $($fmt.Substring(6, 26))" } }
    if ($sr.Contains($diagGate)) { $f += '(L7) all four lines ungated' }
    # L8
    if ((Count $sr 'constexpr float SettledRestoreAdoptMirrorCm = 300.0f;') -ne 1 -or (Count $s 'constexpr float AdoptMatchRadiusCm = 300.0f;') -ne 1) { $f += '(L8) adopt radius mirror (300 cm)' }
    if ((Count $sr 'constexpr float SettledRestoreGateRadiusCm = 800.0f;') -ne 1 -or (Count $s 'constexpr float OverlapRejectRadius = 800.0f; // 8 m: a node footprint') -ne 1) { $f += '(L8) gate radius mirror (800 cm)' }
    if ((Count $s '                if (!IsValid(Other) || Other->IsA<AFGResourceDeposit>())') -ne 1 -or (Count $sr '                if (!IsValid(*It) || It->IsA<AFGResourceDeposit>()) { continue; }') -ne 1) { $f += '(L8) gate deposit skip mirrored' }
    $adoptFn = Span $s 'void ANodeShuffleSubsystem::AdoptRestoredSpawnedNodes()' 'bool ANodeShuffleSubsystem::FinalizeAdoptedNode('
    if ($adoptFn -eq '' -or -not (Ordered $adoptFn $adoptPred)) { $f += '(L8) adopt pass 2 tests changed (re-mirror ADOPTWORLD)' }
    $world = Span $sr 'void ANodeShuffleSubsystem::EmitAdoptWorldCensus()' 'void ANodeShuffleSubsystem::RunSettledRestoreOnce('
    if ($world -eq '' -or -not (Ordered $world $mirrorPred)) { $f += '(L8) ADOPTWORLD mirror order: bound, legacy, net-startup, fracking, component, entry search' }
    # L9
    $hdrTags = @([regex]::Matches($T.mk, 'TEXT\("(20\d\d-[\w-]+)"\)') | ForEach-Object { $_.Groups[1].Value })
    if ($hdrTags.Count -eq 0 -or $hdrTags[-1] -ne $marker -or (Count $T.nsmod ('packet marker (' + $marker + ')=====')) -ne 1) { $f += '(L9) marker newest in the header and once in NodeShuffle.cpp' }
    foreach ($k in @('sr', 'srh')) { if ($T[$k].Contains($marker)) { $f += "(L9) no marker literal in $k" } }
    # L10
    foreach ($fmt in @($fmtWorld, $fmtCensus, $fmtSpawn, $fmtHeld)) {
        foreach ($m in [regex]::Matches($fmt, '([A-Za-z0-9]+)=')) { $tok = $m.Groups[1].Value + '='; if (([regex]::Matches($sr, '(?<![A-Za-z0-9])' + [regex]::Escape($tok))).Count -ne (([regex]::Matches(($fmtWorld + $fmtCensus + $fmtSpawn + $fmtHeld), '(?<![A-Za-z0-9])' + [regex]::Escape($tok))).Count)) { $f += "(L10) field token '$tok' appears outside its format string" } }
    }
    $leg = Span $sr '    // Legend (prose only).' '    UE_LOG(LogNodeShuffle, Display, TEXT("SETTLEDRESTORE candidates'
    if ($leg -eq '' -or $leg.Contains('=')) { $f += '(L10) the census legend is prose with no = sign' }
    # L11
    if ((Count $sr ('bRwgProvisional' + ' = false')) -ne 1 -or -not (Ordered $pass @('        if (IsValid(Now) && Far.HeldGate == nullptr)', '            if (E.bRwgProvisional) { E.bRwgProvisional = false; ++ProvFlagCleared; }', '        ++HeldTotal;'))) { $f += '(L11) the provisional flag is cleared only in the spawned branch' }
    # L12
    foreach ($bad in @(('E.bActive' + ' ='), ('E.Location' + ' ='), ('E.Rotation' + ' ='), ('E.OverlapNudges'), ('E.bRayCasted' + ' ='), ('E.bPinned' + ' ='), ('SpawnFailCounts.' + 'FindOrAdd'), ('SpawnParkedThisSession.' + 'Add'), ('DormantThisSession.' + 'Add'), ('Destroy' + '('), ('SpawnedNodes.' + 'Add('), ('SpawnedNodes.' + 'Remove('))) { if ($sr.Contains($bad)) { $f += "(L12) the pass file must not write '$bad'" } }
    return $f
}

$fails = @(Invoke-Checks $text)
$mutants = @(
    @{ Name = 'M1 call moved after RwgSpawnPass'; Key = 'sub'; Expect = 'L1'; Find = ($callRestore + $N + $callSpawn); Repl = ($callSpawn + $N + $callRestore) }
    @{ Name = 'M2 call removed'; Key = 'sub'; Expect = 'L1'; Find = $callRestore; Repl = '' }
    @{ Name = 'M3 latch ignores adopt'; Key = 'sr'; Expect = 'L2'; Find = 'if (bSettledRestoreDone || !bAdoptedRestoredNodes || Layout.Num() == 0)'; Repl = 'if (bSettledRestoreDone || Layout.Num() == 0)' }
    @{ Name = 'M4 pinned arm dropped (pinned unsettled skipped)'; Key = 'sr'; Expect = 'L3'; Find = 'if (!E.bPinned && !E.bRayCasted) { ++SkipUnpinnedUnsettled'; Repl = 'if (!E.bRayCasted) { ++SkipUnpinnedUnsettled' }
    @{ Name = 'M5 population gains a distance gate'; Key = 'sr'; Expect = 'L3'; Find = '        (E.bPinned ? Pinned : Rest).Add(I);'; Repl = ('        if (!IsLocationNear' + 'AnyPlayer(E.Location, AttemptRadiusCm)) { continue; }' + $N + '        (E.bPinned ? Pinned : Rest).Add(I);') }
    @{ Name = 'M6 dormant exclusion dropped'; Key = 'sr'; Expect = 'L3'; Find = '        if (DormantThisSession.Contains(E.EntryGuid)) { ++SkipDormant; continue; }'; Repl = '' }
    @{ Name = 'M7 scan stops refusing settled entries'; Key = 'scan'; Expect = 'L3'; Find = '        if (E.bRayCasted) { ++RayCastedNoActor; return false; }'; Repl = '        if (false) { ++RayCastedNoActor; return false; }' }
    @{ Name = 'M8 slot hold removed'; Key = 'sub'; Expect = 'L4'; Find = $hSlot; Repl = '' }
    @{ Name = 'M9 notSettled hold removed'; Key = 'sub'; Expect = 'L4'; Find = $hSettle; Repl = '' }
    @{ Name = 'M10 occupied hold loses the enclosure arm'; Key = 'sub'; Expect = 'L4'; Find = '        if (FarRestore && (bSpotOccupied || bSpotEnclosed))'; Repl = '        if (FarRestore && bSpotOccupied)' }
    @{ Name = 'M11 inside-gate hold removed'; Key = 'sub'; Expect = 'L4'; Find = $hInside; Repl = '' }
    @{ Name = 'M12 null-spawn hold removed'; Key = 'sub'; Expect = 'L4'; Find = $hNull; Repl = '' }
    @{ Name = 'M13 pinned-hold once-latch consumed by the far caller'; Key = 'sub'; Expect = 'L4'; Find = $latchPin2; Repl = $latchPin2.Replace('if (!FarRestore && Entry.bPinned', 'if (Entry.bPinned') }
    @{ Name = 'M14 a member flag instead of the parameter'; Key = 'hdr'; Expect = 'L5'; Find = '    bool bSettledRestoreDone = false;'; Repl = ('    bool bSettledRestoreDone = false;' + $N + '    bool b' + 'FarRestore = false;') }
    @{ Name = 'M15 default argument dropped'; Key = 'hdr'; Expect = 'L5'; Find = 'FNodeShuffleFarRestore* FarRestore = nullptr);'; Repl = 'FNodeShuffleFarRestore* FarRestore);' }
    @{ Name = 'M16 an existing caller passes a far argument'; Key = 'spass'; Expect = 'L5'; Find = '        EnsureNewNodeSpawned(E, bChangedWorld);' + $N + '        bRwgSteepArmed = false;'; Repl = '        EnsureNewNodeSpawned(E, bChangedWorld, &Far);' + $N + '        bRwgSteepArmed = false;' }
    @{ Name = 'M17 census skipped with zero candidates'; Key = 'sr'; Expect = 'L6'; Find = '    Cands.Append(Rest);'; Repl = ('    Cands.Append(Rest);' + $N + '    if (Cands.Num() ' + '== 0) { return; }') }
    @{ Name = 'M18 census diagnostics-gated'; Key = 'sr'; Expect = 'L7'; Find = ('    UE_LOG(LogNodeShuffle, Display, ' + $fmtCensus); Repl = ('    if (FNodeShuffleModule::' + $diagGate + '()) UE_LOG(LogNodeShuffle, Display, ' + $fmtCensus) }
    @{ Name = 'M19 census drops heldStateDiffs'; Key = 'sr'; Expect = 'L7'; Find = ' heldStateDiffs=%d xtabPinRayProv'; Repl = ' xtabPinRayProv' }
    @{ Name = 'M20 adopt mirror radius drifts'; Key = 'sr'; Expect = 'L8'; Find = 'constexpr float SettledRestoreAdoptMirrorCm = 300.0f;'; Repl = 'constexpr float SettledRestoreAdoptMirrorCm = 500.0f;' }
    @{ Name = 'M21 adopt pass 2 drops its net-startup test'; Key = 'sub'; Expect = 'L8'; Find = '        if (Node->IsNetStartupActor()) { continue; }              // level-placed original, not ours'; Repl = '' }
    @{ Name = 'M22 ADOPTWORLD tests fracking before net-startup'; Key = 'sr'; Expect = 'L8'; Find = ('        if (Node->IsNetStartupActor()) { ++Row->NetStartup; continue; }           // adopt: level-placed original, not ours' + $N + '        if (IsFrackingActor(Node)) { ++Row->Fracking; continue; }                 // adopt: ns-review-h2 F9'); Repl = ('        if (IsFrackingActor(Node)) { ++Row->Fracking; continue; }                 // adopt: ns-review-h2 F9' + $N + '        if (Node->IsNetStartupActor()) { ++Row->NetStartup; continue; }           // adopt: level-placed original, not ours') }
    @{ Name = 'M23 marker dropped from the header'; Key = 'mk'; Expect = 'L9'; Find = ('        TEXT("' + $marker + '"),' + $N); Repl = '' }
    @{ Name = 'M24 legend echoes a field'; Key = 'sr'; Expect = 'L10'; Find = '    // Legend (prose only). candidates counts'; Repl = ('    // Legend (prose only). candidates' + '=N counts') }
    @{ Name = 'M25 provisional flag cleared on the hold path too'; Key = 'sr'; Expect = 'L11'; Find = '        ++HeldTotal;'; Repl = ('        ++HeldTotal;' + $N + '        E.bRwgProvisional' + ' = false;') }
    @{ Name = 'M26 the pass resets the strike counter'; Key = 'sr'; Expect = 'L12'; Find = '        ++HeldTotal;'; Repl = ('        ++HeldTotal;' + $N + '        E.OverlapNudges' + ' = 0;') }
)
$mutantFails = @(); $caught = 0
foreach ($m in $mutants) {
    $mut = [ordered]@{}; foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = @(Invoke-Checks $mut)
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++; Write-Host "RED  (ok) $($m.Name)" } else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) { foreach ($x in $all) { Write-Host "FAIL: $x" }; Write-Host "LEADRESTORE lint: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."; exit 1 }
Write-Host "PASS: LEADRESTORE lint -- L1..L12 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
