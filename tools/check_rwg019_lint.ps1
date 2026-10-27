# RWG 0.1.9 / 0.1.12 gate lint (2026-10-18, extended 2026-10-26): HeightApi accept LIST {0.1.7,705} {0.1.9,745} {0.1.12,802},
# member-offset wildcards in EVERY HeightApi mask that carries one (SigField, SigSpliced, SigOutside disp8, SigGMin, SigGMax,
# SigVoidAt, SigKillboxTop), Warning-level refusals, the UNLISTED-VERSION TRIAL (strict full 16-function check) and the IN-GAME
# NOTICE on every refusal / unlisted-verified run. Measurements: _team/test-logs/2026-10-18-rwg-0.1.9-export-diff.md,
# _team/test-logs/2026-10-26-rwg-0.1.12-export-diff.md.
# LOADS and PINS every source file the packets touched. Anchors are full statements or a parsed mask declaration; needles naming a
# forbidden literal are BUILT BY CONCATENATION so this file cannot match itself.
#   V1  Accept list: exactly the three measured {SemVer, build} pairs, in one declaration; the lookup compares BOTH SemVer and build;
#       the old single-constant pair is gone; bVersion is set true at one site only.
#   V2  Masks (parsed from the declaration, structural): each listed mask wildcards exactly its member-offset bytes and keeps every
#       other byte pinned to the build-705 byte; the HeightApi file carries 27 wildcards in total (Outside 4+1, Field 4, Spliced 4,
#       GMin 1, GMax 1, VoidAt 4, KillboxTop 8); the measured 705/745/802 values sit in the comment beside each mask.
#   V3  Refusals are Warning: exportMissing, signatureMismatch (the statement right before the pinned return), the unlisted verdict,
#       and RWGSIGREFUSED at every signature-mismatch site (6 required via Check, 5 optional); SigOk records the first differing
#       pinned byte before returning false; no refusal format names a cause. The old versionMismatch refusal is GONE.
#   V4  The bind census prints accepted + matchedEntry; no field repeats in any format this lane wrote or edited (bind census,
#       signature check, every RWGHEIGHTAPI-REFUSED / -UNLISTED, RWGSIGREFUSED, RWGVOIDBIND, RWGGATE-CAVESIG, RWGGATENOTICE).
#   V5  Marker rwg-0-1-12-1 present once; rwg-steep-2 and the earlier markers absent; no marker string in the new notice files.
#   V6  Unlisted trial: `if (!bVersion)` no longer returns (it logs RWGHEIGHTAPI-UNLISTED at Warning and sets unlistedTrial);
#       WarnSigRefused counts optional refusals; the verdict block sits between RWGVOIDBIND-OFF and `A.Hmod = H`, refuses on
#       OptionalSigBad > 0 OR CaveSigBad > 0 OR any of the 16 exports missing, with a return, and only then logs
#       signatureVerifiedUnlisted; the final status is the
#       ternary on bUnlisted; the cave half's five masks + five names are token-identical to NodeShuffleMapModeCave.cpp's.
#   V7  Notice: QueueRefusalNotice is called in all three refusal statements (exportMissing, required signatureMismatch, unlisted
#       verdict) and QueueUnlistedVerifiedNotice in the verified branch; TickNotice runs in RwgLogHeightApi BEFORE the callsAllowed
#       guard; the drain posts through FNodeShuffleModule::PostChatNotice with tag RWGGATE, holds for a player pawn, one further
#       tick, bounded by MaxNoticeAttempts; it posts once per world (PostedWorld); the posted/abandoned lines say what they measured.
# Mutants run in-file. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    hapi  = 'NodeShuffle\Private\NodeShuffleMapModeHeightApi.cpp'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
    gate  = 'NodeShuffle\Private\NodeShuffleRwgGateNotice.cpp'
    gateh = 'NodeShuffle\Private\NodeShuffleRwgGateNotice.h'
    cave  = 'NodeShuffle\Private\NodeShuffleMapModeCave.cpp'
}
$text = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) { Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist"; exit 2 }
    $text[$k] = (Get-Content -Raw -Path $p) -replace "`r`n", "`n"
}
function Count([string]$t, [string]$n) { return ([regex]::Matches($t, [regex]::Escape($n))).Count }
function Mask([string]$t, [string]$name) {
    # Structural: the ONE declaration line `[static] const int16 <name>[] = { ... };` -> its byte tokens + the trailing comment.
    $ms = [regex]::Matches($t, '(?m)^\s*(?:static )?const int16 ' + [regex]::Escape($name) + '\[\] = \{([^}]*)\};(.*)$')
    if ($ms.Count -ne 1) { return $null }
    return @{ Tokens = @($ms[0].Groups[1].Value -split ',' | ForEach-Object { $_.Trim() }); Comment = $ms[0].Groups[2].Value }
}
function Fmt-Fields([string]$fmt) { return [regex]::Matches($fmt, '(?<![A-Za-z0-9])([A-Za-z][A-Za-z0-9]*)=') | ForEach-Object { $_.Groups[1].Value } }
function Lines-Between([string]$t, [int]$a, [int]$b) { if ($a -lt 0 -or $b -lt $a) { return -1 }; return $t.Substring($a, $b - $a).Split("`n").Count }

