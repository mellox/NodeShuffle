# THORIUMCLASS2 lint (2026-11-01, packet rwg-thoriumclass-2; handoff _team/test-logs/2026-11-01-nodeshuffle-thoriumclass2-handoff.md).
# Measured 2026-11-01 on the real RWG save (build 69f52e1, re-roll 20:38 UTC): the rwg-thoriumclass-1 decision loop and the REPCLASS
# census iterated VanillaResourceCounts AFTER NodeShuffleRwgSeedGas::Restore had removed the SEEDED kinds (rwg-seedgas-1), so a seeded
# kind (lithium: 0 originals on the map) got no RepClassStats entry, SpawnClassForResource returned null for it and all 30 lithium
# cards kept their slot's class (16 lead_C, 6 Res_*2_C, 8 BP_ResourceNode_C; 0 on lithium's own class). The fix iterates the DEAL
# population, ResourceKinds, which is generated after DecideAndApply and before Restore.
# LOADS and PINS every source file the packet touched:
#   sub    NodeShuffleSubsystem.cpp     decision loop + census over ResourceKinds, REPCLASS seeded field, kind-table bSeeded, 2 DRESS-NATIVE calls
#   diag   NodeShuffleRepClassDiag.cpp  EmitDressNativeOnce (DRESS-NATIVE)
#   hdr    NodeShuffleSubsystem.h       NativeDressLogged + the emitter declaration
#   dealh/dealc NodeShuffleRwgDeal.h/.cpp  FKindInfo.bSeeded + RWGDEAL-RES seeded field
#   sg     NodeShuffleRwgSeedGas.cpp    premise: Restore removes the seeded kinds; IsSeededKind reads the seeded set
#   nsmod  NodeShuffle.cpp              packet marker
#   U1 POPULATION (decision): the class decision iterates ResourceKinds (generated once from VanillaResourceCounts after DecideAndApply,
#      sorted, never mutated, and read after Restore), never VanillaResourceCounts; premise pins on the seed-gas state
#   U2 POPULATION (census): the REPCLASS census keys are ResourceKinds too, so every non-/Game/ kind in the deal population (seeded kinds included) prints one REPCLASS line
#   U3 SPAWNABILITY NEVER ASSUMED: bChosenSpawnable stays the rwg-thoriumclass-1 resident-class predicate for every stat, seeded kinds
#      included; neither the chosen-flag loop nor SpawnClassForResource consults the seeded set
#   U4 REPCLASS: exact format with the trailing seeded field, measured by IsSeededKind on the roll's seed state; source prints seeded
#      for a seeded kind (it is neither a live latch nor a registry pick)
#   U5 RWGDEAL-RES: exact format with the trailing seeded field; FKindInfo.bSeeded set from IsSeededKind after bLook; Dealable unchanged
#   U6 DRESS-NATIVE: Display, exact format and arguments, diagnostics-gated, once per entry (NativeDressLogged), never loads; called from
#      the spawn and the adopt native-visual branches (exactly two call sites); the Verbose native-visual spawn line is gone; the
#      once-per-entry set is cleared at every roll beside ModdedVisualRebuilt (review F2), so a carried entry prints again after a re-roll
#   U7 Marker 2026-11-01-rwg-thoriumclass-2 once on its own line in NodeShuffle.cpp and nowhere in the packet sources
#   U8 Every new field token appears exactly once in its file (no legend echoes a field in field=value form)
# Every forbidden or superseded needle is BUILT BY CONCATENATION so this file cannot satisfy or trip its own checks.
# Mutants run in-file. Exit 0 = all hold; 1 = a rule or mutant fails; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    diag  = 'NodeShuffle\Private\NodeShuffleRepClassDiag.cpp'
    hdr   = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    dealh = 'NodeShuffle\Private\NodeShuffleRwgDeal.h'
    dealc = 'NodeShuffle\Private\NodeShuffleRwgDeal.cpp'
    sg    = 'NodeShuffle\Private\NodeShuffleRwgSeedGas.cpp'
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
function TokCount([string]$t, [string]$tok) { return ([regex]::Matches($t, '(?<![A-Za-z])' + [regex]::Escape($tok))).Count }
function Spec-Count([string]$fmt) { return ([regex]::Matches(($fmt -replace '%%', ''), '%[-+ #0]*\d*(?:\.\d+)?[a-zA-Z]')).Count }
function Get-Logs([string]$t, [string]$prefix) {
    $out = @()
    $needle = 'UE_LOG(LogNodeShuffle, '
    $at = 0
    while (($s = $t.IndexOf($needle, $at)) -ge 0) {
        $at = $s + $needle.Length
        $i = $s + 'UE_LOG'.Length; $depth = 0; $inStr = $false; $args = @(); $cur = New-Object System.Text.StringBuilder
        for (; $i -lt $t.Length; $i++) {
            $c = $t[$i]
            if ($inStr) { [void]$cur.Append($c); if ($c -eq '\') { $i++; [void]$cur.Append($t[$i]) } elseif ($c -eq '"') { $inStr = $false }; continue }
            if ($c -eq '"') { $inStr = $true; [void]$cur.Append($c); continue }
            if ($c -eq '(') { $depth++; if ($depth -eq 1) { continue } }
            if ($c -eq ')') { $depth--; if ($depth -eq 0) { $args += $cur.ToString(); break } }
            if ($c -eq ',' -and $depth -eq 1) { $args += $cur.ToString(); [void]$cur.Clear(); continue }
            [void]$cur.Append($c)
        }
        if ($args.Count -lt 3) { continue }
        $fmt = (([regex]::Matches($args[2], 'TEXT\("((?:[^"\\]|\\.)*)"\)')) | ForEach-Object { $_.Groups[1].Value }) -join ''
        if (-not $fmt.StartsWith($prefix)) { continue }
        $out += @{ Level = $args[1].Trim(); Fmt = $fmt; Args = @($args | Select-Object -Skip 3 | ForEach-Object { ($_ -replace '\s+', ' ').Trim() }) }
    }
    return $out
}
function Check-Log($T, [string]$tag, [string]$key, [string]$prefix, $wantFmt, $wantArgs, [int]$wantN) {
    $f = @()
    $logs = @(Get-Logs $T[$key] $prefix)
    if ($logs.Count -ne $wantN) { return @("($tag) expected $wantN statement(s) with format prefix '$prefix' in $key, found $($logs.Count)") }
    foreach ($l in $logs) {
        if ($l.Level -ne 'Display') { $f += "($tag) '$prefix' must log at Display, found $($l.Level)" }
        if ($null -ne $wantFmt -and $l.Fmt -ne $wantFmt) { $f += "($tag) format drifted: '$($l.Fmt)'" }
        $n = Spec-Count $l.Fmt
        if ($n -ne $l.Args.Count) { $f += "($tag) arity: $n specifier(s) vs $($l.Args.Count) argument(s) in '$prefix'" }
        if ($null -ne $wantArgs -and (($l.Args -join ' | ') -ne ($wantArgs -join ' | '))) { $f += "($tag) arguments drifted: $($l.Args -join ' | ')" }
    }
    return $f
}

# ---- anchors (unique context) ----
$marker     = '2026-11-01-rwg-' + 'thoriumclass-2'
$decideCall = 'NodeShuffleRwgSeedGas::DecideAndApply(this, Config.ModdedVanillaCountsOnRwg, GetMapMode() == ENodeShuffleMapMode::Rwg,'
$genKeys    = '    VanillaResourceCounts.GenerateKeyArray(ResourceKinds);'
$sortKinds  = '    ResourceKinds.Sort(); // deterministic order for the RNG'
$restore    = 'NodeShuffleRwgSeedGas::Restore(ModQuota, VanillaResourceCounts, VanillaCount);'
$regWanted  = 'TArray<FString> RegistryWanted;'
$decLoop    = 'for (const FString& Res : ResourceKinds)'
$decSkip    = 'if (NodeShuffleRepClass::IsGenericNodeClassPath(Res)) { continue; }'
$decStat    = 'NodeShuffleRepClass::FStat& St = RepClassStats.FindOrAdd(Res);'
$decLatch   = 'const FString* Latched = NodeClassByResource.Find(Res);'
$regPop     = 'if (!Latched || NodeShuffleRepClass::RepClassRank(*Latched, Res) < 2) { RegistryWanted.Add(Res); }'
$regSort    = 'RegistryWanted.Sort();'
$chosenLoop = 'for (TPair<FString, NodeShuffleRepClass::FStat>& StatKV : RepClassStats)'
$chosenPin  = 'StatKV.Value.bChosenSpawnable = Chosen && NodeShuffleRepClass::IsSpawnableNodeClass(FindObject<UClass>(nullptr, **Chosen));'
$censusHead = '// REPCLASS census: one line per modded resource in the pool'
$censusKeys = 'TArray<FString> CensusKeys = ResourceKinds;'
$censusLoop = 'for (const FString& Res : CensusKeys)'
$seededDef  = 'const bool bSeededRes = NodeShuffleRwgSeedGas::IsSeededKind(ModQuota, Res);'
$srcPin     = 'const TCHAR* Source = !Chosen ? TEXT("none") : ((St && St->bFromRegistry) ? TEXT("registry") : (bSeededRes ? TEXT("seeded") : TEXT("live")));'
$repFmt     = 'REPCLASS res=%s chosen=%s rank=%s seen=%d replacedFirstSeen=%d source=%s originalsClass=%s reason=%s chosenSpawnable=%d seeded=%d'
$lambdaDef  = 'const auto SpawnClassForResource = [&NodeClassByResource, &RepClassStats](const FString& Res) -> const FString*'
$lambdaRet  = 'return (St && St->bChosenSpawnable) ? NodeClassByResource.Find(Res) : nullptr;'
$kindSpawn  = 'K.bSpawnClass = SpawnClassForResource(Kind) != nullptr;'
$kindLook   = 'K.bLook = ResourceHasAuthoredLook(LoadClassByPath(Kind));'
$kindSeeded = 'K.bSeeded = NodeShuffleRwgSeedGas::IsSeededKind(ModQuota, Kind);'
$kindAdd    = 'RwgKinds.Add(Kind, K);'
$dealFmt    = 'RWGDEAL-RES res=%s cards=%d dealt=%d dealable=%d spawnClass=%d look=%d seeded=%d'
$dealable   = 'bool Dealable() const { return bGeneric || bSpawnClass || bLook; }'
$nativeFmt  = 'DRESS-NATIVE site=%s node=%s resource=%s nodeClass=%s nativeMesh=%s'
$emitDef    = 'void ANodeShuffleSubsystem::EmitDressNativeOnce(const TCHAR* Site, const FNodeShuffleEntry& Entry, AActor* Node, UStaticMeshComponent* ExcludeRock)'
$emitGate   = 'if (!FNodeShuffleModule::AreDiagnosticsEnabled() || !IsValid(Node)) { return; }'
$emitAdd    = 'NativeDressLogged.Add(Entry.EntryGuid, &bAlreadyLogged);'
$emitOnce   = 'if (bAlreadyLogged) { return; }'
$emitWalk   = 'if (!IsValid(MC) || MC == ExcludeRock || MC->GetStaticMesh() == nullptr) { continue; }'
$spawnBranch = 'else if (NodeHasOwnVisual(Node, Comp ? Comp->RockMesh : nullptr))'
$spawnCall  = 'EmitDressNativeOnce(TEXT("spawn"), Entry, Node, Comp ? Comp->RockMesh : nullptr);'
$adoptBranch = 'else if (NodeHasOwnVisual(*Existing, RockMC))'
$adoptUnhide = 'if ((*Existing)->IsHidden()) { (*Existing)->SetActorHiddenInGame(false); }'
$adoptCall  = 'EmitDressNativeOnce(TEXT("adopt"), Entry, *Existing, RockMC);'
$hdrSet     = 'TSet<FGuid> NativeDressLogged;'
$rollReset  = 'NativeDressLogged.Empty();'
$hdrDecl    = 'void EmitDressNativeOnce(const TCHAR* Site, const FNodeShuffleEntry& Entry, class AActor* Node, class UStaticMeshComponent* ExcludeRock);'
$sgRestore  = 'for (const FString& K : S.SeededKinds) { VanillaResourceCounts.Remove(K); }'
$sgIsSeeded = 'return S.SeededKinds.Num() > 0 && S.SeededKinds.Contains(Kind);'
$oldVerbose = 'spawned MODDED node %s as real class %s (native' + ' visual)'   # the Verbose line DRESS-NATIVE replaces
$loadNeedle = 'Lo' + 'ad'

function Invoke-ThoriumClass2Checks($T) {
    $f = @()
    $s = $T.sub; $d = $T.diag; $h = $T.hdr
    # U1
    if (-not (Ordered $s @($decideCall, $genKeys, $sortKinds, $restore, $regWanted, $decLoop, $decSkip, $decStat, $decLatch, $regPop, $regSort))) { $f += '(U1) decision population: ResourceKinds generated once after DecideAndApply and sorted, Restore before the decision, the decision loop iterates ResourceKinds with the stat, latch and registry population keyed on Res' }
    if ((Count $s $decLoop) -ne 1 -or (Count $s $genKeys) -ne 1) { $f += '(U1) exactly one decision loop over ResourceKinds and one ResourceKinds generation' }
    $decBody = Span $s $regWanted $regSort
    if ($decBody -eq '') { $f += '(U1) decision block not found between RegistryWanted and its sort' }
    elseif ((Count $decBody ': VanillaResourceCounts') -ne 0 -or (Count $decBody 'VanillaResourceCounts.') -ne 0 -or (Count $decBody 'VanillaResourceCounts[') -ne 0) { $f += '(U1) the decision block must not read VanillaResourceCounts (Restore has already removed the seeded kinds from it)' }
    foreach ($m in @('ResourceKinds.Add(', 'ResourceKinds.Remove', 'ResourceKinds.Reset', 'ResourceKinds.Empty', 'ResourceKinds.RemoveAt', 'ResourceKinds.Insert')) { if ((Count $s $m) -ne 0) { $f += "(U1) ResourceKinds must not be mutated after generation: $m" } }
    if ((Count $T.sg $sgRestore) -ne 1 -or (Count $T.sg $sgIsSeeded) -ne 1) { $f += '(U1) premise: Restore removes the seeded kinds from VanillaResourceCounts and IsSeededKind reads the seeded set' }
    # U2
    if ((Count $s $censusKeys) -ne 1 -or (Count $s 'VanillaResourceCounts.GetKeys(CensusKeys)') -ne 0) { $f += '(U2) the REPCLASS census keys are ResourceKinds (the deal population), not the post-Restore VanillaResourceCounts' }
    if (-not (Ordered $s @($chosenLoop, $chosenPin, $censusHead, $censusKeys, $censusLoop, 'TEXT("REPCLASS res='))) { $f += '(U2) census order: chosen-flag loop, census keys from ResourceKinds, census loop, REPCLASS line' }
    # U3
    if ((Count $s $chosenPin) -ne 1) { $f += '(U3) bChosenSpawnable must stay the resident-class spawnable predicate for every stat' }
    $chosenBody = Span $s $chosenLoop $censusHead
    if ($chosenBody -eq '') { $f += '(U3) chosen-flag loop not found before the census' }
    elseif ((Count $chosenBody 'IsSeededKind') -ne 0 -or (Count $chosenBody 'SeededKinds') -ne 0 -or (Count $chosenBody 'bChosenSpawnable = true') -ne 0) { $f += '(U3) the chosen-flag loop must not consult the seeded set or force the flag' }
    $lambdaBody = Span $s $lambdaDef '    // 6. Deal resources'
    if ($lambdaBody -eq '') { $f += '(U3) SpawnClassForResource lambda not found before the deal' }
    elseif ((Count $lambdaBody $lambdaRet) -ne 1 -or (Count $lambdaBody 'Seeded') -ne 0) { $f += '(U3) SpawnClassForResource returns the map entry only through bChosenSpawnable and never consults the seeded set' }
    # U4
    $f += Check-Log $T 'U4' 'sub' 'REPCLASS res=' $repFmt `
        @('*FPackageName::ObjectPathToObjectName(Res)', 'Chosen ? *FPackageName::ObjectPathToObjectName(*Chosen) : TEXT("none")', 'Chosen ? NodeShuffleRepClass::RepClassRankName(NodeShuffleRepClass::RepClassRank(*Chosen, Res)) : TEXT("none")', 'SeenN', '(St && St->bReplacedFirstSeen) ? 1 : 0', 'Source', '(St && !St->OriginalsClass.IsEmpty()) ? *FPackageName::ObjectPathToObjectName(St->OriginalsClass) : TEXT("none")', '(St && !St->OriginalsReason.IsEmpty()) ? *St->OriginalsReason : TEXT("none")', 'NodeShuffleRepClass::IsGenericNodeClassPath(Res) ? -1 : ((St && St->bChosenSpawnable) ? 1 : 0)', 'bSeededRes ? 1 : 0') 1
    if ((Count $s $seededDef) -ne 1 -or (Count $s $srcPin) -ne 1 -or -not (Ordered $s @($censusLoop, $seededDef, $srcPin, 'TEXT("REPCLASS res='))) { $f += '(U4) the seeded field is IsSeededKind on the roll seed state, computed per census key before the source branch, and source prints seeded for a seeded kind' }
    # U5
    $f += Check-Log $T 'U5' 'dealc' 'RWGDEAL-RES ' $dealFmt @('*FPackageName::ObjectPathToObjectName(Res)', 'C.Key', 'C.Value', 'K.Dealable() ? 1 : 0', 'K.bSpawnClass ? 1 : 0', 'K.bLook ? 1 : 0', 'K.bSeeded ? 1 : 0') 1
    if ((Count $T.dealh 'bool bSeeded = false;') -ne 1 -or (Count $T.dealh $dealable) -ne 1 -or (Count $T.dealh 'bSeeded') -ne 1) { $f += '(U5) FKindInfo declares bSeeded once, and Dealable does not read it' }
    if ((Count $s $kindSeeded) -ne 1 -or -not (Ordered $s @($kindSpawn, $kindLook, $kindSeeded, $kindAdd))) { $f += '(U5) the kind table sets bSeeded from IsSeededKind after the look, before the kind is added' }
    # U6
    $f += Check-Log $T 'U6' 'diag' 'DRESS-NATIVE ' $nativeFmt @('Site', '*Node->GetName()', '*FPackageName::ObjectPathToObjectName(Entry.AssignedResourceClassPath)', '*Node->GetClass()->GetName()', '*MeshName') 1
    $emitBody = Span $d $emitDef 'TEXT("DRESS-NATIVE '
    if ($emitBody -eq '') { $f += '(U6) EmitDressNativeOnce body not found' }
    else {
        if (-not (Ordered $emitBody @($emitGate, 'bool bAlreadyLogged = false;', $emitAdd, $emitOnce, $emitWalk, 'MeshName = MC->GetStaticMesh()->GetName();'))) { $f += '(U6) DRESS-NATIVE: diagnostics gate, once-per-entry guard, then the NodeHasOwnVisual mesh walk' }
        if ((Count $emitBody $loadNeedle) -ne 0) { $f += '(U6) DRESS-NATIVE must never load anything' }
    }
    if ((Count $s 'EmitDressNativeOnce(') -ne 2 -or (Count $s $spawnCall) -ne 1 -or (Count $s $adoptCall) -ne 1) { $f += '(U6) exactly two DRESS-NATIVE call sites: spawn and adopt' }
    if (-not (Ordered $s @($adoptBranch, $adoptUnhide, $adoptCall, $spawnBranch, 'if (Comp) { Comp->ForceVisible(); }', $spawnCall))) { $f += '(U6) the adopt call sits in the adopt native-visual branch and the spawn call in the spawn native-visual branch' }
    if ((Count $s $oldVerbose) -ne 0) { $f += '(U6) the Verbose native-visual spawn line must be gone (DRESS-NATIVE replaces it)' }
    if ((Count $h $hdrSet) -ne 1 -or (Count $h $hdrDecl) -ne 1) { $f += '(U6) NativeDressLogged and EmitDressNativeOnce must be declared' }
    if ((Count $s $rollReset) -ne 1 -or -not (Ordered $s @('ModdedVisualRebuilt.Empty();', $rollReset))) { $f += '(U6) NativeDressLogged is cleared at every roll, right after ModdedVisualRebuilt (review F2)' }
    # U7
    if ((Count $T.nsmod ('    UE_LOG(LogNodeShuffle, Display, TEXT("===== NodeShuffle packet marker (' + $marker + ')====="));')) -ne 1 -or (Count $T.nsmod $marker) -ne 1) { $f += '(U7) marker missing or not on its own line' }
    foreach ($k in @('sub', 'diag', 'hdr', 'dealh', 'dealc', 'sg')) { if ((Count $T[$k] $marker) -ne 0) { $f += "(U7) marker must not appear in the packet sources ($k)" } }
    # U8
    if ((TokCount $s 'seeded=') -ne 1) { $f += '(U8) field token echoed or missing in the subsystem: seeded=' }
    if ((TokCount $T.dealc 'seeded=') -ne 1) { $f += '(U8) field token echoed or missing in the deal: seeded=' }
    foreach ($tok in @('site=', 'nativeMesh=', 'resource=', 'nodeClass=')) { if ((TokCount $d $tok) -ne 1) { $f += "(U8) field token echoed or missing in the diagnostics file: $tok" } }
    return ,$f
}

$base = Invoke-ThoriumClass2Checks $text
if ($base.Count -gt 0) { $base | ForEach-Object { Write-Host "FAIL$_" }; Write-Host "THORIUMCLASS2 lint: FAIL -- baseline $($base.Count) problem(s)"; exit 1 }
Write-Host 'BASELINE GREEN: U1..U8 hold across 7 loaded files'

$mutants = @(
    @{ Name = 'N1 decision loop reverts to VanillaResourceCounts (seeded kinds skipped)'; Key = 'sub'; Expect = 'U1'; Find = $decLoop; Repl = 'for (const TPair<FString, int32>& KV : VanillaResourceCounts) for (const FString& Res : TArray<FString>({ KV.Key }))' }
    @{ Name = 'N2 census keys revert to VanillaResourceCounts (seeded kinds skipped)'; Key = 'sub'; Expect = 'U2'; Find = ($censusKeys + ' // rwg-thoriumclass-2: the deal population, seeded kinds included'); Repl = "TArray<FString> CensusKeys;`n        VanillaResourceCounts.GetKeys(CensusKeys);" }
    @{ Name = 'N3 seeded kinds assumed spawnable'; Key = 'sub'; Expect = 'U3'; Find = $chosenPin; Repl = 'StatKV.Value.bChosenSpawnable = Chosen && (NodeShuffleRwgSeedGas::IsSeededKind(ModQuota, StatKV.Key) || NodeShuffleRepClass::IsSpawnableNodeClass(FindObject<UClass>(nullptr, **Chosen)));' }
    @{ Name = 'N4 lambda assumes a seeded kind spawnable'; Key = 'sub'; Expect = 'U3'; Find = $lambdaRet; Repl = 'return (St && (St->bChosenSpawnable || NodeShuffleRwgSeedGas::IsSeededKind(ModQuota, Res))) ? NodeClassByResource.Find(Res) : nullptr;' }
    @{ Name = 'N5 REPCLASS drops seeded'; Key = 'sub'; Expect = 'U4'; Find = ' chosenSpawnable=%d seeded=%d"),'; Repl = ' chosenSpawnable=%d"),' }
    @{ Name = 'N6 source prints live for a seeded kind'; Key = 'sub'; Expect = 'U4'; Find = $srcPin; Repl = 'const TCHAR* Source = !Chosen ? TEXT("none") : ((St && St->bFromRegistry) ? TEXT("registry") : TEXT("live"));' }
    @{ Name = 'N7 seeded field read from the stat instead of the seed state'; Key = 'sub'; Expect = 'U4'; Find = $seededDef; Repl = 'const bool bSeededRes = St && St->bFromRegistry;' }
    @{ Name = 'N8 RWGDEAL-RES drops seeded'; Key = 'dealc'; Expect = 'U5'; Find = ' look=%d seeded=%d"),'; Repl = ' look=%d"),' }
    @{ Name = 'N9 kind table never sets bSeeded'; Key = 'sub'; Expect = 'U5'; Find = ("            " + $kindSeeded + " // rwg-thoriumclass-2: printed by RWGDEAL-RES; Dealable does not read it`n"); Repl = '' }
    @{ Name = 'N10 Dealable reads bSeeded'; Key = 'dealh'; Expect = 'U5'; Find = $dealable; Repl = 'bool Dealable() const { return bGeneric || bSpawnClass || bLook || bSeeded; }' }
    @{ Name = 'N11 DRESS-NATIVE demoted below Display'; Key = 'diag'; Expect = 'U6'; Find = 'UE_LOG(LogNodeShuffle, Display, TEXT("DRESS-NATIVE '; Repl = 'UE_LOG(LogNodeShuffle, Verbose, TEXT("DRESS-NATIVE ' }
    @{ Name = 'N12 DRESS-NATIVE ungated'; Key = 'diag'; Expect = 'U6'; Find = $emitGate; Repl = 'if (!IsValid(Node)) { return; }' }
    @{ Name = 'N13 DRESS-NATIVE once-guard dropped (adopt floods every pass)'; Key = 'diag'; Expect = 'U6'; Find = ("    " + $emitOnce + "`n"); Repl = '' }
    @{ Name = 'N14 adopt call removed'; Key = 'sub'; Expect = 'U6'; Find = ("                " + $adoptCall + " // rwg-thoriumclass-2: once per entry per session`n"); Repl = '' }
    @{ Name = 'N15 spawn call removed'; Key = 'sub'; Expect = 'U6'; Find = ("            " + $spawnCall + "`n"); Repl = '' }
    @{ Name = 'N16 DRESS-NATIVE asserts a cause'; Key = 'diag'; Expect = 'U6'; Find = 'nativeMesh=%s"),'; Repl = 'nativeMesh=%s because the class owns the mesh"),' }
    @{ Name = 'N17 the Verbose native-visual line returns'; Key = 'sub'; Expect = 'U6'; Find = $spawnCall; Repl = ($spawnCall + "`n            UE_LOG(LogNodeShuffle, Verbose, TEXT(""" + $oldVerbose + """), *ResourceClass->GetName(), *SpawnClass->GetName());") }
    @{ Name = 'N18 marker reverted'; Key = 'nsmod'; Expect = 'U7'; Find = $marker; Repl = ('2026-11-01-rwg-' + 'thoriumclass-9') }
    @{ Name = 'N19 legend echoes the seeded field'; Key = 'sub'; Expect = 'U8'; Find = '// rwg-thoriumclass-2: printed by RWGDEAL-RES; Dealable does not read it'; Repl = ('// rwg-thoriumclass-2: prints ' + 'seeded=' + '1 for a seeded kind') }
    @{ Name = 'N20 ResourceKinds mutated after generation'; Key = 'sub'; Expect = 'U1'; Find = $sortKinds; Repl = ($sortKinds + "`n    ResourceKinds.Remove(TEXT(""x""));") }
    @{ Name = 'N21 header set removed'; Key = 'hdr'; Expect = 'U6'; Find = ("    " + $hdrSet + "`n"); Repl = '' }
    @{ Name = 'N22 Restore stops removing seeded kinds (premise)'; Key = 'sg'; Expect = 'U1'; Find = $sgRestore; Repl = '' }
    @{ Name = 'N23 decision block reads VanillaResourceCounts for the latch'; Key = 'sub'; Expect = 'U1'; Find = ($decStat + "`n            " + $decLatch); Repl = ($decStat + "`n            " + 'const FString* Latched = VanillaResourceCounts.Contains(Res) ? NodeClassByResource.Find(Res) : nullptr;') }
    @{ Name = 'N25 once-per-entry set never cleared at a roll (review F2)'; Key = 'sub'; Expect = 'U6'; Find = ('    ' + $rollReset + ' // rwg-thoriumclass-2: DRESS-NATIVE is once per entry per ROLL, so a carried entry that still wears a foreign class prints again after a re-roll' + "`n"); Repl = '' }
    @{ Name = 'N24 DRESS-NATIVE loads the mesh'; Key = 'diag'; Expect = 'U6'; Find = 'MeshName = MC->GetStaticMesh()->GetName();'; Repl = ('MeshName = ' + $loadNeedle + 'Object<UStaticMesh>(nullptr, *MC->GetStaticMesh()->GetPathName())->GetName();') }
)
$bad = 0
foreach ($m in $mutants) {
    $t2 = [ordered]@{}; foreach ($k in $text.Keys) { $t2[$k] = $text[$k] }
    if ((Count $t2[$m.Key] $m.Find) -ne 1) { Write-Host "MUTANT-SETUP FAIL $($m.Name): find text not unique in $($m.Key)"; $bad++; continue }
    $t2[$m.Key] = $t2[$m.Key].Replace($m.Find, $m.Repl)
    $r = Invoke-ThoriumClass2Checks $t2
    $hits = @($r | Where-Object { $_.StartsWith("($($m.Expect))") })
    if ($hits.Count -gt 0) { Write-Host "RED  (ok) $($m.Name)" }
    else { Write-Host "GREEN (BAD) $($m.Name): expected $($m.Expect), got: $($r -join ' | ')"; $bad++ }
}
if ($bad -gt 0) { Write-Host "FAIL: $bad mutant(s) not caught"; exit 1 }
Write-Host "PASS: THORIUMCLASS2 lint -- U1..U8 hold across 7 loaded files; mutants caught $($mutants.Count)/$($mutants.Count)."
exit 0
