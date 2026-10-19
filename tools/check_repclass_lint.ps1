# REPCLASS lint (2026-10-19, packets rwg-repclass-1/-2/-3): modded resources spawn their own mod's node class.
# Spec: _team/test-logs/2026-10-19-nodeshuffle-modclass-lookup-feasibility.md. LOADS and PINS every source file the packet touched.
# Needles naming a forbidden or superseded literal are BUILT BY CONCATENATION so this file cannot match itself.
#   R1  Ranking predicate: generic = /Game/ or /Script/FactoryGame.; base-game resource ranks every class 0 (first-seen kept);
#       own-mount 2 > non-generic 1 > generic 0; replace only on a STRICTLY higher rank; the latch lambda routes through it.
#   R2  Replace-before-stamp: every LatchByResource call, every NodeClassByResource write and the registry fallback sit
#       textually before the Phase-2 stamp loop (the map is read by that loop and the conversion restamp); no write after it.
#   R3  Own-class dress skip at all three dress sites (spawn, adopt, mid-session redress): own class (IsOwnNodeClassForResource:
#       own-mount AND CDO resource == resource, fail-open on a null CDO resource; repclass-3 F7) AND NodeHasOwnVisual skips
#       the hide and the dress; the authored/captured branch follows unchanged; ResourceHasAuthoredLook still counts a capture.
#   R4  Registry fallback: one LoadObject, behind the RegistryLoadCap (=4) check on FRESH loads only (repclass-3 F9), after the mount filter and the refused exclusion;
#       CDO resource must equal the resource; the fallback only runs for non-/Game/ resources below rank 2.
#   R5  Census lines: REPCLASS, REPCLASS-REGISTRY, REPCLASS-REGISTRY-QUERY, REPCLASS-REGISTRY-CONTROL, REPCLASS-CONVERT, RESTAMP, REPCLASS-PURITY,
#       DRESS-SKIP (x3), DRESS-NOSKIP (x2):
#       Display, exact formats, exact args where single, specifier count == argument count; DRESS lines diagnostics-gated.
#   R6  No repeated field inside any new format; the superseded first-seen CONFLICT line is gone.
#   R7  Marker rwg-density-1 present once, rwg-rockcache-2 absent (moved by rockcache-1).
#   R8  (repclass-2) Conversion restamp: only a modded deal with a rank-2 class differing from the slot class; writes ONLY
#       E.RepClassSpawnPath (SaveGame), AFTER Rec.OriginalNodeClassPath took E.NodeClassPath (trap i); the re-roll rebuild
#       neither carries RepClassSpawnPath nor latches anything but Old.NodeClassPath (trap ii); resolver + eviction share it.
#       repclass-3 F8: the restamp class must be resident, a child of AFGResourceNode, and not abstract.
#   R9  (repclass-3 cold review F6) Own-visual branch BODIES (adopt un-hides only, spawn force-visibles only), registry candidate
#       and resource Sorts, the /Script/ skip, and no NodeClassByResource.Remove( after the stamp loop.
# Mutants run in-file. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    rc    = 'NodeShuffle\Private\NodeShuffleRepClass.cpp'
    rch   = 'NodeShuffle\Private\NodeShuffleRepClass.h'
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

# Parse every `UE_LOG(LogNodeShuffle, <Level>, TEXT("...")...)` statement whose format starts with $prefix.
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
        $out += @{ Level = $args[1].Trim(); Fmt = $fmt; Args = @($args | Select-Object -Skip 3 | ForEach-Object { ($_ -replace '\s+', ' ').Trim() }); Start = $s }
    }
    return $out
}
function Check-Log($T, [string]$tag, [string]$prefix, $wantFmt, $wantArgs, [int]$wantN, [string]$key = 'sub') {
    $f = @()
    $logs = @(Get-Logs $T[$key] $prefix)
    if ($logs.Count -ne $wantN) { return @("($tag) expected $wantN statement(s) with format prefix '$prefix', found $($logs.Count)") }
    foreach ($l in $logs) {
        if ($l.Level -ne 'Display') { $f += "($tag) '$prefix' must log at Display, found $($l.Level)" }
        if ($null -ne $wantFmt -and $l.Fmt -ne $wantFmt) { $f += "($tag) format drifted: '$($l.Fmt)'" }
        $n = Spec-Count $l.Fmt
        if ($n -ne $l.Args.Count) { $f += "($tag) arity: $n specifier(s) vs $($l.Args.Count) argument(s) in '$prefix'" }
        if ($null -ne $wantArgs -and (($l.Args -join ' | ') -ne ($wantArgs -join ' | '))) { $f += "($tag) arguments drifted: $($l.Args -join ' | ')" }
    }
    return $f
}


