# THORIUMCLASS lint (2026-11-01, packet rwg-thoriumclass-1; handoff _team/test-logs/2026-11-01-nodeshuffle-thoriumclass-handoff.md).
# Measured 2026-11-01 on an RWG save with RefinedPower: all 61 relocated thorium entries spawned as BP_Thorium_C, a class RP placed
# on none of that save's originals (the captured originals are BP_ResourceNode_C), because the rwg-repclass registry fallback replaced the originals' class
# with the first own-mount class whose CDO resource matched; no Miner Mk1 bound on any of them (hologram: encroaching clearance).
# LOADS and PINS every source file the packet touched:
#   sub   NodeShuffleSubsystem.cpp   decision block, registry apply gate, chosen-spawnable flag, SpawnClassForResource, 3 consumers, census, PostLoadGame call
#   diag  NodeShuffleRepClassDiag.cpp REPCLASS-CLEARANCE + REPCLASS-STALE
#   rc/rch NodeShuffleRepClass.cpp/.h spawnable predicate, FStat fields
#   hdr   NodeShuffleSubsystem.h     member declarations
#   dealh/dealc NodeShuffleRwgDeal.h/.cpp  bSpawnClass + RWGDEAL-RES field
#   nsmod NodeShuffle.cpp            packet marker
#   T1 Decision precedes the registry: the originals' class is the live latch only when St.Seen holds it; spawnability is
#      UnspawnableNodeClassReason on the RESIDENT class (no load); the registry population is unchanged (modded, rank < 2); the
#      candidate is recorded whether or not it is applied; a registry match is APPLIED only when the originals' class is not spawnable
#   T2 Spawnable predicate: NotResident, NotNodeSubclass (AFGResourceNode), Deposit (AFGResourceDeposit, review F5), Abstract -- in that
#      order; IsSpawnableNodeClass is the null test
#   T3 The chosen class's spawnability is computed once, from the resident class, into FStat.bChosenSpawnable; SpawnClassForResource
#      returns the map entry only through that flag and sits between the census and the deal
#   T4 The three roll-time consumers (deal-kind table, Phase-2 RWG stamp, conversion restamp) read SpawnClassForResource and nothing
#      else; the subsystem no longer reads IsOwnNodeClass; the superseded field names are gone
#   T5 Lines: REPCLASS (+originalsClass, reason), REPCLASS-REGISTRY (+applied), REPCLASS-CONVERT (renamed fields), REPCLASS-CLEARANCE,
#      REPCLASS-STALE, RWGDEAL-RES: Display, exact formats, exact arguments, specifier count == argument count
#   T6 REPCLASS-CLEARANCE: emitted for the chosen class, for the originals' and registry classes when they differ, and once as the
#      control; reads mClearanceData and the native box on the CDO; never loads a class; never calls the FactoryGame-exported GetBoxExtent
#   T7 REPCLASS-STALE: once per load, latched on a non-empty Layout, called once from PostLoadGame_Implementation; originals' class
#      rebuilt from entries that record an original; stamped class read through the spawner's resolver; absent/unspawnable originals
#      count as unresolved; spawnability is tested on the RESIDENT class (review F2: never a load); the function writes NO entry field
#   T8 Marker 2026-11-01-rwg-thoriumclass-1 once on its own line in NodeShuffle.cpp and nowhere in the packet sources
#   T9 Every new field token appears exactly once in its file (no legend echoes a field in field=value form)
# Every forbidden or superseded needle is BUILT BY CONCATENATION so this file cannot satisfy or trip its own checks.
# Mutants run in-file. Exit 0 = all hold; 1 = a rule or mutant fails; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    diag  = 'NodeShuffle\Private\NodeShuffleRepClassDiag.cpp'
    rc    = 'NodeShuffle\Private\NodeShuffleRepClass.cpp'
    rch   = 'NodeShuffle\Private\NodeShuffleRepClass.h'
    hdr   = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    dealh = 'NodeShuffle\Private\NodeShuffleRwgDeal.h'
    dealc = 'NodeShuffle\Private\NodeShuffleRwgDeal.cpp'
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
$marker    = '2026-11-01-rwg-' + 'thoriumclass-1'
$seenPin   = 'const bool bSeenOnOriginal = Latched && St.Seen.Contains(*Latched);'
$whyPin    = 'const TCHAR* Why = bSeenOnOriginal ? NodeShuffleRepClass::UnspawnableNodeClassReason(FindObject<UClass>(nullptr, **Latched)) : TEXT("Absent");'
$origSpawn = 'St.bOriginalsSpawnable = bSeenOnOriginal && Why == nullptr;'
$regPop    = 'if (!Latched || NodeShuffleRepClass::RepClassRank(*Latched, KV.Key) < 2) { RegistryWanted.Add(KV.Key); }'
$regRecord = 'RegSt.RegistryClass = RegMatch.Matched;'
$regApply  = "if (!RegMatch.Matched.IsEmpty() && !RegSt.bOriginalsSpawnable`n                    && (!Latched || NodeShuffleRepClass::RepClassShouldReplace(*Latched, RegMatch.Matched, Res)))"
$chosenPin = 'StatKV.Value.bChosenSpawnable = Chosen && NodeShuffleRepClass::IsSpawnableNodeClass(FindObject<UClass>(nullptr, **Chosen));'
$lambdaDef = 'const auto SpawnClassForResource = [&NodeClassByResource, &RepClassStats](const FString& Res) -> const FString*'
$lambdaRet = 'return (St && St->bChosenSpawnable) ? NodeClassByResource.Find(Res) : nullptr;'
$kindPin   = 'K.bSpawnClass = SpawnClassForResource(Kind) != nullptr;'
$p2Pin     = 'if (!SpawnClassForResource(E.AssignedResourceClassPath))'
$restampPin = "const FString* SpawnByRes = SpawnClassForResource(E.AssignedResourceClassPath);`n                    const bool bSpawnAvailable = SpawnByRes != nullptr;"
$emitChosen = 'if (Chosen) { EmitRepClassClearanceLine(Res, TEXT("chosen"), *Chosen); }'
$emitOrig   = 'if (St && !St->OriginalsClass.IsEmpty() && (!Chosen || St->OriginalsClass != *Chosen)) { EmitRepClassClearanceLine(Res, TEXT("originals"), St->OriginalsClass); }'
$emitReg    = 'if (St && !St->RegistryClass.IsEmpty() && (!Chosen || St->RegistryClass != *Chosen)) { EmitRepClassClearanceLine(Res, TEXT("registry"), St->RegistryClass); }'
$emitCtl    = 'if (!SpawnableVanillaNodeClassPath.IsEmpty()) { EmitRepClassClearanceLine(TEXT("none"), TEXT("control"), SpawnableVanillaNodeClassPath); }'
$clearFind  = 'const UClass* Cls = FindObject<UClass>(nullptr, *ClassPath);'
$staleLatch = 'if (bRepClassStaleCensusDone || !bLayoutGenerated || Layout.Num() == 0)'
$staleCall  = '    EmitRepClassStaleCensusOnce();'
$staleOrig  = 'if (E.VanillaNodePath.IsEmpty() || E.OriginalResourceClassPath.IsEmpty() || E.NodeClassPath.IsEmpty()) { continue; }'
$staleRes   = 'const FString Stamped = ResolveSpawnNodeClassPath(E);'
$staleUnres = 'if (!Orig || !bSpawnable) { ++Unresolved; continue; }'
$clearFmt   = 'REPCLASS-CLEARANCE res=%s role=%s class=%s resident=%d nodeBase=%d clearN=%d clear=%s boxExtent=%s boxProfile=%s boxCollision=%d scsPrims=%d scs=[%s]'
$staleFmt   = 'REPCLASS-STALE count=%d pinned=%d spawned=%d modded=%d unresolved=%d byClass=[%s]'
$ownNeedle  = 'NodeShuffleRepClass::IsOwnNode' + 'Class('     # the superseded rank-2 read (IsOwnNodeClassForResource is a different name and stays)
$oldConvA   = 'ownClass' + 'Available='
$oldConvB   = 'already' + 'Own='
$oldDealF   = ' own' + 'Class=%d'
$boxExtentFn = 'GetBox' + 'Extent('                              # FACTORYGAME_API non-inline: a new import the packet must not add
$entryWrites = @(('RepClassSpawnPath' + ' ='), ('NodeClassPath' + ' ='), ('SpawnNodeClassPath' + ' ='), ('bActive' + ' ='), ('bRayCasted' + ' ='), ('OverlapNudges' + ' ='))

