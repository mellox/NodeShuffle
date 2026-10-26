# check_rockcache_lint.ps1 -- rwg-rockcache-2: RWG rock pairing into MeshActorCache (NodeShuffleRwgRockCache.cpp).
# Source-text pins, then an in-file mutation suite (every mutant must be caught by the pin it names).
#   K1  ApplyLayout's RWG branch calls RebuildRwgRockCache, directly after the byte-identical Vanilla gate and before
#       the hide pass; the old combined skip-once text (pairing + orphan cleanup) is gone; exactly one call site.
#   K2  The 8 m rock-owns-node constant is defined ONCE (NodeShuffleRwgRockCache.h) and the backstop and the RWG
#       pairing both reference it; no 800 literal in the new .cpp, none on the backstop's RockOwnRange; the
#       backstop's own proximity test is unchanged.
#   K3  Well-satellite exclusion: rock type/back-link test, Frack piece-name test, fracking contest rule, and the
#       originals exclusion (fracking / geyser / deposit / ours).
#   K4  One rock per original, global nearest-wins, never a rock already claimed (ascending sort, two guards, claim write);
#       kept originals in VanillaNodeCache compete for rocks but are never cached.
#   K5  The new .cpp resets the cache once, adds once, runs the T64 stream-in hide immediately after the add, and
#       never writes the engine back-link.
#   K6  Census formats: RWGROCKCACHE (brief field order + appended fields), ROCKPAIR, ROCKPAIR-NOCAND, ROCKPAIR-UNPAIRED, ROCKPAIR-CAP,
#       RWGROCKHIDE (carries rocksHiddenWithNode), RWGROCKCACHE-CAPTURE-SKIP -- spec count == arg count == expected,
#       no field repeats.
#   K7  Vanilla inertness: both Vanilla gates byte-identical; the capture skip, the backstop attribution set and the
#       RWGROCKHIDE line are each behind an Rwg map-mode test.
#   K8  Marker rwg-density-2 present once, rwg-rockcache-2 absent.
# Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    rcc   = 'NodeShuffle\Private\NodeShuffleRwgRockCache.cpp'
    rch   = 'NodeShuffle\Private\NodeShuffleRwgRockCache.h'
    hdr   = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
}
$text = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = (Get-Content -Raw -Path $p) -replace "`r`n", "`n"
}
function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }
function Ordered([string]$t, [string[]]$needles) {
    $at = 0
    foreach ($n in $needles) { $i = $t.IndexOf($n, $at); if ($i -lt 0) { return $false }; $at = $i + $n.Length }
    return $true
}
function Fmt-Fields([string]$fmt) { return [regex]::Matches($fmt, '(?<![A-Za-z0-9])([A-Za-z][A-Za-z0-9]*)=') | ForEach-Object { $_.Groups[1].Value } }
function Spec-Count([string]$fmt) { return ([regex]::Matches(($fmt -replace '%%', ''), '%[-+ #0]*\d*(?:\.\d+)?[a-zA-Z]')).Count }
# Every `UE_LOG(LogNodeShuffle, <Level>, TEXT("...")...)` whose concatenated format starts with $prefix.
function Get-Logs([string]$t, [string]$prefix) {
    $out = @()
    $needle = 'UE_LOG(LogNodeShuffle, '
    $at = 0
    while (($s = $t.IndexOf($needle, $at)) -ge 0) {
        $at = $s + $needle.Length
        $i = $s + 'UE_LOG'.Length; $depth = 0; $inStr = $false; $a = @(); $cur = New-Object System.Text.StringBuilder
        for (; $i -lt $t.Length; $i++) {
            $c = $t[$i]
            if ($inStr) { [void]$cur.Append($c); if ($c -eq '\') { $i++; [void]$cur.Append($t[$i]) } elseif ($c -eq '"') { $inStr = $false }; continue }
            if ($c -eq '"') { $inStr = $true; [void]$cur.Append($c); continue }
            if ($c -eq '(') { $depth++; if ($depth -eq 1) { continue } }
            if ($c -eq ')') { $depth--; if ($depth -eq 0) { $a += $cur.ToString(); break } }
            if ($c -eq ',' -and $depth -eq 1) { $a += $cur.ToString(); [void]$cur.Clear(); continue }
            [void]$cur.Append($c)
        }
        if ($a.Count -lt 3) { continue }
        $fmt = (([regex]::Matches($a[2], 'TEXT\("((?:[^"\\]|\\.)*)"\)')) | ForEach-Object { $_.Groups[1].Value }) -join ''
        if (-not $fmt.StartsWith($prefix)) { continue }
        $out += @{ Fmt = $fmt; Args = @($a | Select-Object -Skip 3) }
    }
    return $out
}

function Invoke-RockcacheChecks($T) {
    $f = @()
    # K1
    $vanGate = 'if (GetMapMode() == ENodeShuffleMapMode::Vanilla) { Rebuild' + 'MeshActorCache(); }'
    $rwgCall = 'else if (GetMapMode() == ENodeShuffleMapMode::Rwg) { Rebuild' + 'RwgRockCache(); }'
    if ((Count $T.sub $rwgCall) -ne 1) { $f += '(K1) the RWG branch must call RebuildRwgRockCache exactly once' }
    if ((Count $T.sub ('Rebuild' + 'RwgRockCache();')) -ne 1) { $f += '(K1) exactly one RebuildRwgRockCache call site' }
    $vi = $T.sub.IndexOf($vanGate); $ri = $T.sub.IndexOf($rwgCall)
    if ($vi -lt 0 -or $ri -lt $vi -or $T.sub.Substring($vi, $ri - $vi).Contains('RwgLogSkip' + 'Once')) { $f += '(K1) the RWG call must directly follow the Vanilla gate with no skip-once between' }
    if (-not (Ordered $T.sub @($rwgCall, ('RehideVisibleOriginals' + 'Pre();'), 'SuppressOriginal' + 'Nodes();'))) { $f += '(K1) the RWG rebuild must run before the hide pass' }
    if ((Count $T.sub ('rock pairing and orphan-rock ' + 'cleanup')) -ne 0) { $f += '(K1) the old combined pairing+orphan skip-once text must be gone' }
    # K2
    if ((Count $T.rch ('constexpr float RockOwnRangeCm = ' + '800.0f;')) -ne 1) { $f += '(K2) RockOwnRangeCm must be defined once as 800.0f in NodeShuffleRwgRockCache.h' }
    if ((Count $T.sub ('constexpr float RockOwnRange = NodeShuffleRock::' + 'RockOwnRangeCm;')) -ne 1) { $f += '(K2) the backstop RockOwnRange must reference NodeShuffleRock::RockOwnRangeCm' }
    if ([regex]::IsMatch($T.sub, 'RockOwnRange\s*=\s*' + '800')) { $f += '(K2) the backstop RockOwnRange must not carry its own 800 literal' }
    if ([regex]::IsMatch($T.rcc, '(?<![0-9.])' + '800' + '(\.0*)?f?(?![0-9])')) { $f += '(K2) no 800 literal in NodeShuffleRwgRockCache.cpp (use the shared constant)' }
    if ((Count $T.rcc ('NodeShuffleRock::' + 'RockOwnRangeCm')) -lt 3) { $f += '(K2) the RWG pairing must use the shared constant for the range and the grid cell' }
    if ((Count $T.sub 'if (FVector::DistSquared2D(OrigLoc, Loc) < FMath::Square(RockOwnRange)) { bAtSuppressed = true; break; }') -ne 1) { $f += '(K2) the backstop proximity test must be unchanged' }
    # K3
    foreach ($n in @(
        'if (MA->mNodeMeshType != ENodeMeshType::MT_Node || IsFrackingActor(Back)) { ++WellExcluded; continue; }',
        ('if (MeshName.Contains(TEXT("' + 'Frack"))) { ++WellExcluded; continue; }'),
        'if (Rocks[R].NearestFrackSq <= D) { HadContested[O] = true; continue; }',
        'if (!IsValid(*It) || !IsFrackingActor(*It)) { continue; }',
        'const bool bCacheIneligible = IsFrackingActor(Node) || Node->GetResourceNodeType() == EResourceNodeType::Geyser || Node->IsA<AFGResourceDeposit>() || NodeShuffleMapMode::IsOursForRwg(Node);',
        'if (bRecorded && bCacheIneligible) { ++Ineligible; SeenOrig.Add(Node); continue; }',
        'if (!bRecorded && IsFrackingActor(Node)) { continue; }',
        'if (AFGResourceNodeBase* OwnNode = Cast<AFGResourceNodeBase>(MA->GetOwner())) { if (NodeShuffleMapMode::IsOursForRwg(OwnNode)) { ++OwnerOursSkips; continue; } }',
        'if (Rocks[R].Back && Rocks[R].Back != Node) { ++LinkedElsewhereSkips; continue; }')) {
        if ((Count $T.rcc $n) -ne 1) { $f += "(K3) well/originals exclusion missing: $n" }
    }
    # K4
    if (-not (Ordered $T.rcc @('if (A.DistSq != B.DistSq) { return A.DistSq < B.DistSq; }',
        'if (OrigRock[Cd.Orig] != INDEX_NONE) { continue; }', 'if (RockClaimed[Cd.Rock]) { continue; }',
        'RockClaimed[Cd.Rock] = true;', ('MeshActorCache.' + 'Add(Node, MA);')))) { $f += '(K4) one-rock-per-original nearest-wins claim must precede the cache add' }
    # K4 (competitors): kept originals contest rocks but only recorded originals [0, RecordedOrigs) reach the cache.
    if ((Count $T.rcc "for (int32 O = 0; O < RecordedOrigs; ++O)`n    {`n        AFGResourceNodeBase* Node = Origs[O];") -ne 1) { $f += '(K4) only recorded originals may be cached (competitors contest, never cached)' }
    if (-not (Ordered $T.rcc @('const int32 RecordedClaimants = Claimants.Num();', 'for (const TPair<FString, TWeakObjectPtr<AFGResourceNodeBase>>& KV : VanillaNodeCache)', 'Cands.Sort('))) { $f += '(K4) kept originals must enter the contest before the sort' }
    # K5
    if ((Count $T.rcc ('MeshActorCache.' + 'Reset();')) -ne 1) { $f += '(K5) the RWG rebuild must reset the cache once' }
    if ((Count $T.rcc ('MeshActorCache.' + 'Add(')) -ne 1) { $f += '(K5) exactly one cache add in the RWG rebuild' }
    if ((Count $T.rcc ("MeshActorCache." + "Add(Node, MA);`n            TryHideStreamedRockForSteadyOriginal(Node, MA);")) -ne 1) { $f += '(K5) the T64 stream-in hide must run immediately after the add' }
    if ((Count $T.rcc ('SetNode' + 'Actor(')) -ne 0) { $f += '(K5) the RWG rebuild must never write the engine back-link' }
    # K6
    $specs = @(
        @{ P = 'RWGROCKCACHE '; N = 20; Head = 'RWGROCKCACHE originals=%d paired=%d unpairedNoCandidateWithin8m=%d ambiguousMultiRock=%d alreadyClaimed=%d rocksEligible=%d ms=%.1f'; K = 'rcc' }
        @{ P = 'ROCKPAIR node='; N = 6; Head = 'ROCKPAIR node=%s rock=%s dist2dM=%.1f'; K = 'rcc' }
        @{ P = 'ROCKPAIR-NOCAND '; N = 4; Head = 'ROCKPAIR-NOCAND node=%s wellContested=%d atM=%.0f/%.0f'; K = 'rcc' }
        @{ P = 'ROCKPAIR-UNPAIRED '; N = 3; Head = 'ROCKPAIR-UNPAIRED node=%s'; K = 'rcc' }
        @{ P = 'ROCKPAIR-CAP '; N = 2; Head = 'ROCKPAIR-CAP lines=%d'; K = 'rcc' }
        @{ P = 'RWGROCKHIDE '; N = 9; Head = 'RWGROCKHIDE hide-pass=%d nodesHidden=%d rocksHiddenWithNode=%d'; K = 'sub' }
        @{ P = 'RWGROCKCACHE-CAPTURE-SKIP '; N = 2; Head = 'RWGROCKCACHE-CAPTURE-SKIP first of session: node=%s'; K = 'sub' }
    )
    foreach ($s in $specs) {
        $logs = @(Get-Logs $T[$s.K] $s.P)
        if ($logs.Count -ne 1) { $f += "(K6) expected exactly one $($s.P.Trim()) line, found $($logs.Count)"; continue }
        $fmt = $logs[0].Fmt; $sc = Spec-Count $fmt; $ac = $logs[0].Args.Count
        if ($sc -ne $s.N -or $ac -ne $s.N) { $f += "(K6) $($s.P.Trim()): specs=$sc args=$ac expected=$($s.N)" }
        if (-not $fmt.StartsWith($s.Head)) { $f += "(K6) $($s.P.Trim()) must start with: $($s.Head)" }
        $fields = @(Fmt-Fields $fmt)
        if (($fields | Select-Object -Unique).Count -ne $fields.Count) { $f += "(K6) $($s.P.Trim()) repeats a field" }
    }
    if ((Count $T.sub 'rocksHiddenWithNode=') -ne 1) { $f += '(K6) the hide-pass census must carry rocksHiddenWithNode' }
    # K7
    if ((Count $T.sub $vanGate) -ne 1) { $f += '(K7) Vanilla cache gate must stay byte-identical' }
    if ((Count $T.sub ('if (GetMapMode() == ENodeShuffleMapMode::Vanilla) { Orphan' + 'RockCleanup(); }')) -ne 1) { $f += '(K7) Vanilla orphan gate must stay byte-identical' }
    if ((Count $T.sub 'if (MeshActor && GetMapMode() == ENodeShuffleMapMode::Rwg)') -ne 1) { $f += '(K7) the capture Source-1 skip must be gated to Rwg' }
    if ((Count $T.sub 'const bool bRwgRockAttrib = GetMapMode() == ENodeShuffleMapMode::Rwg;') -ne 1) { $f += '(K7) backstop attribution flag must be Rwg-gated' }
    if ((Count $T.sub 'if (bRwgRockAttrib && bRunBackstop)') -ne 1) { $f += '(K7) the attribution set must be built only on Rwg' }
    if ((Count $T.sub "if (bRwgRockAttrib)`n    {`n        const int32 RwgHide[6]") -ne 1) { $f += '(K7) RWGROCKHIDE must be behind the Rwg flag' }
    if ((Count $T.sub "            if (bRwgRockAttrib)`n            {`n                if (RockOwner && RwgCachePairedRocks.Contains(RockOwner))") -ne 1) { $f += '(K7) backstop hit attribution must be behind the Rwg flag' }
    # K8
    if ((Count $T.nsmod ('LOADED (2026-10-26-rwg-' + '0-1-12-1)')) -ne 1 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'steep-2)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'steep-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'deal-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'density-2)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'rockcache-2')) -ne 0) { $f += '(K8) marker must be rwg-density-2 with rwg-rockcache-2 absent' }
    if ((Count $T.rcc ('if (D >= Range' + 'Sq) { continue; }')) -ne 1) { $f += '(K2) the 8 m pairing range test must be present once' }
    if ((Count $T.rcc ('if (!IsNodeRockMeshName(MeshName, TArray<FString>())) { ++Shape' + 'Rejected; continue; }')) -ne 1) { $f += '(K3) rocks must pass IsNodeRockMeshName' }
    if ((Count $T.sub (("        MeshActor = " + "nullptr;") + "`n    }`n    if (MeshActor)`n    {`n        UStaticMeshComponent* MaSmc")) -ne 1) { $f += '(K7) the Rwg capture skip must null MeshActor before Source 1 reads it' }
    return $f
}

