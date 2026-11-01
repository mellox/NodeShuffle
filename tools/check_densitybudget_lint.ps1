# DENSITYBUDGET lint (2026-11-01, packet rwg-density-1 PER-CELL BUDGET; handoff _team/test-logs/2026-11-01-nodeshuffle-density-handoff.md).
# NOT check_density_lint.ps1: that one pins the 2026-10-19 RWGDENSITY/RWGSTUCK diagnostics packet of the same name. This one pins the
# per-1-km-cell land-share caps on the RWG deal (NodeShuffleRwgDensityBudget.h/.cpp, the hooks in GenerateRwgLocations, the Begin/End
# window in RollLayout, the config toggle, the marker). LOADS: the two budget files, NodeShuffleMapModePlace.cpp, NodeShuffleSubsystem.cpp,
# NodeShuffleSubsystem.h, NodeShuffleConfig.h, NodeShuffleConfig.cpp, NodeShuffle.cpp, NodeShuffleLateNodes.cpp.
#   B1  Formats: ROLLDENSITY (32 fields), ROLLDENSITY-CALL (13), ROLLDENSITY-CELL (7) once each; field count == conversion count; every
#       name= is followed by a conversion; the brief's required fields present; the LEGEND line carries no field=value token.
#   B2  Begin/End window in RollLayout: one Begin behind an Rwg test fed by Config.RwgDensityBudget; the pre-fill is every carried entry's
#       location plus every bPinned NewLayout entry; one End behind an Rwg test, ordered before EmitRollCensus (the density trigger and the
#       wipe audit keep their pinned places after it); the late-node file never calls Begin.
#   B3  Land fraction and caps: one RwgDensityLandSample call site in the budget file, on the lattice; LandFrac = passed / (Sub * Sub); the
#       apportionment weights ARE LandFrac; fill = prefill + accepted; room = max(0, budget - fill) + slack; cap = accepted + room (a ceiling
#       on the roll-cumulative accepted count, review F1); the no-land call opens its caps.
#   B4  Dealer hooks, in order: BeginCall after the water cache, MaybeOpen directly after ++Drawn, Admit directly after the pinned land-test
#       line and before the spacing block, a rejected draw `continue`s (redraw) and releases a cave pick, Accept directly after
#       OutLocations.Add(C), EndCall before the deal-bounds comment; the loop head is unchanged.
#   B5  The budget files consume no random stream and trace nothing (forbidden tokens built by concatenation).
#   B6  Fallback + constants: MaybeOpen's reserve test, Admit's bypass on !bEnabled || bOpen; CellSizeCm 100000, LandSamplesPerCellSide 4,
#       FallbackReserveDrawsPerSpot 20, CellLinesMax 120.
#   B7  Apportion is exact: floors, remainders sorted descending with ties to the lower index, slack to the heaviest cell.
#   B8  Config: mirror field default true, panel AddBool default true, display name once.
#   B9  Marker (2026-11-01-rwg-density-1) once on its own line in NodeShuffle.cpp, absent from the packet sources; header declarations present.
#   B10 End drops the state (one Reset), the dealer reads it through Get() once.
# Every forbidden needle is BUILT BY CONCATENATION. Mutants run in-file. Exit 0 = all hold; 1 = a rule or mutant fails; 2 = missing file.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    bud   = 'NodeShuffle\Private\NodeShuffleRwgDensityBudget.cpp'
    budh  = 'NodeShuffle\Private\NodeShuffleRwgDensityBudget.h'
    place = 'NodeShuffle\Private\NodeShuffleMapModePlace.cpp'
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    hdr   = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    cfgh  = 'NodeShuffle\Public\NodeShuffleConfig.h'
    cfg   = 'NodeShuffle\Private\NodeShuffleConfig.cpp'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
    late  = 'NodeShuffle\Private\NodeShuffleLateNodes.cpp'
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
function FormatOf([string]$t, [string]$head) {
    $m = [regex]::Match($t, 'TEXT\("' + [regex]::Escape($head) + '[^"]*"\)')
    if (-not $m.Success) { return '' }
    return $m.Value
}
function FieldCheck([string]$fmt, [int]$expect, [string[]]$required, [string]$rule) {
    $f = @()
    if ($fmt -eq '') { $f += "($rule) format missing"; return $f }
    $names = ([regex]::Matches($fmt, '[A-Za-z]\w*=')).Count
    $convs = ([regex]::Matches($fmt, '%(?!%)[-+ 0#]*\d*(\.\d+)?[dfsu]')).Count
    if ($names -ne $expect -or $convs -ne $expect) { $f += "($rule) field count $names / conversion count $convs, expected $expect" }
    if ([regex]::IsMatch($fmt, '=(?!%)')) { $f += "($rule) a name= is not followed by a conversion" }
    foreach ($r in $required) { if ((Count $fmt (' ' + $r + '=')) -ne 1) { $f += "($rule) required field $r missing or repeated" } }
    return $f
}