function Invoke-019Checks($T) {
    $f = @()
    # V1
    if ((Count $T.hapi ('const FRwgAcceptedBuild RwgHeightApiAccepted[] = { { TEXT("0.1.7"), 705 }, { TEXT("0.1.9"), 745 }, { TEXT("0.1.12"), ' + '802 } };')) -ne 1) { $f += '(V1) the accept list must be exactly {0.1.7,705} {0.1.9,745} {0.1.12,802} in one declaration' }
    if ((Count $T.hapi 'struct FRwgAcceptedBuild { const TCHAR* SemVer; int32 Build; };') -ne 1) { $f += '(V1) FRwgAcceptedBuild must pair a SemVer with a build' }
    if ((Count $T.hapi ('if (A.Build == E.Build && A.SemVer == E.SemVer) { bVersion = ' + 'true;')) -ne 1 -or (Count $T.hapi ('bVersion = ' + 'true')) -ne 1) { $f += '(V1) the lookup must require BOTH build and SemVer of one entry, at the only bVersion = true site' }
    if ((Count $T.hapi 'for (const FRwgAcceptedBuild& E : RwgHeightApiAccepted)') -lt 1 -or (Count $T.hapi 'bool bVersion = false;') -ne 1) { $f += '(V1) bVersion must start false and be set only by walking the accept list' }
    if ($T.hapi.Contains('RwgHeightApiAccepted' + 'SemVer') -or $T.hapi.Contains('RwgHeightApiAccepted' + 'Build')) { $f += '(V1) the old single accepted SemVer/build constants must be gone' }
    # V2
    $exp = @{
        SigField      = @{ Pinned = @('0x48','0x8B','0x81',$null,$null,$null,$null,'0x48','0x89','0x02'); Wild = 4; Notes = @('705 = 0x00000F40', '745 = 0x00001008', '802 = 0x000010F8') }
        SigSpliced    = @{ Pinned = @('0x48','0x8B','0x81',$null,$null,$null,$null,'0xC3'); Wild = 4; Notes = @('705 = 0x90', '745 = 0x90', '802 = 0x90') }
        SigOutside    = @{ Pinned = @('0xF2','0x0F','0x10','0x1D',$null,$null,$null,$null,'0xF2','0x0F','0x10','0x41',$null,'0x0F','0x54','0xCB'); Wild = 5; Notes = @('705 = 0x08', '745 = 0x08', '802 = 0x08') }
        SigGMin       = @{ Pinned = @('0xF2','0x0F','0x10','0x41',$null,'0xC3'); Wild = 1; Notes = @('705 = 0x18', '745 = 0x18', '802 = 0x18') }
        SigGMax       = @{ Pinned = @('0xF2','0x0F','0x10','0x41',$null,'0xC3'); Wild = 1; Notes = @('705 = 0x20', '745 = 0x20', '802 = 0x20') }
        SigVoidAt     = @{ Pinned = @('0x40','0x53','0x48','0x83','0xEC','0x30','0x48','0x83','0xB9',$null,$null,$null,$null,'0x00','0x0F','0x28','0xE2','0x0F','0x28','0xC1','0x48','0x8B','0xD9','0x0F','0x84'); Wild = 4; Notes = @('705 = 0x2E8', '745 = 0x2E8', '802 = 0x2F0') }
        SigKillboxTop = @{ Pinned = @('0x48','0x8B','0x81',$null,$null,$null,$null,'0x48','0x89','0x02','0x83','0xB9',$null,$null,$null,$null,'0x00','0x0F','0x9F','0xC0','0xC3'); Wild = 8; Notes = @('705 = 0xC58', '745 = 0xC60', '802 = 0xD50', '705 = 0xC48', '745 = 0xC50', '802 = 0xD40') }
    }
    foreach ($name in $exp.Keys) {
        $m = Mask $T.hapi $name; $e = $exp[$name]
        if (-not $m) { $f += "(V2) $name declaration not found exactly once"; continue }
        $wild = @($m.Tokens | Where-Object { $_ -eq '-1' }).Count
        if ($wild -ne $e.Wild) { $f += "(V2) $name must carry exactly $($e.Wild) wildcards (its member-offset / RIP bytes), found $wild" }
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
    if ($allWild -ne 27) { $f += "(V2) the HeightApi masks must carry 27 wildcards in total (Outside 5 + Field 4 + Spliced 4 + GMin 1 + GMax 1 + VoidAt 4 + KillboxTop 8), found $allWild" }
    # V3
    if ($T.hapi.Contains('TEXT("version' + 'Mismatch")')) { $f += '(V3) the versionMismatch label refusal must be gone: an unlisted label is tried, not refused' }
    if ((Count $T.hapi ('if (A.Required != 6) { A.Status = TEXT("exportMissing"); UE_LOG(LogNodeShuffle, Warning, TEXT("RWGHEIGHTAPI-REFUSED: status=exportMissing semVer=' + "'%s' build=%d matchedEntry='%s' requiredResolved=%d of 6 missing='%s' callsAllowed=0")) -ne 1) { $f += '(V3) the exportMissing refusal must log at Warning with the measured values' }
    $sigWarn = 'if (bRequiredSigBad) { UE_LOG(LogNodeShuffle, Warning, TEXT("RWGHEIGHTAPI-REFUSED: status=signatureMismatch semVer=' + "'%s' build=%d matchedEntry='%s' accepted='%s' mismatched='%s' "
    $iW = $T.hapi.IndexOf($sigWarn); $iR = $T.hapi.IndexOf('if (bRequiredSigBad) { A.Status = TEXT("signatureMismatch"); ' + 'return; }')
    if ((Count $T.hapi $sigWarn) -ne 1 -or (Lines-Between $T.hapi $iW $iR) -ne 2) { $f += '(V3) the signatureMismatch refusal must log at Warning on the statement right before the pinned return' }
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
        foreach ($tag in @('RWGHEIGHTAPI-REFUSED', 'RWGSIGREFUSED', 'RWGHEIGHTAPI-UNLISTED')) { if ($T.hapi.Contains("UE_LOG(LogNodeShuffle, $lvl, TEXT(`"" + $tag)) { $f += "(V3) $tag must never log at $lvl" } }
    }
    foreach ($m in [regex]::Matches($T.hapi + $T.gate, 'TEXT\("((RWGHEIGHTAPI-REFUSED|RWGSIGREFUSED|RWGHEIGHTAPI-UNLISTED|RWGGATENOTICE|RWGGATE-CAVESIG):[^"]*)"\)')) {
        foreach ($w in @(('bec' + 'ause'), ('mo' + 'ved'), ('lay' + 'out'), ('rebu' + 'ild'), ('upd' + 'ated'))) { if ($m.Groups[1].Value -match ('\b' + $w + '\b')) { $f += "(V3) refusal/notice format names a cause ('$w'): measured values only" } }
    }
    # V4
    if ((Count $T.hapi ("build=%d accepted='%s' matchedEntry='%s' requiredResolved=%d of 6")) -ne 1 -or (Count $T.hapi '*A.SemVer, A.Build, *RwgAcceptedList(), *A.Matched,') -ne 1) { $f += '(V4) the bind census must print accepted + matchedEntry with matching args' }
    # Scope: every format this lane wrote or edited. Formats are TEXT("...") runs that START with the tag (multi-TEXT bodies are not used here).
    $v4n = 0
    foreach ($m in [regex]::Matches($T.hapi + $T.gate, 'TEXT\("((RWGHEIGHTAPI: bind |RWGHEIGHTAPI: signature check |RWGHEIGHTAPI-REFUSED:|RWGHEIGHTAPI-UNLISTED:|RWGSIGREFUSED:|RWGVOIDBIND:|RWGGATE-CAVESIG:|RWGGATENOTICE:)[^"]*)"\)')) {
        $v4n++
        $dup = (Fmt-Fields $m.Groups[1].Value) | Group-Object | Where-Object { $_.Count -gt 1 }
        if ($dup) { $f += "(V4) repeated field(s) $(($dup | ForEach-Object { $_.Name }) -join ',') in '$($m.Groups[1].Value.Substring(0, [Math]::Min(40, $m.Groups[1].Value.Length)))'" }
    }
    if ($v4n -ne 15) { $f += "(V4) expected 15 scoped formats (bind census, signature check, 3 REFUSED, 2 UNLISTED, RWGSIGREFUSED, RWGVOIDBIND, CAVESIG, 5 GATENOTICE), found $v4n -- the duplicate scan would be vacuous" }
    # V5
    if ((Count $T.nsmod ('LOADED (2026-10-26-rwg-' + '0-1-12-1)')) -ne 1 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'steep-2)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'steep-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'deal-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'density-2)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'rockcache-2')) -ne 0 -or (Count $T.nsmod ('2026-10-18-rwg-' + 'p4c-2')) -ne 0) { $f += '(V5) marker must be rwg-0-1-12-1 with rwg-steep-2 and the earlier markers absent' }
    if ($T.gate.Contains('LOADED' + ' (') -or $T.gateh.Contains('LOADED' + ' (')) { $f += '(V5) no build-marker string in the notice files' }
    # V6 -- the unlisted trial
    $tb = $T.hapi.IndexOf('void TryBind(FRwgHeightApi& A)')
    if ($tb -lt 0) { $f += '(V6) TryBind not found' } else {
        $b = $T.hapi.Substring($tb)
        $unl = 'if (!bVersion) { A.Status = TEXT("unlistedTrial"); UE_LOG(LogNodeShuffle, Warning, TEXT("RWGHEIGHTAPI-UNLISTED: status=unlistedTrial semVer=' + "'%s' build=%d accepted='%s' uplugin='%s'"
        $iU = $b.IndexOf($unl)
        if ($iU -lt 0) { $f += '(V6) the unlisted label must log RWGHEIGHTAPI-UNLISTED at Warning and set unlistedTrial, not refuse' }
        elseif ($b.Substring($iU, $b.IndexOf("`n", $iU) - $iU) -match ('ret' + 'urn;')) { $f += '(V6) the unlisted label statement must not return: the trial runs the signature check' }
        if ((Count $b 'const bool bUnlisted = !bVersion;') -ne 1) { $f += '(V6) bUnlisted must be the negation of the accept-list walk' }
        if ((Count $b 'const bool bAllSixteenResolved = A.GMin && A.GMax && A.InSea && A.VoidAt && A.KillboxTop && CaveMissing.IsEmpty();') -ne 1) { $f += '(V6) the trial must require all five optional exports and all five cave exports resolved' }
        if ((Count $b 'NameOpt(A.VoidStatus, TEXT("VoidAt"));') -ne 1 -or (Count $b 'NameOpt(A.SeaStatus, TEXT("InSeaVolume"));') -ne 1 -or (Count $b 'NameOpt(A.KillboxStatus, TEXT("GetKillboxTop"));') -ne 1) { $f += '(V6) the unlisted refusal must name the raw-resolved optionals it refused on' }
        if ((Count $b ('if (!bRequired) { ++' + 'OptionalSigBad; }')) -ne 1 -or (Count $b 'int32 OptionalSigBad = 0;') -ne 1) { $f += '(V6) WarnSigRefused must count optional refusals into OptionalSigBad' }
        $iOff = $b.IndexOf('TEXT("RWGVOIDBIND-OFF:'); $iV = $b.IndexOf('if (bUnlisted)'); $iC = $b.IndexOf('NodeShuffleRwgGate::CheckCaveSignatures(H, CaveMissing, CaveBad);')
        $iRef = $b.IndexOf('if (OptionalSigBad > 0 || CaveSigBad > 0 || !bAllSixteenResolved)'); $iVer = $b.IndexOf('TEXT("RWGHEIGHTAPI-UNLISTED: status=signatureVerifiedUnlisted '); $iH = $b.IndexOf('A.Hmod = H;')
        if ($iOff -lt 0 -or $iV -lt $iOff -or $iC -lt $iV -or $iRef -lt $iC -or $iVer -lt $iRef -or $iH -lt $iVer) { $f += '(V6) the unlisted verdict (cave check, strict refusal, then the verified line) must sit between RWGVOIDBIND-OFF and A.Hmod = H' }
        if ($iRef -ge 0 -and $iVer -gt $iRef) {
            $blk = $b.Substring($iRef, $iVer - $iRef)
            if (-not ($blk.Contains('A.Status = (OptionalSigBad > 0 || CaveSigBad > 0) ? TEXT("signatureMismatch") : TEXT("exportMissing");') -and $blk.Contains('NodeShuffleRwgGate::QueueRefusalNotice(A.SemVer, A.Build, A.Status,') -and $blk -match ('\n\s*ret' + 'urn;\n'))) { $f += '(V6) the strict unlisted refusal must set signatureMismatch or exportMissing, queue the notice and return' }
        }
        if ((Count $b ('A.Status = bUnlisted ? TEXT("signatureVerifiedUnlisted") : TEXT("bo' + 'und");')) -ne 1) { $f += '(V6) the usable status must be signatureVerifiedUnlisted on an unlisted build and bound otherwise' }
        if ((Count $b 'A.bUsable = true;') -ne 1) { $f += '(V6) A.bUsable must be set true at exactly one site' }
    }
    foreach ($name in @('SigCaves', 'SigNum', 'SigAnyNear', 'SigFloor', 'SigInside')) {
        $a = Mask $T.cave $name; $g = Mask $T.gate $name
        if (-not $a -or -not $g) { $f += "(V6) $name must be declared exactly once in both the cave file and the gate notice file"; continue }
        if (($a.Tokens -join ',') -ne ($g.Tokens -join ',')) { $f += "(V6) $name differs between NodeShuffleMapModeCave.cpp and NodeShuffleRwgGateNotice.cpp: the trial's cave half has drifted" }
    }
    foreach ($sym in @('SymCaves', 'SymNum', 'SymAnyNear', 'SymFloor', 'SymInside')) {
        $ra = [regex]::Match($T.cave, 'const char\* const ' + $sym + ' = "([^"]*)";'); $rg = [regex]::Match($T.gate, 'const char\* const ' + $sym + ' = "([^"]*)";')
        if (-not $ra.Success -or -not $rg.Success -or $ra.Groups[1].Value -ne $rg.Groups[1].Value) { $f += "(V6) decorated name $sym must be identical in the cave file and the gate notice file" }
    }
    if ((Count $T.gate 'Check(SymCaves, SigCaves, UE_ARRAY_COUNT(SigCaves), TEXT("CarvedCaves"));') -ne 1 -or (Count $T.gate 'Check(SymInside, SigInside, UE_ARRAY_COUNT(SigInside), TEXT("IsInsideAt"));') -ne 1 -or ([regex]::Matches($T.gate, '(?m)^\s*Check\(Sym[A-Za-z]+, Sig[A-Za-z]+, UE_ARRAY_COUNT\(Sig[A-Za-z]+\), TEXT\("[A-Za-z]+"\)\);')).Count -ne 5) { $f += '(V6) CheckCaveSignatures must check all five cave exports' }
    if ((Count $T.gate ('if (Sig[I] >= 0 && B[I] != static_cast<uint8>(Sig[I]))')) -ne 1 -or (Count $T.gate ('++' + 'Bad;')) -ne 1) { $f += '(V6) the cave check must compare pinned bytes only and count each mismatching export once' }
    # V7 -- the in-game notice
    if ((Count $T.hapi 'NodeShuffleRwgGate::QueueRefusalNotice(') -ne 3) { $f += '(V7) every refusal statement (exportMissing, required signatureMismatch, unlisted verdict) must queue the refusal notice: expected 3 call sites' }
    if (-not ($T.hapi -match ('status=exportMissing[^\n]*NodeShuffleRwgGate::QueueRefusalNotice\(A\.SemVer, A\.Build, A\.Status, [^\n]*ret' + 'urn; \}'))) { $f += '(V7) the exportMissing refusal statement must queue the notice before its return' }
    if (-not ($T.hapi -match ('status=signatureMismatch semVer=[^\n]*NodeShuffleRwgGate::QueueRefusalNotice\(A\.SemVer, A\.Build, TEXT\("signatureMismatch"\),'))) { $f += '(V7) the required signatureMismatch refusal statement must queue the notice' }
    if ((Count $T.hapi 'NodeShuffleRwgGate::QueueUnlistedVerifiedNotice(A.SemVer, A.Build, RwgAcceptedList());') -ne 1) { $f += '(V7) the verified-unlisted branch must queue its notice exactly once' }
    $lh = $T.hapi.IndexOf('void ANodeShuffleSubsystem::RwgLogHeightApi(bool bFull)')
    if ($lh -lt 0) { $f += '(V7) RwgLogHeightApi not found' } else {
        $lb = $T.hapi.Substring($lh)
        $iT = $lb.IndexOf('NodeShuffleRwgGate::TickNotice(GetWorld());'); $iG = $lb.IndexOf('if (!A.bUsable || !Pawn)')
        if ($iT -lt 0 -or $iG -lt 0 -or $iT -gt $iG) { $f += '(V7) TickNotice must run in RwgLogHeightApi before the callsAllowed guard, so a refused bind still posts' }
    }
    if ((Count $T.gate 'if (FNodeShuffleModule::PostChatNotice(World, N.Body, TEXT("RWGGATE")))') -ne 1) { $f += '(V7) the drain must post through the one shared chat emitter with the RWGGATE tag' }
    if ((Count $T.gate ('if (!PC || !Pawn)')) -ne 1 -or (Count $T.gate 'const APawn* Pawn = PC ? PC->GetPawn() : nullptr;') -ne 1) { $f += '(V7) the drain must hold until a player controller with a pawn exists' }
    if ((Count $T.gate ('if (N.GateTicks < 0)')) -ne 1 -or (Count $T.gate 'N.GateTicks = 0;') -ne 1) { $f += '(V7) the drain must hold one further tick after the pawn appears' }
    if ((Count $T.gate 'if (N.PostedWorld.Get() == World) { return; }') -ne 1 -or (Count $T.gate 'N.PostedWorld = World;') -ne 2) { $f += '(V7) the drain must post once per world: PostedWorld set on delivery AND on abandonment, tested first' }
    if ((Count $T.gate 'if (N.Attempts >= MaxNoticeAttempts)') -ne 1 -or (Count $T.gate 'constexpr int32 MaxNoticeAttempts = 8;') -ne 1 -or (Count $T.gate ('++' + 'N.Attempts;')) -ne 1) { $f += '(V7) retries must be bounded by MaxNoticeAttempts = 8, counted once per post attempt' }
    if ((Count $T.gate 'if (!N.bQueued || !World) { return; }') -ne 1) { $f += '(V7) the drain must return on the first line when nothing is queued' }
    $posted = [regex]::Match($T.gate, 'TEXT\("RWGGATENOTICE: posted[^"]*"\)').Value
    foreach ($w in @(('is vis' + 'ible'), ('sho' + 'wn on screen'))) { if ($posted -match $w) { $f += "(V7) the posted line must not claim on-screen visibility ('$w'): delivery to chat history is what the emitter measures" } }
    if (-not $posted.Contains('on-screen visibility is not measured')) { $f += '(V7) the posted line must state that on-screen visibility is not measured' }
    foreach ($body in @('NODE SHUFFLE - Random World Generator %s (build %d) not verified', 'NODE SHUFFLE - Random World Generator %s (build %d) is not on the tested list')) { if ((Count $T.gate $body) -ne 1) { $f += "(V7) notice body heading missing: '$body'" } }
    return $f
}

