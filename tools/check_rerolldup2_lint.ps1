# REROLLDUP2 lint (2026-11-01, packet rwg-rerolldup-2; handoff _team/test-logs/2026-11-01-nodeshuffle-rerolldup2-handoff.md).
# A runtime-spawned original (bamrenew lead_C) is renamed every session; the load-time rematch rebinds ONE of its layout rows, so
# two rows of one original held two different stale names and the path-keyed dedupe of rwg-rerolldup-1 kept both (7 twin pairs on
# the user's save). Our lead_C-class actors (vanilla ore dealt on a lead slot) never came back with the save either.
# LOADS: NodeShuffleSubsystem.cpp (RollLayout sites), NodeShuffleRerollIdentity.{h,cpp} (the identity module), NodeShuffleLayoutDump.cpp
# (IDTWIN census), NodeShuffleAdoptAudit.cpp (ADOPTCLASS census), NodeShuffle.cpp (marker).
#   V1  identity pre-pass runs on a re-roll after the carry loop and BEFORE the carried sets are filled, with the subsystem radius and a
#       live-path resolver; the previous roll's record paths are snapshotted before it
#   V2  the rebuild loop skips a listed row after the carried-path drop and before the carried branch: count, Display line, continue
#   V3  identity: same path, or same resource AND both anchors set AND within the radius; a relocated row without a stamp has no anchor;
#       keeper order carried > pinned raw > pinned > anchored > earliest; a carried member is renamed, never dropped; a dead keeper name
#       takes a member's LIVE name only; both radius literals are 300
#   V4  dropped rows' names join the augments' dedupe set right after the carried names
#   V5  NORECORD: snapshot before the record reset, census call before the reset and the Re-roll pool line, pool rows only, deduped
#   V6  vanilla deal on a non-generic slot class spawns the generic representative: predicate and the single write live in the module;
#       the conversion calls it after both restamps; every carried entry gets it before the carried append; census per role
#   V7  REROLLPOOL-CARRY carries the six new fields once; REROLLPOOL-IDENTITY exact, re-roll only, ungated; Re-roll pool line extended
#   V8  per-class purity sources include carried originals once (review F3): both builders read PuritySourceEntries
#   V9  DumpLayout IDTWIN/IDTWINS after the TWIN loop; same-path pairs excluded; anchors required
#   V10 ADOPTCLASS per resolved spawn class, active spawn entries only, ungated, before the provisional mark
#   V11 marker rwg-rerolldup-2 once on its own line; the thoriumclass marker comment counts five above
# Every forbidden needle is BUILT BY CONCATENATION. Mutants run in-file. Exit 0 = all hold; 1 = a rule or mutant fails; 2 = missing file.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    idc   = 'NodeShuffle\Private\NodeShuffleRerollIdentity.cpp'
    idh   = 'NodeShuffle\Private\NodeShuffleRerollIdentity.h'
    dump  = 'NodeShuffle\Private\NodeShuffleLayoutDump.cpp'
    audit = 'NodeShuffle\Private\NodeShuffleAdoptAudit.cpp'
    nsmod = 'NodeShuffle\Private\NodeShuffle.cpp'
    hdr   = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
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

