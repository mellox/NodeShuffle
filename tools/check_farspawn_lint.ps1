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
#   F13 Marker 2026-10-31-rwg-farspawn-1b once (fix round 1), previous markers absent; the new .cpp stays under 500 lines.
#   F15 (fix round 1, F4) PROVSETTLE reports only what it tested: inPlace/settled counters need the respawned actor, settledZ is that
#       actor's Z (never the entry's), ring/mapwide/redealt need TryRedealWaterLockedEntry to have returned true; (re-review V-R3)
#       settledNoActor / redealNotMoved have session counters printed on PROVCENSUS.
#   F16 (fix round 1, F6) PROVSETTLE prints the scanner/radar reveal counts before the despawn and on the respawned actor; read-only.
#   F17 (fix round 1, F7) Provisioning skips rayCasted actorless entries and counts RwgSeaDealFailed entries as sea-pending.
#   F18 (fix round 1, F9) The RwgSpawnAllAtRoll help text claims only what the code does.
#   F14 (fix round 1, F3) RWGSTUCK / HERE-DENSITY: the provisional settle is bracketed by the RWGSTUCK snapshot/note, and the note,
#       the stuck census and HERE-DENSITY count a valid actor as live only when it is NOT provisional; (re-review V-R1) the
#       HereUnspawned comment says its two lines still count a provisional actor as spawned (code unchanged; rwgtrace H3 pins it).
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
    if ((Count $p ("TEXT(`"NodeShuffle.RwgProvisionalBatchPerPass`"),`n        64,")) -ne 1) { $f += '(F2) NodeShuffle.RwgProvisionalBatchPerPass must default to 64' }
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
    if (-not $spawnFn.Contains('if (!E.bIsNewNode || !E.bActive || E.bPinned) { continue; }')) { $f += '(F7) pinned entries are never provisioned' }
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
    if (-not (Ordered $T.place @('    ++RwgPcPasses;', '    RwgProvisionalSpawnPass(Pawns, AttemptRadiusCm, bChangedWorld);', '    RwgResettlePass();'))) { $f += '(F9) the provisional pass runs after the attempt loop, before the resettle' }
    # F10
    if ((Count $p 'TEXT("PROVSPAWN: pass=%d spawnedThisPass=%d remainingCandidates=%d provisionalTotal=%d zSource=%s msBatch=%.1f refusedThisPass=%d refusedSession=%d batch=%d seaPending=%d caveDealtZ=%d rayCastedNoActor=%d seaDealFailed=%d"),') -ne 1 -or
        (Count $p "RwgPcPasses, Spawned, Cands.Num() - Spawned - Refused, Total, *ZSource, MsBatch, Refused, RwgProvSessRefused, Batch, SeaPending, CaveDealtZ,`n            RayCastedNoActor, SeaDealFailed);") -ne 1) { $f += '(F10) PROVSPAWN format/args' }
    if ((Count $p 'TEXT("PROVSETTLE: entry=%s res=%s provZ=%.0f settledZ=%.0f dzCm=%.0f movedXYm=%.1f outcome=%s hitClass=%d apiUsed=%d onSolidTop=%d localMapRevealsBefore=%d localMapRevealsAfter=%d serverMapRevealsBefore=%d serverMapRevealsAfter=%d"),') -ne 1 -or
        (Count $p "FVector::Dist2D(MovedTo, ProvAt) / 100.0, Outcome, HitClass, bApiUsed ? 1 : 0, E.bRwgOnSolidTop ? 1 : 0,`n            LocalRevealsBefore, LocalRevealsAfter, ServerRevealsBefore, ServerRevealsAfter);") -ne 1) { $f += '(F10) PROVSETTLE format/args' }
    if ((Count $p 'TEXT("PROVSETTLE: entry=%s res=%s provZ=%.0f settledZ=%.0f dzCm=0 movedXYm=0.0 outcome=pinnedKept hitClass=-1 pinned=%d occupied=%d"),') -ne 1) { $f += '(F10) PROVSETTLE pinnedKept format' }
    if ((Count $p 'TEXT("PROVCENSUS: pass=%d provisional=%d settledFromProvisional=%d redealtFromProvisional=%d deferredProvisional=%d pinnedProvisional=%d dzP50Cm=%.0f dzP90Cm=%.0f dzMaxCm=%.0f movedXYmMax=%.1f spawnedSession=%d refusedSession=%d staleFlagCleared=%d flagNoActor=%d spawnAllAtRoll=%d settledNoActor=%d redealNotMoved=%d"),') -ne 1 -or
        (Count $p 'ProvPercentile(RwgProvSessDzCm, 0.5f), ProvPercentile(RwgProvSessDzCm, 0.9f), ProvPercentile(RwgProvSessDzCm, 1.0f), RwgProvSessMovedXYmMax,') -ne 1 -or
        (Count $p 'RwgProvSessSpawned, RwgProvSessRefused, RwgProvSessStale, FlagNoActor, NodeShuffleMapMode::RwgSpawnAllAtRollOn() ? 1 : 0, RwgProvSessSettledNoActor, RwgProvSessRedealNotMoved);') -ne 1) { $f += '(F10) PROVCENSUS format/args' }
    if ((Count $p ('remaining' + 'Unspawned')) -ne 0) { $f += '(F10) fix F10: the PROVSPAWN field is remainingCandidates (the old name is gone)' }
    # F11
    if ((Count $T.place 'solidTopRejectedFootprint=%d provisionalSpawned=%d provisionalSettled=%d"),') -ne 1 -or (Count $T.place 'RwgPcSolidTopRejFootprint, RwgPcProvSpawned, RwgPcProvSettled);') -ne 1) { $f += '(F11) PLACECENSUS tail fields/args' }
    if ((Count $T.place '    RwgPcProvSpawned = RwgPcProvSettled = 0; // rwg-farspawn-1') -ne 1) { $f += '(F11) per-pass reset' }
    # F12
    if ((Count $T.provh 'constexpr int32 RwgProvisionalSettleSamplesPerPass = 8;') -ne 1 -or (Count $p 'if (RwgProvSettleSamples < NodeShuffleMapMode::RwgProvisionalSettleSamplesPerPass)') -ne 2) { $f += '(F12) PROVSETTLE sample cap 8 at both sites' }
    if (-not $spawnFn.Contains('    if ((RwgPcPasses % 12) == 1) { RwgProvisionalEmitCensus(); }') -or (Count $p 'RwgProvisionalEmitCensus();') -ne 1) { $f += '(F12) PROVCENSUS on 12th passes, one call site' }
    if (-not (Ordered $spawnFn @('// RWG-only gate', '    if ((RwgPcPasses % 12) == 1) { RwgProvisionalEmitCensus(); }', '    if (!NodeShuffleMapMode::RwgSpawnAllAtRollOn() || Pawns.Num() == 0) { return; }', 'if (!Q.IsUsable()) { return; }'))) { $f += '(F12) fix F5: PROVCENSUS is emitted after the RWG gate and before the toggle/pawn/query returns' }
    if (-not (Ordered $spawnFn @('const FNodeShuffleEntry Snap = E;', 'EnsureNewNodeSpawned(E, bChangedWorld);', 'if (!IsValid(Node))', 'E = Snap;', 'RwgProvRefused.Add(E.EntryGuid);', 'E.bRayCasted = false;', 'E.bRwgProvisional = true;'))) { $f += '(F12) no actor -> entry restored and refused; actor -> unsettled + provisional' }
    # F13
    if ((Count $T.nsmod ('LOADED (2026-10-31-rwg-' + 'farspawn-1b)')) -ne 1 -or (Count $T.nsmod ('2026-10-31-rwg-' + 'farspawn-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-31-rwg-' + 'seedgas-1b)')) -ne 0) { $f += '(F13) marker farspawn-1b present once, farspawn-1 and seedgas-1b absent' }
    if (($p.Split("`n")).Count -ge 500) { $f += '(F13) NodeShuffleMapModeProvisional.cpp must stay under 500 lines' }
    # F15
    if (-not (Ordered $settleFn @('EnsureNewNodeSpawned(E, bChangedWorld);', 'AFGResourceNode* Fresh = SpawnedNodes.FindRef(E.EntryGuid);', 'const bool bRespawned = IsValid(Fresh);', 'if (bRespawned)', 'FreshAt = Fresh->GetActorLocation();', 'SettledZ = FreshAt.Z;', '++RwgProvSessSettled; ++RwgPcProvSettled;', '    }', 'Outcome = bRespawned ?'))) { $f += '(F15) inPlace, settledZ and the settled counters need the respawned actor' }
    if ((Count $settleFn ('SettledZ = ' + 'E.')) -ne 0 -or (Count $settleFn 'SettledZ = ') -ne 2) { $f += '(F15) settledZ is assigned only from the respawned actor' }
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
    if (-not (Ordered $spawnFn @('if (IsLocationNearAnyPlayer(E.Location, AttemptRadiusCm)) { continue; }', 'if (E.bRayCasted) { ++RayCastedNoActor; continue; }', 'if (RwgSeaDealFailed.Contains(E.EntryGuid)) { ++SeaPending; ++SeaDealFailed; continue; }', 'const bool bSeaDone = bSeaPassIdle || (E.bRwgSeaChecked && (E.bRwgVoidChecked || !bVoidMaybe));', 'Cands.Emplace(Best, I);'))) { $f += '(F17) the candidate loop must skip rayCasted entries and sea-deal-failed entries before the sea-done test' }
    if ((Count $spawnFn 'RwgSeaDealFailed.Contains(') -ne 1) { $f += '(F17) RwgSeaDealFailed is read once, as a skip (never as sea-done)' }
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
    if ((Count $T.dens 'a provisional node, placed far from every player and not yet fitted to the ground, is not counted as live') -ne 1) { $f += '(F14) the HERE-DENSITY legend must say provisional nodes are not live' }
    # F14 (re-review V-R1): HERE-NEAREST-UNSPAWNED / HERE-UNSPAWNED-BANDS deliberately count a provisional actor as spawned; the comment says so
    if ((Count $T.here 'A provisional actor (rwg-farspawn-1) HAS an actor and is not in it, while HERE-DENSITY counts it as not live (fix F3): the two lines are not complements.') -ne 1 -or (Count $T.here ("(HERE-DENSITY's live" + "`n// test, negated)")) -ne 0) { $f += '(F14) V-R1: the HereUnspawned population comment must say a provisional actor is in neither line while HERE-DENSITY counts it not live' }
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
    @{ Name = 'X14 pinned entries provisioned'; Key = 'prov'; Expect = 'F7'; Find = 'if (!E.bIsNewNode || !E.bActive || E.bPinned) { continue; }'; Repl = 'if (!E.bIsNewNode || !E.bActive) { continue; }' }
    @{ Name = 'X15 save field not SaveGame'; Key = 'subh'; Expect = 'F8'; Find = 'UPROPERTY(SaveGame) bool bRwgProvisional = false;'; Repl = 'bool bRwgProvisional = false;' }
    @{ Name = 'X16 rock pass treats provisional as live and fine'; Key = 'rock'; Expect = 'F9'; Find = '        if (E.bRwgProvisional) { continue; } // rwg-farspawn-1: a provisional actor is UNSETTLED'; Repl = '        // skip removed' }
    @{ Name = 'X17 resettle treats provisional as live and fine'; Key = 'rs'; Expect = 'F9'; Find = '        if (E.bRwgProvisional) { continue; } // rwg-farspawn-1: a provisional actor is UNSETTLED'; Repl = '        // skip removed' }
    @{ Name = 'X18 settle-near-players moves provisional'; Key = 'sub'; Expect = 'F9'; Find = '        if (Entry.bRwgProvisional) { continue; } // rwg-farspawn-1: RWG provisional actors settle only through RwgSettleProvisional'; Repl = '' }
    @{ Name = 'X19 PROVSPAWN drops msBatch'; Key = 'prov'; Expect = 'F10'; Find = 'zSource=%s msBatch=%.1f refusedThisPass'; Repl = 'zSource=%s refusedThisPass' }
    @{ Name = 'X20 PROVSETTLE drops hitClass'; Key = 'prov'; Expect = 'F10'; Find = 'outcome=%s hitClass=%d apiUsed='; Repl = 'outcome=%s apiUsed=' }
    @{ Name = 'X21 PROVCENSUS dz percentiles swapped'; Key = 'prov'; Expect = 'F10'; Find = 'ProvPercentile(RwgProvSessDzCm, 0.5f), ProvPercentile(RwgProvSessDzCm, 0.9f),'; Repl = 'ProvPercentile(RwgProvSessDzCm, 0.9f), ProvPercentile(RwgProvSessDzCm, 0.5f),' }
    @{ Name = 'X22 PLACECENSUS tail args swapped'; Key = 'place'; Expect = 'F11'; Find = 'RwgPcSolidTopRejFootprint, RwgPcProvSpawned, RwgPcProvSettled);'; Repl = 'RwgPcSolidTopRejFootprint, RwgPcProvSettled, RwgPcProvSpawned);' }
    @{ Name = 'X23 sample cap widened'; Key = 'provh'; Expect = 'F12'; Find = 'constexpr int32 RwgProvisionalSettleSamplesPerPass = 8;'; Repl = 'constexpr int32 RwgProvisionalSettleSamplesPerPass = 64;' }
    @{ Name = 'X24 refused attempt keeps the mutated entry'; Key = 'prov'; Expect = 'F12'; Find = "        if (!IsValid(Node))`n        {`n            E = Snap;"; Repl = "        if (!IsValid(Node))`n        {" }
    @{ Name = 'X25 marker reverted'; Key = 'nsmod'; Expect = 'F13'; Find = ('2026-10-31-rwg-' + 'farspawn-1b'); Repl = ('2026-10-31-rwg-' + 'farspawn-1') }
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
    @{ Name = 'X40 F7 settled actorless entry re-provisioned'; Key = 'prov'; Expect = 'F17'; Find = "        if (E.bRayCasted) { ++RayCastedNoActor; continue; }"; Repl = '        // skip removed' }
    @{ Name = 'X41 F7 sea-deal-failed counted as sea done'; Key = 'prov'; Expect = 'F17'; Find = 'const bool bSeaDone = bSeaPassIdle || (E.bRwgSeaChecked'; Repl = 'const bool bSeaDone = bSeaPassIdle || RwgSeaDealFailed.Contains(E.EntryGuid) || (E.bRwgSeaChecked' }
    @{ Name = 'X42 F7 sea-deal-failed skip dropped'; Key = 'prov'; Expect = 'F17'; Find = "        if (RwgSeaDealFailed.Contains(E.EntryGuid)) { ++SeaPending; ++SeaDealFailed; continue; }"; Repl = '        // skip removed' }
    @{ Name = 'X43 F9 help text claims simultaneity again'; Key = 'prov'; Expect = 'F18'; Find = 'get a node placed, up to a batch per spawn pass, once'; Repl = ('get a node placed at ' + 'once, up to a batch per spawn pass, once') }
    @{ Name = 'X44 F9 help text drops the skips'; Key = 'prov'; Expect = 'F18'; Find = '(parked, dormant, refused, pinned, or already fitted to the ground) are skipped'; Repl = 'are skipped' }
    @{ Name = 'X45 F10 old PROVSPAWN field name back'; Key = 'prov'; Expect = 'F10'; Find = 'spawnedThisPass=%d remainingCandidates=%d'; Repl = ('spawnedThisPass=%d remaining' + 'Unspawned=%d') }
    @{ Name = 'X46 V-R1 HereUnspawned comment drops the provisional sentence'; Key = 'here'; Expect = 'F14'; Find = 'A provisional actor (rwg-farspawn-1) HAS an actor and is not in it, while HERE-DENSITY counts it as not live (fix F3): the two lines are not complements. '; Repl = '' }
    @{ Name = 'X47 V-R3 settledNoActor counter dropped'; Key = 'prov'; Expect = 'F15'; Find = "        else { ++RwgProvSessSettledNoActor; }`n"; Repl = '' }
    @{ Name = 'X48 V-R3 redealNotMoved counter dropped'; Key = 'prov'; Expect = 'F15'; Find = 'if (bMoved) { ++RwgProvSessRedealt; } else { ++RwgProvSessRedealNotMoved; }'; Repl = 'if (bMoved) { ++RwgProvSessRedealt; }' }
    @{ Name = 'X49 V-R3 PROVCENSUS drops the two fields'; Key = 'prov'; Expect = 'F10'; Find = 'spawnAllAtRoll=%d settledNoActor=%d redealNotMoved=%d"),'; Repl = 'spawnAllAtRoll=%d"),' }
    @{ Name = 'X50 V-R3 PROVCENSUS args swapped'; Key = 'prov'; Expect = 'F10'; Find = 'RwgProvSessSettledNoActor, RwgProvSessRedealNotMoved);'; Repl = 'RwgProvSessRedealNotMoved, RwgProvSessSettledNoActor);' }
    @{ Name = 'X51 V-R3 header counters appended into the comment'; Key = 'subh'; Expect = 'F15'; Find = "RwgProvSessPinned = 0; // session`n    int32 RwgProvSessSettledNoActor"; Repl = 'RwgProvSessPinned = 0; // session int32 RwgProvSessSettledNoActor' }
    @{ Name = 'X52 V-R4 unplaceable branch no longer counts a deferral'; Key = 'prov'; Expect = 'F15'; Find = "        ++DeferredThisPass; // parity with EnsureNewNodeSpawned: its unsettled water branch counts as a deferral (NodeShuffleSubsystem.cpp DeferredThisPass++)`n"; Repl = '' }
    @{ Name = 'X53 V-R4 deferral counted on the settled branch too'; Key = 'prov'; Expect = 'F15'; Find = "        else { ++RwgProvSessSettledNoActor; }`n"; Repl = "        else { ++RwgProvSessSettledNoActor; }`n        ++DeferredThisPass;`n" }
    @{ Name = 'X54 V-R6 help text drops the sea-idle clause'; Key = 'prov'; Expect = 'F18'; Find = 'cleared them (or without it where this world has no usable sea check);'; Repl = 'cleared them;' }
    @{ Name = 'X27 F2 pin check evaluated before the distance test'; Key = 'place'; Expect = 'F9'; Find = 'if (E.bRwgProvisional && IsLocationNearAnyPlayer(E.Location, AttemptRadiusCm) && !RwgProvisionalPinWins(E)) {'; Repl = 'if (E.bRwgProvisional && !RwgProvisionalPinWins(E) && IsLocationNearAnyPlayer(E.Location, AttemptRadiusCm)) {' }
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
Write-Host "PASS: FARSPAWN lint -- F1..F18 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
