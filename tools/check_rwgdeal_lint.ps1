# RWGDEAL lint (2026-10-19, packet rwg-deal-1): on RWG maps the card deal is slot-kind-agnostic (one merged deck, any card
# into any active slot, the card decides the form, no-class-no-look cards keep the old restriction); Vanilla maps keep the
# FIX B deal byte for byte. Spec: _team/test-logs/2026-10-19-nodeshuffle-inactive-entries.md. LOADS and PINS every file touched.
# Needles naming a forbidden or superseded literal are BUILT BY CONCATENATION so this file cannot match itself.
#   G1  Rwg gate: bRwgDeal := GetMapMode() == Rwg; Deal() has exactly one caller, inside `if (bRwgDeal)`, and the Vanilla loop is
#       the `else` body.
#   G2  Vanilla byte-identical: the Vanilla deal loop hashes to the bf071d6 text; the FindVanilla block is pinned verbatim; the only
#       RNG uses between the liquid-deck shuffle and the Vanilla loop are the purity shuffle and the Deal() call (Vanilla stream intact).
#   G3  Merged-deck draw: solid + liquid appended into one deck, shuffled inside Deal(), uniform draw in [Cursor, End) with bounded
#       rejection; an existing slot rejects a Dealable card of the other form ONLY when no spawn class exists for that form.
#   G4  No-class-no-look rule: Dealable = generic || rank-2 own class || look; a non-Dealable card goes only to an existing slot of
#       its own form; the kind table reads the rank-2 predicate and ResourceHasAuthoredLook. F1: the kind table and the Phase-2 RWG
#       gate also require the own class loaded, an AFGResourceNode and not abstract (as the conversion restamp does); the registry
#       fallback (anchored on its MatchByRegistry call, not a comment) runs before the kind table.
#   G5  Form follows the card: ResourceForm := card form for every dealt slot; FormChanged recorded; the conversion FORM RESTAMP writes
#       the form representative into RepClassSpawnPath (after the rank-2 restamp); Phase-2 sends an RWG modded card without a
#       rank-2 class to the form representative BEFORE the resource-keyed stamp.
#   G6  Census: RWGDEAL (10 %d), RWGDEAL-RES, RWGDEAL-SLOTS, RWGDEAL-STAMP exact formats, Display, specifier count == argument count,
#       no repeated field; the Vanilla FIX B line keeps its exact text and RWG prints the claim-free variant.
#   G7  Marker rwg-steep-2 present once; rwg-steep-1, rwg-deal-1 and rwg-density-2 absent.
#   G8  deck-substitute Rung 3 reads the class the entry spawns (PreSubstituteSpawnPath) on RWG, NodeClassPath on Vanilla (F7).
#   G9  Re-roll rebuild: on RWG the rebuilt slot form, FormByResource and the representative latch read the ORIGINAL resource's
#       descriptor form (RebuildForm), never the saved card form; a class that does not load is counted (originalLoadNull);
#       RWGDEAL-REROLL census 3/3.
# Mutants run in-file. Exit 0 = all hold; 1 = at least one does not; 2 = a pinned FILE is missing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'
$files = [ordered]@{
    sub   = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    deal  = 'NodeShuffle\Private\NodeShuffleRwgDeal.cpp'
    dealh = 'NodeShuffle\Private\NodeShuffleRwgDeal.h'
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
$VanillaLoopSha = '20652f80bfc592fe11541a00508e9528024fb037130eba3f68c9f64204a4b069' # bf071d6 (rwg-density-2) Vanilla deal loop, LF

function Invoke-RwgdealChecks($T) {
    $f = @(); $s = $T.sub; $d = $T.deal; $h = $T.dealh
    # G1
    if ((Count $s 'const bool bRwgDeal = (GetMapMode() == ENodeShuffleMapMode::Rwg);') -ne 1) { $f += '(G1) the RWG gate must be GetMapMode() == Rwg' }
    if ((Count $s 'NodeShuffleRwgDeal::Deal(') -ne 1 -or -not (Ordered $s @('const bool bRwgDeal = (GetMapMode() == ENodeShuffleMapMode::Rwg);', "    if (bRwgDeal)`n    {", 'NodeShuffleRwgDeal::Deal(NewLayout, In, Rng, RwgDeal);', "    }`n    else // Vanilla map:", "`n    for (FNodeShuffleEntry& E : NewLayout)`n    {`n        if (!E.bActive || E.bPinned)"))) { $f += '(G1) Deal() must have one caller inside if (bRwgDeal), with the Vanilla loop as the else body' }
    # G2
    $a = $s.IndexOf('    else // Vanilla map:')
    $b = if ($a -ge 0) { $s.IndexOf("    for (FNodeShuffleEntry& E : NewLayout)`n    {`n        if (!E.bActive || E.bPinned)", $a) } else { -1 }
    $e = if ($b -ge 0) { $s.IndexOf("        PurityIdx++;`n    }`n", $b) } else { -1 }
    if ($e -lt 0) { $f += '(G2) Vanilla deal loop not found after the else' }
    else {
        $blk = $s.Substring($b, $e + "        PurityIdx++;`n    }`n".Length - $b)
        $sha = [Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($blk))).ToLower()
        if ($sha -ne $VanillaLoopSha) { $f += "(G2) the Vanilla deal loop changed (sha $sha) -- it must stay byte-identical to bf071d6" }
    }
    $fv = "            auto FindVanilla = [&](TArray<FString>& Pile, int32& Idx) -> FString`n            {`n                for (int32 j = Idx; j < Pile.Num(); j++)`n                {`n                    if (Pile[j].StartsWith(TEXT(`"/Game/`")))`n                    {`n                        Pile.Swap(Idx, j);`n                        return Pile[Idx];`n                    }`n                }`n                return FString();`n            };`n            Card = FindVanilla(SolidDeck, SolidIdx);`n            if (!Card.IsEmpty())`n            {`n                SolidIdx++;`n            }`n            else`n            {`n                Card = FindVanilla(LiquidDeck, LiquidIdx);`n                if (!Card.IsEmpty()) { LiquidIdx++; }`n            }`n            if (Card.IsEmpty())`n            {`n                E.bActive = false; // no vanilla cards of any form left: skip this new node`n                continue;`n            }"
    if ((Count $s $fv) -ne 1) { $f += '(G2) the Vanilla FindVanilla block must be present verbatim, once' }
    $r0 = $s.IndexOf('for (int32 i = LiquidDeck.Num() - 1; i > 0; i--)')
    if ($r0 -lt 0 -or $a -lt $r0) { $f += '(G2) liquid-deck shuffle / Vanilla else anchors missing' }
    else { $mid = $s.Substring($r0, $a - $r0); if ((Count $mid 'Rng') -ne 3) { $f += "(G2) RNG use between the deck shuffles and the Vanilla loop must be exactly the liquid shuffle, the purity shuffle and the Deal() call (found $(Count $mid 'Rng'))" } }
    # G3
    if (-not (Ordered $d @('Deck.Append(*In.SolidDeck);', 'Deck.Append(*In.LiquidDeck);', 'for (int32 i = Deck.Num() - 1; i > 0; i--) { Deck.Swap(i, Rng.RandRange(0, i)); }'))) { $f += '(G3) Deal() must append both piles into one deck and shuffle it' }
    if (-not (Ordered $d @('int32 End = Deck.Num();', 'while (Cursor < End)', 'const int32 j = Rng.RandRange(Cursor, End - 1);', 'if (bOk) { Deck.Swap(Cursor, j); Card = Deck[Cursor]; Cursor++; break; }', 'Deck.Swap(j, End - 1);', 'End--;'))) { $f += '(G3) uniform draw in [Cursor, End) with bounded rejection' }
    if ((Count $d 'else if (!bNew && K.Form != SlotForm && !HaveRep(K.Form))') -ne 1 -or (Count $d 'SolidIdx') -ne 0 -or (Count $d 'LiquidIdx') -ne 0) { $f += '(G3) an existing slot may reject the other form only when no spawn class exists for it; no per-pile cursors' }
    if (-not (Ordered $s @('In.SolidDeck = &SolidDeck;', 'In.LiquidDeck = &LiquidDeck;', 'In.PurityDeck = &PurityDeck;'))) { $f += '(G3) both piles and the purity deck must reach Deal()' }
    # G4
    if ((Count $h 'bool Dealable() const { return bGeneric || bOwnClass || bLook; }') -ne 1) { $f += '(G4) Dealable must be generic || own class || look' }
    if (-not (Ordered $d @('else if (!K.Dealable())', 'bOk = !bNew && K.Form == SlotForm;', 'if (!bOk) { bRuleRejected = true; }'))) { $f += '(G4) a no-class-no-look card goes only to an existing slot of its own form' }
    if (-not (Ordered $s @('K.Form = CardFormOf(Kind);', 'K.bGeneric = NodeShuffleRepClass::IsGenericNodeClassPath(Kind);', 'const UClass* LatchedCls = Latched ? FindObject<UClass>(nullptr, **Latched) : nullptr;', "K.bOwnClass = Latched && NodeShuffleRepClass::IsOwnNodeClass(*Latched, Kind)`n                && LatchedCls && LatchedCls->IsChildOf(AFGResourceNode::StaticClass()) && !LatchedCls->HasAnyClassFlags(CLASS_Abstract);", 'K.bLook = ResourceHasAuthoredLook(LoadClassByPath(Kind));'))) { $f += '(G4) kind table must read form, generic, rank-2 own class (loaded, AFGResourceNode, not abstract) and authored look' }
    if ((Count $s 'if (!RwgLatched || !NodeShuffleRepClass::IsOwnNodeClass(*RwgLatched, E.AssignedResourceClassPath) || !RwgLatchedCls || !RwgLatchedCls->IsChildOf(AFGResourceNode::StaticClass()) || RwgLatchedCls->HasAnyClassFlags(CLASS_Abstract))') -ne 1 -or -not (Ordered $s @('const UClass* RwgLatchedCls = RwgLatched ? FindObject<UClass>(nullptr, **RwgLatched) : nullptr;', 'if (!RwgLatched || !NodeShuffleRepClass::IsOwnNodeClass(*RwgLatched, E.AssignedResourceClassPath) || !RwgLatchedCls || !RwgLatchedCls->IsChildOf(AFGResourceNode::StaticClass()) || RwgLatchedCls->HasAnyClassFlags(CLASS_Abstract))', '++RwgNewFormRepStamps;'))) { $f += '(G4) Phase-2 RWG gate: an own class that is not loaded, not an AFGResourceNode or abstract takes the form representative' }
    if ((Count $s 'NodeShuffleRepClass::MatchByRegistry(Res, RegQuery') -ne 1 -or -not (Ordered $s @('NodeShuffleRepClass::MatchByRegistry(Res, RegQuery', 'TEXT("REPCLASS res=', 'K.bOwnClass = Latched'))) { $f += '(G4) the registry fallback must run before the kind table reads the rank-2 class' }
    # G5
    if ((Count $d 'E.ResourceForm = K.Form; // form follows the card') -ne 1 -or (Count $d 'Out.FormChanged.Add(E.EntryGuid);') -ne 1) { $f += '(G5) ResourceForm must follow the card and form changes must be recorded' }
    if (-not (Ordered $s @('else { ConvKept++; PerRes.Value++; }', 'if (bRwgDeal && RwgDeal.FormChanged.Contains(E.EntryGuid))', 'const FString& FormRep = (E.ResourceForm == FormLiquid) ? SpawnableLiquidNodeClassPath : SpawnableNodeClassPath;', 'if (!E.RepClassSpawnPath.IsEmpty()) { RwgConvFormOwnClass++; }', 'E.RepClassSpawnPath = FormRep;', 'Converted++;'))) { $f += '(G5) conversion FORM RESTAMP must follow the rank-2 restamp and write the form representative' }
    if (-not (Ordered $s @('    int32 RwgNewFormRepStamps = 0;', 'if (bRwgDeal && !NodeShuffleRepClass::IsGenericNodeClassPath(E.AssignedResourceClassPath))', '? SpawnableLiquidNodeClassPath : SpawnableNodeClassPath;', '++RwgNewFormRepStamps;', 'continue;', 'if (const FString* ByResource = NodeClassByResource.Find(E.AssignedResourceClassPath))'))) { $f += '(G5) Phase-2: an RWG modded card without a rank-2 class takes the form representative before the resource-keyed stamp' }
    # G6
    $f += Check-Log $T 'deal' 'G6' 'RWGDEAL: ' 'RWGDEAL: activeSlots=%d cards=%d dealt=%d vanillaCards=%d moddedCards=%d undealtCards=%d slotsLeftInactiveNoCard=%d formChangedSolidToLiquid=%d liquidToSolid=%d noClassNoLookRejectSlots=%d' 10
    $f += Check-Log $T 'deal' 'G6' 'RWGDEAL-RES ' 'RWGDEAL-RES res=%s cards=%d dealt=%d dealable=%d ownClass=%d look=%d' 6
    $f += Check-Log $T 'deal' 'G6' 'RWGDEAL-SLOTS ' 'RWGDEAL-SLOTS existingSlots=%d newSlots=%d existingKeptNoCard=%d reserveForced=%d formChangeNoRepRejects=%d' 5
    $f += Check-Log $T 'sub' 'G6' 'RWGDEAL-STAMP ' 'RWGDEAL-STAMP newFormRep=%d convFormChanged=%d convFormRestamped=%d convFormOwnClass=%d convFormKept=%d' 5
    if ((Count $s ('(no liquid card on a solid slot by ' + 'construction)"),')) -ne 1 -or (Count $s '(rwg: form follows the card)"),') -ne 1 -or -not (Ordered $s @('        if (bRwgDeal)', '(rwg: form follows the card)"),', '        else', ('(no liquid card on a solid slot by ' + 'construction)"),')))) { $f += '(G6) FIX B line: Vanilla keeps its exact text in the else branch; RWG prints the claim-free variant' }
    # G7
    if ((Count $T.nsmod ('LOADED (2026-10-19-rwg-' + 'steep-2)')) -ne 1 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'steep-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'deal-1)')) -ne 0 -or (Count $T.nsmod ('2026-10-19-rwg-' + 'density-2')) -ne 0) { $f += '(G7) marker must be rwg-steep-2 (rwg-steep-1 and rwg-deal-1 absent) with rwg-density-2 absent' }
    # G8
    if (-not (Ordered $s @('// Rung 3: scan the layout', 'const FString& Cand = (GetMapMode() == ENodeShuffleMapMode::Rwg) ? PreSubstituteSpawnPath(E) : E.NodeClassPath;', 'if (!Cand.StartsWith(TEXT("/Game/"))) { continue; }', 'if (!IsEligible(Cand)) { continue; }', 'return Cand;', '// Rung 4 (SOLID ONLY)'))) { $f += '(G8) Rung 3 must read the class the entry spawns (PreSubstituteSpawnPath) on RWG and NodeClassPath on Vanilla' }
    # G9
    if (-not (Ordered $s @('uint8 RebuildForm = Old.ResourceForm;', 'if (GetMapMode() == ENodeShuffleMapMode::Rwg)', 'if (UClass* OrigRC = LoadClassByPath(Old.OriginalResourceClassPath))', '== EResourceForm::RF_LIQUID)', '? FormLiquid : FormSolid;', 'else { RwgRebuildFormLoadNull++; }', 'E.ResourceForm = RebuildForm;', 'RebuildForm != 0 ? RebuildForm : FormSolid;', 'if (RebuildForm == FormLiquid)', 'LatchRepresentative(Old.NodeClassPath, SpawnableLiquidNodeClassPath'))) { $f += '(G9) re-roll rebuild must derive the slot form from the original resource on RWG and use it for the entry, FormByResource and the latch' }
    if ((Count $s ('Old.' + 'ResourceForm')) -ne 2) { $f += '(G9) Old.ResourceForm may be read only as the RebuildForm default and the restored-count compare' }
    $f += Check-Log $T 'sub' 'G9' 'RWGDEAL-REROLL ' 'RWGDEAL-REROLL rebuilt=%d slotFormRestoredFromOriginal=%d originalLoadNull=%d' 3
    return $f
}