$fails = Invoke-019Checks $text
$mutants = @(
    @{ Name = 'M1 SigField displacement byte 3 re-pinned'; Key = 'hapi'; Expect = 'V2'; Find = '0x48,0x8B,0x81,-1,-1,-1,-1,0x48,0x89,0x02 };'; Repl = '0x48,0x8B,0x81,0x40,-1,-1,-1,0x48,0x89,0x02 };' }
    @{ Name = 'M2 SigKillboxTop displacement byte 12 re-pinned'; Key = 'hapi'; Expect = 'V2'; Find = '0x83,0xB9,-1,-1,-1,-1,0x00,0x0F'; Repl = '0x83,0xB9,0x48,-1,-1,-1,0x00,0x0F' }
    @{ Name = 'M3 0.1.9 dropped from the accept list'; Key = 'hapi'; Expect = 'V1'; Find = ', { TEXT("0.1.9"), 745 }'; Repl = '' }
    @{ Name = 'M4 unlisted label refuses again (return re-added)'; Key = 'hapi'; Expect = 'V6'; Find = "the full 16-function signature check decides callsAllowed`"), *A.SemVer, A.Build, *RwgAcceptedList(), *A.Uplugin); }"; Repl = "the full 16-function signature check decides callsAllowed`"), *A.SemVer, A.Build, *RwgAcceptedList(), *A.Uplugin); return; }" }
    @{ Name = 'M5 signatureMismatch refusal downgraded to Display'; Key = 'hapi'; Expect = 'V3'; Find = 'if (bRequiredSigBad) { UE_LOG(LogNodeShuffle, Warning,'; Repl = 'if (bRequiredSigBad) { UE_LOG(LogNodeShuffle, Display,' }
    @{ Name = 'M6 RWGSIGREFUSED downgraded to Display'; Key = 'hapi'; Expect = 'V3'; Find = 'UE_LOG(LogNodeShuffle, Warning, TEXT("RWGSIGREFUSED:'; Repl = 'UE_LOG(LogNodeShuffle, Display, TEXT("RWGSIGREFUSED:' }
    @{ Name = 'M7 SigField ModRM byte wildcarded'; Key = 'hapi'; Expect = 'V2'; Find = '0x48,0x8B,0x81,-1,-1,-1,-1,0x48,0x89,0x02 };'; Repl = '0x48,0x8B,-1,-1,-1,-1,-1,0x48,0x89,0x02 };' }
    @{ Name = 'M8 lookup ignores the build number'; Key = 'hapi'; Expect = 'V1'; Find = 'if (A.Build == E.Build && A.SemVer == E.SemVer)'; Repl = 'if (A.SemVer == E.SemVer)' }
    @{ Name = 'M9 GetKillboxTop refusal warning removed'; Key = 'hapi'; Expect = 'V3'; Find = ' WarnSigRefused(TEXT("GetKillboxTop"), false);'; Repl = '' }
    @{ Name = 'M10 SigKillboxTop cmp imm8 wildcarded'; Key = 'hapi'; Expect = 'V2'; Find = '0x83,0xB9,-1,-1,-1,-1,0x00,0x0F'; Repl = '0x83,0xB9,-1,-1,-1,-1,-1,0x0F' }
    @{ Name = 'M11 marker reverted'; Key = 'nsmod'; Expect = 'V5'; Find = ('2026-10-26-rwg-' + '0-1-12-1'); Repl = ('2026-10-19-rwg-' + 'steep-2') }
    @{ Name = 'M12 745 displacement note dropped from SigField'; Key = 'hapi'; Expect = 'V2'; Find = ', 745 = 0x00001008'; Repl = '' }
    @{ Name = 'M13 census repeats the build field'; Key = 'hapi'; Expect = 'V4'; Find = "matchedEntry='%s' requiredResolved"; Repl = "matchedEntry='%s' build=%d requiredResolved" }
    @{ Name = 'M14 SigVoidAt displacement byte 9 re-pinned (the 0.1.12 move)'; Key = 'hapi'; Expect = 'V2'; Find = '0x48,0x83,0xB9,-1,-1,-1,-1,0x00,0x0F,0x28,0xE2'; Repl = '0x48,0x83,0xB9,0xE8,-1,-1,-1,0x00,0x0F,0x28,0xE2' }
    @{ Name = 'M15 SigVoidAt cmp imm8 wildcarded'; Key = 'hapi'; Expect = 'V2'; Find = '0x48,0x83,0xB9,-1,-1,-1,-1,0x00,0x0F,0x28,0xE2'; Repl = '0x48,0x83,0xB9,-1,-1,-1,-1,-1,0x0F,0x28,0xE2' }
    @{ Name = 'M16 802 displacement note dropped from SigVoidAt'; Key = 'hapi'; Expect = 'V2'; Find = ', 802 = 0x2F0'; Repl = '' }
    @{ Name = 'M17 0.1.12 dropped from the accept list'; Key = 'hapi'; Expect = 'V1'; Find = ', { TEXT("0.1.12"), 802 }'; Repl = '' }
    @{ Name = 'M18 SigSpliced displacement re-pinned'; Key = 'hapi'; Expect = 'V2'; Find = '0x48,0x8B,0x81,-1,-1,-1,-1,0xC3 };'; Repl = '0x48,0x8B,0x81,0x90,0x00,0x00,0x00,0xC3 };' }
    @{ Name = 'M19 SigGMin disp8 re-pinned'; Key = 'hapi'; Expect = 'V2'; Find = 'SigGMin[] = { 0xF2,0x0F,0x10,0x41,-1,0xC3 };'; Repl = 'SigGMin[] = { 0xF2,0x0F,0x10,0x41,0x18,0xC3 };' }
    @{ Name = 'M20 SigOutside ModRM wildcarded with its disp8'; Key = 'hapi'; Expect = 'V2'; Find = '0xF2,0x0F,0x10,0x41,-1,0x0F,0x54,0xCB };'; Repl = '0xF2,0x0F,0x10,-1,-1,0x0F,0x54,0xCB };' }
    @{ Name = 'M21 unlisted trial Warning downgraded to Display'; Key = 'hapi'; Expect = 'V3'; Find = 'UE_LOG(LogNodeShuffle, Warning, TEXT("RWGHEIGHTAPI-UNLISTED: status=unlistedTrial'; Repl = 'UE_LOG(LogNodeShuffle, Display, TEXT("RWGHEIGHTAPI-UNLISTED: status=unlistedTrial' }
    @{ Name = 'M22 optional refusals no longer counted'; Key = 'hapi'; Expect = 'V6'; Find = 'if (!bRequired) { ++OptionalSigBad; }'; Repl = '' }
    @{ Name = 'M23 strict refusal softened to AND'; Key = 'hapi'; Expect = 'V6'; Find = 'if (OptionalSigBad > 0 || CaveSigBad > 0 || !bAllSixteenResolved)'; Repl = 'if (OptionalSigBad > 0 && CaveSigBad > 0 && !bAllSixteenResolved)' }
    @{ Name = 'M38 missing exports no longer refuse an unlisted build'; Key = 'hapi'; Expect = 'V6'; Find = 'if (OptionalSigBad > 0 || CaveSigBad > 0 || !bAllSixteenResolved)'; Repl = 'if (OptionalSigBad > 0 || CaveSigBad > 0)' }
    @{ Name = 'M39 resolved test drops the cave exports'; Key = 'hapi'; Expect = 'V6'; Find = 'A.InSea && A.VoidAt && A.KillboxTop && CaveMissing.IsEmpty();'; Repl = 'A.InSea && A.VoidAt && A.KillboxTop;' }
    @{ Name = 'M40 VoidAt not named in the unlisted refusal'; Key = 'hapi'; Expect = 'V6'; Find = '            NameOpt(A.VoidStatus, TEXT("VoidAt"));' + "`n"; Repl = '' }
    @{ Name = 'M24 strict refusal falls through (return removed)'; Key = 'hapi'; Expect = 'V6'; Find = "*A.Missing, *CaveMissing));`n                return;"; Repl = "*A.Missing, *CaveMissing));" }
    @{ Name = 'M25 usable status always bound'; Key = 'hapi'; Expect = 'V6'; Find = 'A.Status = bUnlisted ? TEXT("signatureVerifiedUnlisted") : TEXT("bound");'; Repl = 'A.Status = TEXT("bound");' }
    @{ Name = 'M26 cave mask drifted in the gate copy'; Key = 'gate'; Expect = 'V6'; Find = 'const int16 SigNum[] = { 0x8B,0x41,0x08,0xC3 };'; Repl = 'const int16 SigNum[] = { 0x8B,0x41,-1,0xC3 };' }
    @{ Name = 'M27 cave check skips IsInsideAt'; Key = 'gate'; Expect = 'V6'; Find = '    Check(SymInside, SigInside, UE_ARRAY_COUNT(SigInside), TEXT("IsInsideAt"));' + "`n"; Repl = '' }
    @{ Name = 'M28 exportMissing refusal no longer queues the notice'; Key = 'hapi'; Expect = 'V7'; Find = ' NodeShuffleRwgGate::QueueRefusalNotice(A.SemVer, A.Build, A.Status, FString::Printf(TEXT("missing: %s"), *A.Missing));'; Repl = '' }
    @{ Name = 'M29 required signatureMismatch refusal no longer queues the notice'; Key = 'hapi'; Expect = 'V7'; Find = ' NodeShuffleRwgGate::QueueRefusalNotice(A.SemVer, A.Build, TEXT("signatureMismatch"), FString::Printf(TEXT("mismatched: %s"), *SigBad));'; Repl = '' }
    @{ Name = 'M30 verified-unlisted branch no longer queues the notice'; Key = 'hapi'; Expect = 'V7'; Find = '            NodeShuffleRwgGate::QueueUnlistedVerifiedNotice(A.SemVer, A.Build, RwgAcceptedList());' + "`n"; Repl = '' }
    @{ Name = 'M31 TickNotice moved behind the callsAllowed guard'; Key = 'hapi'; Expect = 'V7'; Find = "    NodeShuffleRwgGate::TickNotice(GetWorld()); // RWG 0.1.12: drain the refusal / unlisted-verified notice; runs before the callsAllowed guard so a refused bind still posts`n    if (bFull)"; Repl = "    if (bFull)" }
    @{ Name = 'M32 drain posts every tick (once-per-world test removed)'; Key = 'gate'; Expect = 'V7'; Find = '    if (N.PostedWorld.Get() == World) { return; } // delivered (or abandoned) for this world already' + "`n"; Repl = '' }
    @{ Name = 'M33 drain no longer waits for a pawn'; Key = 'gate'; Expect = 'V7'; Find = 'if (!PC || !Pawn)'; Repl = 'if (!PC)' }
    @{ Name = 'M34 retries unbounded'; Key = 'gate'; Expect = 'V7'; Find = 'if (N.Attempts >= MaxNoticeAttempts)'; Repl = 'if (false)' }
    @{ Name = 'M35 posted line claims visibility'; Key = 'gate'; Expect = 'V7'; Find = 'on-screen visibility is not measured by any log line'; Repl = 'the notice is visible on screen' }
    @{ Name = 'M36 notice file carries a marker string'; Key = 'gate'; Expect = 'V5'; Find = '// RWG 0.1.12 gate lane (2026-10-26) -- see'; Repl = '// LOADED (2026-10-26-rwg-0-1-12-1) -- see' }
    @{ Name = 'M37 unlisted verdict moved after A.Hmod'; Key = 'hapi'; Expect = 'V6'; Find = "        A.Hmod = H; // RWG P5: published only on the path that also sets bUsable`n        A.bUsable = true;`n"; Repl = '' }
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
    Write-Host "RWG 0.1.9/0.1.12 GATE LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: RWG 0.1.9/0.1.12 gate lint -- V1..V7 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