# ---- anchors (unique context) ----
$landTest    = '        if (!bCavePick && !RwgDealOneSpot(Q, Rng, C, Outside, Water, Disagree, Slope)) { continue; }'
$admitLine   = '        if (Density && !Density->Admit(C)) { if (bCavePick) { bSlotIsCave = false; } continue; }'
$spacingHead = '        // Spacing: identical to the vanilla deal''s rule'
$drawnLine   = '        ++Drawn;' + "`n" + '        if (Density) { Density->MaybeOpen(Attempts, Count - OutLocations.Num()); }'
$acceptLine  = '        OutLocations.Add(C);' + "`n" + '        if (Density) { Density->Accept(C); }'
$beginCall   = '    FNodeShuffleRwgDensityState* const Density = RwgDensityState.Get();'
$beginCall2  = '    if (Density) { Density->BeginCall(Count); }'
$endCall     = '    if (Density) { Density->EndCall(Drawn, Outside, Water, RwgRcRejectedVoid, RwgRcRejectedKillbox, Slope, Spacing); }'
$boundsHead  = '    // The redeal (TryRedealRwgEntry) and the fallback redeal read these, as on vanilla.'
$loopHead    = '    while (OutLocations.Num() < Count && Attempts-- > 0)'
$waterCache  = '    RwgBuildWaterVolumeCache();' + "`n" + '    const double H = Q.HalfExtent();'
$subBeginIf  = '    if (GetMapMode() == ENodeShuffleMapMode::Rwg)' + "`n" + '    {' + "`n" + '        TArray<FVector> DensityPrefill;'
$subCarried  = '        for (const FNodeShuffleEntry& E : CarriedEntries) { DensityPrefill.Add(E.Location); }'
$subPinned   = '        for (const FNodeShuffleEntry& E : NewLayout) { if (E.bPinned) { DensityPrefill.Add(E.Location); } }'
$subBegin    = '        RwgDensityBudgetBegin(Config.RwgDensityBudget, DensityPrefill);'
$subEnd      = '    if (GetMapMode() == ENodeShuffleMapMode::Rwg) { RwgDensityBudgetEnd(); }'
$subCensus   = '    EmitRollCensus(Seed, bIsReroll, PoolSize, TargetActive, VanillaCount, NewLocations.Num(),'
$subTrigger  = '    if (GetMapMode() == ENodeShuffleMapMode::Rwg) { RwgDensityOnRoll(); }'
$subAudit    = '    RerollWipeEmitListAudit(bIsReroll, AuditNotYieldedBeforeWipe);'
$sampleCall  = '                if (RwgDensityLandSample(Q, X, Y)) { ++Passed; }'
$latticeX    = '                const double X = (Cx + (Sx + 0.5) / Sub) * CellCm;'
$landFracSet = '        S.LandFrac[I] = static_cast<double>(Passed) / (Sub * Sub);'
$apportion   = '    NodeShuffleRwgDensityBudget::Apportion(Count + FillSum, LandFrac, Budget);'
$fillLine    = '    for (int32 I = 0; I < Cells; ++I) { Fill[I] = Prefill[I] + Accepted[I]; FillSum += Fill[I]; }'
$capLine     = '        Cap[I] = Accepted[I] + Room;'
$roomLine    = '        const int32 Room = FMath::Max(0, Budget[I] - Fill[I]) + (LandFrac[I] > 0.0 ? NodeShuffleRwgDensityBudget::RoomSlackPerLandCell : 0);'
$noLandOpen  = '    if (RoomSum == 0) { bOpen = true; ++FallbackCalls;'
$reserveTest = '    if (AttemptsLeft > SpotsLeft * NodeShuffleRwgDensityBudget::FallbackReserveDrawsPerSpot) { return; }'
$openSet     = '    bOpen = true;' + "`n" + '    ++FallbackCalls;' + "`n" + '    const int32 AtCap = CellsAtCap();'
$admitBypass = '    if (!bEnabled || bOpen) { return true; }'
$admitFull   = '    if (Accepted[I] >= Cap[I]) { ++RejCellFull; ++CallRejCellFull; return false; }'
$sortPred    = 'return A.Key > B.Key || (A.Key == B.Key && A.Value < B.Value); });'
$slackLine   = '    if (Left > 0) { Out[Heaviest] += Left; }'
$floorLine   = '        const int32 Floor = FMath::FloorToInt32(Raw);'
$resetLine   = '    RwgDensityState.Reset();'
$marker      = '    UE_LOG(LogNodeShuffle, Display, TEXT("===== NodeShuffle packet marker (2026-11-01-rwg-density-1)====="));'
$markerTag   = '2026-11-01-rwg-' + 'density-1)'
$cfghLine    = '    bool RwgDensityBudget{true};'
$cfgLine     = '    AddBool(TEXT("RwgDensityBudget"), true,'
$cfgDisplay  = '        TEXT("Spread Nodes Evenly By Land (Random World Generator)"),'
$hdrFwd      = 'struct FNodeShuffleRwgDensityState;'
$hdrMember   = '    TSharedPtr<FNodeShuffleRwgDensityState> RwgDensityState;'
$hdrBegin    = '    void RwgDensityBudgetBegin(bool bToggleOn, const TArray<FVector>& Prefill);'
$hdrEnd      = '    void RwgDensityBudgetEnd();'
$legendHead  = 'ROLLDENSITY-LEGEND: '
$requiredRoll = @('cells', 'landCells', 'pinnedPrefilled', 'budgetSum', 'accepted', 'rejectedCellFull', 'rejectedWater', 'rejectedVoid', 'rejectedSlope', 'rejectedSpacing', 'draws', 'fallbackCellsOpened')
$requiredCell = @('cx', 'cy', 'landFrac', 'budget', 'pinned', 'accepted')
# Forbidden tokens, by concatenation (B5, B9).
$tokRng   = 'Rn' + 'g.'         # a member call on the roll stream
$tokFRand = 'FRa' + 'nd('      # the call form: the prose may name the stream type, never call it
$tokTrace = 'Raycast' + 'GroundAt'
$tokLine  = 'Line' + 'TraceSingle'
$tokSweep = 'Sweep' + 'Single'

