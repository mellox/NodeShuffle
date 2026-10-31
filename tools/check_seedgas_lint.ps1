# SEEDGAS / MODQUOTA lint (2026-10-31, packet rwg-seedgas-1 + amendment A1): the RWG modded vanilla-count table.
# Brief: _team/test-logs/2026-10-31-nodeshuffle-seedgas-brief.md (A1 at its end). LOADS and PINS every file the packet touched.
# Needles naming a forbidden literal are BUILT BY CONCATENATION so this file cannot match itself. Pins anchor on context unique
# to their site (function names / exact statements), never on a bare fragment.
#   Q1  Gate order per row: disabled -> notRwg -> badRow -> vanillaRow (/Game/ path) -> duplicate -> OVERRIDE only when captured originals > 0 (then continue)
#       -> SEED path: descriptor loaded (descUnloaded) -> gas form required -> class gate -> noClass skip -> seeded. The seed branch is reachable only past the
#       override branch's continue, i.e. only with zero captured originals.
#   Q2  Class gate: AcceptOwnClass = AFGResourceNode child AND not an AFGResourceDeposit child AND not abstract AND not
#       session-refused AND IsOwnNodeClassForResource (own mount AND CDO resource == row resource, fix round 1 F1); the
#       registry candidate and the configured class both pass through it; registry is tried first.
#   Q3  Override saves the captured count, writes the target, moves VanillaCount by (target - captured); seed writes the target,
#       FormSolid, the resolved class and the seeded set. The table file never writes the quota or the active target.
#   Q4  vanillaOreScale (log field; renamed from the old land-factor name in fix round 1: it includes the player's RWG amount
#       settings as well as land) = captured /Game/ solid originals / 429, clamped [0.25, 4.0]; target = round(VanillaCount * vanillaOreScale).
#   Q5  Subsystem call order: DecideAndApply before ResourceKinds is generated; the share loop keeps max3(Q, floor, pinned);
#       Restore after the trim loop and before step 5; the gas deck drop skips SEEDED kinds only; LogRoll after
#       BuildPurityDecks; EmitSpawnCensus after the RWG spawn pass.
#   Q6  Defaults: toggle ModdedVanillaCountsOnRwg ON in the mirror and the panel; default rows lead 6 and thorium 45 (class empty) and
#       lithium 23 (Alkali class); rows read from the LIVE config tree, never the struct mirror.
#   Q7  (fix round 1 F7: pinnedOrCarried = PinnedPerResource, occupied layout entries plus carried entries of the resource;
#       deckCards = every deck card of the resource; purityDeck = the deck its dealt cards draw purity from, set before the deal.)
#       Diagnostics: MODQUOTA, MODQUOTA-CFG, MODQUOTA-CLASS and SEEDGASSPAWN exact formats, Display, specifier count == argument
#       count, no repeated field; SEEDGASSPAWN is change-driven; the pre-A1 line name is gone.
#   Q8  Seeded purity (fix round 1 F2, D3): /Game/ solid captured originals' purities, RNG only for seeded kinds, sorted order;
#       empty source -> map-wide deck (purityDeck=mapwide); census on MODQUOTA-CFG.
# Mutants run in-file. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    sub  = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    sg   = 'NodeShuffle\Private\NodeShuffleRwgSeedGas.cpp'
    sgh  = 'NodeShuffle\Private\NodeShuffleRwgSeedGas.h'
    cfg  = 'NodeShuffle\Private\NodeShuffleConfig.cpp'
    cfgh = 'NodeShuffle\Public\NodeShuffleConfig.h'
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
function Spec-Count([string]$fmt) { return ([regex]::Matches(($fmt -replace '%%', ''), '%[-+ #0]*\d*(?:\.\d+)?[a-zA-Z]')).Count }
function Fmt-Fields([string]$fmt) { return [regex]::Matches($fmt, '(?<![A-Za-z0-9])([A-Za-z][A-Za-z0-9]*)=') | ForEach-Object { $_.Groups[1].Value } }
function Get-Logs([string]$t, [string]$prefix) {
    $out = @(); $needle = 'UE_LOG(LogNodeShuffle, '; $at = 0
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
function Check-Log($T, [string]$key, [string]$tag, [string]$prefix, [string]$wantFmt, [int]$wantArgs) {
    $f = @(); $logs = @(Get-Logs $T[$key] $prefix)
    if ($logs.Count -ne 1) { return @("($tag) expected 1 statement with format prefix '$prefix', found $($logs.Count)") }
    $l = $logs[0]
    if ($l.Level -ne 'Display') { $f += "($tag) '$prefix' must log at Display" }
    if ($l.Fmt -ne $wantFmt) { $f += "($tag) format drifted: '$($l.Fmt)'" }
    if ((Spec-Count $l.Fmt) -ne $l.Args.Count -or $l.Args.Count -ne $wantArgs) { $f += "($tag) arity: $(Spec-Count $l.Fmt) specifier(s) vs $($l.Args.Count) argument(s) (want $wantArgs) in '$prefix'" }
    $fields = @(Fmt-Fields $l.Fmt); if (($fields | Select-Object -Unique).Count -ne $fields.Count) { $f += "($tag) repeated field in '$prefix'" }
    return $f
}

function Invoke-SeedgasChecks($T) {
    $f = @()
    $g = $T.sg; $s = $T.sub; $h = $T.sgh
    # Q1
    if (-not (Ordered $g @('void DecideAndApply(UObject* WorldContext', 'if (!bEnabled) { R.Skipped = TEXT("disabled"); continue; }', 'if (!bMapIsRwg) { R.Skipped = TEXT("notRwg"); continue; }', 'if (Row.Resource.IsEmpty() || Row.VanillaCount < 1 || Row.VanillaCount > MaxRowCount) { R.Skipped = TEXT("badRow"); continue; }', 'if (Row.Resource.StartsWith(TEXT("/Game/"))) { R.Skipped = TEXT("vanillaRow"); continue; }', 'if (SeenRows.Contains(Row.Resource)) { R.Skipped = TEXT("duplicate"); continue; }', 'if (R.CapturedOriginals > 0)', 'R.Action = TEXT("override");', 'continue;', 'if (Desc == nullptr) { R.Skipped = TEXT("descUnloaded"); continue; }', 'if (R.Form != TEXT("gas")) { R.Skipped = TEXT("notGas"); continue; }', 'if (R.ResolvedClass.IsEmpty()) { R.Skipped = TEXT("noClass"); continue; }', 'Out.SeededKinds.Add(Row.Resource);', 'R.Action = TEXT("seed");'))) { $f += '(Q1) gate order must be disabled, notRwg, badRow, vanillaRow, duplicate, override only when originals > 0, then descUnloaded, gas, class, seed' }
    if ((Count $g 'if (R.CapturedOriginals > 0)') -ne 1 -or (Count $g ('R.CapturedOriginals ' + '>= 0')) -ne 0 -or (Count $g ('R.CapturedOriginals ' + '== 0')) -ne 0) { $f += '(Q1) the override branch is the single originals-positive test; the seed path is its fall-through' }
    # Q2
    $accept = 'return C && C->IsChildOf(AFGResourceNode::StaticClass()) && !C->IsChildOf(AFGResourceDeposit::StaticClass())' + "`n" + '                && !C->HasAnyClassFlags(CLASS_Abstract)' + "`n" + '                && !Refused.Contains(Path) && NodeShuffleRepClass::IsOwnNodeClassForResource(C, Path, Res);'
    if ((Count $g $accept) -ne 1 -or (Count $g '#include "Resources/FGResourceDeposit.h"') -ne 1) { $f += '(Q2) AcceptOwnClass must test node child, not a deposit, not abstract, not refused and the spawn site own-class test (own mount AND CDO resource)' }
    if ((Count $g 'AcceptOwnClass(LoadClassQuiet(Match.Matched), Match.Matched, Row.Resource, RefusedClasses)') -ne 1 -or (Count $g 'AcceptOwnClass(Cfg, Row.NodeClassPath, Row.Resource, RefusedClasses)') -ne 1) { $f += '(Q2) both the registry candidate and the configured class must pass AcceptOwnClass' }
    if (-not (Ordered $g @('NodeShuffleRepClass::MatchByRegistry(Row.Resource, RegQuery, RefusedClasses, RegCache, Match);', 'R.ResolvedBy = TEXT("registry");', 'if (R.ResolvedClass.IsEmpty() && AcceptOwnClass(Cfg', 'R.ResolvedBy = TEXT("config");'))) { $f += '(Q2) registry first, configured class only when the registry did not resolve' }
    # Q3
    if (-not (Ordered $g @('Out.SavedCounts.Add(Row.Resource, R.CapturedOriginals);', 'VanillaResourceCounts.Add(Row.Resource, R.Target);', 'VanillaCount += R.Target - R.CapturedOriginals;', 'R.Action = TEXT("override");'))) { $f += '(Q3) override must save the captured count, write the target and move VanillaCount by target minus captured' }
    if (-not (Ordered $g @('if (R.ResolvedClass.IsEmpty()) { R.Skipped = TEXT("noClass"); continue; }', 'VanillaResourceCounts.Add(Row.Resource, R.Target);', 'VanillaCount += R.Target;', 'FormByResource.Add(Row.Resource, FormSolid);', 'NodeClassByResource.Add(Row.Resource, R.ResolvedClass);', 'Out.SeededKinds.Add(Row.Resource);'))) { $f += '(Q3) seed must write the target, FormSolid, the resolved class and the seeded set, after the class gate' }
    if ((Count $g ('Quota' + '.Add(')) -ne 0 -or (Count $g ('Quota' + '[')) -ne 0 -or (Count $g ('Target' + 'Active')) -ne 0) { $f += '(Q3) the table file must never write the quota or the active target -- the count goes through the shared pipeline' }
    if (-not (Ordered $g @('void Restore(FState& S', 'if (!S.bApplied) { return; }', 'VanillaResourceCounts.Add(KV.Key, KV.Value);', 'VanillaResourceCounts.Remove(K);', 'VanillaCount = S.VanillaCountBefore;'))) { $f += '(Q3) Restore must put back saved counts, remove seeded kinds and restore VanillaCount' }
    # Q4
    if ((Count $h 'constexpr int32 RwgBaseSolidNodes = 429;') -ne 1 -or (Count $h 'constexpr float VanillaOreScaleMin = 0.25f;') -ne 1 -or (Count $h 'constexpr float VanillaOreScaleMax = 4.0f;') -ne 1) { $f += '(Q4) vanillaOreScale constants must be 429, 0.25 and 4.0' }
    if ((Count $g 'Out.VanillaOreScale = FMath::Clamp(static_cast<float>(Out.VanillaOreOriginals) / RwgBaseSolidNodes, VanillaOreScaleMin, VanillaOreScaleMax);') -ne 1 -or (Count $g 'if (KV.Key.StartsWith(TEXT("/Game/")) && FormByResource.FindRef(KV.Key) == FormSolid) { Out.VanillaOreOriginals += KV.Value; }') -ne 1 -or (Count $g 'R.Target = FMath::RoundToInt(FMath::Clamp(Row.VanillaCount, 0, MaxRowCount) * Out.VanillaOreScale);') -ne 1) { $f += '(Q4) vanillaOreScale = /Game/ solid originals over 429, clamped; target = round(count x vanillaOreScale)' }
    # Q5
    if (-not (Ordered $s @('NodeShuffleRwgSeedGas::DecideAndApply(this, Config.ModdedVanillaCountsOnRwg, GetMapMode() == ENodeShuffleMapMode::Rwg,', '    VanillaResourceCounts.GenerateKeyArray(ResourceKinds);', 'Q = FMath::Max3(Q, KindFloor, PinnedPerResource.FindRef(Kind));', 'while (QuotaSum != TargetActive && GuardCounter-- > 0)', 'NodeShuffleRwgSeedGas::Restore(ModQuota, VanillaResourceCounts, VanillaCount);', '    // 5. Choose the active set', '        if (IsGasResourcePath(Kind) && !NodeShuffleRwgSeedGas::IsSeededKind(ModQuota, Kind)) { continue; }', 'NodeShuffleRwgDeal::BuildPurityDecks(NewLayout, PurityDeckSource, SolidDeck, LiquidDeck, Rng, PurityDecks);', 'NodeShuffleRwgSeedGas::AddSeededPuritySources(ModQuota, NewLayout, FormByResource, FormSolid, Rng, PurityDecks);', 'NodeShuffleRwgSeedGas::LogRoll(ModQuota, Quota, PinnedPerResource, SolidDeck, LiquidDeck, PurityDecks);'))) { $f += '(Q5) RollLayout call order: decide, kinds, max3 share, trim, restore, step 5, seeded-only gas drop, purity, MODQUOTA' }
    if ((Count $s 'NodeShuffleRwgSeedGas::Restore(') -ne 1 -or (Count $s 'NodeShuffleRwgSeedGas::DecideAndApply(') -ne 1) { $f += '(Q5) exactly one decide and one restore call' }
    if (-not (Ordered $s @('void ANodeShuffleSubsystem::ApplyLayout()', 'RwgSpawnPass(SpawnRadiusCm, bChangedWorld); }', 'NodeShuffleRwgSeedGas::EmitSpawnCensus(this, GetMapMode() == ENodeShuffleMapMode::Rwg, Layout,'))) { $f += '(Q5) SEEDGASSPAWN census must run after the RWG spawn pass in ApplyLayout' }
    # Q6
    if ((Count $T.cfgh 'bool ModdedVanillaCountsOnRwg{true};') -ne 1 -or (Count $T.cfg 'AddBool(TEXT("ModdedVanillaCountsOnRwg"), true,') -ne 1 -or (Count $T.cfgh 'TArray<FNodeShuffleModdedCountRow> ModdedVanillaCountRows;') -ne 1) { $f += '(Q6) toggle default ON in the mirror and the panel; rows mirror present' }
    if ((Count $g 'const TCHAR* const DefaultLeadResource = TEXT("/bamrenewfp/content/Resource/RawResource/LeadOre/oreleaddesc.oreleaddesc_C");') -ne 1 -or (Count $h 'constexpr int32 DefaultLeadVanillaCount = 6;') -ne 1 -or (Count $h 'constexpr int32 DefaultLithiumVanillaCount = 23;') -ne 1 -or (Count $g 'const TCHAR* const DefaultLithiumNodeClass = TEXT("/Lithium/World/BP_ResourdeNode_Alkali.BP_ResourdeNode_Alkali_C");') -ne 1 -or (Count $g 'const TCHAR* const DefaultLithiumResource = TEXT("/Lithium/Resources/Lithium/Desc_OreLithium.Desc_OreLithium_C");') -ne 1) { $f += '(Q6) default rows must be lead 6 and lithium 23 with the Alkali class' }
    if ((Count $g '{ { DefaultLeadResource, TEXT(""), DefaultLeadVanillaCount },') -ne 1 -or (Count $g '{ DefaultLithiumResource, DefaultLithiumNodeClass, DefaultLithiumVanillaCount },') -ne 1 -or (Count $g '{ DefaultThoriumResource, TEXT(""), DefaultThoriumVanillaCount } };') -ne 1 -or (Count $h 'constexpr int32 DefaultThoriumVanillaCount = 45;') -ne 1 -or (Count $g 'const TCHAR* const DefaultThoriumResource = TEXT("/RefinedPower/World/ResourceNodes/Thorium/Desc_RP_Thorium.Desc_RP_Thorium_C");') -ne 1 -or (Count $T.cfg 'NodeShuffleRwgSeedGas::RegisterConfigRows(Root, ArrayClass, SectionClass, StringClass, IntegerClass);') -ne 1) { $f += '(Q6) all three default rows must be registered (lead and thorium with an empty class)' }
    if ((Count $g '|| static_cast<const UConfigProperty*>(K) == TplCls || N == CountProp)') -ne 1 -or (Count $g 'static_cast<const UConfigProperty*>(R) == TplRes') -ne 1) { $f += '(Q6) the default-row ownership guard must test all three sub-properties against the template' }
    # fix round 1 F3: the tooltips say registry first, row class as fallback, and use no layout words.
    if ((Count $T.cfg 'listed in the Modded Resource Vanilla Counts table gets its vanilla map node count') -ne 1 -or (Count $T.cfg 'otherwise the class listed in the row); if neither is usable, none are added.') -ne 1 -or (Count $g 'nodes of its own node class (one NodeShuffle finds in that resource''s mod, otherwise the class in the row); if ') -ne 1 -or (Count $g 'Used only when NodeShuffle finds no node class of that resource''s own mod. May be empty.') -ne 1 -or (Count $T.cfg ('table ' + 'below')) -ne 0 -or (Count $g ('toggle ' + 'above')) -ne 0) { $f += '(Q6) table tooltips must state registry-first with the row class as fallback, and carry no layout words' }
    if ((Count $g 'Manager->GetConfigurationRootSection(FConfigId{"NodeShuffle", ""})') -ne 1 -or (Count $g ('Config' + '.ModdedVanillaCountRows')) -ne 0 -or (Count $s ('Config' + '.ModdedVanillaCountRows')) -ne 0) { $f += '(Q6) rows must be read from the live config tree, never the struct mirror' }
    # Q7
    $f += Check-Log $T 'sg' 'Q7' 'MODQUOTA resource=' 'MODQUOTA resource=%s form=%s mapIsRwg=%d capturedOriginals=%d vanillaCount=%d vanillaOreOriginals=%d baseMapSolidNodes=%d vanillaOreScale=%.3f target=%d action=%s classResolvedBy=%s pinnedOrCarried=%d quota=%d deckCards=%d purityDeck=%s skipped=%s' 16
    $f += Check-Log $T 'sg' 'Q7' 'MODQUOTA-CFG ' 'MODQUOTA-CFG enabled=%d mapIsRwg=%d rowsTreeFound=%d rowsRead=%d rowsMalformed=%d vanillaOreOriginals=%d baseMapSolidNodes=%d vanillaOreScale=%.3f overridden=%d seeded=%d puritySrc=%d puritySrcImpure=%d puritySrcNormal=%d puritySrcPure=%d' 14
    $f += Check-Log $T 'sg' 'Q7' 'MODQUOTA-CLASS ' 'MODQUOTA-CLASS resource=%s configPath=%s configLoaded=%d configNode=%d configAbstract=%d configRefused=%d configOwnMount=%d configCdoResource=%s registryMountFiltered=%d registryMatched=%s chosen=%s' 11
    $f += Check-Log $T 'sg' 'Q7' 'SEEDGASSPAWN ' 'SEEDGASSPAWN resource=%s entries=%d spawnedOk=%d spawnFailed=%d parked=%d guids=%s' 6
    if (-not (Ordered $g @('void EmitSpawnCensus(', 'if (const FString* Prev = LastSig.Find(Row.Resource)) { if (*Prev == Sig) { continue; } }', 'LastSig.Add(Row.Resource, Sig);', 'TEXT("SEEDGASSPAWN resource='))) { $f += '(Q7) SEEDGASSPAWN must print only when its signature changed' }
    if ((Count $s ('TEXT("SEEDGAS ' + 'resource=')) -ne 0 -or (Count $g ('TEXT("SEEDGAS ' + 'resource=')) -ne 0) { $f += '(Q7) the pre-A1 line name must stay renamed to MODQUOTA' }
    # Q8 (fix round 1 F2, D3): seeded kinds' purity source = captured originals, /Game/ AND FormSolid (the vanillaOreScale numerator's
    # two predicates), Impure/Normal/Pure only; no RNG unless a kind was seeded; empty set keeps the map-wide deck; sorted kinds.
    if (-not (Ordered $g @('int32 AddSeededPuritySources(FState& S, const TArray<FNodeShuffleEntry>& Layout', 'if (E.bIsNewNode || !E.OriginalResourceClassPath.StartsWith(TEXT("/Game/"))', '|| FormByResource.FindRef(E.OriginalResourceClassPath) != FormSolid) { continue; }', 'if (P < 0 || P > 2) { continue; }', 'S.PuritySrc = Src.Num();', 'if (S.SeededKinds.Num() == 0 || Src.Num() == 0) { return Src.Num(); }', 'Kinds.Sort();', 'Copy.Swap(i, Rng.RandRange(0, i));', 'Purity.ByClass.Add(Kind, MoveTemp(Copy));', 'S.PurityFromGameSolid.Add(Kind);', 'void LogRoll('))) { $f += '(Q8) seeded purity source must be /Game/ solid captured originals, drawn only for seeded kinds, sorted' }
    $fnS = $g.IndexOf('int32 AddSeededPuritySources(FState& S'); $fnE = $g.IndexOf('void LogRoll(')
    if ($fnS -lt 0 -or $fnE -lt $fnS) { $f += '(Q8) AddSeededPuritySources not found before LogRoll' } else { $body = $g.Substring($fnS, $fnE - $fnS); $ret = $body.IndexOf('if (S.SeededKinds.Num() == 0 || Src.Num() == 0) { return Src.Num(); }'); $rng = $body.IndexOf('Rng.'); if ((Count $body 'Rng.') -ne 1 -or $ret -lt 0 -or $rng -lt $ret) { $f += '(Q8) the only RNG use in AddSeededPuritySources must sit after the no-seeded-kind early return' } }
    if ((Count $g 'Branch = S.PurityFromGameSolid.Contains(Res) ? TEXT("gameSolidOriginals")') -ne 1 -or (Count $g ('TEXT("' + 'aggregate")')) -ne 0) { $f += '(Q8) purityDeck must name gameSolidOriginals for seeded kinds and mapwide (never the old label) for the map-wide deck' }
    return $f
}

$fails = Invoke-SeedgasChecks $text
$mutants = @(
    @{ Name = 'X1 override also when zero originals'; Key = 'sg'; Expect = 'Q1'; Find = 'if (R.CapturedOriginals > 0)'; Repl = ('if (R.CapturedOriginals ' + '>= 0)') }
    @{ Name = 'X2 RWG gate dropped'; Key = 'sg'; Expect = 'Q1'; Find = 'if (!bMapIsRwg) { R.Skipped = TEXT("notRwg"); continue; }'; Repl = '' }
    @{ Name = 'X3 gas gate dropped'; Key = 'sg'; Expect = 'Q1'; Find = 'if (R.Form != TEXT("gas")) { R.Skipped = TEXT("notGas"); continue; }'; Repl = '' }
    @{ Name = 'X26 unloaded descriptor reported as notGas'; Key = 'sg'; Expect = 'Q1'; Find = 'if (Desc == nullptr) { R.Skipped = TEXT("descUnloaded"); continue; }'; Repl = '' }
    @{ Name = 'X27 vanilla /Game/ row accepted'; Key = 'sg'; Expect = 'Q1'; Find = 'if (Row.Resource.StartsWith(TEXT("/Game/"))) { R.Skipped = TEXT("vanillaRow"); continue; }'; Repl = '' }
    @{ Name = 'X4 seed without a class'; Key = 'sg'; Expect = 'Q1'; Find = 'if (R.ResolvedClass.IsEmpty()) { R.Skipped = TEXT("noClass"); continue; }'; Repl = '' }
    @{ Name = 'X5 class gate accepts abstract'; Key = 'sg'; Expect = 'Q2'; Find = '&& !C->HasAnyClassFlags(CLASS_Abstract)'; Repl = '' }
    @{ Name = 'X6 class gate drops own-mount'; Key = 'sg'; Expect = 'Q2'; Find = '&& NodeShuffleRepClass::IsOwnNodeClassForResource(C, Path, Res);'; Repl = ';' }
    @{ Name = 'X25 class gate admits deposits'; Key = 'sg'; Expect = 'Q2'; Find = ' && !C->IsChildOf(AFGResourceDeposit::StaticClass())'; Repl = '' }
    @{ Name = 'X25b class gate drops the CDO-resource test'; Key = 'sg'; Expect = 'Q2'; Find = 'NodeShuffleRepClass::IsOwnNodeClassForResource(C, Path, Res);'; Repl = 'NodeShuffleRepClass::IsOwnNodeClass(Path, Res);' }
    @{ Name = 'X7 config bypasses the class gate'; Key = 'sg'; Expect = 'Q2'; Find = 'if (R.ResolvedClass.IsEmpty() && AcceptOwnClass(Cfg, Row.NodeClassPath, Row.Resource, RefusedClasses))'; Repl = 'if (R.ResolvedClass.IsEmpty() && Cfg)' }
    @{ Name = 'X8 override keeps VanillaCount'; Key = 'sg'; Expect = 'Q3'; Find = 'VanillaCount += R.Target - R.CapturedOriginals;'; Repl = '' }
    @{ Name = 'X9 table writes the quota'; Key = 'sg'; Expect = 'Q3'; Find = 'R.Action = TEXT("seed");'; Repl = ('R.Action = TEXT("seed"); Quota' + '.Add(Row.Resource, R.Target);') }
    @{ Name = 'X10 restore forgets seeded kinds'; Key = 'sg'; Expect = 'Q3'; Find = 'for (const FString& K : S.SeededKinds) { VanillaResourceCounts.Remove(K); }'; Repl = '' }
    @{ Name = 'X11 vanillaOreScale unclamped'; Key = 'sg'; Expect = 'Q4'; Find = 'FMath::Clamp(static_cast<float>(Out.VanillaOreOriginals) / RwgBaseSolidNodes, VanillaOreScaleMin, VanillaOreScaleMax)'; Repl = '(static_cast<float>(Out.VanillaOreOriginals) / RwgBaseSolidNodes)' }
    @{ Name = 'X12 base-map node count changed'; Key = 'sgh'; Expect = 'Q4'; Find = 'constexpr int32 RwgBaseSolidNodes = 429;'; Repl = 'constexpr int32 RwgBaseSolidNodes = 400;' }
    @{ Name = 'X13 vanillaOreScale counts liquid'; Key = 'sg'; Expect = 'Q4'; Find = ' && FormByResource.FindRef(KV.Key) == FormSolid) { Out.VanillaOreOriginals'; Repl = ') { Out.VanillaOreOriginals' }
    @{ Name = 'X14 restore before the trim'; Key = 'sub'; Expect = 'Q5'; Find = '    // Trim or pad to hit TargetActive exactly'; Repl = ("    NodeShuffleRwgSeedGas::Restore(ModQuota, VanillaResourceCounts, VanillaCount);`n" + '    // Trim or pad to hit TargetActive exactly') }
    @{ Name = 'X15 gas drop bypassed for all'; Key = 'sub'; Expect = 'Q5'; Find = 'if (IsGasResourcePath(Kind) && !NodeShuffleRwgSeedGas::IsSeededKind(ModQuota, Kind)) { continue; }'; Repl = 'if (false) { continue; }' }
    @{ Name = 'X16 pinned floor dropped from the share'; Key = 'sub'; Expect = 'Q5'; Find = 'Q = FMath::Max3(Q, KindFloor, PinnedPerResource.FindRef(Kind));'; Repl = 'Q = FMath::Max(Q, KindFloor);' }
    @{ Name = 'X17 toggle default OFF'; Key = 'cfgh'; Expect = 'Q6'; Find = 'bool ModdedVanillaCountsOnRwg{true};'; Repl = 'bool ModdedVanillaCountsOnRwg{false};' }
    @{ Name = 'X18 panel default OFF'; Key = 'cfg'; Expect = 'Q6'; Find = 'AddBool(TEXT("ModdedVanillaCountsOnRwg"), true,'; Repl = 'AddBool(TEXT("ModdedVanillaCountsOnRwg"), false,' }
    @{ Name = 'X19 lead default count'; Key = 'sgh'; Expect = 'Q6'; Find = 'constexpr int32 DefaultLeadVanillaCount = 6;'; Repl = 'constexpr int32 DefaultLeadVanillaCount = 104;' }
    @{ Name = 'X20 lithium row dropped'; Key = 'sg'; Expect = 'Q6'; Find = '{ DefaultLithiumResource, DefaultLithiumNodeClass, DefaultLithiumVanillaCount },'; Repl = '' }
    @{ Name = 'X28 ownership guard skips NodeClassPath'; Key = 'sg'; Expect = 'Q6'; Find = '|| static_cast<const UConfigProperty*>(K) == TplCls || N == CountProp)'; Repl = '|| N == CountProp)' }
    @{ Name = 'X29 purity source admits modded'; Key = 'sg'; Expect = 'Q8'; Find = 'if (E.bIsNewNode || !E.OriginalResourceClassPath.StartsWith(TEXT("/Game/"))'; Repl = 'if (E.bIsNewNode' }
    @{ Name = 'X30 purity source admits liquid'; Key = 'sg'; Expect = 'Q8'; Find = '|| FormByResource.FindRef(E.OriginalResourceClassPath) != FormSolid) { continue; }'; Repl = ') { continue; }' }
    @{ Name = 'X31 purity RNG on every roll'; Key = 'sg'; Expect = 'Q8'; Find = 'if (S.SeededKinds.Num() == 0 || Src.Num() == 0) { return Src.Num(); }'; Repl = 'if (Src.Num() == 0) { return Src.Num(); }' }
    @{ Name = 'X32 seeded kinds keep the map-wide deck'; Key = 'sg'; Expect = 'Q8'; Find = 'Purity.ByClass.Add(Kind, MoveTemp(Copy));'; Repl = '' }
    @{ Name = 'X33 purity source call dropped'; Key = 'sub'; Expect = 'Q5'; Find = '    NodeShuffleRwgSeedGas::AddSeededPuritySources(ModQuota, NewLayout, FormByResource, FormSolid, Rng, PurityDecks);'; Repl = '' }
    @{ Name = 'X34 toggle tooltip back to listed-class-only'; Key = 'cfg'; Expect = 'Q6'; Find = 'otherwise the class listed in the row); if neither is usable, none are added.'; Repl = 'and none if that class cannot be loaded.' }
    @{ Name = 'X24 thorium count changed'; Key = 'sgh'; Expect = 'Q6'; Find = 'constexpr int32 DefaultThoriumVanillaCount = 45;'; Repl = 'constexpr int32 DefaultThoriumVanillaCount = 35;' }
    @{ Name = 'X21 MODQUOTA field dropped'; Key = 'sg'; Expect = 'Q7'; Find = ' vanillaOreScale=%.3f target=%d action=%s'; Repl = ' target=%d action=%s' }
    @{ Name = 'X22 SEEDGASSPAWN every pass'; Key = 'sg'; Expect = 'Q7'; Find = 'if (const FString* Prev = LastSig.Find(Row.Resource)) { if (*Prev == Sig) { continue; } }'; Repl = '' }
    @{ Name = 'X23 SEEDGASSPAWN loses parked'; Key = 'sg'; Expect = 'Q7'; Find = ' spawnFailed=%d parked=%d guids=%s'; Repl = ' spawnFailed=%d guids=%s' }
)
$mutantFails = @(); $caught = 0
foreach ($m in $mutants) {
    $mut = @{}; foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    $res = Invoke-SeedgasChecks $mut
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ } else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "SEEDGAS LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: SEEDGAS lint -- Q1..Q8 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