function Invoke-ThoriumClassChecks($T) {
    $f = @()
    $s = $T.sub; $d = $T.diag; $rc = $T.rc; $rch = $T.rch; $h = $T.hdr
    # T1
    if (-not (Ordered $s @('TArray<FString> RegistryWanted;', 'NodeShuffleRepClass::FStat& St = RepClassStats.FindOrAdd(KV.Key);', $seenPin, 'St.OriginalsClass = bSeenOnOriginal ? *Latched : FString();', $whyPin, $origSpawn, 'St.OriginalsReason = FString(TEXT("originals")) + (Why ? Why : TEXT("Spawnable"));', $regPop, 'NodeShuffleRepClass::MatchByRegistry(Res, RegQuery, SpawnRefusedClassSubstitute, RegLoadCache, RegMatch);', $regRecord, 'bool bRegApplied = false;', $regApply, 'NodeClassByResource.Add(Res, RegMatch.Matched);', 'RepClassStats.FindOrAdd(Res).bFromRegistry = true;', 'bRegApplied = true;'))) { $f += '(T1) decision block: originals class from the live latch (St.Seen), spawnability on the resident class, unchanged registry population, candidate recorded, match applied only when the originals class is not spawnable -- in that order' }
    if ((Count $s $seenPin) -ne 1 -or (Count $s $regApply) -ne 1 -or (Count $s 'RegSt.bOriginalsSpawnable') -ne 1) { $f += '(T1) the originals-seen test and the apply gate must each appear exactly once' }
    if ((Count $s 'UnspawnableNodeClassReason(FindObject<UClass>(nullptr, **Latched))') -ne 1 -or (Count $s 'UnspawnableNodeClassReason(LoadObject') -ne 0 -or (Count $s 'UnspawnableNodeClassReason(LoadClassByPath') -ne 0) { $f += '(T1) the originals class is tested RESIDENT (FindObject), never loaded' }
    # T2
    if (-not (Ordered $rc @('const TCHAR* UnspawnableNodeClassReason(const UClass* NodeClass)', 'if (!NodeClass) { return TEXT("NotResident"); }', 'if (!NodeClass->IsChildOf(AFGResourceNode::StaticClass())) { return TEXT("NotNodeSubclass"); }', 'if (NodeClass->IsChildOf(AFGResourceDeposit::StaticClass())) { return TEXT("Deposit"); }', 'if (NodeClass->HasAnyClassFlags(CLASS_Abstract)) { return TEXT("Abstract"); }', 'return nullptr;', 'bool IsSpawnableNodeClass(const UClass* NodeClass)', 'return UnspawnableNodeClassReason(NodeClass) == nullptr;'))) { $f += '(T2) spawnable predicate: NotResident, NotNodeSubclass, Deposit, Abstract, else null; IsSpawnableNodeClass is the null test' }
    if ((Count $rc '#include "Resources/FGResourceNode.h"') -ne 1 -or (Count $rc '#include "Resources/FGResourceDeposit.h"') -ne 1) { $f += '(T2) NodeShuffleRepClass.cpp must include FGResourceNode.h and FGResourceDeposit.h' }
    if ((Count $rc 'if (Cls->HasAnyClassFlags(CLASS_Abstract) || !Cls->IsChildOf(AFGResourceNodeBase::StaticClass()) || Cls->IsChildOf(AFGResourceDeposit::StaticClass())) { Out.Rejected++; continue; }') -ne 1) { $f += '(T2) MatchByRegistry must reject deposit classes before they reach NodeClassByResource (review r2 F2)' }
    if ((Count $rch 'const TCHAR* UnspawnableNodeClassReason(const UClass* NodeClass);') -ne 1 -or (Count $rch 'bool IsSpawnableNodeClass(const UClass* NodeClass);') -ne 1) { $f += '(T2) both predicates must be declared' }
    foreach ($m in @('FString OriginalsClass;', 'FString OriginalsReason;', 'bool bOriginalsSpawnable = false;', 'FString RegistryClass;', 'bool bChosenSpawnable = false;')) { if ((Count $rch $m) -ne 1) { $f += "(T2) FStat must declare: $m" } }
    # T3
    if ((Count $s $chosenPin) -ne 1 -or -not (Ordered $s @('bRegApplied = true;', 'for (TPair<FString, NodeShuffleRepClass::FStat>& StatKV : RepClassStats)', 'if (NodeShuffleRepClass::IsGenericNodeClassPath(StatKV.Key)) { continue; }', $chosenPin, 'TEXT("REPCLASS res='))) { $f += '(T3) bChosenSpawnable is computed once from the resident chosen class, after the registry apply and before the census' }
    if ((Count $s $lambdaDef) -ne 1 -or (Count $s $lambdaRet) -ne 1 -or -not (Ordered $s @('TEXT("REPCLASS res=', $emitCtl, $lambdaDef, $lambdaRet, '    // 6. Deal resources'))) { $f += '(T3) SpawnClassForResource returns the map entry only through bChosenSpawnable and sits between the census and the deal' }
    # T4
    if ((Count $s 'SpawnClassForResource(') -ne 3 -or (Count $s $kindPin) -ne 1 -or (Count $s $p2Pin) -ne 1 -or (Count $s $restampPin) -ne 1) { $f += '(T4) exactly three consumer reads of SpawnClassForResource: deal-kind table, Phase-2 RWG gate, conversion restamp' }
    if ((Count $s $ownNeedle) -ne 0) { $f += '(T4) the subsystem must not read the rank-2 own-mount predicate any more' }
    if ((Count $s $oldConvA) -ne 0 -or (Count $s $oldConvB) -ne 0 -or (Count $T.dealc $oldDealF) -ne 0 -or (Count $T.dealh ('bOwn' + 'Class')) -ne 0 -or (Count $s ('K.bOwn' + 'Class')) -ne 0) { $f += '(T4) the superseded field/member names must be gone' }
    if ((Count $T.dealh 'bool Dealable() const { return bGeneric || bSpawnClass || bLook; }') -ne 1) { $f += '(T4) Dealable reads bSpawnClass' }
    if (-not (Ordered $s @('if (bSpawnAvailable) { ConvSpawnClassAvailable++; }', 'if (bSpawnAvailable && *SpawnByRes != E.NodeClassPath)', 'E.RepClassSpawnPath = *SpawnByRes;', 'else if (bSpawnAvailable) { ConvAlreadySpawnClass++; }', 'else { ConvKept++; PerRes.Value++; }'))) { $f += '(T4) restamp branches: available, restamp when it differs from the slot class, already, kept' }
    # T5
    $f += Check-Log $T 'T5' 'sub' 'REPCLASS res=' 'REPCLASS res=%s chosen=%s rank=%s seen=%d replacedFirstSeen=%d source=%s originalsClass=%s reason=%s chosenSpawnable=%d' `
        @('*FPackageName::ObjectPathToObjectName(Res)', 'Chosen ? *FPackageName::ObjectPathToObjectName(*Chosen) : TEXT("none")', 'Chosen ? NodeShuffleRepClass::RepClassRankName(NodeShuffleRepClass::RepClassRank(*Chosen, Res)) : TEXT("none")', 'SeenN', '(St && St->bReplacedFirstSeen) ? 1 : 0', 'Source', '(St && !St->OriginalsClass.IsEmpty()) ? *FPackageName::ObjectPathToObjectName(St->OriginalsClass) : TEXT("none")', '(St && !St->OriginalsReason.IsEmpty()) ? *St->OriginalsReason : TEXT("none")', 'NodeShuffleRepClass::IsGenericNodeClassPath(Res) ? -1 : ((St && St->bChosenSpawnable) ? 1 : 0)') 1
    $f += Check-Log $T 'T5' 'sub' 'REPCLASS-REGISTRY res=' 'REPCLASS-REGISTRY res=%s derived=%d mountFiltered=%d loaded=%d failed=%d rejected=%d cap=%d matched=%s applied=%d ms=%.1f' `
        @('*FPackageName::ObjectPathToObjectName(Res)', 'RegQuery.Derived', 'RegMatch.MountFiltered', 'RegMatch.Loaded', 'RegMatch.Failed', 'RegMatch.Rejected', 'NodeShuffleRepClass::RegistryLoadCap', 'RegMatch.Matched.IsEmpty() ? TEXT("none") : *FPackageName::ObjectPathToObjectName(RegMatch.Matched)', 'bRegApplied ? 1 : 0', 'RegMatch.Ms') 1
    $f += Check-Log $T 'T5' 'sub' 'REPCLASS-CONVERT ' 'REPCLASS-CONVERT converted=%d spawnClassAvailable=%d restamped=%d alreadySpawnClass=%d kept=%d' @('Converted', 'ConvSpawnClassAvailable', 'ConvRestamped', 'ConvAlreadySpawnClass', 'ConvKept') 1
    $f += Check-Log $T 'T5' 'diag' 'REPCLASS-CLEARANCE ' $clearFmt @('*FPackageName::ObjectPathToObjectName(ResourcePath)', 'Role', '*FPackageName::ObjectPathToObjectName(ClassPath)', 'Cls ? 1 : 0', 'Cdo ? 1 : 0', 'ClearN', '*Clear', '*BoxExtent', '*BoxProfile', 'BoxCollision', 'ScsPrims', '*FString::Join(ScsParts, TEXT(";"))') 1
    $f += Check-Log $T 'T5' 'diag' 'REPCLASS-STALE ' $staleFmt @('Stale', 'StalePinned', 'Spawned', 'Modded', 'Unresolved', '*FString::Join(Parts, TEXT(";"))') 1
    $f += Check-Log $T 'T5' 'dealc' 'RWGDEAL-RES ' 'RWGDEAL-RES res=%s cards=%d dealt=%d dealable=%d spawnClass=%d look=%d' @('*FPackageName::ObjectPathToObjectName(Res)', 'C.Key', 'C.Value', 'K.Dealable() ? 1 : 0', 'K.bSpawnClass ? 1 : 0', 'K.bLook ? 1 : 0') 1
    # T6
    if (-not (Ordered $s @('TEXT("REPCLASS res=', 'if (!NodeShuffleRepClass::IsGenericNodeClassPath(Res))', $emitChosen, $emitOrig, $emitReg, $emitCtl))) { $f += '(T6) CLEARANCE emitted for chosen, then originals and registry when they differ, then the control once per roll' }
    if ((Count $s 'EmitRepClassClearanceLine(') -ne 4) { $f += '(T6) exactly four CLEARANCE emission sites in the subsystem' }
    $clearBody = Span $d 'void ANodeShuffleSubsystem::EmitRepClassClearanceLine(' 'void ANodeShuffleSubsystem::EmitRepClassStaleCensusOnce()'
    if ($clearBody -eq '') { $f += '(T6) EmitRepClassClearanceLine body not found before EmitRepClassStaleCensusOnce' }
    else {
        if ((Count $clearBody $clearFind) -ne 1 -or (Count $clearBody 'LoadObject') -ne 0 -or (Count $clearBody 'LoadClassByPath') -ne 0 -or (Count $clearBody 'StaticLoad') -ne 0) { $f += '(T6) the clearance line reads the RESIDENT class and never loads one' }
        if ((Count $clearBody 'Cdo->mClearanceData') -ne 2 -or (Count $clearBody 'Cdo->mBoxComponent.Get()') -ne 1 -or (Count $clearBody $boxExtentFn) -ne 0) { $f += '(T6) the clearance line reads mClearanceData (count + loop) and the native box component, and never calls the FactoryGame-exported box-extent accessor' }
        if ((Count $clearBody 'GetAllNodes()') -ne 1 -or (Count $clearBody 'Cast<UPrimitiveComponent>(N->ComponentTemplate.Get())') -ne 1) { $f += '(T6) the clearance line walks the Blueprint SCS primitive templates' }
    }
    if ((Count $h 'void EmitRepClassClearanceLine(const FString& ResourcePath, const TCHAR* Role, const FString& ClassPath) const;') -ne 1) { $f += '(T6) EmitRepClassClearanceLine must be declared const (friend-grant reads only)' }
    # T7
    $staleBody = Span $d 'void ANodeShuffleSubsystem::EmitRepClassStaleCensusOnce()' 'TEXT("REPCLASS-STALE '
    if ($staleBody -eq '') { $f += '(T7) EmitRepClassStaleCensusOnce body not found' }
    else {
        if ((Count $staleBody ('Lo' + 'ad')) -ne 0) { $f += '(T7) the stale census tests the RESIDENT originals class (review F2); it never loads one' }
        if (-not (Ordered $staleBody @($staleLatch, 'bRepClassStaleCensusDone = true;', $staleOrig, 'NodeShuffleRepClass::RepClassShouldReplace(*Existing, E.NodeClassPath, E.OriginalResourceClassPath)', 'if (!E.bIsNewNode || !E.bActive) { continue; }', 'NodeShuffleRepClass::IsSpawnableNodeClass(FindObject<UClass>(nullptr, **Orig))', $staleUnres, $staleRes, 'if (Stamped == *Orig) { continue; }', '++Stale;', 'if (E.bPinned) { ++StalePinned; }'))) { $f += '(T7) stale census: latch on a non-empty Layout, originals rebuilt from recording entries with the roll ranking, active spawned entries only, unresolved when absent/unspawnable, stamped class via the resolver, pinned counted apart' }
        foreach ($w in $entryWrites) { if ((Count $staleBody $w) -ne 0) { $f += "(T7) the stale census must not write an entry field: $w" } }
        if ((Count $staleBody 'for (const FNodeShuffleEntry& E : Layout)') -ne 2) { $f += '(T7) both Layout walks must be const' }
    }
    if ((Count $s $staleCall) -ne 1 -or -not (Ordered $s @('void ANodeShuffleSubsystem::PostLoadGame_Implementation(', 'LayoutVersion = CurrentLayoutVersion;', $staleCall, 'if (bLayoutGenerated && UnlockModdedScannerKnowledge())'))) { $f += '(T7) the stale census is called once, from PostLoadGame_Implementation, after the layout-version gate' }
    if ((Count $h 'bool bRepClassStaleCensusDone = false;') -ne 1 -or (Count $h 'void EmitRepClassStaleCensusOnce();') -ne 1) { $f += '(T7) stale census members must be declared' }
    # T8
    if ((Count $T.nsmod ('    UE_LOG(LogNodeShuffle, Display, TEXT("===== NodeShuffle packet marker (' + $marker + ')====="));')) -ne 1 -or (Count $T.nsmod $marker) -ne 1) { $f += '(T8) marker missing or not on its own line' }
    foreach ($k in @('sub', 'diag', 'rc', 'rch', 'hdr')) { if ((Count $T[$k] $marker) -ne 0) { $f += "(T8) marker must not appear in the packet sources ($k)" } }
    # T9
    foreach ($tok in @('originalsClass=', 'chosenSpawnable=', 'spawnClassAvailable=', 'alreadySpawnClass=', 'convFormSpawnClass=', 'matched=%s applied=')) { if ((TokCount $s $tok) -ne 1) { $f += "(T9) field token echoed or missing in the subsystem: $tok" } }
    if ((Count $s ('convForm' + 'OwnClass=')) -ne 0) { $f += '(T9) the superseded RWGDEAL-STAMP field name must be gone (review F3)' }
    foreach ($tok in @('role=', 'resident=', 'nodeBase=', 'clearN=', 'boxExtent=', 'boxProfile=', 'boxCollision=', 'scsPrims=', 'scs=', 'count=', 'pinned=', 'spawned=', 'modded=', 'unresolved=', 'byClass=')) { if ((TokCount $d $tok) -ne 1) { $f += "(T9) field token echoed or missing in the diagnostics file: $tok" } }
    if ((TokCount $T.dealc 'spawnClass=') -ne 1) { $f += '(T9) field token echoed or missing in the deal: spawnClass=' }
    return ,$f
}

