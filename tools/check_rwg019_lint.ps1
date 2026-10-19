# RWG 0.1.9 lint (2026-10-18): HeightApi accept LIST {0.1.7,705} {0.1.9,745}, member-displacement wildcards in SigField and
# SigKillboxTop, Warning-level refusals. Measurement: _team/test-logs/2026-10-18-rwg-0.1.9-export-diff.md.
# LOADS and PINS every source file the packet touched. Anchors are full statements or a parsed mask declaration; needles naming a
# forbidden literal are BUILT BY CONCATENATION so this file cannot match itself.
#   V1  Accept list: exactly the two measured {SemVer, build} pairs, in one declaration; the lookup compares BOTH SemVer and build;
#       the old single-constant pair is gone; bVersion is set true at one site only.
#   V2  Masks (parsed from the declaration, structural): SigField wildcards exactly bytes 3..6 (4); SigKillboxTop exactly bytes 3..6 and
#       12..15 (8); every non-wildcard byte equals the build-705 byte; the HeightApi file carries 16 wildcards in total (Outside 4 +
#       Field 4 + KillboxTop 8); both measured displacement values (705 and 745) sit in the comment beside each mask.
#   V3  Refusals are Warning: versionMismatch (in its refusal statement), signatureMismatch (the statement right before the pinned
#       return), and RWGSIGREFUSED at every signature-mismatch site (6 required via Check, 5 optional); SigOk records the first
#       differing pinned byte before returning false; no refusal format names a cause.
#   V4  The bind census prints accepted + matchedEntry; no field repeats in the bind census, the signature-check line, either refusal
#       tag or RWGVOIDBIND (the formats this packet wrote or edited).
#   V5  Marker rwg-capmat-1 (rwg-0-1-9-1 absent) present once, rwg-p4c-2 absent.
# Mutants run in-file. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    hapi  = 'NodeShuffle\Private\NodeShuffleMapModeHeightApi.cpp'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
}
$text = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = (Get-Content -Raw -Path $p) -replace "`r`n", "`n"
}
function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }
function Mask([string]$t, [string]$name) {
    # Structural: the ONE declaration line `static const int16 <name>[] = { ... };` -> its byte tokens + the trailing comment.
    $ms = [regex]::Matches($t, '(?m)^\s*static const int16 ' + [regex]::Escape($name) + '\[\] = \{([^}]*)\};(.*)$')
    if ($ms.Count -ne 1) { return $null }
    return @{ Tokens = @($ms[0].Groups[1].Value -split ',' | ForEach-Object { $_.Trim() }); Comment = $ms[0].Groups[2].Value }
}
function Fmt-Fields([string]$fmt) { return [regex]::Matches($fmt, '(?<![A-Za-z0-9])([A-Za-z][A-Za-z0-9]*)=') | ForEach-Object { $_.Groups[1].Value } }