$fails = @(Invoke-RockcacheChecks $text)
Write-Host ("CONTROL (unmutated tree): {0}" -f $(if ($fails.Count -eq 0) { 'PASS' } else { "FAILING with $($fails.Count)" }))

$mutants = @(
    @{ Name = 'X1 Vanilla-only again (RWG branch reverted to skip-once)'; Key = 'sub'; Expect = 'K1'; Find = ('else if (GetMapMode() == ENodeShuffleMapMode::Rwg) { Rebuild' + 'RwgRockCache(); }'); Repl = ('else { RwgLogSkip' + 'Once(1, TEXT("rock pairing")); }') }
    @{ Name = 'X2 8 m duplicated on the backstop'; Key = 'sub'; Expect = 'K2'; Find = ('constexpr float RockOwnRange = NodeShuffleRock::' + 'RockOwnRangeCm;'); Repl = ('constexpr float RockOwnRange = ' + '800.0f;') }
    @{ Name = 'X3 8 m duplicated in the RWG pairing'; Key = 'rcc'; Expect = 'K2'; Find = 'const double RangeSq = FMath::Square(static_cast<double>(NodeShuffleRock::RockOwnRangeCm));'; Repl = ('const double RangeSq = FMath::Square(' + '800.0);') }
    @{ Name = 'X4 well type/back-link exclusion dropped'; Key = 'rcc'; Expect = 'K3'; Find = 'if (MA->mNodeMeshType != ENodeMeshType::MT_Node || IsFrackingActor(Back)) { ++WellExcluded; continue; }'; Repl = '' }
    @{ Name = 'X5 well contest rule dropped'; Key = 'rcc'; Expect = 'K3'; Find = 'if (Rocks[R].NearestFrackSq <= D) { HadContested[O] = true; continue; }'; Repl = '' }
    @{ Name = 'X6 Frack piece-name exclusion dropped'; Key = 'rcc'; Expect = 'K3'; Find = ('if (MeshName.Contains(TEXT("' + 'Frack"))) { ++WellExcluded; continue; }'); Repl = '' }
    @{ Name = 'X7 claim dropped (a rock can pair twice)'; Key = 'rcc'; Expect = 'K4'; Find = 'if (RockClaimed[Cd.Rock]) { continue; }'; Repl = '' }
    @{ Name = 'X8 one-per-original dropped'; Key = 'rcc'; Expect = 'K4'; Find = 'if (OrigRock[Cd.Orig] != INDEX_NONE) { continue; }'; Repl = '' }
    @{ Name = 'X9 nearest-wins becomes farthest-wins'; Key = 'rcc'; Expect = 'K4'; Find = 'return A.DistSq < B.DistSq; }'; Repl = 'return A.DistSq > B.DistSq; }' }
    @{ Name = 'X17 competitors cached (a kept neighbour enters MeshActorCache)'; Key = 'rcc'; Expect = 'K4'; Find = "for (int32 O = 0; O < RecordedOrigs; ++O)`n    {`n        AFGResourceNodeBase* Node = Origs[O];"; Repl = "for (int32 O = 0; O < Origs.Num(); ++O)`n    {`n        AFGResourceNodeBase* Node = Origs[O];" }
    @{ Name = 'X18 kept originals no longer compete'; Key = 'rcc'; Expect = 'K4'; Find = 'for (const TPair<FString, TWeakObjectPtr<AFGResourceNodeBase>>& KV : VanillaNodeCache)'; Repl = 'for (const TPair<FString, TWeakObjectPtr<AFGResourceNodeBase>>& KV : TMap<FString, TWeakObjectPtr<AFGResourceNodeBase>>())' }
    @{ Name = 'X10 T64 stream-in hide dropped'; Key = 'rcc'; Expect = 'K5'; Find = '            TryHideStreamedRockForSteadyOriginal(Node, MA);'; Repl = '' }
    @{ Name = 'X11 engine back-link written'; Key = 'rcc'; Expect = 'K5'; Find = ('MeshActorCache.' + 'Add(Node, MA);'); Repl = ('MeshActorCache.' + 'Add(Node, MA); MA->SetNode' + 'Actor(Node);') }
    @{ Name = 'X12 RWGROCKCACHE loses an argument'; Key = 'rcc'; Expect = 'K6'; Find = 'NoRockWellContested, LinkedElsewhereSkips, bFirstPassAfterRollOrLoad ? 1 : 0, OwnerOursSkips);'; Repl = 'NoRockWellContested, LinkedElsewhereSkips);' }
    @{ Name = 'X13 RWGROCKHIDE loses rocksHiddenWithNode'; Key = 'sub'; Expect = 'K6'; Find = 'nodesHidden=%d rocksHiddenWithNode=%d cacheEntries'; Repl = 'nodesHidden=%d cacheEntries' }
    @{ Name = 'X14 capture skip ungated (Vanilla loses Source 1)'; Key = 'sub'; Expect = 'K7'; Find = 'if (MeshActor && GetMapMode() == ENodeShuffleMapMode::Rwg)'; Repl = 'if (MeshActor)' }
    @{ Name = 'X15 RWGROCKHIDE printed on Vanilla'; Key = 'sub'; Expect = 'K7'; Find = "if (bRwgRockAttrib)`n    {`n        const int32 RwgHide[6]"; Repl = "if (true)`n    {`n        const int32 RwgHide[6]" }
    @{ Name = 'X19 8 m range test dropped'; Key = 'rcc'; Expect = 'K2'; Find = ('if (D >= Range' + 'Sq) { continue; }'); Repl = '' }
    @{ Name = 'X20 rock-name test dropped'; Key = 'rcc'; Expect = 'K3'; Find = ('if (!IsNodeRockMeshName(MeshName, TArray<FString>())) { ++Shape' + 'Rejected; continue; }'); Repl = '' }
    @{ Name = 'X21 capture skip de-fanged'; Key = 'sub'; Expect = 'K7'; Find = (("        MeshActor = " + "nullptr;") + "`n"); Repl = '' }
    @{ Name = 'X16 marker reverted'; Key = 'nsmod'; Expect = 'K8'; Find = ('2026-10-26-rwg-' + '0-1-12-1'); Repl = ('2026-10-19-rwg-' + 'steep-2') }
)
$caught = 0
$mutantFails = @()
foreach ($m in $mutants) {
    $mut = @{}; foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = Invoke-RockcacheChecks $mut
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ }
    else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "ROCKCACHE LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: ROCKCACHE lint -- K1..K8 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
