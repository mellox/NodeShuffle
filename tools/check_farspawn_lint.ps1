# FARSPAWN lint (2026-10-31, packet rwg-farspawn-1): provisional far spawn on RWG + settle on approach.
# Brief: _team/test-logs/2026-10-31-nodeshuffle-farspawn-brief.md. LOADS and PINS every source file the packet touched.
# Pins are anchored on context unique to their site (function names, the gate comment, the exact statement). Needles naming a
# forbidden literal are BUILT BY CONCATENATION so this file cannot match itself. Run under pwsh 7.
#   F1  RWG-only: the spawn pass and the settle both test GetMapMode() == Rwg themselves; the spawn pass also needs the toggle and a pawn.
#   F2  Defaults: NodeShuffle.RwgSpawnAllAtRoll 1 (ON); NodeShuffle.RwgProvisionalBatchPerPass 64, clamped 8..512 at its only reader.
#   F3  The seam: RwgProvisionalZ declared once, defined once, body = OutSource "api" + Q.SurfaceZ; no world/trace access in the body;
#       the spawn pass takes its Z from the seam; the header names both OutSource values.
#   F4  Provisional actors are NOT hidden and keep collision ON; the settle turns collision off only around the settle call.
#   F5  The three settle outcomes in order: settled (inPlace), unplaceable (ring|mapwide), deferred (entry restored, stays provisional).
#   F6  Despawn before respawn / before the redeal; the teardown destroys, unregisters AFTER, and removes the slot in the same block.
#   F7  Pin wins: checked before the settle touches the actor; pinned entries are never provisioned; the pinnedKept outcome.
#   F8  Save field: UPROPERTY(SaveGame) bRwgProvisional, default false (forward-only).
#   F9  Consumers skip provisional: resettle, rock, killbox, settle-near-players; RwgSpawnPass call sites (settle, stale clear, pass).
#   F10 Diagnostic field lists: PROVSPAWN, PROVSETTLE (both lines), PROVCENSUS formats and argument lists verbatim.
#   F11 PLACECENSUS ends with provisionalSpawned/provisionalSettled; their counters are the last two args and are reset per pass.
#   F12 PROVSETTLE sampled <= 8 per pass; PROVCENSUS on 12th passes; a provisional attempt that made no actor restores the entry.
#   F13 Marker 2026-10-31-rwg-farspawn-1f once (round 1f), previous markers absent; the new .cpp stays under 500 lines.
#   F15 (fix round 1, F4) PROVSETTLE reports only what it tested: inPlace/settled counters need the respawned actor, settledZ is that
#       actor's Z (never the entry's), ring/mapwide/redealt need TryRedealWaterLockedEntry to have returned true; (re-review V-R3)
#       settledNoActor / redealNotMoved have session counters printed on PROVCENSUS.
#   F16 (fix round 1, F6) PROVSETTLE prints the scanner/radar reveal counts before the despawn and on the respawned actor; read-only.
#   F17 (fix round 1, F7) Provisioning skips rayCasted actorless entries and counts RwgSeaDealFailed entries as sea-pending.
#   F18 (fix round 1, F9) The RwgSpawnAllAtRoll help text claims only what the code does.
#   F14 (fix round 1, F3) RWGSTUCK / HERE-DENSITY: the provisional settle is bracketed by the RWGSTUCK snapshot/note, and the note,
#       the stuck census and HERE-DENSITY count a valid actor as live only when it is NOT provisional; (re-review V-R1) the
#       HereUnspawned comment says its two lines still count a provisional actor as spawned (code unchanged; rwgtrace H3 pins it).
#   F23 (1d F1) the move reads back: MoveInPlace returns false when the actor is >10 cm from the settled spot (before anything
#       else runs), prints moveCheck=moveNotApplied, counts it, and the caller then takes the despawn + respawn branch.
#   F24 (1d F2) shadow gates at the settled spot: IsSpotEnclosed + EvaluateInsideShadowGate with this actor excluded, printed on
#       PROVMOVE, counted on PROVCENSUS, and never refusing (no return / bRwgProvisional / Destroy under them).
#   F25 (1d F3/F5-F10) the DESPAWN-ON-MOVE citation and the node-representation update; renamed/added diagnostic fields; legends.
#   F26 (1e C1) within-radius provisioning: only fresh attempts this pass that left no actor (the attempt loop's fresh branch feeds
#       FreshNoActor), never budget-skipped or underground within entries; never at a water-lock spot (both origins), the spot noted
#       before every WaterLockedThisSession.Add; PROVSETTLE origin withinRadius / far / notThisSession.
#   F27 (1e C2) time budget: checked before every spawn but the first, on the msBatch clock started before the candidate scan; the
#       count cap stays the hard cap; budgetHit time / count / none.
#   F28 (1e C3) SCANPING: deposits excluded from the live set and printed apart; layout membership from the managed-node registry.
#   F29 (1e C4) PROVSETTLE sample cap 16; deferredEntries counts distinct entries in the deferred branch.
#   F30 (1f C2) a move whose measurement reads enclosed or allSolid returns gatedRespawn (ungated line, counted) before anything else of
#       the move runs, and the caller takes the despawn + respawn branch; 1d F1 decrement; 1d F3/F4 labels (allSolid/escapeFound,
#       nodeRepUpdateReturned); PROVCENSUS gatedEnclosed/gatedAllSolid/gatedRespawn.
#   F31 (1f C3) the per-session candidate list: built on initial / dirty / layoutNum / periodic (12) / guidMismatch; dirty marks at
#       roll+re-roll, the two redeal sites, branch (b), pin, reload; the spawn pass no longer walks Layout or sorts; scanMs/candidates on
#       PROVSPAWN; set-aside entries dropped from the list; (1e F9) the per-entry within-radius line.
# Mutants run in-file. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    prov  = 'NodeShuffle\Private\NodeShuffleMapModeProvisional.cpp'
    provh = 'NodeShuffle\Private\NodeShuffleMapModeProvisional.h'
    place = 'NodeShuffle\Private\NodeShuffleMapModePlace.cpp'
    rs    = 'NodeShuffle\Private\NodeShuffleMapModeResettle.cpp'
    rock  = 'NodeShuffle\Private\NodeShuffleMapModeRock.cpp'
    void  = 'NodeShuffle\Private\NodeShuffleMapModeVoid.cpp'
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    subh  = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
    dens  = 'NodeShuffle\Private\NodeShuffleMapModeDensity.cpp'
    here  = 'NodeShuffle\Private\NodeShuffleHereUnspawned.cpp'
    move  = 'NodeShuffle\Private\NodeShuffleMapModeProvisionalMove.cpp'
    scan  = 'NodeShuffle\Private\NodeShuffleScanDiag.cpp'
    wmove = 'NodeShuffle\Private\NodeShuffleWellMoveLog.cpp'
    escal = 'NodeShuffle\Private\NodeShuffleWellEscalate.cpp'
    sea   = 'NodeShuffle\Private\NodeShuffleMapModeSea.cpp'
    wroll = 'NodeShuffle\Private\NodeShuffleWellRelocateRoll.cpp'
    wapply = 'NodeShuffle\Private\NodeShuffleWellRelocateApply.cpp'
    pscan = 'NodeShuffle\Private\NodeShuffleMapModeProvisionalScan.cpp' # rwg-farspawn-1f C3
    adopt = 'NodeShuffle\Private\NodeShuffleAdoptAudit.cpp' # rwg-farspawn-1f C1 (pinned in check_adopt_lint.ps1; loaded here for the reload mark)
}
$text = [ordered]@{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = ((Get-Content -Raw -Encoding utf8 -Path $p) -replace "`r`n", "`n")
}
# rwg-farspawn-1f F10: RwgSpawnPass + RwgEmitPlaceCensus moved VERBATIM from NodeShuffleMapModePlace.cpp to NodeShuffleMapModeSpawnPass.cpp;
# the 'place' text is both files, Place.cpp first (every pin keyed on 'place' still reads the moved code).
$spPath = Join-Path $src 'NodeShuffle\Private\NodeShuffleMapModeSpawnPass.cpp'
if (-not (Test-Path $spPath)) { Write-Host "FAIL(load): pinned file 'NodeShuffleMapModeSpawnPass.cpp' does not exist"; exit 2 }
$text['place'] = $text['place'] + "`n" + ((Get-Content -Raw -Encoding utf8 -Path $spPath) -replace "`r`n", "`n")
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