function Invoke-RepclassChecks($T) {
    $f = @()
    $s = $T.sub; $rc = $T.rc
    # R1
    if ((Count $rc 'return ClassPath.StartsWith(TEXT("/Game/")) || ClassPath.StartsWith(TEXT("/Script/FactoryGame."));') -ne 1) { $f += '(R1) generic predicate must be: starts with /Game/ or /Script/FactoryGame.' }
    if (-not (Ordered $rc @('int32 RepClassRank(const FString& ClassPath, const FString& ResourcePath)', 'if (IsGenericNodeClassPath(ResourcePath)) { return 0; }', 'if (IsGenericNodeClassPath(ClassPath)) { return 0; }', 'if (!ClassRoot.IsEmpty() && ClassRoot == NodeShuffleMountRoot(ResourcePath)) { return 2; }', 'return 1;'))) { $f += '(R1) rank must be: base-game resource 0, generic class 0, own-mount 2, else 1 -- in that order' }
    if ((Count $rc 'return RepClassRank(NewClassPath, ResourcePath) > RepClassRank(ExistingClassPath, ResourcePath);') -ne 1) { $f += '(R1) replace only on a strictly higher rank (ties keep the first seen)' }
    if ((Count $rc 'return RepClassRank(ClassPath, ResourcePath) == 2;') -ne 1) { $f += '(R1) own class must be rank 2 exactly' }
    if ((Count $s 'if (NodeShuffleRepClass::RepClassShouldReplace(*Existing, ClassPath, ResourcePath))') -ne 1 -or -not (Ordered $s @('const auto LatchByResource = [', 'St.Seen.Add(ClassPath);', 'if (FString* Existing = Map.Find(ResourcePath))', 'if (NodeShuffleRepClass::RepClassShouldReplace(*Existing, ClassPath, ResourcePath))', '*Existing = ClassPath;', 'St.bReplacedFirstSeen = true;', 'return;', 'St.FirstSeen = ClassPath;', 'Map.Add(ResourcePath, ClassPath);'))) { $f += '(R1) the latch lambda must replace only through RepClassShouldReplace and add only when the resource is new' }
    if ((Count $s ', NodeClassByResource, RepClassStats);') -ne 4) { $f += '(R1) all four live latch sites must call the ranked latch' }
    # R2
    $stamp = $s.IndexOf('    int32 StampedByResource = 0;')
    if ($stamp -lt 0) { $f += '(R2) stamp-loop anchor missing' }
    else {
        foreach ($n in @('LatchByResource(', 'NodeClassByResource.Add(', 'MatchByRegistry(', 'RepClassStats.FindOrAdd(')) {
            $last = $s.LastIndexOf($n)
            if ($last -lt 0) { $f += "(R2) '$n' absent" } elseif ($last -gt $stamp) { $f += "(R2) '$n' appears after the Phase-2 stamp loop (a replacement could land after copies were stamped)" }
        }
        if ($s.IndexOf('NodeClassByResource[', $stamp) -ge 0 -or $s.IndexOf('NodeClassByResource.FindOrAdd(', $stamp) -ge 0 -or $s.IndexOf('NodeClassByResource.Emplace(', $stamp) -ge 0) { $f += '(R2) no NodeClassByResource write after the stamp loop' }
        if (-not (Ordered $s @('// rwg-repclass-1 (b) REGISTRY FALLBACK', 'NodeShuffleRepClass::QueryDerivedNodeClasses(RegQuery);', 'TEXT("REPCLASS res=', '    // Phase 2: now that every active new node', '    int32 StampedByResource = 0;', 'if (const FString* ByResource = NodeClassByResource.Find(E.AssignedResourceClassPath))'))) { $f += '(R2) registry fallback and REPCLASS census must precede the Phase-2 stamp loop' }
    }
    # R3
    if (-not (Ordered $s @('const bool bOwnClass = NodeShuffleRepClass::IsOwnNodeClassForResource(SpawnClass, ResolvedNodeClassPath, Entry.AssignedResourceClassPath);', 'const bool bOwnClassVisual = bOwnClass && NodeHasOwnVisual(Node, Comp ? Comp->RockMesh : nullptr);', 'if (bOwnClassVisual)', 'else if (ResourceHasAuthoredLook(ResourceClass))', 'HideNativeNodeMesh(Node, Comp ? Comp->RockMesh : nullptr);'))) { $f += '(R3) spawn: own class with own visual must skip the hide and the dress, before the authored/captured branch' }
    if (-not (Ordered $s @('const bool bOwnClassAdopt = NodeShuffleRepClass::IsOwnNodeClassForResource((*Existing)->GetClass(), ResolveSpawnNodeClassPath(Entry), Entry.AssignedResourceClassPath);', 'const bool bOwnClassAdoptVisual = bOwnClassAdopt && NodeHasOwnVisual(*Existing, RockMC);', 'if (bOwnClassAdoptVisual)', 'else if (ResourceHasAuthoredLook(RC))', 'HideNativeNodeMesh(*Existing, RockMC);'))) { $f += '(R3) adopt: own class with own visual must skip the hide and the dress, before the authored/captured branch' }
    if (-not (Ordered $s @('void ANodeShuffleSubsystem::RedressSpawnedOfResource(', 'if (NodeShuffleRepClass::IsOwnNodeClassForResource((*Live)->GetClass(), ResolveSpawnNodeClassPath(Entry), Entry.AssignedResourceClassPath)', '&& NodeHasOwnVisual(*Live, LiveRock))', 'continue;', 'HideNativeNodeMesh(*Live, LiveRock);', 'SpawnVisualRockForNode(*Live, ResClass, Entry.EntryGuid);'))) { $f += '(R3) mid-session redress: own class with own visual must skip before the hide' }
    if ((Count $s 'NodeShuffleRepClass::IsOwnNodeClass(') -ne 1) { $f += '(R3) IsOwnNodeClass must be read only by the convert census (the dress sites use IsOwnNodeClassForResource)' }
    if ((Count $s 'NodeShuffleRepClass::IsOwnNodeClassForResource(') -ne 3) { $f += '(R3) IsOwnNodeClassForResource must be read at exactly the three dress sites' }
    if ((Count $rc 'return !CdoRes || CdoRes->GetPathName() == ResourcePath; }') -ne 1 -or (Count $rc 'bool IsOwnNodeClassForResource(const UClass* NodeClass, const FString& ClassPath, const FString& ResourcePath) { if (!IsOwnNodeClass(ClassPath, ResourcePath)) { return false; }') -ne 1) { $f += '(R3) IsOwnNodeClassForResource: own-mount first, then CDO resource == resource, fail-open on a null CDO resource' }
    if ((Count $s ('return FNodeShuffleNodeAssets::FindVisual(FName(*Short)) != nullptr' + "`n" + '        || FindCapturedVisual(Short) != nullptr;')) -ne 1) { $f += '(R3) ResourceHasAuthoredLook must stay unchanged (capture still serves resources with no own class)' }
    # R4
    if ((Count $T.rch 'constexpr int32 RegistryLoadCap = 4;') -ne 1) { $f += '(R4) RegistryLoadCap must be 4' }
    if ((Count $rc 'LoadObject<UClass>(') -ne 1) { $f += '(R4) exactly one LoadObject in the registry helper' }
    if (-not (Ordered $rc @('void MatchByRegistry(', 'if (ResRoot.IsEmpty() || NodeShuffleMountRoot(Path) != ResRoot) { continue; }', 'Out.MountFiltered++;', 'if (RefusedClasses.Contains(Path)) { Out.Rejected++; continue; }', 'if (UClass** Cached = LoadCache.Find(Path)) { Cls = *Cached; }', 'Cls = FindObject<UClass>(nullptr, *Path); if (!Cls) { if (Out.FreshLoads >= RegistryLoadCap) { continue; } Out.FreshLoads++; Cls = LoadObject<UClass>(', 'if (!Cls) { Out.Failed++; continue; }', 'if (!CdoRes || CdoRes->GetPathName() != ResourcePath) { Out.Rejected++; continue; }', 'Out.Matched = Cls->GetPathName();'))) { $f += '(R4) registry: mount filter, refused exclusion, load cap, single load, fail-closed, CDO resource match -- in that order' }
    if ((Count $s 'if (!Latched || NodeShuffleRepClass::RepClassRank(*Latched, KV.Key) < 2) { RegistryWanted.Add(KV.Key); }') -ne 1 -or (Count $s 'if (NodeShuffleRepClass::IsGenericNodeClassPath(KV.Key)) { continue; }') -ne 1) { $f += '(R4) the fallback must run only for non-/Game/ resources below rank 2' }
    if ((Count $s 'NodeShuffleRepClass::MatchByRegistry(Res, RegQuery, SpawnRefusedClassSubstitute, RegLoadCache, RegMatch);') -ne 1) { $f += '(R4) the fallback must exclude session-refused classes' }
    # R5
    $f += Check-Log $T 'R5' 'REPCLASS res=' 'REPCLASS res=%s chosen=%s rank=%s seen=%d replacedFirstSeen=%d source=%s' `
        @('*FPackageName::ObjectPathToObjectName(Res)', 'Chosen ? *FPackageName::ObjectPathToObjectName(*Chosen) : TEXT("none")', 'Chosen ? NodeShuffleRepClass::RepClassRankName(NodeShuffleRepClass::RepClassRank(*Chosen, Res)) : TEXT("none")', 'SeenN', '(St && St->bReplacedFirstSeen) ? 1 : 0', 'Source') 1
    $f += Check-Log $T 'R5' 'REPCLASS-REGISTRY res=' 'REPCLASS-REGISTRY res=%s derived=%d mountFiltered=%d loaded=%d failed=%d rejected=%d cap=%d matched=%s ms=%.1f' `
        @('*FPackageName::ObjectPathToObjectName(Res)', 'RegQuery.Derived', 'RegMatch.MountFiltered', 'RegMatch.Loaded', 'RegMatch.Failed', 'RegMatch.Rejected', 'NodeShuffleRepClass::RegistryLoadCap', 'RegMatch.Matched.IsEmpty() ? TEXT("none") : *FPackageName::ObjectPathToObjectName(RegMatch.Matched)', 'RegMatch.Ms') 1
    $f += Check-Log $T 'R5' 'REPCLASS-REGISTRY-QUERY ' 'REPCLASS-REGISTRY-QUERY resources=%d derived=%d baseGameBp=%d modCandidates=%d cap=%d ms=%.1f' `
        @('RegistryWanted.Num()', 'RegQuery.Derived', 'RegQuery.BaseGameBp', 'RegQuery.CandidatePaths.Num()', 'NodeShuffleRepClass::RegistryLoadCap', 'RegQuery.Ms') 1
    $f += Check-Log $T 'R5' 'REPCLASS-CONVERT ' 'REPCLASS-CONVERT converted=%d ownClassAvailable=%d restamped=%d alreadyOwn=%d kept=%d' @('Converted', 'ConvOwnClassAvailable', 'ConvRestamped', 'ConvAlreadyOwn', 'ConvKept') 1
    $f += Check-Log $T 'R5' 'REPCLASS-CONVERT-RES ' 'REPCLASS-CONVERT-RES res=%s restamped=%d kept=%d' @('*FPackageName::ObjectPathToObjectName(Res)', 'PerRes.Key', 'PerRes.Value') 1
    $f += Check-Log $T 'R5' 'RESTAMP ' 'RESTAMP slot=%s from=%s to=%s res=%s' @('*FPackageName::ObjectPathToObjectName(E.VanillaNodePath)', '*FPackageName::ObjectPathToObjectName(E.NodeClassPath)', '*FPackageName::ObjectPathToObjectName(*OwnByRes)', '*FPackageName::ObjectPathToObjectName(E.AssignedResourceClassPath)') 1
    if (-not (Ordered $s @('E.RepClassSpawnPath = *OwnByRes;', 'if (FNodeShuffleModule::AreDiagnosticsEnabled())', 'TEXT("RESTAMP slot='))) { $f += '(R5) RESTAMP must be diagnostics-gated, after the restamp' }
    $f += Check-Log $T 'R5' 'REPCLASS-REGISTRY-CONTROL ' 'REPCLASS-REGISTRY-CONTROL ownMountLatched=%d ownMountInRegistry=%d' @('OwnLatched', 'OwnInReg') 1
    if (-not (Ordered $s @('if (RegistryWanted.Num() > 0 || FNodeShuffleModule::AreDiagnosticsEnabled())', 'TEXT("REPCLASS-REGISTRY-QUERY ', 'if (FNodeShuffleModule::AreDiagnosticsEnabled()) { int32 OwnLatched = 0, OwnInReg = 0;', 'TEXT("REPCLASS-REGISTRY-CONTROL ', 'for (const FString& Res : RegistryWanted)'))) { $f += '(R5) REPCLASS-REGISTRY-CONTROL must be diagnostics-gated, after the QUERY line and before the match loop' }
    $f += Check-Log $T 'R5' 'REPCLASS-PURITY ' 'REPCLASS-PURITY node=%s class=%s assigned=%d readBack=%d restamped=%d' @('*Node->GetName()', '*Node->GetClass()->GetName()', '(int32)E.AssignedPurity.GetValue()', '(int32)Node->GetResourcePurity()', 'E.RepClassSpawnPath.IsEmpty() ? 0 : 1') 1
    if ((Count $s 'if (FNodeShuffleModule::AreDiagnosticsEnabled() && !bVanillaOrigin) { UE_LOG(LogNodeShuffle, Display, TEXT("REPCLASS-PURITY ') -ne 1 -or -not (Ordered $s @('bool ANodeShuffleSubsystem::FinalizeAdoptedNode(', 'EnsureNodeUseBox(Node);', 'TEXT("REPCLASS-PURITY '))) { $f += '(R5) REPCLASS-PURITY must be diagnostics-gated, modded-origin only, in FinalizeAdoptedNode after EnsureNodeUseBox' }
    $f += Check-Log $T 'R5' 'DRESS-SKIP ' 'DRESS-SKIP node=%s class=%s reason=ownClass' $null 3
    $f += Check-Log $T 'R5' 'DRESS-NOSKIP ' 'DRESS-NOSKIP node=%s class=%s reason=ownClassNoOwnVisual' $null 2
    if ((Count $s 'if (bOwnClass && bSpawnDiag)') -ne 1 -or (Count $s 'if (bOwnClassAdopt && FNodeShuffleModule::AreDiagnosticsEnabled() && !OwnClassDressLogged.Contains(Entry.EntryGuid))') -ne 1 -or (Count $s 'if (FNodeShuffleModule::AreDiagnosticsEnabled() && !OwnClassDressLogged.Contains(Entry.EntryGuid))') -ne 1) { $f += '(R5) every DRESS-SKIP/NOSKIP line must be diagnostics-gated (adopt and redress once per node)' }
    if ((Count $T.hdr 'TSet<FGuid> OwnClassDressLogged;') -ne 1) { $f += '(R5) OwnClassDressLogged must be declared' }
    # R6
    foreach ($p in @('REPCLASS res=', 'REPCLASS-REGISTRY res=', 'REPCLASS-REGISTRY-QUERY ', 'REPCLASS-REGISTRY-CONTROL ', 'REPCLASS-PURITY ', 'REPCLASS-CONVERT ', 'REPCLASS-CONVERT-RES ', 'RESTAMP ', 'DRESS-SKIP ', 'DRESS-NOSKIP ')) {
        foreach ($l in @(Get-Logs $s $p)) {
            $dup = (Fmt-Fields $l.Fmt) | Group-Object | Where-Object { $_.Count -gt 1 }
            if ($dup) { $f += "(R6) repeated field(s) $(($dup | ForEach-Object { $_.Name }) -join ',') in '$($l.Fmt)'" }
        }
    }
    if ((Count $s ('rep-class byResource ' + 'CONFLICT')) -ne 0 -or (Count $s ('NodeClassByResource' + 'ConflictLogged')) -ne 0) { $f += '(R6) the first-seen CONFLICT line and its set must be gone' }
    # R7
    if ((Count $T.nsmod ('LOADED (2026-10-19-rwg-' + 'density-1)')) -ne 1 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'rockcache-2')) -ne 0) { $f += '(R7) marker must be rwg-density-1 with rwg-rockcache-2 absent' }
    if ((Count $s '#include "NodeShuffleRepClass.h"') -ne 1) { $f += '(R7) subsystem must include NodeShuffleRepClass.h' }
    # R8 restamp (rwg-repclass-2)
    if ((Count $s ('RepClassSpawnPath =' + ' ')) -ne 1 -or (Count $s 'E.RepClassSpawnPath = *OwnByRes;') -ne 1 -or (Count $s 'E.RepClassSpawnPath.Reset();') -ne 1) { $f += '(R8) RepClassSpawnPath is written only by the conversion restamp (one assignment, one reset)' }
    if ((Count $s ('Old.' + 'RepClassSpawnPath')) -ne 0 -or (Count $s ('PreSubstituteSpawnPath(' + 'Old)')) -ne 0) { $f += '(R8) trap (ii): the re-roll rebuild must neither carry nor latch a restamped class' }
    if ((Count $s 'LatchByResource(Old.OriginalResourceClassPath, Old.NodeClassPath, NodeClassByResource, RepClassStats);') -ne 1) { $f += '(R8) trap (ii): the re-roll latch must read the ORIGINAL class (Old.NodeClassPath)' }
    if (-not (Ordered $s @('Rec.OriginalResourceClassPath = E.OriginalResourceClassPath;', 'if (!E.OriginalTrueLocation.IsNearlyZero()) { Rec.OriginalNodeClassPath = E.NodeClassPath; }', 'OriginalNodeRecord.Add(Rec);', 'if (E.NodeClassPath.IsEmpty()) { E.NodeClassPath = SpawnableNodeClassPath; }', 'E.RepClassSpawnPath.Reset();', 'if (!NodeShuffleRepClass::IsGenericNodeClassPath(E.AssignedResourceClassPath))', 'const UClass* OwnCls = OwnByRes ? FindObject<UClass>(nullptr, **OwnByRes) : nullptr; const bool bOwnAvailable = OwnByRes && NodeShuffleRepClass::IsOwnNodeClass(*OwnByRes, E.AssignedResourceClassPath) && OwnCls && OwnCls->IsChildOf(AFGResourceNode::StaticClass()) && !OwnCls->HasAnyClassFlags(CLASS_Abstract);', 'if (bOwnAvailable && *OwnByRes != E.NodeClassPath)', 'E.RepClassSpawnPath = *OwnByRes;', 'Converted++;'))) { $f += '(R8) trap (i) + predicate: Rec takes E.NodeClassPath BEFORE the restamp; restamp only for a modded deal with a rank-2 class that differs from the slot class' }
    $rs = $s.IndexOf('E.RepClassSpawnPath.Reset();'); $re = $s.IndexOf('Converted++;', [Math]::Max(0, $rs))
    if ($rs -ge 0 -and $re -gt $rs) { $blk = $s.Substring($rs, $re - $rs); if ($blk.Contains('E.NodeClassPath =') -or $blk.Contains('Rec.')) { $f += '(R8) trap (i): the restamp block must not write E.NodeClassPath or any record field' } }
    if ((Count $s 'return !E.RepClassSpawnPath.IsEmpty() ? E.RepClassSpawnPath') -ne 1 -or (Count $s 'const FString& ClassPath = PreSubstituteSpawnPath(Entry);') -ne 1 -or (Count $s 'const FString& SpawnPath = PreSubstituteSpawnPath(E);') -ne 1) { $f += '(R8) resolver and eviction loop must both read the restamp through PreSubstituteSpawnPath' }
    if ((Count $T.hdr 'UPROPERTY(SaveGame) FString RepClassSpawnPath;') -ne 1) { $f += '(R8) RepClassSpawnPath must be a SaveGame field' }
    # R9 (cold review 2026-10-19): bodies and determinism the R1-R8 anchors did not reach
    if ((Count $s ("            if (bOwnClassAdoptVisual)`n            {`n                if ((*Existing)->IsHidden()) { (*Existing)->SetActorHiddenInGame(false); }`n            }`n            else if (ResourceHasAuthoredLook(RC))")) -ne 1) { $f += '(R9) adopt own-visual branch must only un-hide the actor (no hide, no dress)' }
    if ((Count $s ("        if (bOwnClassVisual)`n        {`n            if (Comp) { Comp->ForceVisible(); }`n        }`n        else if (ResourceHasAuthoredLook(ResourceClass))")) -ne 1) { $f += '(R9) spawn own-visual branch must only force visible (no hide, no dress)' }
    if ((Count $rc 'Out.CandidatePaths.Sort(); // deterministic load order within the cap') -ne 1 -or (Count $s '        RegistryWanted.Sort();') -ne 1) { $f += '(R9) registry candidate and resource order must be sorted (determinism under the cap)' }
    if ((Count $rc 'if (S.StartsWith(TEXT("/Script/"))) { continue; }') -ne 1) { $f += '(R9) registry must skip /Script/ natives (loads Blueprint classes only)' }
    $stamp9 = $s.IndexOf('    int32 StampedByResource = 0;')
    if ($stamp9 -ge 0 -and $s.IndexOf('NodeClassByResource.Remove(', $stamp9) -ge 0) { $f += '(R9) no NodeClassByResource removal after the stamp loop' }
    return $f
}

$fails = Invoke-RepclassChecks $text
$mutants = @(
    @{ Name = 'M1 latch reverted to first-seen'; Key = 'sub'; Expect = 'R1'; Find = 'if (NodeShuffleRepClass::RepClassShouldReplace(*Existing, ClassPath, ResourcePath))'; Repl = 'if (false)' }
    @{ Name = 'M2 ties replace (last-seen wins)'; Key = 'rc'; Expect = 'R1'; Find = 'return RepClassRank(NewClassPath, ResourcePath) > RepClassRank('; Repl = 'return RepClassRank(NewClassPath, ResourcePath) >= RepClassRank(' }
    @{ Name = 'M3 base-game resource no longer a no-op'; Key = 'rc'; Expect = 'R1'; Find = 'if (IsGenericNodeClassPath(ResourcePath)) { return 0; }'; Repl = '' }
    @{ Name = 'M4 generic predicate loses /Script/FactoryGame.'; Key = 'rc'; Expect = 'R1'; Find = ' || ClassPath.StartsWith(TEXT("/Script/FactoryGame."));'; Repl = ';' }
    @{ Name = 'M5 latch call after the stamp loop'; Key = 'sub'; Expect = 'R2'; Find = "            E.NodeClassPath = *ByResource;`n            StampedByResource++;"; Repl = "            E.NodeClassPath = *ByResource;`n            StampedByResource++;`n            LatchByResource(E.AssignedResourceClassPath, E.NodeClassPath, NodeClassByResource, RepClassStats);" }
    @{ Name = 'M6 map write after the stamp loop'; Key = 'sub'; Expect = 'R2'; Find = '            StampedByFormFallback++;'; Repl = '            StampedByFormFallback++; NodeClassByResource.Add(E.AssignedResourceClassPath, E.NodeClassPath);' }
    @{ Name = 'M7 spawn counts captured as authored again'; Key = 'sub'; Expect = 'R3'; Find = 'const bool bOwnClassVisual = bOwnClass && NodeHasOwnVisual('; Repl = 'const bool bOwnClassVisual = false && NodeHasOwnVisual(' }
    @{ Name = 'M8 adopt counts captured as authored again'; Key = 'sub'; Expect = 'R3'; Find = 'const bool bOwnClassAdoptVisual = bOwnClassAdopt && NodeHasOwnVisual('; Repl = 'const bool bOwnClassAdoptVisual = false && NodeHasOwnVisual(' }
    @{ Name = 'M9 redress hides own-class nodes again'; Key = 'sub'; Expect = 'R3'; Find = '                && NodeHasOwnVisual(*Live, LiveRock))'; Repl = '                && false)' }
    @{ Name = 'M10 load cap dropped'; Key = 'rc'; Expect = 'R4'; Find = 'if (Out.FreshLoads >= RegistryLoadCap) { continue; } '; Repl = '' }
    @{ Name = 'M11 load cap raised'; Key = 'rch'; Expect = 'R4'; Find = 'constexpr int32 RegistryLoadCap = 4;'; Repl = 'constexpr int32 RegistryLoadCap = 400;' }
    @{ Name = 'M12 refused classes loaded'; Key = 'rc'; Expect = 'R4'; Find = 'if (RefusedClasses.Contains(Path)) { Out.Rejected++; continue; }'; Repl = '' }
    @{ Name = 'M13 no mount filter'; Key = 'rc'; Expect = 'R4'; Find = 'if (ResRoot.IsEmpty() || NodeShuffleMountRoot(Path) != ResRoot) { continue; }'; Repl = '' }
    @{ Name = 'M14 fallback also runs for base-game resources'; Key = 'sub'; Expect = 'R4'; Find = 'if (NodeShuffleRepClass::IsGenericNodeClassPath(KV.Key)) { continue; }'; Repl = '' }
    @{ Name = 'M15 REPCLASS drops an argument'; Key = 'sub'; Expect = 'R5'; Find = 'SeenN, (St && St->bReplacedFirstSeen) ? 1 : 0, Source);'; Repl = 'SeenN, Source);' }
    @{ Name = 'M16 REPCLASS-REGISTRY drops matched'; Key = 'sub'; Expect = 'R5'; Find = ' cap=%d matched=%s ms=%.1f"),'; Repl = ' cap=%d ms=%.1f"),' }
    @{ Name = 'M17 DRESS-SKIP reason becomes a cause sentence'; Key = 'sub'; Expect = 'R5'; Find = 'reason=ownClass"),'; Repl = 'reason=the mod already dressed it"),' }
    @{ Name = 'M18 DRESS-SKIP ungated at spawn'; Key = 'sub'; Expect = 'R5'; Find = 'if (bOwnClass && bSpawnDiag)'; Repl = 'if (bOwnClass)' }
    @{ Name = 'M19 REPCLASS repeats a field'; Key = 'sub'; Expect = 'R6'; Find = ' replacedFirstSeen=%d source=%s"),'; Repl = ' replacedFirstSeen=%d source=%s seen=%d"),' }
    @{ Name = 'M20 marker reverted'; Key = 'nsmod'; Expect = 'R7'; Find = ('2026-10-19-rwg-' + 'density-1'); Repl = ('2026-10-19-rwg-' + 'rockcache-2') }
    @{ Name = 'M21 trap ii: rebuild carries the restamp'; Key = 'sub'; Expect = 'R8'; Find = '            E.SpawnNodeClassPath = Old.SpawnNodeClassPath;'; Repl = ('            E.SpawnNodeClassPath = Old.SpawnNodeClassPath; E.RepClassSpawnPath = Old.' + 'RepClassSpawnPath;') }
    @{ Name = 'M22 trap ii: re-roll latch learns the copy class'; Key = 'sub'; Expect = 'R8'; Find = 'LatchByResource(Old.OriginalResourceClassPath, Old.NodeClassPath, NodeClassByResource, RepClassStats);'; Repl = ('LatchByResource(Old.OriginalResourceClassPath, PreSubstituteSpawnPath(' + 'Old), NodeClassByResource, RepClassStats);') }
    @{ Name = 'M23 trap i: restamp writes the original class'; Key = 'sub'; Expect = 'R8'; Find = 'E.RepClassSpawnPath = *OwnByRes;'; Repl = 'E.RepClassSpawnPath = *OwnByRes; E.NodeClassPath = *OwnByRes;' }
    @{ Name = 'M24 trap i: record takes the restamped class'; Key = 'sub'; Expect = 'R8'; Find = 'if (!E.OriginalTrueLocation.IsNearlyZero()) { Rec.OriginalNodeClassPath = E.NodeClassPath; }'; Repl = 'if (!E.OriginalTrueLocation.IsNearlyZero()) { Rec.OriginalNodeClassPath = PreSubstituteSpawnPath(E); }' }
    @{ Name = 'M25 restamp vanilla deals too'; Key = 'sub'; Expect = 'R8'; Find = 'if (!NodeShuffleRepClass::IsGenericNodeClassPath(E.AssignedResourceClassPath))'; Repl = 'if (true)' }
    @{ Name = 'M26 restamp without a rank-2 class'; Key = 'sub'; Expect = 'R8'; Find = 'const UClass* OwnCls = OwnByRes ? FindObject<UClass>(nullptr, **OwnByRes) : nullptr; const bool bOwnAvailable = OwnByRes && NodeShuffleRepClass::IsOwnNodeClass(*OwnByRes, E.AssignedResourceClassPath) && OwnCls && OwnCls->IsChildOf(AFGResourceNode::StaticClass()) && !OwnCls->HasAnyClassFlags(CLASS_Abstract);'; Repl = 'const bool bOwnAvailable = OwnByRes != nullptr;' }
    @{ Name = 'M27 eviction loop ignores the restamp'; Key = 'sub'; Expect = 'R8'; Find = 'const FString& SpawnPath = PreSubstituteSpawnPath(E);'; Repl = 'const FString& SpawnPath = E.SpawnNodeClassPath.IsEmpty() ? E.NodeClassPath : E.SpawnNodeClassPath;' }
    @{ Name = 'M28 RESTAMP ungated'; Key = 'sub'; Expect = 'R5'; Find = "                        if (FNodeShuffleModule::AreDiagnosticsEnabled())`n                        {`n                            UE_LOG(LogNodeShuffle, Display, TEXT(`"RESTAMP"; Repl = "                        {`n                            UE_LOG(LogNodeShuffle, Display, TEXT(`"RESTAMP" }
    @{ Name = 'X1 registry candidate Sort removed'; Key = 'rc'; Expect = 'R9'; Find = 'Out.CandidatePaths.Sort(); // deterministic load order within the cap'; Repl = '' }
    @{ Name = 'X3 map entry removed after the stamp'; Key = 'sub'; Expect = 'R9'; Find = "            E.NodeClassPath = *ByResource;`n            StampedByResource++;"; Repl = "            E.NodeClassPath = *ByResource;`n            StampedByResource++;`n            NodeClassByResource.Remove(E.AssignedResourceClassPath);" }
    @{ Name = 'X4 adopt own-visual body hides the node'; Key = 'sub'; Expect = 'R9'; Find = "                if ((*Existing)->IsHidden()) { (*Existing)->SetActorHiddenInGame(false); }`n            }`n            else if (ResourceHasAuthoredLook(RC))"; Repl = "                HideNativeNodeMesh(*Existing, RockMC);`n            }`n            else if (ResourceHasAuthoredLook(RC))" }
    @{ Name = 'X5 /Script/ skip removed'; Key = 'rc'; Expect = 'R9'; Find = 'if (S.StartsWith(TEXT("/Script/"))) { continue; }'; Repl = '' }
    @{ Name = 'X6 spawn own-visual body hides the node'; Key = 'sub'; Expect = 'R9'; Find = "            if (Comp) { Comp->ForceVisible(); }`n        }`n        else if (ResourceHasAuthoredLook(ResourceClass))"; Repl = "            HideNativeNodeMesh(Node, Comp ? Comp->RockMesh : nullptr);`n        }`n        else if (ResourceHasAuthoredLook(ResourceClass))" }
)
$mutantFails = @()
$caught = 0
foreach ($m in $mutants) {
    $mut = @{}
    foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = Invoke-RepclassChecks $mut
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ }
    else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "REPCLASS LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: REPCLASS lint -- R1..R9 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