$base = Invoke-ThoriumClassChecks $text
if ($base.Count -gt 0) { $base | ForEach-Object { Write-Host "FAIL$_" }; Write-Host "THORIUMCLASS lint: FAIL -- baseline $($base.Count) problem(s)"; exit 1 }
Write-Host 'BASELINE GREEN: T1..T9 hold across 8 loaded files'

$mutants = @(
    @{ Name = 'X1 registry still wins when the originals are spawnable'; Key = 'sub'; Expect = 'T1'; Find = $regApply; Repl = $regApply.Replace(' && !RegSt.bOriginalsSpawnable', '') }
    @{ Name = 'X2 a seeded latch counts as the originals class'; Key = 'sub'; Expect = 'T1'; Find = $seenPin; Repl = 'const bool bSeenOnOriginal = Latched != nullptr;' }
    @{ Name = 'X3 originals class loaded instead of tested resident'; Key = 'sub'; Expect = 'T1'; Find = $whyPin; Repl = $whyPin.Replace('FindObject<UClass>(nullptr, **Latched)', 'LoadClassByPath(*Latched)') }
    @{ Name = 'X4 originals spawnable regardless of the predicate'; Key = 'sub'; Expect = 'T1'; Find = $origSpawn; Repl = 'St.bOriginalsSpawnable = bSeenOnOriginal;' }
    @{ Name = 'X5 registry candidate not recorded'; Key = 'sub'; Expect = 'T1'; Find = ("                " + $regRecord + " // measured candidate, applied or not (the registry role of REPCLASS-CLEARANCE)`n"); Repl = '' }
    @{ Name = 'X6 spawnable loses the abstract test'; Key = 'rc'; Expect = 'T2'; Find = "        if (NodeClass->HasAnyClassFlags(CLASS_Abstract)) { return TEXT(""Abstract""); }`n"; Repl = '' }
    @{ Name = 'X7 spawnable loses the node-subclass test'; Key = 'rc'; Expect = 'T2'; Find = "        if (!NodeClass->IsChildOf(AFGResourceNode::StaticClass())) { return TEXT(""NotNodeSubclass""); }`n"; Repl = '' }
    @{ Name = 'X8 chosen flag ignores the predicate'; Key = 'sub'; Expect = 'T3'; Find = $chosenPin; Repl = 'StatKV.Value.bChosenSpawnable = Chosen != nullptr;' }
    @{ Name = 'X9 lambda bypasses the flag'; Key = 'sub'; Expect = 'T3'; Find = $lambdaRet; Repl = 'return NodeClassByResource.Find(Res);' }
    @{ Name = 'X10 deal-kind reads the map directly'; Key = 'sub'; Expect = 'T4'; Find = $kindPin; Repl = 'K.bSpawnClass = NodeClassByResource.Contains(Kind);' }
    @{ Name = 'X11 Phase-2 reads the map directly'; Key = 'sub'; Expect = 'T4'; Find = $p2Pin; Repl = 'if (!NodeClassByResource.Contains(E.AssignedResourceClassPath))' }
    @{ Name = 'X12 restamp reads the map directly'; Key = 'sub'; Expect = 'T4'; Find = $restampPin; Repl = $restampPin.Replace('SpawnClassForResource(E.AssignedResourceClassPath)', 'NodeClassByResource.Find(E.AssignedResourceClassPath)') }
    @{ Name = 'X13 a consumer re-adds the rank-2 own-mount test'; Key = 'sub'; Expect = 'T4'; Find = $kindPin; Repl = ('K.bSpawnClass = SpawnClassForResource(Kind) != nullptr && ' + $ownNeedle + 'NodeClassByResource.FindRef(Kind), Kind);') }
    @{ Name = 'X14 old CONVERT field name returns'; Key = 'sub'; Expect = 'T5'; Find = 'spawnClassAvailable=%d'; Repl = ($oldConvA + '%d') }
    @{ Name = 'X15 REPCLASS drops reason'; Key = 'sub'; Expect = 'T5'; Find = ' originalsClass=%s reason=%s chosenSpawnable=%d"),'; Repl = ' originalsClass=%s chosenSpawnable=%d"),' }
    @{ Name = 'X32 REPCLASS drops chosenSpawnable'; Key = 'sub'; Expect = 'T5'; Find = ' originalsClass=%s reason=%s chosenSpawnable=%d"),'; Repl = ' originalsClass=%s reason=%s"),' }
    @{ Name = 'X33 stale census loads the originals class'; Key = 'diag'; Expect = 'T7'; Find = 'NodeShuffleRepClass::IsSpawnableNodeClass(FindObject<UClass>(nullptr, **Orig))'; Repl = ('NodeShuffleRepClass::IsSpawnableNodeClass(' + 'LoadClass' + 'ByPath(*Orig))') }
    @{ Name = 'X34 spawnable admits deposits'; Key = 'rc'; Expect = 'T2'; Find = "        if (NodeClass->IsChildOf(AFGResourceDeposit::StaticClass())) { return TEXT(""Deposit""); }`n"; Repl = '' }
    @{ Name = 'X35 RWGDEAL-STAMP field name reverts'; Key = 'sub'; Expect = 'T9'; Find = 'convFormSpawnClass=%d'; Repl = ('convForm' + 'OwnClass=%d') }
    @{ Name = 'X36 stale census loads with LoadObject (review r2 F1)'; Key = 'diag'; Expect = 'T7'; Find = $staleUnres; Repl = ('if (Orig) { (void)Load' + 'Object<UClass>(nullptr, **Orig); } ' + $staleUnres) }
    @{ Name = 'X37 stale census loads with LoadClass<> (review r2 F1)'; Key = 'diag'; Expect = 'T7'; Find = $staleUnres; Repl = ('if (Orig) { (void)Load' + 'Class<UObject>(nullptr, **Orig); } ' + $staleUnres) }
    @{ Name = 'X16 REPCLASS-REGISTRY drops applied'; Key = 'sub'; Expect = 'T5'; Find = ' matched=%s applied=%d ms=%.1f"),'; Repl = ' matched=%s ms=%.1f"),' }
    @{ Name = 'X17 CLEARANCE demoted below Display'; Key = 'diag'; Expect = 'T5'; Find = "    UE_LOG(LogNodeShuffle, Display,`n        TEXT(""REPCLASS-CLEARANCE "; Repl = "    UE_LOG(LogNodeShuffle, Verbose,`n        TEXT(""REPCLASS-CLEARANCE " }
    @{ Name = 'X18 STALE drops a field'; Key = 'diag'; Expect = 'T5'; Find = ' unresolved=%d byClass=[%s]"),'; Repl = ' byClass=[%s]"),' }
    @{ Name = 'X19 RWGDEAL-RES field name reverts'; Key = 'dealc'; Expect = 'T5'; Find = ' spawnClass=%d look=%d"),'; Repl = ($oldDealF + ' look=%d"),') }
    @{ Name = 'X20 CLEARANCE skips the registry role'; Key = 'sub'; Expect = 'T6'; Find = ('                ' + $emitReg + "`n"); Repl = '' }
    @{ Name = 'X21 CLEARANCE control removed'; Key = 'sub'; Expect = 'T6'; Find = ('        ' + $emitCtl + "`n"); Repl = '' }
    @{ Name = 'X22 clearance line loads the class'; Key = 'diag'; Expect = 'T6'; Find = $clearFind; Repl = 'const UClass* Cls = LoadClassByPath(ClassPath);' }
    @{ Name = 'X23 clearance line calls the exported box accessor'; Key = 'diag'; Expect = 'T6'; Find = 'BoxExtent = NsRepClassVec(Box->GetUnscaledBoxExtent());'; Repl = ('BoxExtent = NsRepClassVec(Cdo->' + $boxExtentFn + '));') }
    @{ Name = 'X24 stale census restamps the entry'; Key = 'diag'; Expect = 'T7'; Find = "        ++Stale;`n"; Repl = ("        ++Stale; const_cast<FNodeShuffleEntry&>(E)." + 'RepClassSpawnPath' + " = *Orig;`n") }
    @{ Name = 'X25 stale census latch dropped'; Key = 'diag'; Expect = 'T7'; Find = $staleLatch; Repl = 'if (bRepClassStaleCensusDone)' }
    @{ Name = 'X26 stale census call removed'; Key = 'sub'; Expect = 'T7'; Find = ($staleCall + "`n`n    // knowledge-2 EARLY PASS"); Repl = "`n    // knowledge-2 EARLY PASS" }
    @{ Name = 'X27 stale census bypasses the resolver'; Key = 'diag'; Expect = 'T7'; Find = $staleRes; Repl = 'const FString Stamped = E.NodeClassPath;' }
    @{ Name = 'X28 unspawnable originals counted as stale'; Key = 'diag'; Expect = 'T7'; Find = $staleUnres; Repl = 'if (!Orig) { ++Unresolved; continue; }' }
    @{ Name = 'X29 marker reverted'; Key = 'nsmod'; Expect = 'T8'; Find = $marker; Repl = ('2026-11-01-rwg-' + 'thoriumclass-0') }
    @{ Name = 'X30 legend echoes a census field'; Key = 'diag'; Expect = 'T9'; Find = '    // 2. Count.'; Repl = ('    // 2. Count (' + 'unresolved=' + 'N).') }
    @{ Name = 'X31 legend echoes a subsystem field'; Key = 'sub'; Expect = 'T9'; Find = '// measured candidate, applied or not (the registry role of REPCLASS-CLEARANCE)'; Repl = ('// measured candidate (' + 'originalsClass=' + 'X)') }
)
$bad = 0
foreach ($m in $mutants) {
    $t2 = [ordered]@{}; foreach ($k in $text.Keys) { $t2[$k] = $text[$k] }
    if ((Count $t2[$m.Key] $m.Find) -ne 1) { Write-Host "MUTANT-SETUP FAIL $($m.Name): find text not unique in $($m.Key)"; $bad++; continue }
    $t2[$m.Key] = $t2[$m.Key].Replace($m.Find, $m.Repl)
    $r = Invoke-ThoriumClassChecks $t2
    $hits = @($r | Where-Object { $_.StartsWith("($($m.Expect))") })
    if ($hits.Count -gt 0) { Write-Host "RED  (ok) $($m.Name)" }
    else { Write-Host "GREEN (BAD) $($m.Name): expected $($m.Expect), got: $($r -join ' | ')"; $bad++ }
}
if ($bad -gt 0) { Write-Host "FAIL: $bad mutant(s) not caught"; exit 1 }
Write-Host "PASS: THORIUMCLASS lint -- T1..T9 hold across 8 loaded files; mutants caught $($mutants.Count)/$($mutants.Count)."
exit 0