function Invoke-DensityBudgetChecks($T) {
    $f = @()
    # B1
    $roll = FormatOf $T.bud 'ROLLDENSITY: '
    $call = FormatOf $T.bud 'ROLLDENSITY-CALL '
    $cell = FormatOf $T.bud 'ROLLDENSITY-CELL '
    $f += FieldCheck $roll 32 $requiredRoll 'B1'
    $f += FieldCheck $call 13 @('call', 'requested', 'accepted', 'rejectedCellFull', 'draws', 'spotsLeftAtOpen') 'B1'
    $f += FieldCheck $cell 7 $requiredCell 'B1'
    if ((Count $T.bud 'TEXT("ROLLDENSITY: ') -ne 1 -or (Count $T.bud 'TEXT("ROLLDENSITY-CALL ') -ne 1 -or (Count $T.bud 'TEXT("ROLLDENSITY-CELL ') -ne 1 -or (Count $T.bud ('TEXT("' + $legendHead)) -ne 1) { $f += '(B1) each ROLLDENSITY head once' }
    $legend = FormatOf $T.bud $legendHead
    if ($legend -eq '' -or $legend.Contains('=')) { $f += '(B1) the legend must carry no field=value token' }
    # B2
    if ((Count $T.sub $subBeginIf) -ne 1 -or (Count $T.sub $subCarried) -ne 1 -or (Count $T.sub $subPinned) -ne 1 -or (Count $T.sub $subBegin) -ne 1 -or (Count $T.sub 'RwgDensityBudgetBegin(') -ne 1) { $f += '(B2) one Begin behind an Rwg test, pre-filled with carried locations and bPinned NewLayout entries, fed by the config toggle' }
    if (-not (Ordered $T.sub @($subBeginIf, $subCarried, $subPinned, $subBegin, 'GenerateNewLocations(Rng, VanillaLocations, AvoidLocations, RerollNewQuotaEffective, NewLocations,'))) { $f += '(B2) Begin must precede the extras deal' }
    if ((Count $T.sub $subEnd) -ne 1 -or (Count $T.sub 'RwgDensityBudgetEnd(') -ne 1 -or -not (Ordered $T.sub @($subEnd, $subCensus, $subTrigger, $subAudit))) { $f += '(B2) one End behind an Rwg test, ordered before EmitRollCensus, the density trigger and the wipe audit' }
    if ((Count $T.late 'RwgDensityBudgetBegin(') -ne 0 -or (Count $T.late 'RwgDensityState') -ne 0) { $f += '(B2) the late-node deal must not open the window' }
    # B3
    if ((Count $T.bud 'RwgDensityLandSample(') -ne 1 -or (Count $T.bud $sampleCall) -ne 1 -or (Count $T.bud $latticeX) -ne 1 -or (Count $T.bud $landFracSet) -ne 1) { $f += '(B3) one lattice land-sample call site writing LandFrac = passed / samples' }
    if ((Count $T.bud $apportion) -ne 1 -or (Count $T.bud $fillLine) -ne 1 -or (Count $T.bud $capLine) -ne 1 -or (Count $T.bud $roomLine) -ne 1 -or (Count $T.bud $noLandOpen) -ne 1) { $f += '(B3) apportion over LandFrac, fill = prefill + accepted, room = max(0, budget - fill) + slack, cap = accepted + room, no-land call opens' }
    # B4
    if ((Count $T.place $waterCache) -ne 1 -or (Count $T.place $beginCall) -ne 1 -or (Count $T.place $beginCall2) -ne 1 -or -not (Ordered $T.place @($waterCache, $beginCall, $beginCall2, $loopHead))) { $f += '(B4) BeginCall after the water cache and before the loop' }
    if ((Count $T.place $drawnLine) -ne 1) { $f += '(B4) MaybeOpen directly after ++Drawn' }
    if ((Count $T.place $landTest) -ne 1 -or (Count $T.place $admitLine) -ne 1 -or -not (Ordered $T.place @($landTest, $admitLine, $spacingHead))) { $f += '(B4) Admit directly after the land test and before the spacing block; a full cell continues (redraw) and releases a cave pick' }
    if ((Count $T.place $acceptLine) -ne 1) { $f += '(B4) Accept directly after OutLocations.Add(C)' }
    if ((Count $T.place $endCall) -ne 1 -or -not (Ordered $T.place @($endCall, $boundsHead))) { $f += '(B4) EndCall before the deal-bounds comment' }
    if ((Count $T.place $loopHead) -ne 1) { $f += '(B4) the loop head is unchanged' }
    # B5
    foreach ($tok in @($tokRng, $tokFRand, $tokTrace, $tokLine, $tokSweep)) {
        if ((Count $T.bud $tok) -ne 0 -or (Count $T.budh $tok) -ne 0) { $f += "(B5) forbidden token '$tok' in the budget files" }
    }
    # B6
    if ((Count $T.bud $reserveTest) -ne 1 -or (Count $T.bud $openSet) -ne 1 -or (Count $T.bud $admitBypass) -ne 1 -or (Count $T.bud $admitFull) -ne 1) { $f += '(B6) fallback reserve test, open set, Admit bypass and full test' }
    if ((Count $T.budh 'constexpr double CellSizeCm = 100000.0;') -ne 1 -or (Count $T.budh 'constexpr int32 LandSamplesPerCellSide = 4;') -ne 1 -or (Count $T.budh 'constexpr int32 FallbackReserveDrawsPerSpot = 20;') -ne 1 -or (Count $T.budh 'constexpr int32 CellLinesMax = 120;') -ne 1 -or (Count $T.budh 'constexpr int32 RoomSlackPerLandCell = 1;') -ne 1) { $f += '(B6) constants pinned by value' }
    # B7
    if ((Count $T.bud $floorLine) -ne 1 -or (Count $T.bud $sortPred) -ne 1 -or (Count $T.bud $slackLine) -ne 1) { $f += '(B7) Apportion: floors, descending remainders with ties to the lower index, slack to the heaviest' }
    # B8
    if ((Count $T.cfgh $cfghLine) -ne 1 -or (Count $T.cfg $cfgLine) -ne 1 -or (Count $T.cfg $cfgDisplay) -ne 1) { $f += '(B8) toggle default ON in the mirror and the panel, display name once' }
    # B9
    if ((Count $T.nsmod $marker) -ne 1 -or (Count $T.nsmod $markerTag) -ne 1) { $f += '(B9) marker once on its own line' }
    foreach ($k in @('bud', 'budh', 'place', 'sub', 'hdr', 'cfg', 'cfgh')) { if ((Count $T[$k] $markerTag) -ne 0) { $f += "(B9) marker tag must not appear in $k" } }
    if ((Count $T.hdr $hdrFwd) -ne 1 -or (Count $T.hdr $hdrMember) -ne 1 -or (Count $T.hdr $hdrBegin) -ne 1 -or (Count $T.hdr $hdrEnd) -ne 1) { $f += '(B9) header: forward declaration, TSharedPtr member, Begin and End declared once' }
    # B10
    if ((Count $T.bud $resetLine) -ne 1 -or (Count $T.place 'RwgDensityState.Get()') -ne 1) { $f += '(B10) End drops the state once; the dealer reads it through Get() once' }
    return $f
}