function Invoke-FarspawnChecks($T) {
    $f = @()
    $p = $T.prov
    $spawnFn = Span $p 'void ANodeShuffleSubsystem::RwgProvisionalSpawnPass(' 'void ANodeShuffleSubsystem::RwgProvisionalEmitCensus('
    $settleFn = Span $p 'void ANodeShuffleSubsystem::RwgSettleProvisional(' 'void ANodeShuffleSubsystem::RwgProvisionalSpawnPass('
    $pinFn = Span $p 'bool ANodeShuffleSubsystem::RwgProvisionalPinWins(' 'void ANodeShuffleSubsystem::RwgSettleProvisional('
    $despFn = Span $p 'void ANodeShuffleSubsystem::RwgProvisionalDespawn(' 'bool ANodeShuffleSubsystem::RwgProvisionalPinWins('
    $seamFn = Span $p 'double NodeShuffleMapMode::RwgProvisionalZ(' 'bool NodeShuffleMapMode::RwgSpawnAllAtRollOn()'
    if ($spawnFn -eq '' -or $settleFn -eq '' -or $pinFn -eq '' -or $despFn -eq '' -or $seamFn -eq '') { $f += '(F0) a pinned function span is missing or duplicated'; return $f }
    # F1
    if (-not $spawnFn.Contains('    if (GetMapMode() != ENodeShuffleMapMode::Rwg) { return; } // RWG-only gate')) { $f += '(F1) the spawn pass must test the RWG map mode first' }
    if (-not $spawnFn.Contains('    if (!NodeShuffleMapMode::RwgSpawnAllAtRollOn() || Pawns.Num() == 0) { return; }')) { $f += '(F1) the spawn pass must need the toggle and a pawn' }
    if (-not $settleFn.Contains('    if (GetMapMode() != ENodeShuffleMapMode::Rwg || !E.bRwgProvisional) { return; }')) { $f += '(F1) the settle must test the RWG map mode and the flag' }
    # F2
    if ((Count $p ("TEXT(`"NodeShuffle.RwgSpawnAllAtRoll`"),`n        1,")) -ne 1) { $f += '(F2) NodeShuffle.RwgSpawnAllAtRoll must default to 1' }
    if ((Count $p ("TEXT(`"NodeShuffle.RwgProvisionalBatchPerPass`"),`n        16, // rwg-farspawn-1c C2")) -ne 1 -or (Count $p 'is 1. Default 16. Values are clamped to 8..512.') -ne 1) { $f += '(F2) NodeShuffle.RwgProvisionalBatchPerPass must default to 16 (1c C2) and its help text must say 16' }
    if ((Count $T.provh 'constexpr int32 RwgProvisionalBatchMin = 8;') -ne 1 -or (Count $T.provh 'constexpr int32 RwgProvisionalBatchMax = 512;') -ne 1) { $f += '(F2) batch clamp must be 8..512' }
    if ((Count $p 'return FMath::Clamp(CVarNodeShuffleRwgProvisionalBatch.GetValueOnGameThread(), RwgProvisionalBatchMin, RwgProvisionalBatchMax);') -ne 1) { $f += '(F2) the batch reader must clamp' }
    if ((Count $p 'CVarNodeShuffleRwgProvisionalBatch.GetValueOnGameThread()') -ne 1) { $f += '(F2) the batch CVar has exactly one (clamped) reader' }
    # F3
    if ((Count $T.provh '    double RwgProvisionalZ(const FRwgGroundQuery& Q, double X, double Y, FString& OutSource);') -ne 1) { $f += '(F3) seam declared once in the header' }
    if ((Count $p 'double NodeShuffleMapMode::RwgProvisionalZ(const FRwgGroundQuery& Q, double X, double Y, FString& OutSource)') -ne 1) { $f += '(F3) seam defined once' }
    if (-not $seamFn.Contains("    OutSource = TEXT(`"api`");`n    return Q.SurfaceZ(X, Y);`n}")) { $f += '(F3) seam body must be OutSource api + Q.SurfaceZ' }
    foreach ($bad in @(('Get' + 'World('), ('Line' + 'Trace'), ('Sweep' + 'Single'), ('Raycast' + 'Ground'))) { if ($seamFn.Contains($bad)) { $f += "(F3) the seam body must stay pure (found $bad)" } }
    if ((Count $T.provh 'OutSource = "api"') -ne 1 -or (Count $T.provh 'OutSource = "meshStack"') -ne 1) { $f += '(F3) the header must name both OutSource values' }
    if (-not $spawnFn.Contains('const double Z = NodeShuffleMapMode::RwgProvisionalZ(Q, E.Location.X, E.Location.Y, ZSource);')) { $f += '(F3) the spawn pass takes its Z from the seam' }
    # F4
    if (-not $spawnFn.Contains('if (Node->IsHidden()) { Node->SetActorHiddenInGame(false); }') -or -not $spawnFn.Contains('if (!Node->GetActorEnableCollision()) { Node->SetActorEnableCollision(true); }')) { $f += '(F4) provisional actors must be un-hidden with collision ON' }
    if ((Count $p ('SetActorHiddenInGame(' + 'true')) -ne 0) { $f += '(F4) nothing here may hide a node' }
    if (-not (Ordered $settleFn @('Node->SetActorEnableCollision(false);', 'bool bSettled = RwgResolveAtSpawn(E, bApiUsed, bWaterNoLand);', 'if (IsValid(Node)) { Node->SetActorEnableCollision(true); }', 'if (bSettled)'))) { $f += '(F4) collision is off only around the settle call' }
    if ((Count $settleFn ('SetActorEnableCollision(' + 'false')) -ne 1) { $f += '(F4) exactly one collision-off in the settle' }
    # F5
    if (-not (Ordered $settleFn @('if (bSettled)', 'E.bRwgProvisional = false;', 'E.bRayCasted = true;', 'Outcome = bRespawned ? TEXT("inPlace") : TEXT("settledNoActor");', 'else if (bWaterNoLand)', 'Outcome = !bMoved ? TEXT("redealNotMoved") : (RwgPcSteepRing > RingBefore) ? TEXT("ring") : TEXT("mapwide");', '    else', '        E = Snap;', '++RwgProvSessDeferred;'))) { $f += '(F5) the three outcomes, in order' }
    if (-not $settleFn.Contains('const TCHAR* Outcome = TEXT("deferred");')) { $f += '(F5) deferred is the default outcome' }
    # F6
    if (-not (Ordered $settleFn @('if (bSettled)', 'RwgProvisionalDespawn(E);', 'EnsureNewNodeSpawned(E, bChangedWorld);', 'else if (bWaterNoLand)', 'RwgProvisionalDespawn(E);', 'TryRedealWaterLockedEntry(E);'))) { $f += '(F6) despawn before respawn and before the redeal' }
    if (-not (Ordered $despFn @('Mgr->mResourceNodes.RemoveSingleSwap(Node);', 'RepMgr->RemoveRepresentationOfActor(Node);', 'Node->RemoveResourceNodeScan_Local();', 'Node->Destroy();', 'FNodeShuffleModule::UnregisterManagedNode(Node);', 'SpawnedNodes.Remove(E.EntryGuid);', 'SteadyAliveNodes.Remove(E.EntryGuid);'))) { $f += '(F6) the teardown order (RwgRedealLiveEntry parity)' }
    # F7
    if (-not (Ordered $settleFn @('if (RwgProvisionalPinWins(E)) { return; }', 'Node->SetActorEnableCollision(false);'))) { $f += '(F7) the pin is checked before the settle touches the actor' }
    if (-not $pinFn.Contains('if (!E.bPinned && !bOccupied) { return false; }') -or -not $pinFn.Contains('outcome=pinnedKept')) { $f += '(F7) pin wins: pinned or occupied keeps the node' }
    if (-not $T.pscan.Contains('if (!E.bIsNewNode || !E.bActive || E.bPinned) { return false; }')) { $f += '(F7) pinned entries are never provisioned' }
    # F8
    if ((Count $T.subh 'UPROPERTY(SaveGame) bool bRwgProvisional = false;') -ne 1) { $f += '(F8) the SaveGame flag, default false' }
    # F9
    if ((Count $T.rs '        if (E.bRwgProvisional) { continue; } // rwg-farspawn-1: a provisional actor is UNSETTLED') -ne 1) { $f += '(F9) resettle must skip provisional' }
    if ((Count $T.rock '        if (E.bRwgProvisional) { continue; } // rwg-farspawn-1: a provisional actor is UNSETTLED') -ne 1) { $f += '(F9) rock pass must skip provisional' }
    if ((Count $T.void '        if (E.bRwgProvisional) { continue; } // rwg-farspawn-1: a provisional actor is UNSETTLED') -ne 1) { $f += '(F9) killbox pass must skip provisional' }
    if ((Count $T.sub '        if (Entry.bRwgProvisional) { continue; } // rwg-farspawn-1: RWG provisional actors settle only through RwgSettleProvisional') -ne 1) { $f += '(F9) settle-near-players must skip provisional' }
    if ((Count $T.place 'if (E.bRwgProvisional && IsLocationNearAnyPlayer(E.Location, AttemptRadiusCm) && !RwgProvisionalPinWins(E)) { Cands.Emplace(FMath::Square(RwgNearestPawnDist2D(E.Location)), I); continue; }') -ne 1) { $f += '(F9) provisional within the attempt radius joins the attempt candidates' }
    if ((Count $T.place 'if (E.bRwgProvisional) { RwgStuckBeforeAttempt(E); RwgSettleProvisional(E, bChangedWorld);') -ne 1) { $f += '(F9) the attempt loop settles provisional entries' }
    if ((Count $T.place 'if (E.bRwgProvisional) { E.bRwgProvisional = false; ++RwgProvSessStale; }') -ne 1) { $f += '(F9) a flag without a live actor is cleared' }
    if (-not (Ordered $T.place @('    ++RwgPcPasses;', '    RwgProvisionalSpawnPass(Pawns, AttemptRadiusCm, FreshNoActor, bChangedWorld);', '    RwgResettlePass();'))) { $f += '(F9) the provisional pass runs after the attempt loop, before the resettle' }
    # F10
    if ((Count $p 'TEXT("PROVSPAWN: pass=%d spawnedThisPass=%d remainingCandidates=%d provisionalTotal=%d zSource=%s msBatch=%.1f refusedThisPass=%d refusedSession=%d batch=%d seaPending=%d caveDealtZ=%d rayCastedNoActor=%d seaDealFailed=%d withinRadiusProvisioned=%d withinRadiusCandidates=%d wetSpotSkipped=%d withinUndergroundSkipped=%d budgetHit=%s budgetMs=%.1f scanMs=%.1f candidates=%d candidateListBuild=%s candidateListBuilds=%d"),') -ne 1 -or
        (Count $p "RwgPcPasses, Spawned, Cands.Num() - Spawned - Refused, Total, *ZSource, MsBatch, Refused, RwgProvSessRefused, Batch, SeaPending, CaveDealtZ,`n            RayCastedNoActor, SeaDealFailed, WithinSpawned, WithinCands.Num(), WetSpot, WithinUnderground, BudgetHit, BudgetMs,`n            ScanMs, RwgProvCandList.Num(), ListBuild, RwgProvCandBuilds);") -ne 1) { $f += '(F10) PROVSPAWN format/args' }
    if ((Count $p 'TEXT("PROVSETTLE: entry=%s res=%s provZ=%.0f settledZ=%.0f dzCm=%.0f movedXYm=%.1f outcome=%s hitClass=%d apiUsed=%d onSolidTop=%d localMapRevealsBefore=%d localMapRevealsAfter=%d serverMapRevealsBefore=%d serverMapRevealsAfter=%d moveCheck=%s origin=%s"),') -ne 1 -or
        (Count $p "FVector::Dist2D(MovedTo, ProvAt) / 100.0, Outcome, HitClass, bApiUsed ? 1 : 0, E.bRwgOnSolidTop ? 1 : 0,`n            LocalRevealsBefore, LocalRevealsAfter, ServerRevealsBefore, ServerRevealsAfter, MoveCheck, RwgProvisionalOriginName(E.EntryGuid));") -ne 1) { $f += '(F10) PROVSETTLE format/args' }
    if ((Count $p 'TEXT("PROVSETTLE: entry=%s res=%s provZ=%.0f settledZ=%.0f dzCm=0 movedXYm=0.0 outcome=pinnedKept hitClass=-1 pinned=%d occupied=%d origin=%s"),') -ne 1 -or (Count $p 'E.bPinned ? 1 : 0, bOccupied ? 1 : 0, RwgProvisionalOriginName(E.EntryGuid));') -ne 1) { $f += '(F10) PROVSETTLE pinnedKept format' }
    if ((Count $p 'TEXT("PROVCENSUS: pass=%d provisional=%d settledFromProvisional=%d redealtFromProvisional=%d deferredProvisional=%d deferredEntries=%d pinnedProvisional=%d dzP50Cm=%.0f dzP90Cm=%.0f dzMaxCm=%.0f movedXYmMax=%.1f spawnedSession=%d withinRadiusProvisionedSession=%d refusedSession=%d staleFlagCleared=%d flagNoActor=%d spawnAllAtRoll=%d settledNoActor=%d redealNotMoved=%d movedInPlace=%d moveNotApplied=%d gatedEnclosed=%d gatedAllSolid=%d gatedRespawn=%d"),') -ne 1 -or
        (Count $p 'ProvPercentile(RwgProvSessDzCm, 0.5f), ProvPercentile(RwgProvSessDzCm, 0.9f), ProvPercentile(RwgProvSessDzCm, 1.0f), RwgProvSessMovedXYmMax,') -ne 1 -or
        (Count $p 'RwgPcPasses, Live, RwgProvSessSettled, RwgProvSessRedealt, RwgProvSessDeferred, RwgProvSessDeferredEntries.Num(), RwgProvSessPinned,') -ne 1 -or
        (Count $p ('RwgProvSessSpawned, RwgProvSessWithinRadius, RwgProvSessRefused, RwgProvSessStale, FlagNoActor, NodeShuffleMapMode::RwgSpawnAllAtRollOn() ? 1 : 0, RwgProvSessSettledNoActor, RwgProvSessRedealNotMoved,' + "`n        RwgProvSessMovedInPlace, RwgProvSessMoveNotApplied, RwgProvSessMovedEnclosed, RwgProvSessMovedShadowInside, RwgProvSessGatedRespawn);")) -ne 1) { $f += '(F10) PROVCENSUS format/args' }
    if ((Count $p ('remaining' + 'Unspawned')) -ne 0) { $f += '(F10) fix F10: the PROVSPAWN field is remainingCandidates (the old name is gone)' }
    # F11
    if ((Count $T.place 'solidTopRejectedFootprint=%d provisionalSpawned=%d provisionalSettled=%d"),') -ne 1 -or (Count $T.place 'RwgPcSolidTopRejFootprint, RwgPcProvSpawned, RwgPcProvSettled);') -ne 1) { $f += '(F11) PLACECENSUS tail fields/args' }
    if ((Count $T.place '    RwgPcProvSpawned = RwgPcProvSettled = 0; // rwg-farspawn-1') -ne 1) { $f += '(F11) per-pass reset' }
    # F12
    if ((Count $T.provh 'constexpr int32 RwgProvisionalSettleSamplesPerPass = 16;') -ne 1 -or (Count $p 'if (RwgProvSettleSamples < NodeShuffleMapMode::RwgProvisionalSettleSamplesPerPass)') -ne 2) { $f += '(F12) PROVSETTLE sample cap 16 (1e C4) at both sites' }
    if (-not $spawnFn.Contains('    if ((RwgPcPasses % 12) == 1) { RwgProvisionalEmitCensus(); }') -or (Count $p 'RwgProvisionalEmitCensus();') -ne 1) { $f += '(F12) PROVCENSUS on 12th passes, one call site' }
    if (-not (Ordered $spawnFn @('// RWG-only gate', '    if ((RwgPcPasses % 12) == 1) { RwgProvisionalEmitCensus(); }', '    if (!NodeShuffleMapMode::RwgSpawnAllAtRollOn() || Pawns.Num() == 0) { return; }', 'if (!Q.IsUsable()) { return; }'))) { $f += '(F12) fix F5: PROVCENSUS is emitted after the RWG gate and before the toggle/pawn/query returns' }
    if (-not (Ordered $spawnFn @('const FNodeShuffleEntry Snap = E;', 'EnsureNewNodeSpawned(E, bChangedWorld);', 'if (!IsValid(Node))', 'E = Snap;', 'RwgProvRefused.Add(E.EntryGuid);', 'E.bRayCasted = false;', 'E.bRwgProvisional = true;'))) { $f += '(F12) no actor -> entry restored and refused; actor -> unsettled + provisional' }
    # F13
    if ((Count $T.nsmod ('LOADED (2026-10-31-rwg-' + 'farspawn-1f)')) -ne 1 -or (Count $T.nsmod ('2026-10-31-rwg-' + 'farspawn-1e)')) -ne 0 -or (Count $T.nsmod ('2026-10-31-rwg-' + 'seedgas-1b)')) -ne 0) { $f += '(F13) marker farspawn-1e present once, farspawn-1d, farspawn-1b and seedgas-1b absent' }
    if (($p.Split("`n")).Count -ge 500) { $f += '(F13) NodeShuffleMapModeProvisional.cpp must stay under 500 lines' }
    # F15
    if (-not (Ordered $settleFn @('EnsureNewNodeSpawned(E, bChangedWorld);', 'AFGResourceNode* Fresh = SpawnedNodes.FindRef(E.EntryGuid);', 'const bool bRespawned = IsValid(Fresh);', 'if (bRespawned)', 'FreshAt = Fresh->GetActorLocation();', 'SettledZ = FreshAt.Z;', '++RwgProvSessSettled; ++RwgPcProvSettled;', '    }', 'Outcome = bRespawned ?'))) { $f += '(F15) inPlace, settledZ and the settled counters need the respawned actor' }
    if ((Count $settleFn ('SettledZ = ' + 'E.')) -ne 0 -or (Count $settleFn 'SettledZ = ') -ne 3) { $f += '(F15) settledZ is assigned only from the respawned actor' }
    if ((Count $settleFn 'const bool bMoved = TryRedealWaterLockedEntry(E);') -ne 1 -or (Count $settleFn 'if (bMoved) { ++RwgProvSessRedealt; }') -ne 1) { $f += '(F15) the redeal result gates redealt and the ring/mapwide outcome' }
    if ((Count $settleFn 'const FVector MovedTo = bFresh ? FreshAt : E.Location;') -ne 1) { $f += '(F15) movedXYm measures to the respawned actor when there is one' }
    # F15 (re-review V-R3): the two no-actor outcomes have session counters, bumped 1:1 with their Outcome strings
    if (-not (Ordered $settleFn @('++RwgProvSessSettled; ++RwgPcProvSettled;', "        }`n        else { ++RwgProvSessSettledNoActor; }`n        Outcome = bRespawned ?"))) { $f += '(F15) V-R3: settledNoActor is counted exactly when the respawn left no actor' }
    if ((Count $settleFn 'if (bMoved) { ++RwgProvSessRedealt; } else { ++RwgProvSessRedealNotMoved; }') -ne 1) { $f += '(F15) V-R3: redealNotMoved is counted exactly when the redeal returned false' }
    # F15 (re-review V-R4): the unplaceable branch counts as a deferral, as the fresh path's unsettled water branch does; (a) does not, (c) still does
    $branchB = Span $settleFn '    else if (bWaterNoLand)' "    else`n    {"
    if ($branchB -eq '' -or (Count $branchB '        ++DeferredThisPass; // parity with EnsureNewNodeSpawned') -ne 1 -or -not (Ordered $branchB @('const bool bMoved = TryRedealWaterLockedEntry(E);', '++RwgProvSessRedealNotMoved; }', '        ++DeferredThisPass;', 'Outcome = !bMoved ?'))) { $f += '(F15) V-R4: the unplaceable branch bumps DeferredThisPass once, after the redeal' }
    if ((Count $settleFn '++DeferredThisPass;') -ne 2) { $f += '(F15) V-R4: DeferredThisPass is bumped in exactly the unplaceable and the deferred branches' }
    if ((Count $p '++RwgProvSessSettledNoActor;') -ne 1 -or (Count $p '++RwgProvSessRedealNotMoved;') -ne 1) { $f += '(F15) V-R3: one increment site per no-actor counter' }
    if ((Count $T.subh '    int32 RwgProvSessSettledNoActor = 0, RwgProvSessRedealNotMoved = 0; // session') -ne 1) { $f += '(F15) V-R3: the two session counters are declared on their own line (not inside a comment)' }
    # F16 (fix F6, measure only): reveal counts read before the despawn, after-counts only from the respawned actor
    if (-not (Ordered $settleFn @('const int32 LocalRevealsBefore = Node->mLocalMapReveals, ServerRevealsBefore = Node->mServerMapReveals;', 'Node->SetActorEnableCollision(false);', 'RwgProvisionalDespawn(E);', 'if (bRespawned)', 'LocalRevealsAfter = Fresh->mLocalMapReveals; ServerRevealsAfter = Fresh->mServerMapReveals;'))) { $f += '(F16) reveal counts: before the despawn, after only from the respawned actor' }
    if ((Count $p ('mLocalMapReveals ' + '=')) -ne 0 -or (Count $p ('mServerMapReveals ' + '=')) -ne 0 -or (Count $p ('MapReveals' + '++')) -ne 0) { $f += '(F16) fix F6 is measure-only: nothing here writes a reveal count' }
    # F17 (fix F7): an actorless settled entry is never provisioned; a sea-flagged spot the sea pass could not re-deal stays sea-pending
    if (-not (Ordered $T.pscan @('if (bWithin && !FreshNoActor.Contains(I)) { return false; }', 'if (E.bRayCasted) { ++RayCastedNoActor; return false; }', 'if (RwgSeaDealFailed.Contains(E.EntryGuid)) { ++SeaPending; ++SeaDealFailed; return false; }', 'const bool bSeaDone = bSeaPassIdle || (E.bRwgSeaChecked && (E.bRwgVoidChecked || !bVoidMaybe));', 'if (bWithin) { OutWithin.Add(I); }'))) { $f += '(F17) the candidate loop must skip rayCasted entries and sea-deal-failed entries before the sea-done test' }
    if ((Count $T.pscan 'RwgSeaDealFailed.Contains(') -ne 1 -or (Count $spawnFn 'RwgSeaDealFailed.Contains(') -ne 0) { $f += '(F17) RwgSeaDealFailed is read once, as a skip (never as sea-done)' }
    if ((Count $spawnFn 'which the sea pass skips for the rest of the session, so the sea pass never clears them from this count') -ne 1) { $f += '(F17) R7: the seaPending legend must say it includes the sea-deal-failed entries the sea pass never revisits' }
    # F18 (fix F9): the toggle's help text claims no universality or simultaneity and names the radius rule and the skips
    $help = Span $p 'TEXT("NodeShuffle.RwgSpawnAllAtRoll"),' 'ECVF_Default);'
    if ($help -eq '') { $f += '(F18) the RwgSpawnAllAtRoll help text span is missing' }
    else {
        foreach ($bad in @(('every new ' + 'node'), ('at ' + 'once'), ('height read from ' + 'the world generator'))) { if ($help.Contains($bad)) { $f += "(F18) the toggle help text must not claim: $bad" } }
        foreach ($need in @('(1 km, or the ")', 'up to a batch per spawn pass', '(parked, dormant, refused, pinned, or already fitted to the ground) are skipped', '(cave spots at their dealt cave floor)', 'tries to re-fit', 'cleared them (or without it where this world has no usable sea check);')) { if (-not $help.Contains($need)) { $f += "(F18) the toggle help text must say: $need" } }
    }
    # F14
    if ((Count $T.place 'if (E.bRwgProvisional) { RwgStuckBeforeAttempt(E); RwgSettleProvisional(E, bChangedWorld); AFGResourceNode* const* Now = SpawnedNodes.Find(E.EntryGuid); if (Now && IsValid(*Now) && !E.bRwgProvisional) { ++RwgPcSpawned; } RwgStuckAfterAttempt(E); continue; }') -ne 1) { $f += '(F14) the provisional settle must be bracketed by RwgStuckBeforeAttempt / RwgStuckAfterAttempt' }
    if ((Count $T.dens 'if (Now && IsValid(*Now) && !E.bRwgProvisional) { RwgStuckTracker.Remove(E.EntryGuid); return; }') -ne 1) { $f += '(F14) the RWGSTUCK note must not forget a provisional entry as live' }
    if ((Count $T.dens 'if (Live && IsValid(*Live) && !E.bRwgProvisional) { continue; }') -ne 1) { $f += '(F14) the RWGSTUCK census must not skip a provisional entry as live' }
    if ((Count $T.dens 'const bool bLive = Live && IsValid(*Live) && !E.bRwgProvisional;') -ne 1) { $f += '(F14) HERE-DENSITY live must exclude provisional actors' }
    if ((Count $T.dens 'a provisional node, placed but not yet fitted to the ground, is not counted as live') -ne 1) { $f += '(F14) the HERE-DENSITY legend must say provisional nodes are not live' }
    # F14 (re-review V-R1): HERE-NEAREST-UNSPAWNED / HERE-UNSPAWNED-BANDS deliberately count a provisional actor as spawned; the comment says so
    if ((Count $T.here 'A provisional actor (rwg-farspawn-1) HAS an actor and is not in it, while HERE-DENSITY counts it as not live (fix F3): the two lines are not complements.') -ne 1 -or (Count $T.here ("(HERE-DENSITY's live" + "`n// test, negated)")) -ne 0) { $f += '(F14) V-R1: the HereUnspawned population comment must say a provisional actor is in neither line while HERE-DENSITY counts it not live' }
    # F19 (1c C1): branch (c) keeps the actor; the SAME actor is moved only when MoveCheck says moved (XY within tolerance, class and
    # resource unchanged); every other settle despawns + respawns; a redeal despawns before it moves the entry.
    if ((Count $settleFn 'RwgProvisionalDespawn(E);') -ne 2) { $f += '(F19) the settle despawns in exactly the respawn and redeal branches (none in the deferred branch)' }
    $branchC = Span $settleFn '        // (c) no terrain-mesh hit in the window' '    bRwgSteepArmed = false;'
    if ($branchC -eq '' -or $branchC.Contains('Despawn') -or -not $branchC.Contains('E = Snap;')) { $f += '(F19) the deferred branch restores the entry and never despawns' }
    if ((Count $settleFn 'if (bSettled && FCString::Strcmp(MoveCheck, TEXT("moved")) == 0)') -ne 2 -or (Count $settleFn '{ if (const TCHAR* MoveRefused = RwgProvisionalMoveInPlace(E, Node)) { MoveCheck = MoveRefused; } }') -ne 1 -or (Count $settleFn 'RwgProvisionalMoveInPlace(') -ne 1 -or (Count $settleFn '    else if (bSettled)') -ne 1) { $f += '(F19) move-in-place only on MoveCheck moved; else the respawn branch' }
    if (-not (Ordered $settleFn @('const TCHAR* MoveCheck = bSettled ? RwgProvisionalMoveCheck(E, Node, ProvAt) : TEXT("notSettled");', '{ if (const TCHAR* MoveRefused = RwgProvisionalMoveInPlace(E, Node)) { MoveCheck = MoveRefused; } }', '    if (bSettled && FCString::Strcmp(MoveCheck, TEXT("moved")) == 0)', 'Outcome = TEXT("movedInPlace");', '    else if (bSettled)', 'RwgProvisionalDespawn(E);', 'EnsureNewNodeSpawned(E, bChangedWorld);'))) { $f += '(F19) the move branch precedes the despawn + respawn branch' }
    $mv = $T.move
    if ((Count $mv 'if (FVector::Dist2D(E.Location, ProvAt) > NodeShuffleMapMode::RwgProvisionalMoveTolCm) { return TEXT("xyShift"); }') -ne 1 -or
        (Count $mv 'if (Node->GetClass()->GetPathName() != ResolveSpawnNodeClassPath(E)) { return TEXT("classDiffers"); }') -ne 1 -or
        (Count $mv 'if (!Res || Res->GetPathName() != E.AssignedResourceClassPath) { return TEXT("resourceDiffers"); }') -ne 1 -or
        (Count $T.provh 'constexpr double RwgProvisionalMoveTolCm = 300.0;') -ne 1) { $f += '(F19) MoveCheck: xy tolerance 300 cm, same class, same resource' }
    $moveFn = Span $mv 'const TCHAR* ANodeShuffleSubsystem::RwgProvisionalMoveInPlace(' 'void ANodeShuffleSubsystem::RwgProvisionalLogRefusal('
    if ($moveFn -eq '' -or -not (Ordered $moveFn @('Node->SetActorLocationAndRotation(E.Location, E.Rotation);', 'RadSub->RemoveEmitters(Node);', 'Node->InitRadioactivity();', 'Node->UpdateRadioactivity();', 'Node->UpdateNodeRepresentation();', 'NodeRepUpdated = RepMgr->UpdateRepresentation(NodeRep) ? 1 : 0;', 'SpawnVisualRockForNode(Node, RC, E.EntryGuid);')) -or $moveFn.Contains('RemoveRepresentation') -or $moveFn.Contains('Destroy(')) { $f += '(F19) the move re-runs radiation, representation and rock dress and never removes the representation or destroys' }
    if ((Count $p 'Outcome = bRespawned ? TEXT("inPlace") : TEXT("settledNoActor");') -ne 1) { $f += '(F19) the respawn branch keeps its inPlace / settledNoActor outcome' }
    # F20 (1c C3): the PLACECENSUS print gate also fires on provisional passes (the line itself is pinned by F11 / rwgsteep S6)
    if ((Count $T.place '    if (RwgPcSpawned > 0 || RwgPcProvSpawned > 0 || RwgPcProvSettled > 0 || (RwgPcPasses % 12) == 1) // rwg-farspawn-1c C3') -ne 1) { $f += '(F20) PLACECENSUS gate includes provisional spawned and settled' }
    # F21 (1c C4/C5/C6/C7): field lists, detector anchoring, call sites
    if ((Count $T.scan 'TEXT("SCANPING: resource=%s clustersForResource=%d clustersTotal=%d marked=%d nearestMarkedM=%.0f nearestLiveActorM=%.0f nearestProvisionalM=%.0f nearestNoActorM=%.0f pawn=%s pawnFrom=%s nearestLiveClass=%s nearestLiveInLayout=%d markedNearestClass=%s nearestDepositM=%.0f"),') -ne 1 -or (Count $T.scan '*PawnAt.ToCompactString(), P ? TEXT("instigator") : TEXT("scanner"),') -ne 1 -or (Count $T.scan 'LiveNearest ? *LiveNearest->GetClass()->GetName() : TEXT("<none>"), LiveInLayout,') -ne 1 -or (Count $T.scan 'MarkedNearest ? *MarkedNearest->GetClass()->GetName() : TEXT("<none>"), NsOrNeg(Deposit));') -ne 1) { $f += '(F21) SCANPING field list (1d F10 pawnFrom, 1e C3 class/layout/deposit fields)' }
    if ((Count $T.scan 'SUBSCRIBE_UOBJECT_METHOD(AFGResourceScanner, GetClosetClusters,') -ne 1 -or (Count $T.scan 'if (!FNodeShuffleModule::AreDiagnosticsEnabled() || !IsValid(Self) || !Self->GetWorld()) { return; }') -ne 1) { $f += '(F21) SCANPING hooks GetClosetClusters, diagnostics-gated' }
    if ((Count $T.scan 'TEXT("COMPASSWARN: rescan=%d removedWarnings=%d hiddenOriginalsInTowerRadius=%d provisional=%d"),') -ne 1 -or (Count $T.scan ('hiddenOriginals' + 'Stripped')) -ne 0) { $f += '(F21) COMPASSWARN field list (1d F5 rename, old name absent)' }
    if ((Count $T.scan 'if (FCString::Strncmp(V, *Prefix, Prefix.Len()) == 0)') -ne 1 -or (Count $T.scan '(Verbosity & ELogVerbosity::VerbosityMask) != ELogVerbosity::Warning || Category != LogGameName') -ne 1 -or (Count $T.scan ('Str' + 'str(')) -ne 0 -or (Count $T.scan ('.Con' + 'tains(V')) -ne 0) { $f += '(F21) the compass counter anchors on the message start, the LogGame category and Warning verbosity (no substring match)' }
    if ((Count $T.sub '        EmitCompassWarnCensus(TowersRescanned); // rwg-farspawn-1c C5') -ne 1 -or (Count $T.nsmod '    ANodeShuffleSubsystem::InstallScanDiagHooks(); // rwg-farspawn-1c C4/C5') -ne 1) { $f += '(F21) COMPASSWARN beside SCANREGEN; hooks installed at startup' }
    if ((Count $T.move 'TEXT("PROVSPAWN: refused entry=%s res=%s at=%s refusedReason=%s refusedSession=%d"),') -ne 1 -or (Count $p 'RwgProvisionalLogRefusal(E, Reason);') -ne 1 -or (Count $p 'RwgProvisionalLogRefusal(E, TEXT("zNonFinite"));') -ne 1) { $f += '(F21) PROVSPAWN refusedReason at both refusal sites' }
    if (-not (Ordered $spawnFn @('const TCHAR* Reason = !E.bActive ? TEXT("inactiveAfterSpawn")', 'E = Snap;', 'RwgProvisionalLogRefusal(E, Reason);'))) { $f += '(F21) refusedReason is read before the snapshot restore' }
    if ((Count $T.wmove 'TEXT("WELLMOVE: core=%s claim=%s from=%s to=%s distM=%.1f reason=%s nudges=%d redeals=%d"),') -ne 1) { $f += '(F21) WELLMOVE field list' }
    $wm = 'WellMoveLog('
    if ((Count $T.wroll $wm) -ne 2 -or (Count $T.escal $wm) -ne 3 -or (Count $T.sea $wm) -ne 1 -or (Count $T.rock $wm) -ne 1 -or (Count $T.wapply $wm) -ne 1) { $f += '(F21) WELLMOVE at all 8 well location writers (roll 2, escalate 3, sea 1, rock 1, apply 1)' }
    foreach ($r in @('rollDeal', 'dealCleared', 'nudge', 'rwgRedeal', 'boxRedeal', 'seaRedeal', 'resettleRedeal', 'placedCommit')) {
        if ((Count ($T.wroll + $T.escal + $T.sea + $T.rock + $T.wapply) ('TEXT("' + $r + '"))')) -ne 1) { $f += "(F21) WELLMOVE reason $r appears once" }
    }
    # F22 (1c C8): legends-review W1 and the header counter comment
    if ((Count $p 'a redeal that returned true also leaves no actor until') -ne 1 -or (Count $p ('settle outcomes that ended with ' + 'no actor')) -ne 0) { $f += '(F22) W1 PROVCENSUS legend' }
    if ((Count $T.subh 'session: settle outcomes counted by the settledNoActor and redealNotMoved branches (re-review V-R3)') -ne 1 -or (Count $T.subh ('F4 outcomes with ' + 'no actor')) -ne 0) { $f += '(F22) the header counter comment names the branches, not a no-actor claim' }
    foreach ($k in @('move', 'scan', 'wmove')) { if (($T[$k].Split("`n")).Count -ge 500) { $f += "(F22) new file $k stays under 500 lines" } }
    # F23 (1d F1): the move reads back; a move the actor did not take is printed (ungated), counted, and respawned by the caller
    if ($moveFn -eq '' -or -not (Ordered $moveFn @('Node->SetActorLocationAndRotation(E.Location, E.Rotation);', 'const FVector After = Node->GetActorLocation();', 'if (FVector::Dist(After, E.Location) > 10.0)', '++RwgProvSessMoveNotApplied;', 'TEXT("PROVMOVE: entry=%s moveCheck=moveNotApplied requested=%s actorAfter=%s distCm=%.0f notAppliedSession=%d"),', 'return TEXT("moveNotApplied"); // nothing below ran', 'IsSpotEnclosed(', 'MeshActorCache.Remove(', 'return nullptr; // kept and moved'))) { $f += '(F23) the readback follows the move and precedes every other step; false on a move not applied, true at the end' }
    if ($moveFn -eq '' -or (Count $moveFn 'return TEXT("moveNotApplied");') -ne 2 -or $moveFn.IndexOf('moveCheck=moveNotApplied') -lt 0 -or $moveFn.IndexOf('moveCheck=moveNotApplied') -gt $moveFn.IndexOf('AreDiagnosticsEnabled')) { $f += '(F23) the not-applied line prints ungated (before the diagnostics gate); exactly two false returns' }
    if ((Count $T.subh 'const TCHAR* RwgProvisionalMoveInPlace(FNodeShuffleEntry& E, class AFGResourceNode* Node);') -ne 1 -or (Count $T.subh 'bool RwgProvisionalMoveInPlace(') -ne 0 -or (Count $mv 'bool ANodeShuffleSubsystem::RwgProvisionalMoveInPlace(') -ne 0) { $f += '(F23) MoveInPlace returns the refusal word or nullptr (declaration and definition; 1f C2)' }
    # F24 (1d F2): shadow enclosure + inside gate at the settled spot, this actor excluded; printed + counted; never refusing
    if ((Count $moveFn 'const bool bEnclosed = IsSpotEnclosed(E.Location, EncBlocked, EncTotal, nullptr, nullptr, Node);') -ne 1 -or (Count $moveFn 'const bool bRefuseIgnored = EvaluateInsideShadowGate(E.Location, Node,') -ne 1) { $f += '(F24) both shadow gates run at the settled spot with this actor excluded' }
    if ((Count $moveFn ' onSolidTop=%d enclosed=%d encBlocked=%d/%d shadowGate=%s refuseIgnored=%d nodeRepUpdateReturned=%d"),') -ne 1 -or (Count $moveFn 'bEnclosed ? 1 : 0, EncBlocked, EncTotal, ShadowGate, bRefuseIgnored ? 1 : 0, NodeRepUpdated);') -ne 1) { $f += '(F24) PROVMOVE prints enclosed / encBlocked / shadowGate / refuseIgnored / nodeRepUpdated' }
    if ((Count $moveFn '    if (bEnclosed) { ++RwgProvSessMovedEnclosed; }') -ne 1 -or (Count $moveFn '    if (bAllSolid) { ++RwgProvSessMovedShadowInside; }') -ne 1) { $f += '(F24) the two shadow outcomes are counted for PROVCENSUS' }
    if ($moveFn.Contains('bActive = false') -or $moveFn.Contains('Destroy(') -or $moveFn.Contains('bRwgProvisional')) { $f += '(F24) the measurement never refuses the NODE here: no deactivation, destroy or flag write in the move (1f C2 refuses the move only)' }
    # F25 (1d F3, F5-F10): the reversed rule is cited; legends and labels
    if ((Count $mv 'This REVERSES the DESPAWN-ON-MOVE rule (NodeShuffleWellDespawn.cpp:23,70') -ne 1 -or (Count $mv 'if (UFGResourceNodeRepresentation* NodeRep = RepMgr->FindResourceNodeRepresentation(Node))') -ne 1) { $f += '(F25) F3: the DESPAWN-ON-MOVE citation and the node-representation update' }
    if ((Count $p 'the settledNoActor and redealNotMoved fields are session counts of two settle outcomes.') -ne 1 -or (Count $p ('the last two ' + 'fields')) -ne 0) { $f += '(F25) F6 PROVCENSUS legend names its fields' }
    if ((Count $T.subh 'int32 RwgProvSessMovedInPlace = 0; // session: settles that moved the same actor (PROVCENSUS movedInPlace)') -ne 1 -or (Count $T.subh 'void EmitCompassWarnCensus(int32 TowersRescanned); // COMPASSWARN, beside SCANREGEN inside its diagnostics gate') -ne 1) { $f += '(F25) F7 header comments on their own declarations' }
    if ((Count $T.wmove 'ClearAbandonedWellPlacement zeroes PlacedCoreLocation unlogged') -ne 1 -or (Count $T.wmove ('at every site ' + 'that writes')) -ne 0) { $f += '(F25) F8 WELLMOVE legend no longer claims every site' }
    if ((Count $mv 'inactiveAfterSpawn (the spawn call') -ne 1 -or (Count ($p + $mv) ('overlap' + 'GaveUp')) -ne 0) { $f += '(F25) F9 refusedReason label and legend say inactiveAfterSpawn' }
    # F26 (1e C1): within-radius provisioning population, wet-spot predicate (both origins), lock-spot notes, origin values
    if ((Count $T.place '    TSet<int32> FreshNoActor; // rwg-farspawn-1e C1') -ne 1 -or (Count $T.place 'if (Now && IsValid(*Now)) { ++RwgPcSpawned; } else { FreshNoActor.Add(Cands[K].Value); } // rwg-farspawn-1e C1') -ne 1 -or (Count $T.place 'FreshNoActor.Add(') -ne 1) { $f += '(F26) FreshNoActor is fed only by the fresh branch of the attempt loop, when it left no actor' }
    if (-not (Ordered $T.pscan @('const bool bWithin = IsLocationNearAnyPlayer(E.Location, AttemptRadiusCm);', 'if (bWithin && !FreshNoActor.Contains(I)) { return false; }', 'if (bWithin && E.bUnderground) { ++WithinUnderground; return false; }', 'if (E.bRayCasted) { ++RayCastedNoActor; return false; }', 'if (!bSeaDone) { ++SeaPending; return false; }', '        if (RwgProvisionalAtWetSpot(E)) { ++WetSpot; return false; }', 'if (bWithin) { OutWithin.Add(I); }', '        return true;'))) { $f += '(F26) the candidate loop: within needs a fresh no-actor attempt, skips underground, then the shared skips incl. the wet spot' }
    if ((Count $T.pscan 'IsLocationNearAnyPlayer(') -ne 1 -or (Count $T.pscan 'RwgProvisionalAtWetSpot(E)') -ne 1 -or (Count $spawnFn 'IsLocationNearAnyPlayer(') -ne 0) { $f += '(F26) one distance test and one wet-spot test in the spawn pass (shared by both origins)' }
    $wetFn = Span $p 'bool ANodeShuffleSubsystem::RwgProvisionalAtWetSpot(' 'const TCHAR* ANodeShuffleSubsystem::RwgProvisionalOriginName('
    if ($wetFn -eq '' -or -not (Ordered $wetFn @('if (!WaterLockedThisSession.Contains(E.EntryGuid)) { return false; }', 'const FVector* At = RwgWetSpotThisSession.Find(E.EntryGuid);', 'return !At || FVector::Dist2D(*At, E.Location) <= NodeShuffleMapMode::RwgProvisionalMoveTolCm;'))) { $f += '(F26) wet spot = water-locked and within the move tolerance of the recorded lock spot (or none recorded)' }
    $noteSub = "            RwgNoteWetSpot(Entry); // rwg-farspawn-1e C1: the lock spot (provisioning skips an entry still there)`n            WaterLockedThisSession.Add(Entry.EntryGuid);"
    if ((Count $T.sub $noteSub) -ne 2 -or (Count $T.sub ('WaterLockedThisSession.' + 'Add(')) -ne 2 -or (Count $p ('WaterLockedThisSession.' + 'Add(')) -ne 1) { $f += '(F26) the lock spot is noted directly before each WaterLockedThisSession.Add (two in the subsystem, one here)' }
    if (-not (Ordered $settleFn @('    else if (bWaterNoLand)', 'RwgNoteWetSpot(E);', 'RwgProvisionalDespawn(E);', 'WaterLockedThisSession.Add(E.EntryGuid);', 'TryRedealWaterLockedEntry(E);'))) { $f += '(F26) branch (b) notes the lock spot before the redeal moves the entry' }
    if ((Count $p 'return !W ? TEXT("notThisSession") : *W ? TEXT("withinRadius") : TEXT("far");') -ne 1 -or (Count $spawnFn 'RwgProvOriginWithin.Add(E.EntryGuid, bWithinCand);') -ne 1 -or -not (Ordered $spawnFn @('++Spawned; ++RwgProvSessSpawned; ++RwgPcProvSpawned;', 'const bool bWithinCand = WithinCands.Contains(Cands[K]);', 'RwgProvOriginWithin.Add(E.EntryGuid, bWithinCand);', "        if (bWithinCand)`n        {`n            ++WithinSpawned; ++RwgProvSessWithinRadius;"))) { $f += '(F26) origin recorded on each provisional spawn; withinRadius / far / notThisSession' }
    # F27 (1e C2): time budget beside the hard count cap
    if ((Count $p ("TEXT(`"NodeShuffle.RwgProvisionalBatchMs`"),`n        8.0f,")) -ne 1 -or (Count $T.provh 'constexpr float RwgProvisionalBatchMsMin = 1.0f;') -ne 1 -or (Count $T.provh 'constexpr float RwgProvisionalBatchMsMax = 50.0f;') -ne 1 -or (Count $p 'return FMath::Clamp(CVarNodeShuffleRwgProvisionalBatchMs.GetValueOnGameThread(), RwgProvisionalBatchMsMin, RwgProvisionalBatchMsMax);') -ne 1 -or (Count $p 'CVarNodeShuffleRwgProvisionalBatchMs.GetValueOnGameThread()') -ne 1) { $f += '(F27) NodeShuffle.RwgProvisionalBatchMs default 8.0, clamped 1..50 at its only reader' }
    if (-not (Ordered $spawnFn @('const double T0 = FPlatformTime::Seconds();', 'RwgProvisionalCollectCandidates(Pawns, AttemptRadiusCm, FreshNoActor, Cands,', 'const int32 N = FMath::Min(Cands.Num(), Batch);', 'const double BudgetMs = NodeShuffleMapMode::RwgProvisionalBatchMs();', '    for (; K < N; ++K)', "    {`n        if (K > 0 && (FPlatformTime::Seconds() - T0) * 1000.0 >= BudgetMs) { BudgetHit = TEXT(`"time`"); break; }", 'FNodeShuffleEntry& E = Layout[Cands[K]];', 'if (K == N && Cands.Num() > N) { BudgetHit = TEXT("count"); }', 'const double MsBatch = (FPlatformTime::Seconds() - T0) * 1000.0;'))) { $f += '(F27) budget checked first in the loop body on the msBatch clock; count cap kept; count label after the loop' }
    if ((Count $spawnFn 'const TCHAR* BudgetHit = TEXT("none");') -ne 1) { $f += '(F27) budgetHit defaults to none' }
    # F28 (1e C3): deposits apart, layout membership by registry
    $pingFn = Span $T.scan 'void ANodeShuffleSubsystem::EmitScanPing(' 'void ANodeShuffleSubsystem::EmitCompassWarnCensus('
    if ($pingFn -eq '' -or -not (Ordered $pingFn @('ProvActors.Contains(N)) { continue; }', 'if (N->IsA<AFGResourceDeposit>()) { Deposit = FMath::Min(Deposit, D); continue; }', 'if (D < Live) { Live = D; LiveNearest = N; }'))) { $f += '(F28) deposits are excluded from the live set before it is updated' }
    if ((Count $pingFn 'const int32 LiveInLayout = LiveNearest ? (FNodeShuffleModule::IsManagedSpawnedNode(LiveNearest) ? 1 : 0) : -1;') -ne 1 -or (Count $pingFn 'IsManagedSpawnedNode(') -ne 1) { $f += '(F28) nearestLiveInLayout comes from the managed-node registry' }
    if ((Count $T.scan '#include "Resources/FGResourceDeposit.h"') -ne 1) { $f += '(F28) the deposit header is included' }
    # F29 (1e C4): distinct deferred entries
    if (-not (Ordered $settleFn @('        ++RwgProvSessDeferred;', '        RwgProvSessDeferredEntries.Add(E.EntryGuid);', '    bRwgSteepArmed = false;')) -or (Count $p 'RwgProvSessDeferredEntries.Add(') -ne 1) { $f += '(F29) deferredEntries: one Add, in the deferred branch beside the event count' }
    # F30 (1f C2): inside / enclosed moves take the gated branch
    $gIdx = $moveFn.IndexOf('moveCheck=gatedRespawn')
    if ($moveFn -eq '' -or -not (Ordered $moveFn @('const bool bAllSolid = InsideGateWouldRefuseThisPass > GateInsideBefore;', '    if (bRefuseIgnored)', '--InsideGateRefusedThisPass;', '    if (bEnclosed) { ++RwgProvSessMovedEnclosed; }', '    if (bEnclosed || bAllSolid)', '++RwgProvSessGatedRespawn;', 'TEXT("PROVMOVE: entry=%s moveCheck=gatedRespawn enclosed=%d encBlocked=%d/%d shadowGate=%s refuseIgnored=%d at=%s gatedRespawnSession=%d"),', 'return TEXT("gatedRespawn");', 'MeshActorCache.Remove(', 'RadSub->RemoveEmitters(Node);', 'return nullptr; // kept and moved'))) { $f += '(F30) enclosed or allSolid returns gatedRespawn before the move''s radiation / representation / rock steps' }
    if ($gIdx -lt 0 -or $gIdx -gt $moveFn.IndexOf('AreDiagnosticsEnabled') -or (Count $moveFn 'return TEXT("gatedRespawn");') -ne 1 -or (Count $moveFn '--InsideGateRefusedThisPass;') -ne 1) { $f += '(F30) the gatedRespawn line prints ungated; one gated return; the 1d F1 decrement' }
    if ((Count $moveFn 'TEXT("allSolid")') -ne 1 -or (Count $moveFn 'TEXT("escapeFound")') -ne 1 -or $mv.Contains('TEXT("in' + 'side")') -or $mv.Contains('TEXT("out' + 'side")') -or $mv.Contains('nodeRepUpd' + 'ated=')) { $f += '(F30) 1d F3/F4 labels: allSolid / escapeFound, nodeRepUpdateReturned (old labels absent)' }
    if ((Count $p ' gatedEnclosed=%d gatedAllSolid=%d gatedRespawn=%d"),') -ne 1 -or $p.Contains('moved' + 'Enclosed=') -or $p.Contains('movedShadow' + 'Inside=')) { $f += '(F30) PROVCENSUS gatedEnclosed / gatedAllSolid / gatedRespawn (old field names absent)' }
    # F31 (1f C3): the per-session candidate list
    $ps = $T.pscan
    foreach ($w in @('TEXT("initial")', 'TEXT("layoutNum")', 'TEXT("guidMismatch")', '? TEXT("periodic") : nullptr;')) { if ((Count $ps $w) -ne 1) { $f += "(F31) build trigger $w must appear once" } }
    if ((Count $ps ': (RwgPcPasses - RwgProvCandBuiltPass >= NodeShuffleMapMode::RwgProvisionalCandRebuildPasses) ? TEXT("periodic") : nullptr;') -ne 1 -or (Count $T.provh 'constexpr int32 RwgProvisionalCandRebuildPasses = 12;') -ne 1) { $f += '(F31) periodic rebuild every 12 passes (constant, no console variable)' }
    if ((Count $ps 'if (!Layout.IsValidIndex(C.Value) || Layout[C.Value].EntryGuid != C.Key) { Why = TEXT("guidMismatch"); break; }') -ne 1) { $f += '(F31) the structural (guid, index) check forces a build' }
    if (-not (Ordered $ps @('Sorted.Sort(', 'RwgProvCandBuiltPass = RwgPcPasses;', 'RwgProvCandBuiltLayoutNum = Layout.Num();', '        RwgProvCandDirtyWhy = nullptr;', '++RwgProvCandBuilds;', 'for (const int32 I : FreshNoActor)', '        if (!SetAsideOk(Layout[I])) { continue; }', 'if (Tested.Contains(I)) { continue; }', 'RwgProvCandList.SetNum(W);'))) { $f += '(F31) build clears the mark; FreshNoActor first; set-aside entries dropped; list compacted' }
    if ((Count $T.sub 'RwgProvCandMarkDirty(bIsReroll ? TEXT("reroll") : TEXT("roll"));') -ne 1 -or (Count $T.sub 'RwgProvCandMarkDirty(TEXT("redeal")); // rwg-farspawn-1f C3: a redeal moves') -ne 1 -or (Count $T.rock 'RwgProvCandMarkDirty(TEXT("redeal")); // rwg-farspawn-1f C3: this live entry') -ne 1 -or (Count $p 'RwgProvCandMarkDirty(TEXT("redeal")); // rwg-farspawn-1f C3: this entry loses') -ne 1 -or (Count $p 'RwgProvCandMarkDirty(TEXT("pin"));') -ne 1 -or (Count $T.adopt 'RwgProvCandMarkDirty(TEXT("reload"));') -ne 1) { $f += '(F31) dirty marks at roll/re-roll, both redeal sites, branch (b), pin and reload' }
    if ($spawnFn.Contains('for (int32 I = 0; I < Layout.Num(); ++I)') -or $spawnFn.Contains('Cands.Sort(') -or -not (Ordered $spawnFn @('RwgProvisionalCollectCandidates(', '    const double ScanMs = (FPlatformTime::Seconds() - T0) * 1000.0;', '    for (; K < N; ++K)'))) { $f += '(F31) the spawn pass takes the list (no Layout walk, no sort) and measures scanMs before the first try' }
    if ((Count $p ' budgetHit=%s budgetMs=%.1f scanMs=%.1f candidates=%d candidateListBuild=%s candidateListBuilds=%d"),') -ne 1) { $f += '(F31) PROVSPAWN prints scanMs / candidates / candidateListBuild / candidateListBuilds' }
    if ((Count $spawnFn 'TEXT("PROVSPAWN: withinRadius entry=%s res=%s at=%s pass=%d"),') -ne 1) { $f += '(F31) 1e F9: one line per within-radius provisional spawn' }
    return $f
}

$fails = @(Invoke-FarspawnChecks $text)
$mutants = @(
    @{ Name = 'X1 RWG gate dropped from the spawn pass'; Key = 'prov'; Expect = 'F1'; Find = '    if (GetMapMode() != ENodeShuffleMapMode::Rwg) { return; } // RWG-only gate'; Repl = '    // gate removed' }
    @{ Name = 'X2 toggle default OFF'; Key = 'prov'; Expect = 'F2'; Find = "TEXT(`"NodeShuffle.RwgSpawnAllAtRoll`"),`n        1,"; Repl = "TEXT(`"NodeShuffle.RwgSpawnAllAtRoll`"),`n        0," }
    @{ Name = 'X3 batch clamp widened'; Key = 'provh'; Expect = 'F2'; Find = 'constexpr int32 RwgProvisionalBatchMax = 512;'; Repl = 'constexpr int32 RwgProvisionalBatchMax = 4096;' }
    @{ Name = 'X4 batch read unclamped'; Key = 'prov'; Expect = 'F2'; Find = 'const int32 Batch = NodeShuffleMapMode::RwgProvisionalBatchPerPass();'; Repl = 'const int32 Batch = CVarNodeShuffleRwgProvisionalBatch.GetValueOnGameThread();' }
    @{ Name = 'X5 seam traces the world'; Key = 'prov'; Expect = 'F3'; Find = "    OutSource = TEXT(`"api`");`n    return Q.SurfaceZ(X, Y);"; Repl = "    OutSource = TEXT(`"api`");`n    return Q.SurfaceZ(X, Y) + (GEngine ? 0.0 : 0.0); // Get" + "World(" }
    @{ Name = 'X6 spawn pass bypasses the seam'; Key = 'prov'; Expect = 'F3'; Find = 'const double Z = NodeShuffleMapMode::RwgProvisionalZ(Q, E.Location.X, E.Location.Y, ZSource);'; Repl = 'const double Z = Q.SurfaceZ(E.Location.X, E.Location.Y);' }
    @{ Name = 'X7 provisional actor hidden'; Key = 'prov'; Expect = 'F4'; Find = 'if (Node->IsHidden()) { Node->SetActorHiddenInGame(false); }'; Repl = ('Node->SetActorHiddenInGame(' + 'true);') }
    @{ Name = 'X8 collision never restored after the settle'; Key = 'prov'; Expect = 'F4'; Find = '    if (IsValid(Node)) { Node->SetActorEnableCollision(true); }'; Repl = '' }
    @{ Name = 'X9 deferred no longer restores the entry'; Key = 'prov'; Expect = 'F5'; Find = "    else`n    {`n        // (c) no terrain-mesh hit in the window: the entry and the actor stay exactly as they were (provisional); retried next pass.`n        E = Snap;"; Repl = "    else`n    {`n        // (c) no terrain-mesh hit in the window: the entry and the actor stay exactly as they were (provisional); retried next pass.`n        E.bRwgProvisional = false;" }
    @{ Name = 'X10 respawn before the despawn'; Key = 'prov'; Expect = 'F6'; Find = "        RwgProvisionalDespawn(E);`n        E.bRwgProvisional = false;`n        E.bRayCasted = true;`n        EnsureNewNodeSpawned(E, bChangedWorld);"; Repl = "        E.bRwgProvisional = false;`n        E.bRayCasted = true;`n        EnsureNewNodeSpawned(E, bChangedWorld);`n        RwgProvisionalDespawn(E);" }
    @{ Name = 'X11 redeal before the despawn'; Key = 'prov'; Expect = 'F6'; Find = "        RwgProvisionalDespawn(E);`n        E.bRwgProvisional = false;`n        ++NodeGateSettleRejectedWater;`n        WaterLockedThisSession.Add(E.EntryGuid);`n        const bool bMoved = TryRedealWaterLockedEntry(E);"; Repl = "        const bool bMoved = TryRedealWaterLockedEntry(E);`n        RwgProvisionalDespawn(E);`n        E.bRwgProvisional = false;`n        ++NodeGateSettleRejectedWater;`n        WaterLockedThisSession.Add(E.EntryGuid);" }
    @{ Name = 'X12 teardown keeps the slot'; Key = 'prov'; Expect = 'F6'; Find = "    SpawnedNodes.Remove(E.EntryGuid);`n    SteadyAliveNodes.Remove(E.EntryGuid);`n    AdoptRotRefit.Remove(E.EntryGuid);"; Repl = "    SteadyAliveNodes.Remove(E.EntryGuid);`n    AdoptRotRefit.Remove(E.EntryGuid);" }
    @{ Name = 'X13 pin check dropped from the settle'; Key = 'prov'; Expect = 'F7'; Find = '    if (RwgProvisionalPinWins(E)) { return; }'; Repl = '' }
    @{ Name = 'X14 pinned entries provisioned'; Key = 'pscan'; Expect = 'F7'; Find = 'if (!E.bIsNewNode || !E.bActive || E.bPinned) { return false; }'; Repl = 'if (!E.bIsNewNode || !E.bActive) { return false; }' }
    @{ Name = 'X15 save field not SaveGame'; Key = 'subh'; Expect = 'F8'; Find = 'UPROPERTY(SaveGame) bool bRwgProvisional = false;'; Repl = 'bool bRwgProvisional = false;' }
    @{ Name = 'X16 rock pass treats provisional as live and fine'; Key = 'rock'; Expect = 'F9'; Find = '        if (E.bRwgProvisional) { continue; } // rwg-farspawn-1: a provisional actor is UNSETTLED'; Repl = '        // skip removed' }
    @{ Name = 'X17 resettle treats provisional as live and fine'; Key = 'rs'; Expect = 'F9'; Find = '        if (E.bRwgProvisional) { continue; } // rwg-farspawn-1: a provisional actor is UNSETTLED'; Repl = '        // skip removed' }
    @{ Name = 'X18 settle-near-players moves provisional'; Key = 'sub'; Expect = 'F9'; Find = '        if (Entry.bRwgProvisional) { continue; } // rwg-farspawn-1: RWG provisional actors settle only through RwgSettleProvisional'; Repl = '' }
    @{ Name = 'X19 PROVSPAWN drops msBatch'; Key = 'prov'; Expect = 'F10'; Find = 'zSource=%s msBatch=%.1f refusedThisPass'; Repl = 'zSource=%s refusedThisPass' }
    @{ Name = 'X20 PROVSETTLE drops hitClass'; Key = 'prov'; Expect = 'F10'; Find = 'outcome=%s hitClass=%d apiUsed='; Repl = 'outcome=%s apiUsed=' }
    @{ Name = 'X21 PROVCENSUS dz percentiles swapped'; Key = 'prov'; Expect = 'F10'; Find = 'ProvPercentile(RwgProvSessDzCm, 0.5f), ProvPercentile(RwgProvSessDzCm, 0.9f),'; Repl = 'ProvPercentile(RwgProvSessDzCm, 0.9f), ProvPercentile(RwgProvSessDzCm, 0.5f),' }
    @{ Name = 'X22 PLACECENSUS tail args swapped'; Key = 'place'; Expect = 'F11'; Find = 'RwgPcSolidTopRejFootprint, RwgPcProvSpawned, RwgPcProvSettled);'; Repl = 'RwgPcSolidTopRejFootprint, RwgPcProvSettled, RwgPcProvSpawned);' }
    @{ Name = 'X23 sample cap widened'; Key = 'provh'; Expect = 'F12'; Find = 'constexpr int32 RwgProvisionalSettleSamplesPerPass = 16;'; Repl = 'constexpr int32 RwgProvisionalSettleSamplesPerPass = 64;' }
    @{ Name = 'X24 refused attempt keeps the mutated entry'; Key = 'prov'; Expect = 'F12'; Find = "            E = Snap;`n            ++Refused; ++RwgProvSessRefused;"; Repl = "            ++Refused; ++RwgProvSessRefused;" }
    @{ Name = 'X25 marker reverted'; Key = 'nsmod'; Expect = 'F13'; Find = ('2026-10-31-rwg-' + 'farspawn-1f'); Repl = ('2026-10-31-rwg-' + 'farspawn-1e') }
    @{ Name = 'X26 killbox pass treats provisional as live and fine'; Key = 'void'; Expect = 'F9'; Find = '        if (E.bRwgProvisional) { continue; } // rwg-farspawn-1: a provisional actor is UNSETTLED'; Repl = '        // skip removed' }
    @{ Name = 'X28 F3 provisional settle no longer tracked by RWGSTUCK'; Key = 'place'; Expect = 'F14'; Find = 'if (E.bRwgProvisional) { RwgStuckBeforeAttempt(E); RwgSettleProvisional(E, bChangedWorld);'; Repl = 'if (E.bRwgProvisional) { RwgSettleProvisional(E, bChangedWorld);' }
    @{ Name = 'X29 F3 RWGSTUCK note forgets a deferring provisional entry'; Key = 'dens'; Expect = 'F14'; Find = 'if (Now && IsValid(*Now) && !E.bRwgProvisional) { RwgStuckTracker.Remove(E.EntryGuid); return; }'; Repl = 'if (Now && IsValid(*Now)) { RwgStuckTracker.Remove(E.EntryGuid); return; }' }
    @{ Name = 'X30 F3 RWGSTUCK census skips provisional as live'; Key = 'dens'; Expect = 'F14'; Find = 'if (Live && IsValid(*Live) && !E.bRwgProvisional) { continue; }'; Repl = 'if (Live && IsValid(*Live)) { continue; }' }
    @{ Name = 'X31 F3 HERE-DENSITY counts provisional as live'; Key = 'dens'; Expect = 'F14'; Find = 'const bool bLive = Live && IsValid(*Live) && !E.bRwgProvisional;'; Repl = 'const bool bLive = Live && IsValid(*Live);' }
    @{ Name = 'X32 F4 inPlace without the respawn test'; Key = 'prov'; Expect = 'F5'; Find = 'Outcome = bRespawned ? TEXT("inPlace") : TEXT("settledNoActor");'; Repl = 'Outcome = TEXT("inPlace");' }
    @{ Name = 'X33 F4 settledZ from the entry'; Key = 'prov'; Expect = 'F15'; Find = 'SettledZ = FreshAt.Z;'; Repl = ('SettledZ = ' + 'E.Location.Z;') }
    @{ Name = 'X34 F4 redeal result ignored'; Key = 'prov'; Expect = 'F15'; Find = 'const bool bMoved = TryRedealWaterLockedEntry(E);'; Repl = 'TryRedealWaterLockedEntry(E); const bool bMoved = true;' }
    @{ Name = 'X35 F4 redealt counted when nothing moved'; Key = 'prov'; Expect = 'F15'; Find = 'if (bMoved) { ++RwgProvSessRedealt; }'; Repl = '++RwgProvSessRedealt;' }
    @{ Name = 'X36 F4 settled counted without an actor'; Key = 'prov'; Expect = 'F15'; Find = "            ++RwgProvSessSettled; ++RwgPcProvSettled;`n        }`n"; Repl = "        }`n        ++RwgProvSessSettled; ++RwgPcProvSettled;`n" }
    @{ Name = 'X37 F4 movedXYm back to the entry location'; Key = 'prov'; Expect = 'F15'; Find = 'const FVector MovedTo = bFresh ? FreshAt : E.Location;'; Repl = 'const FVector MovedTo = E.Location;' }
    @{ Name = 'X38 F5 PROVCENSUS back behind the toggle return'; Key = 'prov'; Expect = 'F12'; Find = "    if ((RwgPcPasses % 12) == 1) { RwgProvisionalEmitCensus(); } // fix F5: before the toggle/pawn/query returns, so spawnAllAtRoll=0 can print`n    if (!NodeShuffleMapMode::RwgSpawnAllAtRollOn() || Pawns.Num() == 0) { return; }"; Repl = "    if (!NodeShuffleMapMode::RwgSpawnAllAtRollOn() || Pawns.Num() == 0) { return; }`n    if ((RwgPcPasses % 12) == 1) { RwgProvisionalEmitCensus(); }" }
    @{ Name = 'X39 F6 reveal counts read after the despawn'; Key = 'prov'; Expect = 'F16'; Find = "    const int32 LocalRevealsBefore = Node->mLocalMapReveals, ServerRevealsBefore = Node->mServerMapReveals;`n"; Repl = '' }
    @{ Name = 'X39b F6 reveal count written'; Key = 'prov'; Expect = 'F16'; Find = 'bFresh = true;'; Repl = ('bFresh = true; Fresh->mLocalMapReveals ' + '= LocalRevealsBefore;') }
    @{ Name = 'X40 F7 settled actorless entry re-provisioned'; Key = 'pscan'; Expect = 'F17'; Find = "        if (E.bRayCasted) { ++RayCastedNoActor; return false; }"; Repl = '        // skip removed' }
    @{ Name = 'X41 F7 sea-deal-failed counted as sea done'; Key = 'pscan'; Expect = 'F17'; Find = 'const bool bSeaDone = bSeaPassIdle || (E.bRwgSeaChecked'; Repl = 'const bool bSeaDone = bSeaPassIdle || RwgSeaDealFailed.Contains(E.EntryGuid) || (E.bRwgSeaChecked' }
    @{ Name = 'X42 F7 sea-deal-failed skip dropped'; Key = 'pscan'; Expect = 'F17'; Find = "        if (RwgSeaDealFailed.Contains(E.EntryGuid)) { ++SeaPending; ++SeaDealFailed; return false; }"; Repl = '        // skip removed' }
    @{ Name = 'X43 F9 help text claims simultaneity again'; Key = 'prov'; Expect = 'F18'; Find = 'get a node placed, up to a batch per spawn pass, once'; Repl = ('get a node placed at ' + 'once, up to a batch per spawn pass, once') }
    @{ Name = 'X44 F9 help text drops the skips'; Key = 'prov'; Expect = 'F18'; Find = '(parked, dormant, refused, pinned, or already fitted to the ground) are skipped'; Repl = 'are skipped' }
    @{ Name = 'X45 F10 old PROVSPAWN field name back'; Key = 'prov'; Expect = 'F10'; Find = 'spawnedThisPass=%d remainingCandidates=%d'; Repl = ('spawnedThisPass=%d remaining' + 'Unspawned=%d') }
    @{ Name = 'X46 V-R1 HereUnspawned comment drops the provisional sentence'; Key = 'here'; Expect = 'F14'; Find = 'A provisional actor (rwg-farspawn-1) HAS an actor and is not in it, while HERE-DENSITY counts it as not live (fix F3): the two lines are not complements. '; Repl = '' }
    @{ Name = 'X47 V-R3 settledNoActor counter dropped'; Key = 'prov'; Expect = 'F15'; Find = "        else { ++RwgProvSessSettledNoActor; }`n"; Repl = '' }
    @{ Name = 'X48 V-R3 redealNotMoved counter dropped'; Key = 'prov'; Expect = 'F15'; Find = 'if (bMoved) { ++RwgProvSessRedealt; } else { ++RwgProvSessRedealNotMoved; }'; Repl = 'if (bMoved) { ++RwgProvSessRedealt; }' }
    @{ Name = 'X49 V-R3 PROVCENSUS drops the two fields'; Key = 'prov'; Expect = 'F10'; Find = 'spawnAllAtRoll=%d settledNoActor=%d redealNotMoved=%d movedInPlace=%d '; Repl = 'spawnAllAtRoll=%d movedInPlace=%d ' }
    @{ Name = 'X50 V-R3 PROVCENSUS args swapped'; Key = 'prov'; Expect = 'F10'; Find = 'RwgProvSessSettledNoActor, RwgProvSessRedealNotMoved,'; Repl = 'RwgProvSessRedealNotMoved, RwgProvSessSettledNoActor,' }
    @{ Name = 'X51 V-R3 header counters appended into the comment'; Key = 'subh'; Expect = 'F15'; Find = "RwgProvSessPinned = 0; // session`n    int32 RwgProvSessSettledNoActor"; Repl = 'RwgProvSessPinned = 0; // session int32 RwgProvSessSettledNoActor' }
    @{ Name = 'X52 V-R4 unplaceable branch no longer counts a deferral'; Key = 'prov'; Expect = 'F15'; Find = "        ++DeferredThisPass; // parity with EnsureNewNodeSpawned: its unsettled water branch counts as a deferral (NodeShuffleSubsystem.cpp DeferredThisPass++)`n"; Repl = '' }
    @{ Name = 'X53 V-R4 deferral counted on the settled branch too'; Key = 'prov'; Expect = 'F15'; Find = "        else { ++RwgProvSessSettledNoActor; }`n"; Repl = "        else { ++RwgProvSessSettledNoActor; }`n        ++DeferredThisPass;`n" }
    @{ Name = 'X54 V-R6 help text drops the sea-idle clause'; Key = 'prov'; Expect = 'F18'; Find = 'cleared them (or without it where this world has no usable sea check);'; Repl = 'cleared them;' }
    @{ Name = 'X55 R7 seaPending legend drops the never-revisited clause'; Key = 'prov'; Expect = 'F17'; Find = 'which the sea pass skips for the rest of the session, so the sea pass never clears them from this count'; Repl = 'which the sea pass re-deals later' }
    @{ Name = 'X27 F2 pin check evaluated before the distance test'; Key = 'place'; Expect = 'F9'; Find = 'if (E.bRwgProvisional && IsLocationNearAnyPlayer(E.Location, AttemptRadiusCm) && !RwgProvisionalPinWins(E)) {'; Repl = 'if (E.bRwgProvisional && !RwgProvisionalPinWins(E) && IsLocationNearAnyPlayer(E.Location, AttemptRadiusCm)) {' }
    @{ Name = 'X56 C1 deferred branch despawns'; Key = 'prov'; Expect = 'F19'; Find = "        E = Snap;`n        ++NodeGateSettleRejectedNoWater;"; Repl = "        E = Snap;`n        RwgProvisionalDespawn(E);`n        ++NodeGateSettleRejectedNoWater;" }
    @{ Name = 'X57 C1 move on any settle'; Key = 'prov'; Expect = 'F19'; Find = 'if (bSettled && FCString::Strcmp(MoveCheck, TEXT("moved")) == 0)'; Repl = 'if (bSettled)' }
    @{ Name = 'X58 C1 xy tolerance dropped'; Key = 'move'; Expect = 'F19'; Find = 'if (FVector::Dist2D(E.Location, ProvAt) > NodeShuffleMapMode::RwgProvisionalMoveTolCm) { return TEXT("xyShift"); }'; Repl = '' }
    @{ Name = 'X59 C1 class check dropped'; Key = 'move'; Expect = 'F19'; Find = 'if (Node->GetClass()->GetPathName() != ResolveSpawnNodeClassPath(E)) { return TEXT("classDiffers"); }'; Repl = '' }
    @{ Name = 'X60 C1 resource check dropped'; Key = 'move'; Expect = 'F19'; Find = 'if (!Res || Res->GetPathName() != E.AssignedResourceClassPath) { return TEXT("resourceDiffers"); }'; Repl = '' }
    @{ Name = 'X61 C1 move removes the representation'; Key = 'move'; Expect = 'F19'; Find = '    Node->UpdateNodeRepresentation();'; Repl = ('    Node->RemoveResource' + 'NodeScan_Local(); // RemoveRepresentation') }
    @{ Name = 'X62 C1 radiation not re-initialised'; Key = 'move'; Expect = 'F19'; Find = '            Node->InitRadioactivity();'; Repl = '' }
    @{ Name = 'X63 C1 redeal no longer despawns first'; Key = 'prov'; Expect = 'F19'; Find = "        RwgProvisionalDespawn(E);`n        E.bRwgProvisional = false;`n        ++NodeGateSettleRejectedWater;"; Repl = "        E.bRwgProvisional = false;`n        ++NodeGateSettleRejectedWater;" }
    @{ Name = 'X64 C2 default back to 64'; Key = 'prov'; Expect = 'F2'; Find = "        16, // rwg-farspawn-1c C2"; Repl = "        64, // rwg-farspawn-1c C2" }
    @{ Name = 'X65 C3 gate drops provisional spawned'; Key = 'place'; Expect = 'F20'; Find = 'RwgPcSpawned > 0 || RwgPcProvSpawned > 0 || RwgPcProvSettled > 0 ||'; Repl = 'RwgPcSpawned > 0 || RwgPcProvSettled > 0 ||' }
    @{ Name = 'X66 C4 SCANPING drops nearestNoActorM'; Key = 'scan'; Expect = 'F21'; Find = ' nearestNoActorM=%.0f pawn=%s'; Repl = ' pawn=%s' }
    @{ Name = 'X67 C5 compass counter becomes a substring match'; Key = 'scan'; Expect = 'F21'; Find = 'if (FCString::Strncmp(V, *Prefix, Prefix.Len()) == 0)'; Repl = ('if (FCString::Str' + 'str(V, *Prefix) != nullptr)') }
    @{ Name = 'X68 C6 refusedReason read after the restore'; Key = 'prov'; Expect = 'F21'; Find = "            E = Snap;`n            ++Refused; ++RwgProvSessRefused;`n            RwgProvRefused.Add(E.EntryGuid);`n            RwgProvisionalLogRefusal(E, Reason);"; Repl = "            ++Refused; ++RwgProvSessRefused;`n            RwgProvRefused.Add(E.EntryGuid);`n            RwgProvisionalLogRefusal(E, Reason);`n            E = Snap;" }
    @{ Name = 'X69 C7 WELLMOVE dropped at the RWG redeal'; Key = 'escal'; Expect = 'F21'; Find = ' WellMoveLog(E, WmFrom, Site, TEXT("rwgRedeal"));'; Repl = '' }
    @{ Name = 'X70 C7 WELLMOVE dropped at the sea redeal'; Key = 'sea'; Expect = 'F21'; Find = '            WellMoveLog(W, WmFrom, Site, TEXT("seaRedeal")); // rwg-farspawn-1c C7'; Repl = '' }
    @{ Name = 'X71 C8 W1 legend reverted'; Key = 'prov'; Expect = 'F22'; Find = 'a redeal that returned true also leaves no actor until'; Repl = 'a redeal leaves no actor until' }
    @{ Name = 'X72 1d F1 readback reverted'; Key = 'move'; Expect = 'F23'; Find = 'if (FVector::Dist(After, E.Location) > 10.0)'; Repl = 'if (false)' }
    @{ Name = 'X73 1d F1 / 1f C2 caller ignores the refusal word'; Key = 'prov'; Expect = 'F19'; Find = '{ if (const TCHAR* MoveRefused = RwgProvisionalMoveInPlace(E, Node)) { MoveCheck = MoveRefused; } }'; Repl = '{ RwgProvisionalMoveInPlace(E, Node); }' }
    @{ Name = 'X74 1d F1 not-applied line diagnostics-gated'; Key = 'move'; Expect = 'F23'; Find = "        ++RwgProvSessMoveNotApplied;`n        UE_LOG("; Repl = "        ++RwgProvSessMoveNotApplied;`n        if (FNodeShuffleModule::AreDiagnosticsEnabled()) UE_LOG(" }
    @{ Name = 'X75 1d F2 shadowGate field dropped'; Key = 'move'; Expect = 'F24'; Find = ' enclosed=%d encBlocked=%d/%d shadowGate=%s'; Repl = ' enclosed=%d encBlocked=%d/%d' }
    @{ Name = 'X76 1d F2 enclosure not measured'; Key = 'move'; Expect = 'F24'; Find = 'const bool bEnclosed = IsSpotEnclosed(E.Location, EncBlocked, EncTotal, nullptr, nullptr, Node);'; Repl = 'const bool bEnclosed = false;' }
    @{ Name = 'X77 1d F2 / 1f C2 the measurement deactivates the node'; Key = 'move'; Expect = 'F24'; Find = "    if (bEnclosed) { ++RwgProvSessMovedEnclosed; }"; Repl = "    if (bRefuseIgnored) { E.bActive = false; }`n    if (bEnclosed) { ++RwgProvSessMovedEnclosed; }" }
    @{ Name = 'X78 1d F2 census drops the shadow counts'; Key = 'prov'; Expect = 'F10'; Find = ' gatedEnclosed=%d gatedAllSolid=%d gatedRespawn=%d"),'; Repl = '"),' }
    @{ Name = 'X79 1d F3 citation dropped'; Key = 'move'; Expect = 'F25'; Find = 'This REVERSES the DESPAWN-ON-MOVE rule'; Repl = 'This changes the rule' }
    @{ Name = 'X80 1d F3 node representation update dropped'; Key = 'move'; Expect = 'F19'; Find = 'NodeRepUpdated = RepMgr->UpdateRepresentation(NodeRep) ? 1 : 0;'; Repl = '' }
    @{ Name = 'X81 1d F5 old field name back'; Key = 'scan'; Expect = 'F21'; Find = 'removedWarnings=%d hiddenOriginalsInTowerRadius=%d'; Repl = ('removedWarnings=%d hiddenOriginals' + 'Stripped=%d') }
    @{ Name = 'X82 1d F6 stale legend back'; Key = 'prov'; Expect = 'F25'; Find = 'the settledNoActor and redealNotMoved fields are session counts'; Repl = ('the last two ' + 'fields are session counts') }
    @{ Name = 'X83 1d F9 label reverted'; Key = 'prov'; Expect = 'F25'; Find = 'TEXT("inactiveAfterSpawn")'; Repl = ('TEXT("overlap' + 'GaveUp")') }
    @{ Name = 'X84 1d F10 pawnFrom dropped'; Key = 'scan'; Expect = 'F21'; Find = ' pawnFrom=%s nearestLiveClass='; Repl = ' nearestLiveClass=' }
    @{ Name = 'X85 1d F7 misplaced comment back'; Key = 'subh'; Expect = 'F25'; Find = 'void EmitCompassWarnCensus(int32 TowersRescanned); // COMPASSWARN, beside SCANREGEN inside its diagnostics gate'; Repl = 'void EmitCompassWarnCensus(int32 TowersRescanned); // session: settles that moved the same actor' }
    @{ Name = 'X86 1d F8 legend dropped'; Key = 'wmove'; Expect = 'F25'; Find = 'ClearAbandonedWellPlacement zeroes PlacedCoreLocation unlogged'; Repl = 'x' }
    @{ Name = 'X87 1e C1 within entries provisioned without a fresh attempt'; Key = 'pscan'; Expect = 'F26'; Find = '        if (bWithin && !FreshNoActor.Contains(I)) { return false; }'; Repl = '' }
    @{ Name = 'X88 1e C1 wet-spot skip dropped'; Key = 'pscan'; Expect = 'F26'; Find = "        if (RwgProvisionalAtWetSpot(E)) { ++WetSpot; return false; }"; Repl = '' }
    @{ Name = 'X89 1e C1 wet-spot predicate ignores the lock spot'; Key = 'prov'; Expect = 'F26'; Find = 'return !At || FVector::Dist2D(*At, E.Location) <= NodeShuffleMapMode::RwgProvisionalMoveTolCm;'; Repl = 'return false;' }
    @{ Name = 'X90 1e C1 lock-spot notes dropped in the subsystem'; Key = 'sub'; Expect = 'F26'; Find = "            RwgNoteWetSpot(Entry); // rwg-farspawn-1e C1: the lock spot (provisioning skips an entry still there)`n"; Repl = '' }
    @{ Name = 'X91 1e C1 branch (b) lock-spot note dropped'; Key = 'prov'; Expect = 'F26'; Find = "        RwgNoteWetSpot(E); // rwg-farspawn-1e C1: the lock spot, before the redeal moves the entry`n"; Repl = '' }
    @{ Name = 'X92 1e C1 FreshNoActor fed on success too'; Key = 'place'; Expect = 'F26'; Find = '{ ++RwgPcSpawned; } else { FreshNoActor.Add(Cands[K].Value); }'; Repl = '{ ++RwgPcSpawned; } FreshNoActor.Add(Cands[K].Value);' }
    @{ Name = 'X93 1e C1 underground within entries provisioned'; Key = 'pscan'; Expect = 'F26'; Find = '        if (bWithin && E.bUnderground) { ++WithinUnderground; return false; }'; Repl = '' }
    @{ Name = 'X94 1e C1 origin values swapped'; Key = 'prov'; Expect = 'F26'; Find = 'TEXT("withinRadius") : TEXT("far")'; Repl = 'TEXT("far") : TEXT("withinRadius")' }
    @{ Name = 'X95 1e C2 time budget never checked'; Key = 'prov'; Expect = 'F27'; Find = '        if (K > 0 && (FPlatformTime::Seconds() - T0) * 1000.0 >= BudgetMs) { BudgetHit = TEXT("time"); break; }'; Repl = '' }
    @{ Name = 'X96 1e C2 budget default 16 ms'; Key = 'prov'; Expect = 'F27'; Find = "TEXT(`"NodeShuffle.RwgProvisionalBatchMs`"),`n        8.0f,"; Repl = "TEXT(`"NodeShuffle.RwgProvisionalBatchMs`"),`n        16.0f," }
    @{ Name = 'X97 1e C2 budget clamp widened'; Key = 'provh'; Expect = 'F27'; Find = 'constexpr float RwgProvisionalBatchMsMax = 50.0f;'; Repl = 'constexpr float RwgProvisionalBatchMsMax = 500.0f;' }
    @{ Name = 'X98 1e C2 count cap dropped'; Key = 'prov'; Expect = 'F27'; Find = 'const int32 N = FMath::Min(Cands.Num(), Batch);'; Repl = 'const int32 N = Cands.Num();' }
    @{ Name = 'X99 1e C2 count label dropped'; Key = 'prov'; Expect = 'F27'; Find = 'if (K == N && Cands.Num() > N) { BudgetHit = TEXT("count"); }'; Repl = '' }
    @{ Name = 'X100 1e C3 deposits counted as live'; Key = 'scan'; Expect = 'F28'; Find = 'if (N->IsA<AFGResourceDeposit>()) { Deposit = FMath::Min(Deposit, D); continue; }'; Repl = '' }
    @{ Name = 'X101 1e C3 layout membership by position'; Key = 'scan'; Expect = 'F28'; Find = '(FNodeShuffleModule::IsManagedSpawnedNode(LiveNearest) ? 1 : 0)'; Repl = '(FVector::Dist(LiveNearest->GetActorLocation(), PawnAt) < 500.0 ? 1 : 0)' }
    @{ Name = 'X102 1e C3 SCANPING drops nearestDepositM'; Key = 'scan'; Expect = 'F21'; Find = ' nearestDepositM=%.0f"),'; Repl = '"),' }
    @{ Name = 'X103 1e C4 distinct deferred set never filled'; Key = 'prov'; Expect = 'F29'; Find = "        RwgProvSessDeferredEntries.Add(E.EntryGuid);"; Repl = '' }
    @{ Name = 'X104 1e C4 deferredEntries prints the event count'; Key = 'prov'; Expect = 'F10'; Find = 'RwgProvSessDeferred, RwgProvSessDeferredEntries.Num(),'; Repl = 'RwgProvSessDeferred, RwgProvSessDeferred,' }
    @{ Name = 'X105 1e C1 PROVSPAWN drops withinRadiusProvisioned'; Key = 'prov'; Expect = 'F10'; Find = ' withinRadiusProvisioned=%d withinRadiusCandidates='; Repl = ' withinRadiusCandidates=' }
    @{ Name = 'X106 1e C4 sample cap back to 8'; Key = 'provh'; Expect = 'F12'; Find = 'constexpr int32 RwgProvisionalSettleSamplesPerPass = 16;'; Repl = 'constexpr int32 RwgProvisionalSettleSamplesPerPass = 8;' }
    @{ Name = 'X107 1f C2 gate removed'; Key = 'move'; Expect = 'F30'; Find = '    if (bEnclosed || bAllSolid)'; Repl = '    if (false)' }
    @{ Name = 'X108 1f C2 gated move kept'; Key = 'move'; Expect = 'F30'; Find = 'return TEXT("gatedRespawn");'; Repl = 'return nullptr;' }
    @{ Name = 'X109 1f C2 gated line diagnostics-gated'; Key = 'move'; Expect = 'F30'; Find = "        ++RwgProvSessGatedRespawn;`n        UE_LOG("; Repl = "        ++RwgProvSessGatedRespawn;`n        if (FNodeShuffleModule::AreDiagnosticsEnabled()) UE_LOG(" }
    @{ Name = 'X110 1d F1 decrement dropped'; Key = 'move'; Expect = 'F30'; Find = '        --InsideGateRefusedThisPass;'; Repl = '' }
    @{ Name = 'X111 1d F3 old shadow label'; Key = 'move'; Expect = 'F30'; Find = 'TEXT("allSolid")'; Repl = ('TEXT("in' + 'side")') }
    @{ Name = 'X112 1d F4 old representation field name'; Key = 'move'; Expect = 'F30'; Find = ' nodeRepUpdateReturned=%d"),'; Repl = (' nodeRepUpd' + 'ated=%d"),') }
    @{ Name = 'X113 1f C3 periodic rebuild dropped'; Key = 'pscan'; Expect = 'F31'; Find = ': (RwgPcPasses - RwgProvCandBuiltPass >= NodeShuffleMapMode::RwgProvisionalCandRebuildPasses) ? TEXT("periodic") : nullptr;'; Repl = ': nullptr;' }
    @{ Name = 'X114 1f C3 guid check dropped'; Key = 'pscan'; Expect = 'F31'; Find = 'if (!Layout.IsValidIndex(C.Value) || Layout[C.Value].EntryGuid != C.Key) { Why = TEXT("guidMismatch"); break; }'; Repl = '' }
    @{ Name = 'X115 1f C3 dirty mark never cleared'; Key = 'pscan'; Expect = 'F31'; Find = '        RwgProvCandDirtyWhy = nullptr;'; Repl = '' }
    @{ Name = 'X116 1f C3 redeal mark dropped'; Key = 'sub'; Expect = 'F31'; Find = 'RwgProvCandMarkDirty(TEXT("redeal")); // rwg-farspawn-1f C3: a redeal moves'; Repl = '// a redeal moves' }
    @{ Name = 'X117 1f C3 roll mark dropped'; Key = 'sub'; Expect = 'F31'; Find = 'RwgProvCandMarkDirty(bIsReroll ? TEXT("reroll") : TEXT("roll"));'; Repl = '' }
    @{ Name = 'X118 1f C3 full Layout walk back in the spawn pass'; Key = 'prov'; Expect = 'F31'; Find = '    const double ScanMs = (FPlatformTime::Seconds() - T0) * 1000.0;'; Repl = "    for (int32 I = 0; I < Layout.Num(); ++I) { }`n    const double ScanMs = (FPlatformTime::Seconds() - T0) * 1000.0;" }
    @{ Name = 'X119 1f C3 scanMs dropped from PROVSPAWN'; Key = 'prov'; Expect = 'F31'; Find = ' scanMs=%.1f candidates=%d'; Repl = ' candidates=%d' }
    @{ Name = 'X120 1f C3 set-aside entries kept on the list'; Key = 'pscan'; Expect = 'F31'; Find = '        if (!SetAsideOk(Layout[I])) { continue; }'; Repl = '' }
    @{ Name = 'X121 1e F9 within line dropped'; Key = 'prov'; Expect = 'F31'; Find = 'TEXT("PROVSPAWN: withinRadius entry=%s res=%s at=%s pass=%d"),'; Repl = 'TEXT("PROVSPAWN: withinRadius"),' }
)
$mutantFails = @()
$caught = 0
foreach ($m in $mutants) {
    $mut = [ordered]@{}
    foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = @(Invoke-FarspawnChecks $mut)
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ }
    else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "FARSPAWN LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: FARSPAWN lint -- F1..F31 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