# ---- anchors (unique context) ----
$carryAdd    = '                CarriedEntries.Add(Kept);'
$fillGuid    = '        RerollCarriedGuids.Add(C.EntryGuid);'
$snapLoop    = '        for (const FNodeShuffleSuppressedOriginal& R : OriginalNodeRecord) { if (!R.VanillaNodePath.IsEmpty()) { RerollPriorRecordPaths.Add(R.VanillaNodePath); } }'
$prePass     = "        NodeShuffleRerollIdentity::DedupeOriginals(Layout, CarriedEntries, AdoptMatchRadiusCm,`n            [this](const FString& Path) { return IsValid(FindOriginalBaseByPath(Path)); }, RerollIdentity);"
$identFmt    = 'TEXT("REROLLPOOL-IDENTITY candidates=%d candidatesWithoutAnchor=%d groups=%d anchorDrops=%d pathDrops=%d carriedTwinsUnified=%d pathTransfers=%d radiusCm=%.0f thirdRowDrops=%d pathPairsApart=%d anchorPairsBothLive=%d"),'
$dupeContinue = "                    Old.bActive ? 1 : 0, Old.bPinned ? 1 : 0, Old.bIsNewNode ? 1 : 0);`n                continue;`n            }"
$skipPred    = '            if (const NodeShuffleRerollIdentity::FDrop* Drop = RerollIdentity.Drops.Find(Old.EntryGuid))'
$skipCount   = '                ++RerollIdentityDupesDropped;'
$skipFmt     = 'TEXT("REROLLPOOL-IDENTITYDUPE dropped path=''%s'' res=''%s'' guid=%s anchor=%s keeperGuid=%s keeperPath=''%s'' keeperAnchor=%s match=%s distCm=%.0f active=%d pinned=%d newNode=%d"),'
$skipTail    = "                    Drop->DistCm, Old.bActive ? 1 : 0, Old.bPinned ? 1 : 0, Old.bIsNewNode ? 1 : 0);`n                continue;`n            }`n            if (bCarriedOld)"
$carriedFlag = '            const bool bCarriedOld = RerollCarriedGuids.Contains(Old.EntryGuid);'
$guardedAdd  = '            if (!bCarriedOld) { NewLayout.Add(E); } // rwg-rerolldup-1 I1'
$anchorRaw   = '        if (!E.bIsNewNode) { return E.Location; } // a raw original still sits where it was captured'
$anchorZero  = '        return FVector::ZeroVector;              // a relocated entry''s Location is its destination'
$samePath    = '        if (!A.VanillaNodePath.IsEmpty() && A.VanillaNodePath == B.VanillaNodePath) { const FVector PA = OriginalAnchor(A), PB = OriginalAnchor(B); if (!PA.IsNearlyZero() && !PB.IsNearlyZero() && FVector::DistSquared(PA, PB) >= FMath::Square(RadiusCm)) { return false; } bOutByPath = true; OutDistCm = 0.0f; return true; }'
# review F2/F3/F4 (verbatim): the two union guards, the pre-transfer grading copy, the third-row bucket
$pathApart   = 'if (!AX.IsNearlyZero() && !AY.IsNearlyZero() && FVector::DistSquared(AX, AY) >= RadiusSq) { ++Out.PathPairsApart; continue; } Unite(Parent, Same[x], Same[y]);'
$bothLive    = 'if (!EX.VanillaNodePath.IsEmpty() && !EY.VanillaNodePath.IsEmpty() && EX.VanillaNodePath != EY.VanillaNodePath && PathResolvesLive(EX.VanillaNodePath) && PathResolvesLive(EY.VanillaNodePath)) { ++Out.AnchorPairsBothLive; continue; } Unite(Parent, Bucket[x], Bucket[y]);'
$keeperBefore = '            const FNodeShuffleEntry KeeperBefore = Keeper;'
$gradeBefore = '                SameOriginal(KeeperBefore, E, RadiusCm, bByPath, DistCm);'
$thirdRow    = '                if (bByPath) { ++Out.PathDrops; } else if (DistCm >= 0.0f) { ++Out.AnchorDrops; } else { ++Out.ThirdRowDrops; }'
# review F1 (verbatim, reviewer-authored): live carried actors keep their class unless ADOPTCLASS saw the class with no bound actor
$liveKeptSkip = '            AFGResourceNode* const* LiveC = SpawnedNodes.Find(C.EntryGuid); if (LiveC && IsValid(*LiveC) && !AdoptUnboundSpawnClasses.Contains(ResolveSpawnNodeClassPath(C))) { ++RerollVanillaRestampLiveKept; FNodeShuffleEntry RestampProbe = C; NodeShuffleRerollIdentity::FVanillaRestamp RestampProbeCensus; if (NodeShuffleRerollIdentity::RestampVanillaDealOffForeignClass(RestampProbe, ResolvedSolidRepresentative, ResolvedLiquidRepresentative, FormLiquid, RestampProbeCensus)) { ++RerollVanillaRestampEligibleKept; } continue; }'
$liveKeptFmt = 'TEXT("REPCLASS-VANRESTAMP-LIVEKEPT carriedTotal=%d liveNotInUnboundSet=%d eligibleKept=%d unboundClassesAtAdopt=%d adoptRan=%d preAdoptKept=%d"), CarriedEntries.Num(), RerollVanillaRestampLiveKept, RerollVanillaRestampEligibleKept, AdoptUnboundSpawnClasses.Num(), bAdoptedRestoredNodes ? 1 : 0, RerollVanillaRestampPreAdoptKept);'
# round 2 R2-2: no carried restamp before adopt-on-load has run (SpawnedNodes holds no restored actor yet)
$preAdoptSkip = '            if (!bAdoptedRestoredNodes) { ++RerollVanillaRestampPreAdoptKept; continue; }'
$adoptSetReset = '        AdoptUnboundSpawnClasses.Reset();'
# round 2 R2-1: restorability judged on pinned, settled rows only
$adoptPinned = '            if (E.bPinned && !E.bRwgProvisional) { ++Row.PinnedSettled; if (IsValid(SpawnedNodes.FindRef(E.EntryGuid))) { ++Row.PinnedSettledBound; } }'
$adoptBound  = '            if (IsValid(SpawnedNodes.FindRef(E.EntryGuid))) { ++Row.Bound; }'
# round 3 R3-1: the strict intersection -- pinned settled rows exist AND no row of the class at all is bound
$adoptSetAdd = '            if (Row.PinnedSettled > 0 && Row.Bound == 0) { AdoptUnboundSpawnClasses.Add(C); }'
$hdrSet      = 'TSet<FString> AdoptUnboundSpawnClasses;'
# round 2 R2-5: the path leg's fill and loop header are pinned, not only the guard inside the loop
$pathFill    = '            if (!E.VanillaNodePath.IsEmpty()) { ByPath.FindOrAdd(E.VanillaNodePath).Add(c); }'
$pathLoop    = '        for (const TPair<FString, TArray<int32>>& KV : ByPath) { const TArray<int32>& Same = KV.Value;'
$sameRes     = '        if (A.OriginalResourceClassPath.IsEmpty() || A.OriginalResourceClassPath != B.OriginalResourceClassPath) { return false; }'
$sameZero    = '        if (AnchorA.IsNearlyZero() || AnchorB.IsNearlyZero()) { return false; } // an unanchored row never matches by distance'
$sameRadius  = '        if (Dist >= RadiusCm) { return false; }'
$rankBlock   = "            if (bCarried) { return 4; }`n            if (!E.bIsNewNode && E.bPinned) { return 3; }`n            if (E.bPinned) { return 2; }`n            if (!E.OriginalTrueLocation.IsNearlyZero()) { return 1; }`n            return 0;"
$carriedKeep = "                        ++Out.CarriedTwinsUnified;`n                    }`n                    continue;`n                }"
$carriedSame = '                if (bKeeperCarried && !E.VanillaNodePath.IsEmpty() && E.VanillaNodePath == Keeper.VanillaNodePath)'
$dropAdd     = '                Out.Drops.Add(E.EntryGuid, D);'
$liveKeeper  = '            const bool bKeeperPathLive = !Keeper.VanillaNodePath.IsEmpty() && PathResolvesLive(Keeper.VanillaNodePath);'
$liveMember  = '                    if (P.IsEmpty() || P == Keeper.VanillaNodePath || !PathResolvesLive(P)) { continue; }'
$radiusH     = '    constexpr float OriginalIdentityRadiusCm = 300.0f;'
$radiusS     = '    constexpr float AdoptMatchRadiusCm = 300.0f;'
$augCarried  = '        ExistingPaths.Append(RerollCarriedPaths);'
$augDropped  = '        ExistingPaths.Append(RerollIdentity.DroppedPaths);'
$noRecCall   = '        RerollPathsWithoutPriorRecord = NodeShuffleRerollIdentity::ReportPathsWithoutPriorRecord(NewLayout, CarriedEntries, RerollPriorRecordPaths, 48);'
$recReset    = '        OriginalNodeRecord.Reset();'
$poolLine    = 'Re-roll pool (redesign-6)'
$poolTail    = 'rwg-rerolldup-2: %d rows dropped as grouped with a keeper (REROLLPOOL-IDENTITY grades the pre-pass drops by leg: pathDrops, anchorDrops, thirdRowDrops; an earlier skip can take a listed row), %d rebuilt paths had no record in the previous roll)"),'
$poolArgs    = "            RerollIdentityDupesDropped, RerollPathsWithoutPriorRecord);"
$noRecFmt    = 'TEXT("REROLLPOOL-NORECORD path=''%s'' res=''%s'' guid=%s trueLoc=%s carried=%d pinnedNow=%d"),'
$noRecPool   = '        for (const FNodeShuffleEntry& E : Pool) { if (!E.bIsNewNode) { Visit(E, false); } }'
$noRecSeen   = '            if (bSeen || PriorRecordPaths.Contains(E.VanillaNodePath)) { return; }'
$vanPred1    = '        if (E.AssignedResourceClassPath.IsEmpty() || !NodeShuffleRepClass::IsGenericNodeClassPath(E.AssignedResourceClassPath)) { return false; }'
$vanPred2    = '        if (Slot.IsEmpty() || NodeShuffleRepClass::IsGenericNodeClassPath(Slot)) { return false; }'
$vanPred3    = '        if (Representative.IsEmpty() || !NodeShuffleRepClass::IsGenericNodeClassPath(Representative)) { ++Census.RepNotGeneric; return false; }'
$vanWrite    = '        E.RepClassSpawnPath = Representative;'
$vanSlot     = "        const FString& Slot = !E.RepClassSpawnPath.IsEmpty() ? E.RepClassSpawnPath`n            : (E.SpawnNodeClassPath.IsEmpty() ? E.NodeClassPath : E.SpawnNodeClassPath);"
$convForm    = '                    else { RwgConvFormKept++; }'
$convCall    = '                NodeShuffleRerollIdentity::RestampVanillaDealOffForeignClass(E, ResolvedSolidRepresentative, ResolvedLiquidRepresentative, FormLiquid, RerollVanillaRestampConv);'
$convDone    = '                Converted++;'
$convEmit    = '        NodeShuffleRerollIdentity::EmitVanillaRestampCensus(TEXT("conversion"), RerollVanillaRestampConv);'
$carrLoop    = "        int32 RerollVanillaRestampLiveKept = 0, RerollVanillaRestampEligibleKept = 0, RerollVanillaRestampPreAdoptKept = 0;`n        for (FNodeShuffleEntry& C : CarriedEntries)`n        {`n" + $preAdoptSkip + " // adopt-on-load has not run: SpawnedNodes holds no restored actor yet`n" + $liveKeptSkip + " // a live actor whose class is not in the set keeps its class`n            NodeShuffleRerollIdentity::RestampVanillaDealOffForeignClass(C, ResolvedSolidRepresentative, ResolvedLiquidRepresentative, FormLiquid, RerollVanillaRestampCarried);`n        }`n        NodeShuffleRerollIdentity::EmitVanillaRestampCensus(TEXT(""carried""), RerollVanillaRestampCarried);`n        UE_LOG(LogNodeShuffle, Display, " + $liveKeptFmt
$appendCarry = '    NewLayout.Append(CarriedEntries);'
$vanFmt      = 'TEXT("REPCLASS-VANRESTAMP role=%s slotClass=%s to=%s restamped=%d"),'
$vanEndFmt   = 'TEXT("REPCLASS-VANRESTAMP-END role=%s restamped=%d repNotGeneric=%d slotClasses=%d"),'
$censusHead  = 'TEXT("REROLLPOOL-CARRY carried=%d'
$censusTail  = 'poolAfter=%d identityDupesDropped=%d carriedTwinsUnified=%d pathTransfers=%d pathsWithoutPriorRecord=%d vanillaRestampedConversion=%d vanillaRestampedCarried=%d"),'
$censusArgs2 = '            RerollIdentityDupesDropped, RerollIdentity.CarriedTwinsUnified, RerollIdentity.PathTransfers, RerollPathsWithoutPriorRecord, RerollVanillaRestampConv.Restamped, RerollVanillaRestampCarried.Restamped);'
$purSrc      = '    TArray<FNodeShuffleEntry> PuritySourceEntries = NewLayout;'
$purSeen     = "        if (!C.VanillaNodePath.IsEmpty()) { PuritySourceCarriedPaths.Add(C.VanillaNodePath, &bSeen); }`n        if (bSeen) { continue; }"
$purFlag     = '        P.bIsNewNode = false; // read as an original by both source builders; never dealt, never saved'
$purCall1    = '    NodeShuffleRwgDeal::BuildPurityDecks(PuritySourceEntries, PurityDeckSource, SolidDeck, LiquidDeck, Rng, PurityDecks);'
$purCall2    = '    NodeShuffleRwgSeedGas::AddSeededPuritySources(ModQuota, PuritySourceEntries, FormByResource, FormSolid, Rng, PurityDecks);'
$twinFmt     = 'TEXT("LAYOUTDUMP-TWIN dumpId=%d path=%s entries=%d pinned=%d guids=%s"),'
$idtCall     = '    const int32 IdentityTwinPaths = NodeShuffleRerollIdentity::ReportIdentityTwins(Layout, NodeShuffleRerollIdentity::OriginalIdentityRadiusCm, DumpId, 32);'
$idtsFmt     = 'TEXT("LAYOUTDUMP-IDTWINS dumpId=%d identityTwinPaths=%d radiusCm=%.0f"),'
$idtFmt      = 'TEXT("LAYOUTDUMP-IDTWIN dumpId=%d res=%s distCm=%.0f guidA=%s pathA=%s pinnedA=%d guidB=%s pathB=%s pinnedB=%d"),'
$idtSame     = '                    if (A.VanillaNodePath == B.VanillaNodePath) { continue; } // the path-keyed TWINS census already counts these'
$idtAnchor   = '            if (OriginalAnchor(E).IsNearlyZero()) { continue; }'
$adoptPred   = '            if (!E.bIsNewNode || !E.bActive) { continue; }'
$adoptKey    = '            FClassRow& Row = ByClass.FindOrAdd(ResolveSpawnNodeClassPath(E));'
$adoptFmt    = 'TEXT("ADOPTCLASS class=%s entries=%d bound=%d unbound=%d provisional=%d pinnedSettled=%d pinnedSettledBound=%d"),'
$adoptMark   = '    RwgProvCandMarkDirty(TEXT("reload"));'
$adoptCensus = 'TEXT("ADOPTCENSUS: preRegistered=%d'
$marker      = '2026-11-01-rwg-' + 'rerolldup-2'
$diagGate    = 'AreDiagnostics' + 'Enabled'

