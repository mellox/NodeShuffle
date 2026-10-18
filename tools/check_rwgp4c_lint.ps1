# RWG P4c lint (2026-10-18): a reloaded RWG well's census members come from its bound claim satellite paths, and the
# WELLROLL authored-resource source is measured. LOADS and PINS every source file the P4c packet touched. Anchors are full
# statements or function bodies; needles naming a forbidden literal are BUILT BY CONCATENATION so this file cannot match itself.
#   K1  RwgFillWellMembersFromClaims: vanilla early return first; a census record already holding every layout satellite is
#       untouched; per satellite, in order: sentinel skip, live lookup by path, already-member skip, OUR-copy skip (managed
#       registry or SpawnedWellSatellites), linked-to-another-core skip, then the add with the census finite flag.
#   K2  Call sites: RollWellLayout fills after CollectWellCensus and before the merge copy; RollWellRelocation fills after
#       CollectWellCensus and before ByCorePath points at the records; both behind the off-vanilla test.
#   K3  Authored resource: the live core read is written only when non-empty (one guarded assignment, no bare one); the
#       accounting runs off vanilla only; order live > saved > bound satellite (AUTHORED read) > noAuthored, and the
#       effective-resource accessor is never used for it.
#   K4  WELLROLL carries authoredFromLive/Saved/Satellite + membersFromClaimWells/Sats with matching args; the counters are
#       zeroed in RwgWellRollBegin; WELLCLAIMMEMBERS is line-initial in its TEXT(.
#   K5  No field repeated inside the WELLROLL or WELLCLAIMMEMBERS format.
#   K6  Marker rwg-p4c-1 present once, rwg-p5-2 absent.
#   K7  Header: forward declaration, both declarations, the five counters.
# Mutants run in-file. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    mem     = 'NodeShuffle\Private\NodeShuffleWellRwgMembers.cpp'
    wrwg    = 'NodeShuffle\Private\NodeShuffleWellRwg.cpp'
    wroll   = 'NodeShuffle\Private\NodeShuffleWellRoll.cpp'
    wrr     = 'NodeShuffle\Private\NodeShuffleWellRelocateRoll.cpp'
    subsysh = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    nsmod   = 'NodeShuffle\Private\NodeShuffle.cpp'
}
$text = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = (Get-Content -Raw -Path $p) -replace "`r`n", "`n"
}
$cls = 'ANodeShuffleSubsystem::'
function Get-Body([string]$t, [string]$sig) {
    $i = $t.IndexOf($sig); if ($i -lt 0) { return $null }
    $m = [regex]::Match($t.Substring($i), "(?m)^\}$"); if (-not $m.Success) { return $null }
    return $t.Substring($i, $m.Index)
}
function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }
function Ordered([string]$t, [string[]]$needles) {
    $at = -1
    foreach ($n in $needles) { $i = $t.IndexOf($n, $at + 1); if ($i -lt 0) { return $false }; $at = $i }
    return $true
}
function Fmt-Fields([string]$fmt) { return @([regex]::Matches($fmt, '(?<![A-Za-z0-9_])([A-Za-z][A-Za-z0-9_]*)=') | ForEach-Object { $_.Groups[1].Value }) }