$fails = Invoke-RwgdealChecks $text
$mutants = @(
    @{ Name = 'A1 drop the Rwg gate'; Key = 'sub'; Expect = 'G1'; Find = 'const bool bRwgDeal = (GetMapMode() == ENodeShuffleMapMode::Rwg);'; Repl = 'const bool bRwgDeal = true;' }
    @{ Name = 'A2 Vanilla FindVanilla accepts modded'; Key = 'sub'; Expect = 'G2'; Find = '                    if (Pile[j].StartsWith(TEXT("/Game/")))'; Repl = '                    if (true)' }
    @{ Name = 'A3 extra RNG draw on the Vanilla path'; Key = 'sub'; Expect = 'G2'; Find = '    NodeShuffleRwgDeal::FResult RwgDeal;'; Repl = '    NodeShuffleRwgDeal::FResult RwgDeal; Rng.RandRange(0, 1);' }
    @{ Name = 'A4 re-split the deck'; Key = 'deal'; Expect = 'G3'; Find = 'Deck.Append(*In.LiquidDeck);'; Repl = '' }
    @{ Name = 'A5 merged deck not shuffled'; Key = 'deal'; Expect = 'G3'; Find = 'for (int32 i = Deck.Num() - 1; i > 0; i--) { Deck.Swap(i, Rng.RandRange(0, i)); }'; Repl = '' }
    @{ Name = 'A6 existing slots form-matched again'; Key = 'deal'; Expect = 'G3'; Find = 'else if (!bNew && K.Form != SlotForm && !HaveRep(K.Form))'; Repl = 'else if (!bNew && K.Form != SlotForm)' }
    @{ Name = 'A7 drop the no-class-no-look rule'; Key = 'deal'; Expect = 'G4'; Find = 'bOk = !bNew && K.Form == SlotForm;'; Repl = 'bOk = true;' }
    @{ Name = 'A8 Dealable ignores the look'; Key = 'dealh'; Expect = 'G4'; Find = 'return bGeneric || bOwnClass || bLook;'; Repl = 'return bGeneric || bOwnClass;' }
    @{ Name = 'A9 any latched class counts as own'; Key = 'sub'; Expect = 'G4'; Find = "K.bOwnClass = Latched && NodeShuffleRepClass::IsOwnNodeClass(*Latched, Kind)`n                && LatchedCls && LatchedCls->IsChildOf(AFGResourceNode::StaticClass()) && !LatchedCls->HasAnyClassFlags(CLASS_Abstract);"; Repl = 'K.bOwnClass = Latched != nullptr;' }
    @{ Name = 'A22 kind table drops the loaded/type test'; Key = 'sub'; Expect = 'G4'; Find = "K.bOwnClass = Latched && NodeShuffleRepClass::IsOwnNodeClass(*Latched, Kind)`n                && LatchedCls && LatchedCls->IsChildOf(AFGResourceNode::StaticClass()) && !LatchedCls->HasAnyClassFlags(CLASS_Abstract);"; Repl = 'K.bOwnClass = Latched && NodeShuffleRepClass::IsOwnNodeClass(*Latched, Kind);' }
    @{ Name = 'A23 Phase-2 drops the loaded/type test'; Key = 'sub'; Expect = 'G4'; Find = 'if (!RwgLatched || !NodeShuffleRepClass::IsOwnNodeClass(*RwgLatched, E.AssignedResourceClassPath) || !RwgLatchedCls || !RwgLatchedCls->IsChildOf(AFGResourceNode::StaticClass()) || RwgLatchedCls->HasAnyClassFlags(CLASS_Abstract))'; Repl = 'if (!RwgLatched || !NodeShuffleRepClass::IsOwnNodeClass(*RwgLatched, E.AssignedResourceClassPath))' }
    @{ Name = 'A10 revert form-follows-card'; Key = 'deal'; Expect = 'G5'; Find = 'E.ResourceForm = K.Form; // form follows the card'; Repl = '' }
    @{ Name = 'A11 drop the conversion form restamp'; Key = 'sub'; Expect = 'G5'; Find = 'E.RepClassSpawnPath = FormRep;'; Repl = '' }
    @{ Name = 'A12 drop the Phase-2 RWG gate'; Key = 'sub'; Expect = 'G5'; Find = 'if (bRwgDeal && !NodeShuffleRepClass::IsGenericNodeClassPath(E.AssignedResourceClassPath))'; Repl = 'if (false)' }
    @{ Name = 'A13 RWGDEAL drops an argument'; Key = 'deal'; Expect = 'G6'; Find = 'Out.SolidToLiquid, Out.LiquidToSolid, Out.VanillaOnlyFallbacks);'; Repl = 'Out.SolidToLiquid, Out.LiquidToSolid);' }
    @{ Name = 'A14 RWGDEAL-RES drops look'; Key = 'deal'; Expect = 'G6'; Find = ' ownClass=%d look=%d"),'; Repl = ' ownClass=%d"),' }
    @{ Name = 'A15 RWG FIX B line keeps the false claim'; Key = 'sub'; Expect = 'G6'; Find = '(rwg: form follows the card)"),'; Repl = ('(no liquid card on a solid slot by ' + 'construction)"),') }
    @{ Name = 'A16 marker reverted'; Key = 'nsmod'; Expect = 'G7'; Find = ('2026-10-19-rwg-' + 'steep-2'); Repl = ('2026-10-19-rwg-' + 'density-2') }
    @{ Name = 'A17 Rung 3 reads NodeClassPath on RWG'; Key = 'sub'; Expect = 'G8'; Find = '? PreSubstituteSpawnPath(E) : E.NodeClassPath;'; Repl = '? E.NodeClassPath : E.NodeClassPath;' }
    @{ Name = 'A19 rebuild entry form from the saved card'; Key = 'sub'; Expect = 'G9'; Find = 'E.ResourceForm = RebuildForm;'; Repl = ('E.ResourceForm = Old.' + 'ResourceForm;') }
    @{ Name = 'A20 rebuild latch from the saved card form'; Key = 'sub'; Expect = 'G9'; Find = '                if (RebuildForm == FormLiquid)'; Repl = ('                if (Old.' + 'ResourceForm == FormLiquid)') }
    @{ Name = 'A21 rebuild form not derived on RWG'; Key = 'sub'; Expect = 'G9'; Find = 'if (UClass* OrigRC = LoadClassByPath(Old.OriginalResourceClassPath))'; Repl = 'if (UClass* OrigRC = nullptr)' }
    @{ Name = 'A24 load-null rebuild uncounted'; Key = 'sub'; Expect = 'G9'; Find = 'else { RwgRebuildFormLoadNull++; }'; Repl = '' }
    @{ Name = 'A18 registry fallback after the kind table'; Key = 'sub'; Expect = 'G4'; Find = 'NodeShuffleRepClass::MatchByRegistry(Res, RegQuery'; Repl = 'NodeShuffleRepClass::MatchByRegistryMovedOut(Res, RegQuery'; Find2 = 'K.bLook = ResourceHasAuthoredLook(LoadClassByPath(Kind));'; Repl2 = 'K.bLook = ResourceHasAuthoredLook(LoadClassByPath(Kind)); NodeShuffleRepClass::MatchByRegistry(Res, RegQuery' }
)
$mutantFails = @(); $caught = 0
foreach ($m in $mutants) {
    $mut = @{}; foreach ($k in $text.Keys) { $mut[$k] = $text[$k] }
    if (-not $mut[$m.Key].Contains($m.Find)) { $mutantFails += "FAIL(mutation): '$($m.Name)' cannot be applied -- anchor absent from '$($m.Key)' (a mutant that does not mutate is a vacuous pass)"; continue }
    $mut[$m.Key] = $mut[$m.Key].Replace($m.Find, $m.Repl)
    if ($m.Find2) { if (-not $mut[$m.Key].Contains($m.Find2)) { $mutantFails += "FAIL(mutation): '$($m.Name)' second anchor absent"; continue }; $mut[$m.Key] = $mut[$m.Key].Replace($m.Find2, $m.Repl2) }
    $res = Invoke-RwgdealChecks $mut
    if ($res | Where-Object { $_ -like "*($($m.Expect))*" }) { $caught++ } else { $mutantFails += "FAIL(mutation): '$($m.Name)' was NOT caught by any $($m.Expect) pin" }
}
$all = @($fails) + @($mutantFails)
if ($all.Count -gt 0) {
    foreach ($x in $all) { Write-Host "FAIL: $x" }
    Write-Host "RWGDEAL LINT: FAIL -- $($all.Count) problem(s). Mutants caught $caught/$($mutants.Count)."
    exit 1
}
Write-Host "PASS: RWGDEAL lint -- G1..G9 hold across $($text.Count) loaded files; mutants caught $caught/$($mutants.Count)."
exit 0