function Invoke-RerollDup2Checks($T) {
    $f = @()
    $s = $T.sub; $c = $T.idc; $h = $T.idh; $d = $T.dump; $a = $T.audit
    # V1
    if ((Count $s $prePass) -ne 1 -or (Count $s $snapLoop) -ne 1 -or (Count $s $identFmt) -ne 1) { $f += '(V1) identity pre-pass, record snapshot or its census line missing' }
    if (-not (Ordered $s @($carryAdd, '    if (bIsReroll)' + "`n    {`n" + $snapLoop, $prePass, $identFmt, $fillGuid))) { $f += '(V1) order: carry loop -> re-roll-only snapshot -> pre-pass -> census -> carried sets' }
    $v1 = Span $s $prePass $fillGuid
    if (-not $v1 -or $v1.Contains($diagGate)) { $f += '(V1) the identity census must be ungated' }
    # V2
    if ((Count $s $skipPred) -ne 1 -or (Count $s $skipCount) -ne 1 -or (Count $s $skipFmt) -ne 1 -or (Count $s $skipTail) -ne 1) { $f += '(V2) identity skip branch: predicate, count, Display line or continue-before-carried-branch missing' }
    if (-not (Ordered $s @($carriedFlag, $dupeContinue, $skipPred, $skipCount, $skipFmt, $skipTail, $guardedAdd))) { $f += '(V2) order: carried flag -> path dupe -> identity skip -> carried branch -> guarded Add' }
    # V3
    foreach ($n in @($anchorRaw, $anchorZero, $samePath, $sameRes, $sameZero, $sameRadius, $rankBlock, $carriedKeep, $carriedSame, $dropAdd, $liveKeeper, $liveMember)) {
        if ((Count $c $n) -ne 1) { $f += "(V3) identity module text missing: $($n.Substring(0, [Math]::Min(60, $n.Length)).Trim())" }
    }
    if (-not (Ordered $c @($samePath, $sameRes, $sameZero, $sameRadius))) { $f += '(V3) SameOriginal: path leg, then resource, then both anchors set, then the radius' }
    if (-not (Ordered $c @($keeperBefore, $liveKeeper, $liveMember, $gradeBefore, $carriedKeep, $carriedSame, $dropAdd, $thirdRow))) { $f += '(V3) group loop: pre-transfer copy, live-name transfer, legs graded on the copy, carried member renamed and kept, carried-keeper same-path skip, the drop, three drop buckets' }
    foreach ($n in @($pathApart, $bothLive, $keeperBefore, $gradeBefore, $thirdRow)) { if ((Count $c $n) -ne 1) { $f += "(V3) review guard missing: $($n.Trim().Substring(0, 50))" } }
    if ((Count $c $pathFill) -ne 1 -or (Count $c $pathLoop) -ne 1 -or -not (Ordered $c @($pathFill, 'const float RadiusSq = FMath::Square(RadiusCm);', $pathLoop, $pathApart, $bothLive))) { $f += '(V3) union order: path fill, radius, same-name loop over ByPath (apart guard), same-resource pairs (both-live guard)' }
    if ((Count $h $radiusH) -ne 1 -or (Count $s $radiusS) -ne 1) { $f += '(V3) both identity radius literals must read 300' }
    if ((Count $s ('AdoptMatchRadiusCm,' + "`n            [this]")) -ne 1) { $f += '(V3) the pre-pass must receive the subsystem radius' }
    # V4
    if ((Count $s $augDropped) -ne 1 -or -not (Ordered $s @($augCarried, $augDropped, 'Non-solid augment'))) { $f += '(V4) dropped names must join the augments dedupe set right after the carried names' }
    # V5
    if ((Count $s $noRecCall) -ne 1 -or -not (Ordered $s @($guardedAdd, $noRecCall, $recReset, $poolLine, $poolTail, $poolArgs))) { $f += '(V5) NORECORD call must run after the rebuild loop, before the record reset and the Re-roll pool line (extended)' }
    $snapI = $s.IndexOf($snapLoop); $resetI = $s.IndexOf($recReset)
    if ($snapI -lt 0 -or $resetI -lt 0 -or $snapI -gt $resetI) { $f += '(V5) the record snapshot must precede the first record reset' }
    if ((Count $c $noRecFmt) -ne 1 -or (Count $c $noRecPool) -ne 1 -or (Count $c $noRecSeen) -ne 1) { $f += '(V5) NORECORD: format, pool-rows-only visit or the seen/prior test missing' }
    # V6
    foreach ($n in @($vanPred1, $vanPred2, $vanPred3, $vanWrite, $vanSlot, $vanFmt, $vanEndFmt)) { if ((Count $c $n) -ne 1) { $f += "(V6) restamp helper text missing: $($n.Substring(0, [Math]::Min(60, $n.Length)).Trim())" } }
    if (-not (Ordered $c @($vanPred1, $vanSlot, $vanPred2, $vanPred3, $vanWrite))) { $f += '(V6) restamp: generic deal, slot read, non-generic slot, generic representative, then the one write' }
    if ((Count $s ('RepClassSpawnPath =' + ' ')) -ne 2) { $f += '(V6) the subsystem keeps exactly two RepClassSpawnPath writes (repclass R8); the vanilla-deal write lives in the module' }
    if ((Count $s $convCall) -ne 1 -or -not (Ordered $s @($convForm, $convCall, $convDone))) { $f += '(V6) the conversion must call the restamp after the form restamp and before Converted++' }
    if ((Count $s $convEmit) -ne 1 -or (Count $s $carrLoop) -ne 1 -or -not (Ordered $s @($convEmit, "    if (bIsReroll)`n    {`n" + $carrLoop, $appendCarry))) { $f += '(V6) conversion census, then every carried entry restamped (re-roll only) before the carried append' }
    if ((Count $s $liveKeptSkip) -ne 1 -or (Count $s $liveKeptFmt) -ne 1 -or (Count $s $preAdoptSkip) -ne 1 -or (Count $T.hdr $hdrSet) -ne 1 -or (Count $T.hdr ("binds/destroys nothing`n    TSet<FString> AdoptUnboundSpawnClasses;")) -ne 1) { $f += '(V6) review F1/R2-2: no carried restamp before adopt ran; live carried actors skip it unless their class is in the session-only unbound set; the set is a plain member right after AdoptAuditEmit, never saved' }
    # V7
    if ((Count $s $censusTail) -ne 1 -or (Count $s $censusArgs2) -ne 1) { $f += '(V7) REROLLPOOL-CARRY must carry the six new fields with their arguments' }
    foreach ($tok in @('identityDupesDropped=', 'pathsWithoutPriorRecord=', 'vanillaRestampedConversion=', 'vanillaRestampedCarried=', 'thirdRowDrops=', 'pathPairsApart=', 'anchorPairsBothLive=', 'liveNotInUnboundSet=', 'unboundClassesAtAdopt=', 'eligibleKept=', 'adoptRan=', 'preAdoptKept=')) {
        if ((Count $s (' ' + $tok)) -ne 1) { $f += "(V7) census token echoed or missing: $tok" }
    }
    foreach ($tok in @('carriedTwinsUnified=', 'pathTransfers=')) { # review F6: one counter, one name, printed by both the IDENTITY and the CARRY census
        if ((Count $s (' ' + $tok)) -ne 2) { $f += "(V7) census token echoed or missing (two print sites): $tok" }
    }
    foreach ($tok in @('REROLLPOOL-IDENTITY ', 'REROLLPOOL-IDENTITYDUPE ', 'REROLLPOOL-NORECORD ', 'REPCLASS-VANRESTAMP ', 'REPCLASS-VANRESTAMP-END ', 'REPCLASS-VANRESTAMP-LIVEKEPT ', 'LAYOUTDUMP-IDTWIN ', 'LAYOUTDUMP-IDTWINS ', 'ADOPTCLASS ')) {
        $all = $s + $c + $d + $a
        if ((Count $all ('TEXT("' + $tok)) -ne 1) { $f += "(V7) each new tag is printed from exactly one site: $tok" }
    }
    # V8
    if ((Count $s $purSrc) -ne 1 -or (Count $s $purSeen) -ne 1 -or (Count $s $purFlag) -ne 1 -or (Count $s $purCall1) -ne 1 -or (Count $s $purCall2) -ne 1) { $f += '(V8) purity sources: copy of the pool, carried dedupe, original flag, both builders reading PuritySourceEntries' }
    if ((Count $s ('BuildPurityDecks(' + 'NewLayout')) -ne 0 -or (Count $s ('AddSeededPuritySources(ModQuota, ' + 'NewLayout')) -ne 0) { $f += '(V8) no purity builder may read NewLayout directly' }
    if (-not (Ordered $s @($purSrc, $purSeen, $purFlag, $purCall1, $purCall2))) { $f += '(V8) purity source order' }
    # V9
    if ((Count $d $idtCall) -ne 1 -or (Count $d $idtsFmt) -ne 1 -or -not (Ordered $d @($twinFmt, $idtCall, $idtsFmt))) { $f += '(V9) IDTWINS after the TWIN loop with the header radius' }
    if ((Count $c $idtFmt) -ne 1 -or (Count $c $idtSame) -ne 1 -or (Count $c $idtAnchor) -ne 1 -or -not (Ordered $c @($idtAnchor, $idtSame, $idtFmt))) { $f += '(V9) IDTWIN: anchors required, same-path pairs excluded, exact format' }
    # V10
    $emit = Span $a 'void ANodeShuffleSubsystem::AdoptAuditEmit(int32 AdoptLineAdopted)' $adoptMark
    if (-not $emit -or (Count $emit $adoptPred) -ne 1 -or (Count $emit $adoptKey) -ne 1 -or (Count $emit $adoptFmt) -ne 1 -or -not (Ordered $emit @($adoptCensus, $adoptPred, $adoptKey, $adoptBound, $adoptPinned, $adoptSetReset, $adoptFmt, $adoptSetAdd))) { $f += '(V10) ADOPTCLASS: active spawn entries, keyed by the resolved spawn class, bound and pinned-settled counted per row, after ADOPTCENSUS and before the provisional mark; the unbound set is reset then filled per class' }
    if ((Count $emit $adoptSetReset) -ne 1 -or (Count $emit $adoptSetAdd) -ne 1 -or (Count $emit $adoptPinned) -ne 1 -or (Count $emit $adoptBound) -ne 1) { $f += '(V10) review F1/R2-1/R3-1: the unbound-class set is reset once and filled only where pinned settled rows exist and no row of the class is bound' }
    foreach ($tok in @('pinnedSettled=', 'pinnedSettledBound=', 'ADOPTCLASS class=')) { if ((Count $a (' ' + $tok)) -ne 1 -and (Count $a ('"' + $tok)) -ne 1) { $f += "(V10) ADOPTCLASS token echoed or missing: $tok" } }
    if ($a.Contains($diagGate)) { $f += '(V10) ADOPTCLASS must be ungated' }
    # V11
    if ((Count $T.nsmod ('    UE_LOG(LogNodeShuffle, Display, TEXT("===== NodeShuffle packet marker (' + $marker + ')====="));')) -ne 1 -or (Count $T.nsmod $marker) -ne 1) { $f += '(V11) marker missing or not on its own line' }
    if ((Count $T.nsmod ('rwg-thoriumclass-1)====="));' + ' // own line, same reason as the five above')) -ne 1) { $f += '(V11) the thoriumclass marker comment must count five above' }
    foreach ($k in @('sub', 'idc', 'idh', 'dump', 'audit')) { if ((Count $T[$k] $marker) -ne 0) { $f += "(V11) marker must not appear in the packet sources ($k)" } }
    return ,$f
}