function Invoke-P4cChecks($T) {
    $f = @()
    # K1
    $fb = Get-Body $T.mem ('void ' + $cls + 'RwgFillWellMembersFromClaims(')
    if (-not $fb) { $f += '(K1) RwgFillWellMembersFromClaims body not found' }
    else {
        if (-not (Ordered $fb @('if (GetMapMode() == ENodeShuffleMapMode::Vanilla) { return; }', 'for (FNodeShuffleWellRecord& W : Census.Wells)'))) { $f += '(K1) the vanilla early return must precede the census walk' }
        if ((Count $fb 'if (W.Members.Num() >= E.Satellites.Num()) { continue; }') -ne 1) { $f += '(K1) a census record already holding every layout satellite must be untouched' }
        if (-not (Ordered $fb @(
            'if (S.SatellitePath.StartsWith(Sentinel)) { ++SatsSentinel; continue; }',
            'AFGResourceNodeFrackingSatellite* Sat = LiveByPath.FindRef(S.SatellitePath);',
            'if (Have.Contains(Sat)) { ++SatsAlready; continue; }',
            'if (FNodeShuffleModule::IsManagedSpawnedNode(Sat) || Ours.Contains(Sat)) { ++SatsOurs; continue; }',
            'if (LinkedCore && LinkedCore != W.Core) { ++SatsOtherCore; continue; }',
            'M.bLocationFinite = IsFiniteVector(M.Location);',
            'W.Members.Add(M);'))) { $f += '(K1) per-satellite order: sentinel, live lookup, member, our copy, other core, finite flag, add' }
        if (-not $fb.Contains('for (const TPair<FString, AFGResourceNodeFrackingSatellite*>& KV : SpawnedWellSatellites) { if (KV.Value) { Ours.Add(KV.Value); } }')) { $f += '(K1) our spawned copies must come from SpawnedWellSatellites' }
        if ((Count $fb 'W.Members.Add(') -ne 1) { $f += '(K1) exactly one member add' }
    }
    # K2
    $rl = Get-Body $T.wroll ('void ' + $cls + 'RollWellLayout(')
    if (-not $rl -or -not (Ordered $rl @('CollectWellCensus(GetWorld(), Census);', 'if (GetMapMode() != ENodeShuffleMapMode::Vanilla) { RwgFillWellMembersFromClaims(Census, TEXT("layout")); }', 'TArray<FNodeShuffleWellEntry> Merged = WellLayout;'))) { $f += '(K2) RollWellLayout must fill members after the census and before the merge' }
    $rr = Get-Body $T.wrr ('void ' + $cls + 'RollWellRelocation(')
    if (-not $rr -or -not (Ordered $rr @('CollectWellCensus(GetWorld(), Census);', 'if (GetMapMode() != ENodeShuffleMapMode::Vanilla) { RwgFillWellMembersFromClaims(Census, TEXT("relocation")); }', 'ByCorePath.Add(WellPathOf(W.Core), &W);'))) { $f += '(K2) RollWellRelocation must fill members after the census and before ByCorePath' }
    # K3
    if (-not $rl -or (Count $rl 'if (!Authored.IsEmpty()) { E.OriginalResourceClassPath = Authored; }') -ne 1 -or (Count $rl ('E.OriginalResourceClassPath = ' + 'Authored;')) -ne 1) { $f += '(K3) the live core read must be written only when non-empty (a bare assignment would blank the saved field)' }
    if (-not $rl -or -not $rl.Contains('if (GetMapMode() != ENodeShuffleMapMode::Vanilla) { RwgNoteWellAuthoredSource(E, !Authored.IsEmpty(), W.Members); }') -or $rl.Contains('++RwgWr' + 'NoAuthored')) { $f += '(K3) the authored accounting must run off vanilla through RwgNoteWellAuthoredSource only' }
    $nb = Get-Body $T.mem ('void ' + $cls + 'RwgNoteWellAuthoredSource(')
    if (-not $nb -or -not (Ordered $nb @(
        'if (bLiveCoreRead) { ++RwgWrAuthoredLive; return; }',
        'if (!E.OriginalResourceClassPath.IsEmpty()) { ++RwgWrAuthoredSaved; return; }',
        'const FString FromSat = WellAuthoredResourcePath(M.Actor);',
        'if (!FromSat.IsEmpty()) { E.OriginalResourceClassPath = FromSat; ++RwgWrAuthoredSatellite; return; }',
        '++RwgWrNoAuthored;'))) { $f += '(K3) authored order must be live > saved > bound satellite > noAuthored' }
    if ($nb -and ($nb.Contains('->GetResource' + 'Class()') -or $nb.Contains('.GetResource' + 'Class()'))) { $f += '(K3) the effective-resource accessor must never feed the authored resource' }
    # K4
    if ((Count $T.wrwg 'voidUsable=%d authoredFromLive=%d authoredFromSaved=%d authoredFromSatellite=%d membersFromClaimWells=%d membersFromClaimSats=%d"),') -ne 1 -or (Count $T.wrwg 'RwgWrAuthoredLive, RwgWrAuthoredSaved, RwgWrAuthoredSatellite, RwgWrMembersFromClaimWells, RwgWrMembersFromClaimSats);') -ne 1) { $f += '(K4) WELLROLL must carry the authoredFrom and membersFromClaim fields with matching args' }
    $wb = Get-Body $T.wrwg ('void ' + $cls + 'RwgWellRollBegin(')
    if (-not $wb -or -not $wb.Contains('RwgWrAuthoredLive = RwgWrAuthoredSaved = RwgWrAuthoredSatellite = RwgWrMembersFromClaimWells = RwgWrMembersFromClaimSats = 0;')) { $f += '(K4) the P4c counters must be zeroed per well roll' }
    if ((Count $T.mem 'TEXT("WELLCLAIMMEMBERS: caller=%s') -ne 1) { $f += '(K4) WELLCLAIMMEMBERS must be line-initial in its TEXT(' }
    if (-not $fb -or -not $fb.Contains('RwgWrMembersFromClaimWells = WellsFilled;') -or -not $fb.Contains('RwgWrMembersFromClaimSats = SatsAdded;')) { $f += '(K4) the fill must publish its counts to WELLROLL' }
    # K5
    foreach ($fmt in @([regex]::Matches(($T.wrwg + $T.mem), 'TEXT\("((?:WELLROLL: map=rwg|WELLCLAIMMEMBERS: caller)[^"]*)"\)') | ForEach-Object { $_.Groups[1].Value })) {
        $dup = (Fmt-Fields $fmt) | Group-Object | Where-Object { $_.Count -gt 1 }
        if ($dup) { $f += "(K5) repeated field(s) $(($dup | ForEach-Object { $_.Name }) -join ',') in '$($fmt.Substring(0, [Math]::Min(40, $fmt.Length)))'" }
    }
    # K6
    if ((Count $T.nsmod ('LOADED (2026-10-18-rwg-' + 'p4c-1)')) -ne 1 -or (Count $T.nsmod ('2026-10-18-rwg-' + 'p5-2')) -ne 0) { $f += '(K6) marker must be rwg-p4c-1 with rwg-p5-2 absent' }
    # K7
    if ((Count $T.subsysh 'struct FNodeShuffleWellCensus; // RWG P4c') -ne 1 -or (Count $T.subsysh 'void RwgFillWellMembersFromClaims(FNodeShuffleWellCensus& Census, const TCHAR* Caller);') -ne 1 -or (Count $T.subsysh 'void RwgNoteWellAuthoredSource(FNodeShuffleWellEntry& E, bool bLiveCoreRead, const TArray<FNodeShuffleWellMember>& Members);') -ne 1 -or (Count $T.subsysh 'int32 RwgWrAuthoredLive = 0, RwgWrAuthoredSaved = 0, RwgWrAuthoredSatellite = 0;') -ne 1 -or (Count $T.subsysh 'int32 RwgWrMembersFromClaimWells = 0, RwgWrMembersFromClaimSats = 0;') -ne 1) { $f += '(K7) header declarations and counters' }
    return $f
}

