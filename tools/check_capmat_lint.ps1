# CAPTURE-MAT lint (2026-10-19, packet capmat-1): material provenance at capture and dress. DIAGNOSTICS ONLY.
# Spec: _team/test-logs/2026-10-19-nodeshuffle-lead-visual-provenance.md (Q5). LOADS and PINS every source file the packet touched.
# Each UE_LOG is PARSED (paren/string aware): its level, its concatenated format, and its top-level argument list are compared
# verbatim, and the printf specifier count is compared to the argument count (the arity pin). Needles naming a forbidden or
# superseded literal are BUILT BY CONCATENATION so this file cannot match itself.
#   (capmat-1 review F1-F4: the skipped chain line is pinned behind EnableDiagnostics; srcCreatedS/worldS replace srcAgeS; cpdN, midVec,
#    midScalar, ovr, shows and DRESS-MAT node are pinned in their formats and arg lists.)
#   C1  CAPTURE-MAT: one Display line per slot; exact format; exact args in order; override and meshDefault predicates pinned by
#       their named declarations and by the aggregate-init order; the raw pointer is taken BEFORE the MID-to-parent unwrap.
#   C2  CAPTURE-SRC: one Display line per capture; exact format and args; srcCreatedS = the owner's CreationTime, worldS = world time;
#       pawnDistM defaults to -1; both capture lines sit inside the EnableDiagnostics gate after the unchanged CAPTURE line.
#   C3  DRESS-MAT: captured dresses only, behind EnableDiagnostics, after DressRock; exact format and args; applied predicate pinned.
#   C4  CAPTURE-CHAIN keyed per (resource, node class): the key carries the node class; both chain gates read the key; no gate reads
#       the bare resource name; the attempt and skipped lines name the class; the skipped line precedes the already-captured return.
#   C5  Existing lines unchanged: CAPTURE stored, dressed, and the four pre-existing CAPTURE-CHAIN formats.
#   C6  No field repeats inside any of the five new formats.
#   C7  Marker rwg-rockcache-1 present once (moved by rockcache-1), rwg-repclass-3 absent.
# Mutants run in-file. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
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
function Check-Log($T, [string]$tag, [string]$prefix, $wantFmt, $wantArgs, [int]$wantN) {
    $f = @()
    $logs = @(Get-Logs $T.sub $prefix)
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

function Invoke-CapmatChecks($T) {
    $f = @()
    $s = $T.sub
    # C1
    $f += Check-Log $T 'C1' 'CAPTURE-MAT ' 'CAPTURE-MAT res=%s slot=%d path=%s isMID=%d midParent=%s override=%d meshDefault=%d midVec=%d midScalar=%d' `
        @('*ShortName', 'Slot', '*D.Path', 'D.bIsMid ? 1 : 0', '*D.MidParent', 'D.bOverride ? 1 : 0', 'D.bMeshDefault ? 1 : 0', 'D.MidVec', 'D.MidScalar') 1
    if ((Count $s 'const bool bOverride = Smc->OverrideMaterials.IsValidIndex(i) && Smc->OverrideMaterials[i] != nullptr;') -ne 1) { $f += '(C1) override must be: the override array holds a non-null entry for the slot' }
    if ((Count $s 'const bool bMeshDefault = RawMat == Smc->GetStaticMesh()->GetMaterial(i);') -ne 1) { $f += '(C1) meshDefault must be: the read pointer equals the static mesh asset material for the slot' }
    if ((Count $s 'struct FCapMatDiag { FString Path; FString MidParent; bool bIsMid; bool bOverride; bool bMeshDefault; int32 MidVec; int32 MidScalar; };') -ne 1 -or (Count $s 'RawMid != nullptr, bOverride, bMeshDefault,') -ne 1) { $f += '(C1) FCapMatDiag field order and its aggregate init must keep isMID, override, meshDefault in that order' }
    if ((Count $s 'RawMid ? RawMid->VectorParameterValues.Num() : -1, RawMid ? RawMid->ScalarParameterValues.Num() : -1 });') -ne 1) { $f += '(C1) midVec and midScalar must be the MID vector and scalar override counts, -1 when the read pointer is not a MID' }
    if (-not (Ordered $s @('UMaterialInterface* Mat = Smc->GetMaterial(i);', 'UMaterialInterface* const RawMat = Mat;', 'Mat = Mid->Parent;', 'Cap.MaterialPaths.Add(Mat ? Mat->GetPathName() : FString());'))) { $f += '(C1) the raw pointer must be taken before the MID-to-parent unwrap, and the persisted path must stay the unwrapped one' }
    # C2
    $f += Check-Log $T 'C2' 'CAPTURE-SRC ' 'CAPTURE-SRC res=%s srcClass=%s srcComp=%s srcCreatedS=%.1f worldS=%.1f srcLoc=%.0f/%.0f/%.0f pawnDistM=%.0f cpdN=%d' `
        @('*ShortName', 'SrcActor ? *SrcActor->GetClass()->GetName() : TEXT("null")', '*Smc->GetName()', 'SrcActor ? SrcActor->CreationTime : -1.0f', 'World ? static_cast<float>(World->GetTimeSeconds()) : -1.0f', 'SrcLoc.X', 'SrcLoc.Y', 'SrcLoc.Z', 'PawnDistM', 'Smc->GetCustomPrimitiveData().Data.Num()') 1
    if ((Count $s 'const float SrcAgeS') -ne 0) { $f += '(C2) the derived srcAgeS is retired: print the raw srcCreatedS and worldS' }
    if ((Count $s 'const float PawnDistM = NearestPawnDistSq < 0.0 ? -1.0f : static_cast<float>(FMath::Sqrt(NearestPawnDistSq) / 100.0);') -ne 1 -or (Count $s 'double NearestPawnDistSq = -1.0;') -ne 1) { $f += '(C2) pawnDistM must be metres to the nearest pawn, -1 when none' }
    if (-not (Ordered $s @('TEXT("CAPTURE: stored visual for %s', 'if (FNodeShuffleModule::AreDiagnosticsEnabled())', 'const AActor* SrcActor = Smc->GetOwner();', 'TEXT("CAPTURE-SRC ', 'TEXT("CAPTURE-MAT ', 'CapturePendingPasses.Remove(ShortName);'))) { $f += '(C2) CAPTURE-SRC and CAPTURE-MAT must sit inside the diagnostics gate, after the CAPTURE line, before the retry-budget wipe' }
    # C3
    $f += Check-Log $T 'C3' 'DRESS-MAT ' 'DRESS-MAT res=%s slot=%d loaded=%d path=%s applied=%d ovr=%d shows=%s node=%s' `
        @('*ResourceClass->GetName()', 'Slot', 'Loaded ? 1 : 0', 'bHasPath ? *Persisted->MaterialPaths[Slot] : TEXT("null")', 'bApplied ? 1 : 0', '(RockMC->OverrideMaterials.IsValidIndex(Slot) && RockMC->OverrideMaterials[Slot] == Loaded) ? 1 : 0', 'RockMC->GetMaterial(Slot) ? *RockMC->GetMaterial(Slot)->GetPathName() : TEXT("null")', '*Node->GetName()') 1
    if ((Count $s 'const bool bApplied = Loaded != nullptr && RockMC->GetMaterial(Slot) == Loaded;') -ne 1) { $f += '(C3) applied must be: loaded AND GetMaterial(slot) after DressRock equals the loaded object' }
    if ((Count $s 'if (bCaptured && FNodeShuffleModule::AreDiagnosticsEnabled() && IsValid(RockMC))') -ne 1) { $f += '(C3) DRESS-MAT must be gated to CAPTURED dresses behind EnableDiagnostics' }
    if (-not (Ordered $s @('else { Legacy->DressRock(Mesh, MatPtrs, WantScale, RelOffset, RockRelQuat.Rotator()); }', 'if (bCaptured && FNodeShuffleModule::AreDiagnosticsEnabled() && IsValid(RockMC))', 'const FNodeShuffleCapturedVisual* Persisted = FindCapturedVisual(ResourceClass->GetName());', 'TEXT("DRESS-MAT '))) { $f += '(C3) DRESS-MAT must read the PERSISTED capture and log after DressRock' }
    # C4
    if ((Count $s 'const FString ChainKey = ShortName + TEXT("|") + Node->GetClass()->GetName();') -ne 1) { $f += '(C4) the chain key must carry the node class' }
    if ((Count $s '!CaptureChainLogged.Contains(ChainKey)') -ne 2 -or (Count $s 'CaptureChainLogged.Add(ChainKey);') -ne 2) { $f += '(C4) both chain gates (attempt and skipped) must read and record the (resource, class) key' }
    if ((Count $s 'if (FNodeShuffleModule::AreDiagnosticsEnabled() && !CaptureChainLogged.Contains(ChainKey))') -ne 1) { $f += '(C4) the skipped chain line must be behind EnableDiagnostics' }
    if ((Count $s ('CaptureChainLogged.Contains(' + 'ShortName)')) -ne 0 -or (Count $s ('CaptureChainLogged.Add(' + 'ShortName)')) -ne 0) { $f += '(C4) no chain gate may key on the bare resource name' }
    if ((Count $s ('&& !CaptureChainLogged.Contains(ChainKey); // decision chain once per (resource, node class) per session')) -ne 1) { $f += '(C4) bDiagChain must be gated by the key (once per resource and class, never per attempt)' }
    $f += Check-Log $T 'C4' 'CAPTURE-CHAIN %s: nodeClass=' $null @('*ShortName', '*Node->GetClass()->GetName()', '*Node->GetName()') 2
    if ((Count $s 'TEXT("CAPTURE-CHAIN %s: nodeClass=%s node=%s skipped (FindCapturedVisual returned a capture for this resource)")') -ne 1 -or (Count $s 'TEXT("CAPTURE-CHAIN %s: nodeClass=%s node=%s attempt (first for this resource and node class this session)")') -ne 1) { $f += '(C4) the skipped and attempt chain lines must both exist with their pinned text' }
    if (-not (Ordered $s @('const FString ChainKey = ', 'const bool bAlreadyCaptured = FindCapturedVisual(ShortName) != nullptr;', 'node=%s skipped (', 'return false; // already captured', 'if (CaptureTerminalThisSession.Contains(ShortName)) { return false; }', 'node=%s attempt ('))) { $f += '(C4) order must be key, captured test, skipped line, already-captured return, terminal return, attempt line' }
    # C5
    if ((Count $s 'TEXT("CAPTURE: stored visual for %s from ''%s'' (source=%s) — mesh ''%s'', %d mat(s), scale=(%.2f,%.2f,%.2f) (persisted)")') -ne 1) { $f += '(C5) the CAPTURE stored line must be unchanged' }
    if ((Count $s 'TEXT("dressed %s rock (node subobject) for %s: mesh ''%s'' scale=(%.2f,%.2f,%.2f) relZ=%.0f %d mat(s)")') -ne 1) { $f += '(C5) the dressed line must be unchanged' }
    foreach ($old in @('TEXT("CAPTURE-CHAIN %s: paired mesh actor ''%s'' rejected (%s)")', 'TEXT("CAPTURE-CHAIN %s: node ''%s'' has NO paired mesh actor (engine links unset)")', 'TEXT("CAPTURE-CHAIN %s: candidate [%s] ''%s'' mesh=''%s'' visible=%d -> %s")', 'TEXT("CAPTURE-CHAIN %s: no capturable source on ''%s'' -> %s. Nearest mesh actor within 10m: %s")')) {
        if ((Count $s $old) -ne 1) { $f += "(C5) pre-existing chain line changed: $($old.Substring(0, 50))..." }
    }
    # C6
    foreach ($p in @('CAPTURE-MAT ', 'CAPTURE-SRC ', 'DRESS-MAT ', 'CAPTURE-CHAIN %s: nodeClass=')) {
        foreach ($l in @(Get-Logs $s $p)) {
            $dup = (Fmt-Fields $l.Fmt) | Group-Object | Where-Object { $_.Count -gt 1 }
            if ($dup) { $f += "(C6) repeated field(s) $(($dup | ForEach-Object { $_.Name }) -join ',') in '$($l.Fmt.Substring(0, [Math]::Min(40, $l.Fmt.Length)))'" }
        }
    }
    # C7
    if ((Count $T.nsmod ('LOADED (2026-10-19-rwg-' + 'rockcache-1)')) -ne 1 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'repclass-3')) -ne 0) { $f += '(C7) marker must be rwg-rockcache-1 with rwg-repclass-3 absent' }
    return $f
}

$fails = Invoke-CapmatChecks $text
$mutants = @(
    @{ Name = 'M1 CAPTURE-MAT drops the meshDefault field'; Key = 'sub'; Expect = 'C1'; Find = ' meshDefault=%d midVec=%d midScalar=%d"),'; Repl = ' midVec=%d midScalar=%d"),' }
    @{ Name = 'M2 CAPTURE-MAT swaps override/meshDefault args'; Key = 'sub'; Expect = 'C1'; Find = 'D.bOverride ? 1 : 0, D.bMeshDefault ? 1 : 0'; Repl = 'D.bMeshDefault ? 1 : 0, D.bOverride ? 1 : 0' }
    @{ Name = 'M3 override/meshDefault predicates swapped'; Key = 'sub'; Expect = 'C1'; Find = 'RawMid != nullptr, bOverride, bMeshDefault,'; Repl = 'RawMid != nullptr, bMeshDefault, bOverride,' }
    @{ Name = 'M4 CAPTURE-CHAIN downgraded to per-attempt'; Key = 'sub'; Expect = 'C4'; Find = '&& !CaptureChainLogged.Contains(ChainKey); // decision chain'; Repl = '; // decision chain' }
    @{ Name = 'M5 CAPTURE-CHAIN back to per-resource key'; Key = 'sub'; Expect = 'C4'; Find = 'const FString ChainKey = ShortName + TEXT("|") + Node->GetClass()->GetName();'; Repl = 'const FString ChainKey = ShortName;' }
    @{ Name = 'M6 chain lines drop the class'; Key = 'sub'; Expect = 'C4'; Find = '*ShortName, *Node->GetClass()->GetName(), *Node->GetName());'; Repl = '*ShortName, *Node->GetName());' }
    @{ Name = 'M7 srcCreatedS sign flipped'; Key = 'sub'; Expect = 'C2'; Find = 'SrcActor ? SrcActor->CreationTime : -1.0f'; Repl = 'SrcActor ? -SrcActor->CreationTime : -1.0f' }
    @{ Name = 'M8 CAPTURE-SRC loses an argument'; Key = 'sub'; Expect = 'C2'; Find = 'SrcLoc.X, SrcLoc.Y, SrcLoc.Z, PawnDistM, Smc->GetCustomPrimitiveData().Data.Num());'; Repl = 'SrcLoc.X, SrcLoc.Y, PawnDistM, Smc->GetCustomPrimitiveData().Data.Num());' }
    @{ Name = 'M9 DRESS-MAT applied ignores loaded'; Key = 'sub'; Expect = 'C3'; Find = 'const bool bApplied = Loaded != nullptr && RockMC'; Repl = 'const bool bApplied = RockMC' }
    @{ Name = 'M10 DRESS-MAT for every dress'; Key = 'sub'; Expect = 'C3'; Find = 'if (bCaptured && FNodeShuffleModule::AreDiagnosticsEnabled() && IsValid(RockMC))'; Repl = 'if (FNodeShuffleModule::AreDiagnosticsEnabled() && IsValid(RockMC))' }
    @{ Name = 'M11 marker reverted'; Key = 'nsmod'; Expect = 'C7'; Find = ('2026-10-19-rwg-' + 'rockcache-1'); Repl = ('2026-10-19-rwg-' + 'repclass-3') }
    @{ Name = 'M12 CAPTURE stored line edited'; Key = 'sub'; Expect = 'C5'; Find = '%d mat(s), scale=(%.2f,%.2f,%.2f) (persisted)'; Repl = '%d mat(s) scale=(%.2f,%.2f,%.2f) (persisted)' }
    @{ Name = 'M13 CAPTURE-MAT repeats a field'; Key = 'sub'; Expect = 'C6'; Find = 'midScalar=%d"),'; Repl = 'midScalar=%d slot=%d"),' }
    @{ Name = 'M14 CAPTURE-MAT downgraded to Verbose'; Key = 'sub'; Expect = 'C1'; Find = 'UE_LOG(LogNodeShuffle, Display, TEXT("CAPTURE-MAT '; Repl = 'UE_LOG(LogNodeShuffle, Verbose, TEXT("CAPTURE-MAT ' }
    @{ Name = 'M15 raw pointer read after the unwrap'; Key = 'sub'; Expect = 'C1'; Find = 'UMaterialInterface* const RawMat = Mat;'; Repl = 'UMaterialInterface* const RawMat = Smc->GetMaterial(i);' }
    @{ Name = 'M16 skipped line removed (already-captured early return restored)'; Key = 'sub'; Expect = 'C4'; Find = 'const bool bAlreadyCaptured = FindCapturedVisual(ShortName) != nullptr;'; Repl = 'if (FindCapturedVisual(ShortName) != nullptr) { return false; } const bool bAlreadyCaptured = false;' }
    @{ Name = 'M17 skipped line ungated'; Key = 'sub'; Expect = 'C4'; Find = 'if (FNodeShuffleModule::AreDiagnosticsEnabled() && !CaptureChainLogged.Contains(ChainKey))'; Repl = 'if (!CaptureChainLogged.Contains(ChainKey))' }
    @{ Name = 'M18 DRESS-MAT ovr compares to nullptr instead of the loaded object'; Key = 'sub'; Expect = 'C3'; Find = 'RockMC->OverrideMaterials[Slot] == Loaded) ? 1 : 0'; Repl = 'RockMC->OverrideMaterials[Slot] != nullptr) ? 1 : 0' }
    @{ Name = 'M19 DRESS-MAT drops ovr'; Key = 'sub'; Expect = 'C3'; Find = ' applied=%d ovr=%d shows=%s'; Repl = ' applied=%d shows=%s' }
    @{ Name = 'M20 DRESS-MAT drops shows'; Key = 'sub'; Expect = 'C3'; Find = ' ovr=%d shows=%s node=%s"),'; Repl = ' ovr=%d node=%s"),' }
    @{ Name = 'M21 DRESS-MAT drops node identity'; Key = 'sub'; Expect = 'C3'; Find = ' shows=%s node=%s"),'; Repl = ' shows=%s"),' }
    @{ Name = 'M22 srcCreatedS/worldS reverted to a derived age'; Key = 'sub'; Expect = 'C2'; Find = 'srcCreatedS=%.1f worldS=%.1f'; Repl = 'srcAgeS=%.1f' }
    @{ Name = 'M23 worldS arg replaced by a constant'; Key = 'sub'; Expect = 'C2'; Find = 'World ? static_cast<float>(World->GetTimeSeconds()) : -1.0f'; Repl = '-1.0f' }
    @{ Name = 'M24 CAPTURE-SRC drops cpdN'; Key = 'sub'; Expect = 'C2'; Find = ' pawnDistM=%.0f cpdN=%d"),'; Repl = ' pawnDistM=%.0f"),' }
    @{ Name = 'M25 CAPTURE-MAT drops midVec'; Key = 'sub'; Expect = 'C1'; Find = ' midVec=%d midScalar=%d"),'; Repl = ' midScalar=%d"),' }
    @{ Name = 'M26 CAPTURE-MAT midVec/midScalar init swapped'; Key = 'sub'; Expect = 'C1'; Find = 'RawMid ? RawMid->VectorParameterValues.Num() : -1, RawMid ? RawMid->ScalarParameterValues.Num() : -1 });'; Repl = 'RawMid ? RawMid->ScalarParameterValues.Num() : -1, RawMid ? RawMid->VectorParameterValues.Num() : -1 });' }
)
$mutantFails = @()
$caught = 0
foreach ($m in $mutants) {
    $mut = @{}
    foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = Invoke-CapmatChecks $mut
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ }
    else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "CAPMAT LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: CAPMAT lint -- C1..C7 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