function Invoke-019Checks($T) {
    $f = @()
    # V1
    if ((Count $T.hapi ('const FRwgAcceptedBuild RwgHeightApiAccepted[] = { { TEXT("0.1.7"), 705 }, { TEXT("0.1.9"), ' + '745 } };')) -ne 1) { $f += '(V1) the accept list must be exactly {0.1.7,705} {0.1.9,745} in one declaration' }
    if ((Count $T.hapi 'struct FRwgAcceptedBuild { const TCHAR* SemVer; int32 Build; };') -ne 1) { $f += '(V1) FRwgAcceptedBuild must pair a SemVer with a build' }
    if ((Count $T.hapi ('if (A.Build == E.Build && A.SemVer == E.SemVer) { bVersion = ' + 'true;')) -ne 1 -or (Count $T.hapi ('bVersion = ' + 'true')) -ne 1) { $f += '(V1) the lookup must require BOTH build and SemVer of one entry, at the only bVersion = true site' }
    if ((Count $T.hapi 'for (const FRwgAcceptedBuild& E : RwgHeightApiAccepted)') -lt 1 -or (Count $T.hapi 'bool bVersion = false;') -ne 1) { $f += '(V1) bVersion must start false and be set only by walking the accept list' }
    if ($T.hapi.Contains('RwgHeightApiAccepted' + 'SemVer') -or $T.hapi.Contains('RwgHeightApiAccepted' + 'Build')) { $f += '(V1) the old single accepted SemVer/build constants must be gone' }
    # V2
    $exp = @{
        SigField      = @{ Pinned = @('0x48','0x8B','0x81',$null,$null,$null,$null,'0x48','0x89','0x02'); Wild = 4; Notes = @('705 = 0x00000F40', '745 = 0x00001008') }
        SigKillboxTop = @{ Pinned = @('0x48','0x8B','0x81',$null,$null,$null,$null,'0x48','0x89','0x02','0x83','0xB9',$null,$null,$null,$null,'0x00','0x0F','0x9F','0xC0','0xC3'); Wild = 8; Notes = @('705 = 0xC58', '745 = 0xC60', '705 = 0xC48', '745 = 0xC50') }
    }
    foreach ($name in $exp.Keys) {
        $m = Mask $T.hapi $name; $e = $exp[$name]
        if (-not $m) { $f += "(V2) $name declaration not found exactly once"; continue }
        $wild = @($m.Tokens | Where-Object { $_ -eq '-1' }).Count
        if ($wild -ne $e.Wild) { $f += "(V2) $name must carry exactly $($e.Wild) wildcards (its disp32 member displacements), found $wild" }
        if ($m.Tokens.Count -ne $e.Pinned.Count) { $f += "(V2) $name must be $($e.Pinned.Count) bytes, found $($m.Tokens.Count)"; continue }
        for ($i = 0; $i -lt $e.Pinned.Count; $i++) {
            $want = $e.Pinned[$i]; $got = $m.Tokens[$i]
            if ($null -eq $want -and $got -ne '-1') { $f += "(V2) $name byte $i is a displacement byte and must be wildcarded (found $got)" }
            elseif ($null -ne $want -and $got -ne $want) { $f += "(V2) $name byte $i must stay pinned to the build-705 byte $want (found $got)" }
        }
        foreach ($n in $e.Notes) { if (-not $m.Comment.Contains($n)) { $f += "(V2) the comment beside $name must record the measured displacement '$n'" } }
    }
    $allWild = 0
    foreach ($d in [regex]::Matches($T.hapi, '(?m)^\s*static const int16 Sig[A-Za-z]+\[\] = \{([^}]*)\};')) { $allWild += @($d.Groups[1].Value -split ',' | Where-Object { $_.Trim() -eq '-1' }).Count }
    if ($allWild -ne 16) { $f += "(V2) the HeightApi masks must carry 16 wildcards in total (IsOutsideWorld 4 + GetField 4 + GetKillboxTop 8), found $allWild" }
    # V3
    if ((Count $T.hapi ('if (!bVersion) { A.Status = TEXT("versionMismatch"); UE_LOG(LogNodeShuffle, Warning, TEXT("RWGHEIGHTAPI-REFUSED: status=versionMismatch semVer=' + "'%s' build=%d accepted='%s' ")) -ne 1) { $f += '(V3) the versionMismatch refusal must log at Warning with the measured semVer/build and the accept list' }
    $sigWarn = 'if (bRequiredSigBad) { UE_LOG(LogNodeShuffle, Warning, TEXT("RWGHEIGHTAPI-REFUSED: status=signatureMismatch semVer=' + "'%s' build=%d matchedEntry='%s' accepted='%s' mismatched='%s' "
    $iW = $T.hapi.IndexOf($sigWarn); $iR = $T.hapi.IndexOf('if (bRequiredSigBad) { A.Status = TEXT("signatureMismatch"); ' + 'return; }')
    if ((Count $T.hapi $sigWarn) -ne 1 -or $iR -lt $iW -or $T.hapi.Substring($iW, $iR - $iW).Split("`n").Count -ne 2) { $f += '(V3) the signatureMismatch refusal must log at Warning on the statement right before the pinned return' }
    if ((Count $T.hapi ('UE_LOG(LogNodeShuffle, Warning, TEXT("RWGSIGREFUSED: export=%s required=%d firstDiffByte=%d ')) -ne 1) { $f += '(V3) RWGSIGREFUSED must log at Warning with export, required and firstDiffByte' }
    if ((Count $T.hapi ('WarnSigRefused(Name, ' + 'true);')) -ne 1) { $f += '(V3) every required signature mismatch (the Check lambda) must call WarnSigRefused' }
    $optSites = @(
        ('A.GMin = nullptr; SigBad += FString::Printf(TEXT(" GlobalMinSurfaceCm@+%d(optional,dropped)"), LastSigDiff); WarnSigRefused(TEXT("GlobalMinSurfaceCm"), ' + 'false); }'),
        ('A.GMax = nullptr; SigBad += FString::Printf(TEXT(" GlobalMaxSurfaceCm@+%d(optional,dropped)"), LastSigDiff); WarnSigRefused(TEXT("GlobalMaxSurfaceCm"), ' + 'false); }'),
        ('A.InSea = nullptr; A.SeaStatus = TEXT("signatureMismatch"); WarnSigRefused(TEXT("InSeaVolume"), ' + 'false); }'),
        ('A.VoidAt = nullptr; A.VoidStatus = TEXT("signatureMismatch"); WarnSigRefused(TEXT("VoidAt"), ' + 'false); }'),
        ('A.KillboxTop = nullptr; A.KillboxStatus = TEXT("signatureMismatch"); WarnSigRefused(TEXT("GetKillboxTop"), ' + 'false); }'))
    foreach ($s in $optSites) { if ((Count $T.hapi $s) -ne 1) { $f += "(V3) optional signature refusal must warn in its nulling branch: $($s.Substring(0, 40))..." } }
    if ((Count $T.hapi ('WarnSigRefused(TEXT(')) -ne 5) { $f += '(V3) exactly 5 optional WarnSigRefused sites expected' }
    if ((Count $T.hapi ('if (Sig[I] >= 0 && B[I] != static_cast<uint8>(Sig[I])) { LastSigDiff = I; return ' + 'false; }')) -ne 1) { $f += '(V3) SigOk must record the first differing pinned byte before returning false' }
    foreach ($lvl in @('Display', 'Log', 'Verbose')) {
        foreach ($tag in @('RWGHEIGHTAPI-REFUSED', 'RWGSIGREFUSED')) { if ($T.hapi.Contains("UE_LOG(LogNodeShuffle, $lvl, TEXT(`"" + $tag)) { $f += "(V3) $tag must never log at $lvl" } }
    }
    foreach ($m in [regex]::Matches($T.hapi, 'TEXT\("((RWGHEIGHTAPI-REFUSED|RWGSIGREFUSED):[^"]*)"\)')) {
        foreach ($w in @(('bec' + 'ause'), ('mo' + 'ved'), ('lay' + 'out'), ('rebu' + 'ild'))) { if ($m.Groups[1].Value -match ('\b' + $w + '\b')) { $f += "(V3) refusal format names a cause ('$w'): measured values only" } }
    }
    # V4
    if ((Count $T.hapi ("build=%d accepted='%s' matchedEntry='%s' requiredResolved=%d of 6")) -ne 1 -or (Count $T.hapi '*A.SemVer, A.Build, *RwgAcceptedList(), *A.Matched,') -ne 1) { $f += '(V4) the bind census must print accepted + matchedEntry with matching args' }
    # Scope: the formats this packet wrote or edited (bind census, signature check, the two refusal tags, RWGVOIDBIND).
    $v4n = 0
    foreach ($m in [regex]::Matches($T.hapi, 'TEXT\("((RWGHEIGHTAPI: bind |RWGHEIGHTAPI: signature check |RWGHEIGHTAPI-REFUSED:|RWGSIGREFUSED:|RWGVOIDBIND:)[^"]*)"\)')) {
        $v4n++
        $dup = (Fmt-Fields $m.Groups[1].Value) | Group-Object | Where-Object { $_.Count -gt 1 }
        if ($dup) { $f += "(V4) repeated field(s) $(($dup | ForEach-Object { $_.Name }) -join ',') in '$($m.Groups[1].Value.Substring(0, [Math]::Min(40, $m.Groups[1].Value.Length)))'" }
    }
    if ($v4n -ne 7) { $f += "(V4) expected 7 scoped formats (bind census, signature check, 3 refusals, RWGSIGREFUSED, RWGVOIDBIND), found $v4n -- the duplicate scan would be vacuous" }
    # V5
    if ((Count $T.nsmod ('LOADED (2026-10-19-rwg-' + 'capmat-1)')) -ne 1 -or (Count $T.nsmod ('2026-10-18-rwg-' + '0-1-9-1')) -ne 0 -or (Count $T.nsmod ('2026-10-18-rwg-' + 'p4c-2')) -ne 0) { $f += '(V5) marker must be rwg-capmat-1 (rwg-0-1-9-1 absent) with rwg-p4c-2 absent' }
    return $f
}

$fails = Invoke-019Checks $text
$mutants = @(
    @{ Name = 'M1 SigField displacement byte 3 re-pinned'; Key = 'hapi'; Expect = 'V2'; Find = '0x48,0x8B,0x81,-1,-1,-1,-1,0x48,0x89,0x02 };'; Repl = '0x48,0x8B,0x81,0x40,-1,-1,-1,0x48,0x89,0x02 };' }
    @{ Name = 'M2 SigKillboxTop displacement byte 12 re-pinned'; Key = 'hapi'; Expect = 'V2'; Find = '0x83,0xB9,-1,-1,-1,-1,0x00,0x0F'; Repl = '0x83,0xB9,0x48,-1,-1,-1,0x00,0x0F' }
    @{ Name = 'M3 0.1.9 dropped from the accept list'; Key = 'hapi'; Expect = 'V1'; Find = ', { TEXT("0.1.9"), 745 } };'; Repl = ' };' }
    @{ Name = 'M4 versionMismatch refusal downgraded to Display'; Key = 'hapi'; Expect = 'V3'; Find = 'TEXT("versionMismatch"); UE_LOG(LogNodeShuffle, Warning,'; Repl = 'TEXT("versionMismatch"); UE_LOG(LogNodeShuffle, Display,' }
    @{ Name = 'M5 signatureMismatch refusal downgraded to Display'; Key = 'hapi'; Expect = 'V3'; Find = 'if (bRequiredSigBad) { UE_LOG(LogNodeShuffle, Warning,'; Repl = 'if (bRequiredSigBad) { UE_LOG(LogNodeShuffle, Display,' }
    @{ Name = 'M6 RWGSIGREFUSED downgraded to Display'; Key = 'hapi'; Expect = 'V3'; Find = 'UE_LOG(LogNodeShuffle, Warning, TEXT("RWGSIGREFUSED:'; Repl = 'UE_LOG(LogNodeShuffle, Display, TEXT("RWGSIGREFUSED:' }
    @{ Name = 'M7 SigField ModRM byte wildcarded'; Key = 'hapi'; Expect = 'V2'; Find = '0x48,0x8B,0x81,-1,-1,-1,-1,0x48,0x89,0x02 };'; Repl = '0x48,0x8B,-1,-1,-1,-1,-1,0x48,0x89,0x02 };' }
    @{ Name = 'M8 lookup ignores the build number'; Key = 'hapi'; Expect = 'V1'; Find = 'if (A.Build == E.Build && A.SemVer == E.SemVer)'; Repl = 'if (A.SemVer == E.SemVer)' }
    @{ Name = 'M9 GetKillboxTop refusal warning removed'; Key = 'hapi'; Expect = 'V3'; Find = ' WarnSigRefused(TEXT("GetKillboxTop"), false);'; Repl = '' }
    @{ Name = 'M10 SigKillboxTop cmp imm8 wildcarded'; Key = 'hapi'; Expect = 'V2'; Find = '0x83,0xB9,-1,-1,-1,-1,0x00,0x0F'; Repl = '0x83,0xB9,-1,-1,-1,-1,-1,0x0F' }
    @{ Name = 'M11 marker reverted'; Key = 'nsmod'; Expect = 'V5'; Find = ('2026-10-19-rwg-' + 'capmat-1'); Repl = ('2026-10-18-rwg-' + '0-1-9-1') }
    @{ Name = 'M13 census repeats the build field'; Key = 'hapi'; Expect = 'V4'; Find = "matchedEntry='%s' requiredResolved"; Repl = "matchedEntry='%s' build=%d requiredResolved" }
    @{ Name = 'M12 745 displacement note dropped from SigField'; Key = 'hapi'; Expect = 'V2'; Find = ', 745 = 0x00001008'; Repl = '' }
)
$mutantFails = @()
$caught = 0
foreach ($m in $mutants) {
    $mut = @{}
    foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = Invoke-019Checks $mut
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ }
    else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "RWG 0.1.9 LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: RWG 0.1.9 lint -- V1..V5 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