$fails = Invoke-P4cChecks $text
$mutants = @(
    @{ Name = 'M1 bound-satellite authored fallback removed'; Key = 'mem'; Expect = 'K3'; Find = 'if (!FromSat.IsEmpty()) { E.OriginalResourceClassPath = FromSat; ++RwgWrAuthoredSatellite; return; }'; Repl = '' }
    @{ Name = 'M2 saved field overwritten with an empty live read'; Key = 'wroll'; Expect = 'K3'; Find = 'if (!Authored.IsEmpty()) { E.OriginalResourceClassPath = Authored; }'; Repl = 'E.OriginalResourceClassPath = Authored;' }
    @{ Name = 'M3 census field authoredFromSaved dropped'; Key = 'wrwg'; Expect = 'K4'; Find = 'authoredFromSaved=%d '; Repl = '' }
    @{ Name = 'M4 linked-to-another-core exclusion dropped'; Key = 'mem'; Expect = 'K1'; Find = 'if (LinkedCore && LinkedCore != W.Core) { ++SatsOtherCore; continue; }'; Repl = '' }
    @{ Name = 'M5 member fill removed from the relocation roll'; Key = 'wrr'; Expect = 'K2'; Find = 'if (GetMapMode() != ENodeShuffleMapMode::Vanilla) { RwgFillWellMembersFromClaims(Census, TEXT("relocation")); }'; Repl = '' }
    @{ Name = 'M6 complete census record no longer left untouched'; Key = 'mem'; Expect = 'K1'; Find = 'if (W.Members.Num() >= E.Satellites.Num()) { continue; }'; Repl = '' }
    @{ Name = 'M7 a populated saved field counted as no authored'; Key = 'mem'; Expect = 'K3'; Find = 'if (!E.OriginalResourceClassPath.IsEmpty()) { ++RwgWrAuthoredSaved; return; }'; Repl = '' }
    @{ Name = 'M8 our spawned copy accepted as a member'; Key = 'mem'; Expect = 'K1'; Find = 'if (FNodeShuffleModule::IsManagedSpawnedNode(Sat) || Ours.Contains(Sat)) { ++SatsOurs; continue; }'; Repl = '' }
    @{ Name = 'M9 member fill removed from the layout roll'; Key = 'wroll'; Expect = 'K2'; Find = 'if (GetMapMode() != ENodeShuffleMapMode::Vanilla) { RwgFillWellMembersFromClaims(Census, TEXT("layout")); }'; Repl = '' }
    @{ Name = 'M10 fill runs on vanilla'; Key = 'mem'; Expect = 'K1'; Find = 'if (GetMapMode() == ENodeShuffleMapMode::Vanilla) { return; }'; Repl = '' }
    @{ Name = 'M11 satellite fallback reads the effective resource'; Key = 'mem'; Expect = 'K3'; Find = 'const FString FromSat = WellAuthoredResourcePath(M.Actor);'; Repl = 'const FString FromSat = M.Actor->GetResourceClass() ? M.Actor->GetResourceClass()->GetPathName() : FString();' }
    @{ Name = 'M12 counters not zeroed per roll'; Key = 'wrwg'; Expect = 'K4'; Find = 'RwgWrAuthoredLive = RwgWrAuthoredSaved = RwgWrAuthoredSatellite = RwgWrMembersFromClaimWells = RwgWrMembersFromClaimSats = 0;'; Repl = '' }
)
$mutantFails = @()
$caught = 0
foreach ($m in $mutants) {
    $mut = @{}
    foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = Invoke-P4cChecks $mut
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ }
    else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "RWG P4c LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: RWG P4c lint -- K1..K7 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