$baseline = Invoke-DensityBudgetChecks $text
if ($baseline.Count -gt 0) { foreach ($x in $baseline) { Write-Host "FAIL: $x" }; Write-Host "DENSITYBUDGET LINT: FAIL -- $($baseline.Count) problem(s)."; exit 1 }

$mutants = @(
    @{ Name = 'Y1 pinned originals not pre-filled'; Key = 'sub'; Expect = 'B2'; Find = $subPinned; Repl = '        for (const FNodeShuffleEntry& E : NewLayout) { (void)E; }' }
    @{ Name = 'Y2 carried entries not pre-filled'; Key = 'sub'; Expect = 'B2'; Find = ($subCarried + "`n"); Repl = '' }
    @{ Name = 'Y3 Begin not behind the Rwg test'; Key = 'sub'; Expect = 'B2'; Find = $subBeginIf; Repl = ('    if (true)' + "`n" + '    {' + "`n" + '        TArray<FVector> DensityPrefill;') }
    @{ Name = 'Y4 toggle not consulted'; Key = 'sub'; Expect = 'B2'; Find = $subBegin; Repl = '        RwgDensityBudgetBegin(true, DensityPrefill);' }
    @{ Name = 'Y5 End after the density trigger'; Key = 'sub'; Expect = 'B2'; Find = $subEnd; Repl = ''; Find2 = $subAudit; Repl2 = ($subEnd + "`n" + $subAudit) }
    @{ Name = 'Y6 late-node deal opens the window'; Key = 'late'; Expect = 'B2'; Find = 'if (Active > 0) { GenerateNewLocations('; Repl = 'if (Active > 0) { RwgDensityBudgetBegin(true, AvoidLocations); GenerateNewLocations(' }
    @{ Name = 'Y7 landFrac ignored (uniform weights)'; Key = 'bud'; Expect = 'B3'; Find = $apportion; Repl = '    NodeShuffleRwgDensityBudget::Apportion(Count + FillSum, Ones, Budget);' }
    @{ Name = 'Y8 landFrac hard-coded to full land'; Key = 'bud'; Expect = 'B3'; Find = $landFracSet; Repl = '        S.LandFrac[I] = 1.0;' }
    @{ Name = 'Y9 fill ignores earlier accepted spots'; Key = 'bud'; Expect = 'B3'; Find = $fillLine; Repl = '    for (int32 I = 0; I < Cells; ++I) { Fill[I] = Prefill[I]; FillSum += Fill[I]; }' }
    @{ Name = 'Y10 room ignores the fill'; Key = 'bud'; Expect = 'B3'; Find = $roomLine; Repl = '        const int32 Room = Budget[I];' }
    @{ Name = 'Y11 cap compares cumulative accepted with per-call room (review F1)'; Key = 'bud'; Expect = 'B3'; Find = $capLine; Repl = '        Cap[I] = Room;' }
    @{ Name = 'Y12 cell-full reject never redraws'; Key = 'place'; Expect = 'B4'; Find = $admitLine; Repl = '        if (Density && !Density->Admit(C)) { if (bCavePick) { bSlotIsCave = false; } }' }
    @{ Name = 'Y13 cave pick not released on cell-full'; Key = 'place'; Expect = 'B4'; Find = $admitLine; Repl = '        if (Density && !Density->Admit(C)) { continue; }' }
    @{ Name = 'Y14 Admit moved after the spacing test'; Key = 'place'; Expect = 'B4'; Find = $admitLine; Repl = ''; Find2 = $acceptLine; Repl2 = ($admitLine + "`n" + $acceptLine) }
    @{ Name = 'Y15 accepted spot never fills its cell'; Key = 'place'; Expect = 'B4'; Find = $acceptLine; Repl = '        OutLocations.Add(C);' }
    @{ Name = 'Y16 fallback test skipped'; Key = 'place'; Expect = 'B4'; Find = $drawnLine; Repl = '        ++Drawn;' }
    @{ Name = 'Y17 call never closed'; Key = 'place'; Expect = 'B4'; Find = $endCall; Repl = '' }
    @{ Name = 'Y18 budget draws from the roll stream'; Key = 'bud'; Expect = 'B5'; Find = $landFracSet; Repl = ($landFracSet + ' (void)' + $tokFRand + ');') }
    @{ Name = 'Y19 budget traces physics'; Key = 'bud'; Expect = 'B5'; Find = $sampleCall; Repl = ('                if (' + $tokTrace + '(X, Y)) { ++Passed; }') }
    @{ Name = 'Y20 fallback never opens'; Key = 'bud'; Expect = 'B6'; Find = $reserveTest; Repl = '    if (true) { return; }' }
    @{ Name = 'Y21 reserve constant zeroed'; Key = 'budh'; Expect = 'B6'; Find = 'constexpr int32 FallbackReserveDrawsPerSpot = 20;'; Repl = 'constexpr int32 FallbackReserveDrawsPerSpot = 0;' }
    @{ Name = 'Y22 Admit ignores the open flag'; Key = 'bud'; Expect = 'B6'; Find = $admitBypass; Repl = '    if (!bEnabled) { return true; }' }
    @{ Name = 'Y23 apportion drops the slack'; Key = 'bud'; Expect = 'B7'; Find = $slackLine; Repl = '    // slack dropped' }
    @{ Name = 'Y24 ties to the higher index'; Key = 'bud'; Expect = 'B7'; Find = $sortPred; Repl = 'return A.Key > B.Key || (A.Key == B.Key && A.Value > B.Value); });' }
    @{ Name = 'Y25 mirror default OFF'; Key = 'cfgh'; Expect = 'B8'; Find = $cfghLine; Repl = '    bool RwgDensityBudget{false};' }
    @{ Name = 'Y26 panel default OFF'; Key = 'cfg'; Expect = 'B8'; Find = $cfgLine; Repl = '    AddBool(TEXT("RwgDensityBudget"), false,' }
    @{ Name = 'Y27 marker reverted'; Key = 'nsmod'; Expect = 'B9'; Find = $markerTag; Repl = ('2026-11-01-rwg-' + 'density-0)') }
    @{ Name = 'Y28 marker tag leaks into a legend'; Key = 'bud'; Expect = 'B9'; Find = 'Cells are squares of the printed cell size'; Repl = ('Cells (' + $markerTag + ' are squares of the printed cell size') }
    @{ Name = 'Y29 state never dropped at End'; Key = 'bud'; Expect = 'B10'; Find = $resetLine; Repl = '    // state kept' }
    @{ Name = 'Y30 census field dropped'; Key = 'bud'; Expect = 'B1'; Find = ' rejectedCellFull=%d rejectedWater=%d'; Repl = ' rejectedWater=%d' }
    @{ Name = 'Y31 legend echoes a field token'; Key = 'bud'; Expect = 'B1'; Find = 'A draw counted as cell full passed the land chain'; Repl = ('A draw counted as rejectedCellFull' + '=1 passed the land chain') }
    @{ Name = 'Y32 cell line loses its budget field'; Key = 'bud'; Expect = 'B1'; Find = 'landFrac=%.3f budget=%d pinned=%d'; Repl = 'landFrac=%.3f pinned=%d' }
    @{ Name = 'Y33 slack zeroed'; Key = 'budh'; Expect = 'B6'; Find = 'constexpr int32 RoomSlackPerLandCell = 1;'; Repl = 'constexpr int32 RoomSlackPerLandCell = 0;' }
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
    $r = Invoke-DensityBudgetChecks $t2
    $hits = @($r | Where-Object { $_.StartsWith("($($m.Expect))") })
    if ($hits.Count -gt 0) { Write-Host "RED  (ok) $($m.Name)" }
    else { Write-Host "GREEN (BAD) $($m.Name): expected $($m.Expect), got: $($r -join ' | ')"; $bad++ }
}
if ($bad -gt 0) { Write-Host "DENSITYBUDGET LINT: FAIL -- $bad mutant(s) not caught."; exit 1 }
Write-Host "PASS: DENSITYBUDGET lint -- B1..B10 hold across $($files.Count) loaded files; mutants caught $($mutants.Count)/$($mutants.Count)."
exit 0