$base = Invoke-RerollDup2Checks $text
if ($base.Count -gt 0) { $base | ForEach-Object { Write-Host "FAIL$_" }; Write-Host "REROLLDUP2 lint: FAIL -- baseline $($base.Count) problem(s)"; exit 1 }
Write-Host 'BASELINE GREEN: V1..V11 hold across 6 loaded files'

$mutants = @(
    @{ Name = 'M1 identity drop logged but kept in the pool'; Key = 'sub'; Expect = 'V2'; Find = $skipTail; Repl = "                    Drop->DistCm, Old.bActive ? 1 : 0, Old.bPinned ? 1 : 0, Old.bIsNewNode ? 1 : 0);`n            }`n            if (bCarriedOld)" }
    @{ Name = 'M2 pre-pass never runs'; Key = 'sub'; Expect = 'V1'; Find = $prePass; Repl = '        (void)RerollIdentity;' }
    @{ Name = 'M3 pre-pass runs after the carried sets are filled'; Key = 'sub'; Expect = 'V1'; Find = ($fillGuid + "`n"); Repl = ''; Find2 = $carryAdd; Repl2 = ($carryAdd + "`n                " + $fillGuid.Trim()) }
    @{ Name = 'M4 header radius widened'; Key = 'idh'; Expect = 'V3'; Find = $radiusH; Repl = '    constexpr float OriginalIdentityRadiusCm = 500.0f;' }
    @{ Name = 'M5 subsystem radius narrowed'; Key = 'sub'; Expect = 'V3'; Find = $radiusS; Repl = '    constexpr float AdoptMatchRadiusCm = 250.0f;' }
    @{ Name = 'M6 zero anchors match by distance'; Key = 'idc'; Expect = 'V3'; Find = ($sameZero + "`n"); Repl = '' }
    @{ Name = 'M7 resource leg dropped'; Key = 'idc'; Expect = 'V3'; Find = $sameRes; Repl = '        if (A.OriginalResourceClassPath.IsEmpty()) { return false; }' }
    @{ Name = 'M8 relocated row anchored at its destination'; Key = 'idc'; Expect = 'V3'; Find = $anchorZero; Repl = '        return E.Location;' }
    @{ Name = 'M9 carried member dropped'; Key = 'idc'; Expect = 'V3'; Find = $carriedKeep; Repl = "                        ++Out.CarriedTwinsUnified;`n                    }`n                }" }
    @{ Name = 'M10 carried row not preferred as keeper'; Key = 'idc'; Expect = 'V3'; Find = '            if (bCarried) { return 4; }'; Repl = '            if (bCarried) { return 0; }' }
    @{ Name = 'M11 dead name transferred'; Key = 'idc'; Expect = 'V3'; Find = $liveMember; Repl = '                    if (P.IsEmpty() || P == Keeper.VanillaNodePath) { continue; }' }
    @{ Name = 'M12 dropped names left out of the augments set'; Key = 'sub'; Expect = 'V4'; Find = ($augDropped + " // rwg-rerolldup-2: a dropped row's name (dead or live) never re-enters through the live scans`n"); Repl = '' }
    @{ Name = 'M13 NORECORD measured after the reset'; Key = 'sub'; Expect = 'V5'; Find = ($noRecCall + "`n"); Repl = ''; Find2 = ($recReset + "`n        UE_LOG(LogNodeShuffle, Display,`n            TEXT(""Re-roll pool"); Repl2 = ($recReset + "`n" + $noRecCall + "`n        UE_LOG(LogNodeShuffle, Display,`n            TEXT(""Re-roll pool") }
    @{ Name = 'M14 record snapshot dropped'; Key = 'sub'; Expect = 'V1'; Find = ($snapLoop + "`n"); Repl = '' }
    @{ Name = 'M15 NORECORD counts relocated rows too'; Key = 'idc'; Expect = 'V5'; Find = $noRecPool; Repl = '        for (const FNodeShuffleEntry& E : Pool) { Visit(E, false); }' }
    @{ Name = 'M16 restamp reaches modded deals'; Key = 'idc'; Expect = 'V6'; Find = $vanPred1; Repl = '        if (E.AssignedResourceClassPath.IsEmpty()) { return false; }' }
    @{ Name = 'M17 restamp rewrites a generic slot'; Key = 'idc'; Expect = 'V6'; Find = $vanPred2; Repl = '        if (Slot.IsEmpty()) { return false; }' }
    @{ Name = 'M18 non-generic representative written'; Key = 'idc'; Expect = 'V6'; Find = $vanPred3; Repl = '        if (Representative.IsEmpty()) { ++Census.RepNotGeneric; return false; }' }
    @{ Name = 'M19 conversion call dropped'; Key = 'sub'; Expect = 'V6'; Find = ($convCall + "`n"); Repl = '' }
    @{ Name = 'M20 carried loop dropped'; Key = 'sub'; Expect = 'V6'; Find = $carrLoop; Repl = '        (void)RerollVanillaRestampCarried;' }
    @{ Name = 'M21 conversion restamp before the form restamp'; Key = 'sub'; Expect = 'V6'; Find = ($convCall + "`n"); Repl = ''; Find2 = '                E.RepClassSpawnPath.Reset();'; Repl2 = ("                E.RepClassSpawnPath.Reset();`n" + $convCall) }
    @{ Name = 'M22 write copied into the subsystem'; Key = 'sub'; Expect = 'V6'; Find = $convCall; Repl = ($convCall + "`n                if (E.RepClassSpawnPath.IsEmpty()) { E.RepClassSpawnPath = ResolvedSolidRepresentative; }") }
    @{ Name = 'M23 census field dropped'; Key = 'sub'; Expect = 'V7'; Find = ' pathsWithoutPriorRecord=%d'; Repl = '' }
    @{ Name = 'M24 identity census gated'; Key = 'sub'; Expect = 'V1'; Find = ("        UE_LOG(LogNodeShuffle, Display,`n            " + $identFmt); Repl = ("        if (FNodeShuffleModule::" + $diagGate + "()) UE_LOG(LogNodeShuffle, Display,`n            " + $identFmt) }
    @{ Name = 'M25 legend echoes a census token'; Key = 'sub'; Expect = 'V7'; Find = '    // The previous roll''s record paths are snapshotted here'; Repl = ('    // identityDupesDropped= counts them; the previous roll''s record paths are snapshotted here') }
    @{ Name = 'M26 purity builder reads the pool again'; Key = 'sub'; Expect = 'V8'; Find = $purCall1; Repl = '    NodeShuffleRwgDeal::BuildPurityDecks(NewLayout, PurityDeckSource, SolidDeck, LiquidDeck, Rng, PurityDecks);' }
    @{ Name = 'M27 carried twins counted twice in purity'; Key = 'sub'; Expect = 'V8'; Find = $purSeen; Repl = '        if (!C.VanillaNodePath.IsEmpty()) { PuritySourceCarriedPaths.Add(C.VanillaNodePath, &bSeen); }' }
    @{ Name = 'M28 carried purity source kept as a spawn entry'; Key = 'sub'; Expect = 'V8'; Find = $purFlag; Repl = '        (void)P;' }
    @{ Name = 'M29 IDTWIN counts same-path pairs'; Key = 'idc'; Expect = 'V9'; Find = ($idtSame + "`n"); Repl = '' }
    @{ Name = 'M30 IDTWINS line dropped'; Key = 'dump'; Expect = 'V9'; Find = $idtCall; Repl = '    (void)DumpId;' }
    @{ Name = 'M31 ADOPTCLASS counts inactive entries'; Key = 'audit'; Expect = 'V10'; Find = $adoptPred; Repl = '            if (!E.bIsNewNode) { continue; }' }
    @{ Name = 'M32 ADOPTCLASS gated'; Key = 'audit'; Expect = 'V10'; Find = $adoptMark; Repl = ("    if (FNodeShuffleModule::" + $diagGate + "()) {}`n" + $adoptMark) }
    @{ Name = 'M33 ADOPTCLASS keyed on the original class'; Key = 'audit'; Expect = 'V10'; Find = $adoptKey; Repl = '            FClassRow& Row = ByClass.FindOrAdd(E.NodeClassPath);' }
    @{ Name = 'M34 marker reverted'; Key = 'nsmod'; Expect = 'V11'; Find = $marker; Repl = ('2026-11-01-rwg-' + 'rerolldup-1b') }
    @{ Name = 'M35 style debt back (four above)'; Key = 'nsmod'; Expect = 'V11'; Find = 'rwg-thoriumclass-1)====="));' + ' // own line, same reason as the five above'; Repl = 'rwg-thoriumclass-1)====="));' + ' // own line, same reason as the four above' }
    @{ Name = 'M36 (review F2) path leg ignores the anchors'; Key = 'idc'; Expect = 'V3'; Find = $samePath; Repl = '        if (!A.VanillaNodePath.IsEmpty() && A.VanillaNodePath == B.VanillaNodePath) { bOutByPath = true; OutDistCm = 0.0f; return true; }' }
    @{ Name = 'M37 (review F2) same-name pairs united at any distance'; Key = 'idc'; Expect = 'V3'; Find = $pathApart; Repl = 'Unite(Parent, Same[x], Same[y]);' }
    @{ Name = 'M38 (review F3) two live names united'; Key = 'idc'; Expect = 'V3'; Find = $bothLive; Repl = 'Unite(Parent, Bucket[x], Bucket[y]);' }
    @{ Name = 'M39 (review F4) legs graded after the transfer'; Key = 'idc'; Expect = 'V3'; Find = $gradeBefore; Repl = '                SameOriginal(Keeper, E, RadiusCm, bByPath, DistCm);' }
    @{ Name = 'M40 (review F4) third-row drops counted as anchor drops'; Key = 'idc'; Expect = 'V3'; Find = $thirdRow; Repl = '                if (bByPath) { ++Out.PathDrops; } else { ++Out.AnchorDrops; }' }
    @{ Name = 'M41 (review F1) live restorable actor restamped'; Key = 'sub'; Expect = 'V6'; Find = ($liveKeptSkip + " // a live actor whose class is not in the set keeps its class`n"); Repl = '' }
    @{ Name = 'M45 (r2 R2-1) set judged on all entries'; Key = 'audit'; Expect = 'V10'; Find = $adoptSetAdd; Repl = '            if (Row.Entries > 0 && Row.Bound == 0) { AdoptUnboundSpawnClasses.Add(C); }' }
    @{ Name = 'M49 (r3 R3-1) set ignores unpinned bound rows'; Key = 'audit'; Expect = 'V10'; Find = $adoptSetAdd; Repl = '            if (Row.PinnedSettled > 0 && Row.PinnedSettledBound == 0) { AdoptUnboundSpawnClasses.Add(C); }' }
    @{ Name = 'M50 (r3 R3-3) bound never counted'; Key = 'audit'; Expect = 'V10'; Find = ($adoptBound + "`n"); Repl = '' }
    @{ Name = 'M46 (r2 R2-2) carried restamp before adopt'; Key = 'sub'; Expect = 'V6'; Find = ($preAdoptSkip + " // adopt-on-load has not run: SpawnedNodes holds no restored actor yet`n"); Repl = '' }
    @{ Name = 'M47 (r2 R2-5) same-name loop retargeted at ByResource'; Key = 'idc'; Expect = 'V3'; Find = $pathLoop; Repl = '        for (const TPair<FString, TArray<int32>>& KV : ByResource) { const TArray<int32>& Same = KV.Value;' }
    @{ Name = 'M48 (r2 R2-5) UPROPERTY on the line above the set'; Key = 'hdr'; Expect = 'V6'; Find = ("binds/destroys nothing`n    TSet<FString> AdoptUnboundSpawnClasses;"); Repl = ("binds/destroys nothing`n    UPROPERTY" + "(SaveGame)`n    TSet<FString> AdoptUnboundSpawnClasses;") }
    @{ Name = 'M42 (review F1) unbound set never filled'; Key = 'audit'; Expect = 'V10'; Find = ($adoptSetAdd + "`n"); Repl = '' }
    @{ Name = 'M43 (review F1) unbound set never reset'; Key = 'audit'; Expect = 'V10'; Find = ($adoptSetReset + "`n"); Repl = '' }
    @{ Name = 'M44 (review F1) unbound set saved'; Key = 'hdr'; Expect = 'V6'; Find = ('    ' + $hdrSet); Repl = ('    UPROPERTY' + '(SaveGame) ' + $hdrSet) }
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
    $r = Invoke-RerollDup2Checks $t2
    $hits = @($r | Where-Object { $_.StartsWith("($($m.Expect))") })
    if ($hits.Count -gt 0) { Write-Host "RED  (ok) $($m.Name)" }
    else { Write-Host "GREEN (BAD) $($m.Name): expected $($m.Expect), got: $($r -join ' | ')"; $bad++ }
}
if ($bad -gt 0) { Write-Host "FAIL: $bad mutant(s) not caught"; exit 1 }
Write-Host "PASS: REROLLDUP2 lint -- V1..V11 hold; mutants caught $($mutants.Count)/$($mutants.Count)."
exit 0
