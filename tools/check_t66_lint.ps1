# ns-t66-probe-self-ignore -- THE LINT FOR THE PROBE-COMMAND GATE-LEG SELF-IGNORE.
#
# THE DEFECT THIS PINS AGAINST. NodeShuffle.ProbeNearestNode (NODEPROBE) replays the shipped 8-ray
# enclosure gate at a layout entry's own RECORDED CENTRE. When that entry already has a live spawned
# actor standing there, every ray died at 0 cm on the entry's OWN actor -- measured 2026-08-11 as 8/8
# blocked on the probed entry's own live node, a false REFUSE that is a probe-fidelity artifact, not a
# gate defect (the real placement-time gate runs before any node exists at the candidate spot). The fix
# (ns-t66) adds the probed entry's own live actor to the gate leg's ignore list, mirroring what the
# containment instrument already did with its own Subject argument, for BOTH probe commands that replay
# this predicate at a specific entry/member's own location: NodeShuffle.ProbeNearestNode (NODEPROBE) and
# NodeShuffle.WellProbe (WELLPROBE, per member -- P2 on the T66 packet measured this leg IS exposed the
# same way, because MemberLoc is the live actor's own transform whenever a member resolved from one).
#
# THE SHARED PREDICATE ITSELF (NodeShuffleWellFootprint.cpp's IsSpotEnclosed) GAINED A NEW OPTIONAL
# TRAILING PARAMETER, IgnoreActor2, defaulting to nullptr -- same defaulting rule ns-t36-probefix already
# established for IgnoreActor. Both placement call sites (NodeShuffleSubsystem.cpp's EnsureNewNodeSpawned
# lambda and NodeShuffleWellFootprint.cpp's well gate six) stay 3-arg and pass nothing for it, so
# placement behaviour is bit-identical to before this packet -- THAT PARITY IS ITSELF A PIN below.
#
# EACH GATE-LEG VERDICT LINE NOW CARRIES ignoredOwnActor=%s, READ BACK FROM THE SAME VARIABLE THE CALL
# ABOVE IT WAS ACTUALLY GIVEN (never a separately-typed claim), printing "none" when this entry/member had
# no live actor this run -- the one case where the reading is unchanged from pre-T66.
#
# THE EDITS IT PINS, each a genuine one-liner someone would write under pressure:
#   1. DROP THE 7TH ARGUMENT FROM EITHER GATE CALL while leaving everything else (including the
#      ignoredOwnActor log field) in place. This is the dangerous one: the log line still CLAIMS an
#      exclusion, but the call no longer applies it, so a reader trusts a field that is now lying about
#      what the trace actually ignored.
#   2. DELETE IgnoreActor2 FROM THE SHARED PREDICATE'S DECLARATION OR ITS EncParams.AddIgnoredActor CALL
#      while leaving callers passing it -- the argument becomes silently inert (compiles, changes
#      nothing) rather than failing to build.
#   3. HARDCODE THE ignoredOwnActor FIELD TO A FIXED STRING instead of reading back from the same pointer
#      the call was given -- the diagnostics-must-not-assert-a-cause rule wearing a readback's clothes.
#   4. SOURCE THE SELF-IGNORE FROM THE WRONG ACTOR (e.g. the pawn, or an un-validity-checked pointer) --
#      a copy-paste that compiles and looks identical in a diff review.
#   5. LET A "TIDY-UP" TOUCH EITHER PLACEMENT CALL SITE (add the pawn or the new param "for consistency")
#      -- the packet's explicit boundary is that the GATE ITSELF (placement path) is untouched.
#   6. CHANGE THE "no live actor" FALLBACK TOKEN away from "none" -- breaks the stated parity contract
#      that a no-live-actor reading prints identically to the pre-T66 build.
#
# IT READS THE FILES ON DISK, NOT `git show` -- same reason as check_t54/t59/t61/t64/t65_lint.ps1: a
# git-backed read goes vacuous on exactly the uncommitted tree it exists to check.
#
# IT STRIPS `//` COMMENTS BEFORE MATCHING, preserving line numbers by blanking rather than removing.
#
# NON-VACUITY IS CHECKED FOR EVERY HALF: each ban is paired with an assertion that the thing being
# protected still EXISTS, so a rename or a deletion cannot turn a ban into a silent pass.
#
# SILENT ON PASS. Exit 0 = every pin holds. Exit 1 = at least one does not, one line each naming
# file+pattern+why.
#
# MUTATION TEST (in memory, no files written):  pwsh -File tools\check_t66_lint.ps1 -MutationTest

param([switch]$MutationTest)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$hFile          = 'NodeShuffleSubsystem.h'
$footprintFile  = 'NodeShuffleWellFootprint.cpp'
$nodeProbeFile  = 'NodeShuffleNodeProbe.cpp'
$wellProbeFile  = 'NodeShuffleWellMemberProbe.cpp'
$subsystemFile  = 'NodeShuffleSubsystem.cpp'
$auditFile      = 'NodeShuffleAuditPlacements.cpp'

$hPath          = Join-Path $root ('Source\NodeShuffle\Public\' + $hFile)
$footprintPath  = Join-Path $root ('Source\NodeShuffle\Private\' + $footprintFile)
$nodeProbePath  = Join-Path $root ('Source\NodeShuffle\Private\' + $nodeProbeFile)
$wellProbePath  = Join-Path $root ('Source\NodeShuffle\Private\' + $wellProbeFile)
$subsystemPath  = Join-Path $root ('Source\NodeShuffle\Private\' + $subsystemFile)
$auditPath      = Join-Path $root ('Source\NodeShuffle\Private\' + $auditFile)

function Remove-LineComments([string]$text) {
    ($text -split "`n" | ForEach-Object { $_ -replace '//.*$', '' }) -join "`n"
}

# Returns an array of failure strings. Empty array = every pin holds.
function Get-T66Failures([string]$hText, [string]$footprintText, [string]$nodeProbeText,
                         [string]$wellProbeText, [string]$subsystemText, [string]$auditText) {
    $fails = @()
    $h          = Remove-LineComments $hText
    $footprint  = Remove-LineComments $footprintText
    $nodeProbe  = Remove-LineComments $nodeProbeText
    $wellProbe  = Remove-LineComments $wellProbeText
    $subsystem  = Remove-LineComments $subsystemText
    $audit      = Remove-LineComments $auditText

    # ---- HALF 1: the shared predicate's declaration and body actually gained IgnoreActor2 -----------
    # T66 cold review F2 aftermath: the header now declares IgnoreActor2 on TWO functions (IsSpotEnclosed
    # and IsProbeEyeInsideSolidForDiag), so each pin anchors on its own function name -- a bare
    # trailing-param match is satisfiable by the other declaration (M4 MISSED, caught 2026-08-11).
    if ($h -notmatch 'IsSpotEnclosed\([^;]*IgnoreActor2\s*=\s*nullptr\)\s*const;') {
        $fails += "FAIL: $hFile no longer declares IsSpotEnclosed's IgnoreActor2 trailing optional param -- half 1 is vacuous, every gate-leg call below relies on this parameter existing."
    }
    if ($h -notmatch 'IsProbeEyeInsideSolidForDiag\([^;]*IgnoreActor2\s*=\s*nullptr\)\s*const;') {
        $fails += "FAIL: $hFile no longer declares IsProbeEyeInsideSolidForDiag's IgnoreActor2 trailing optional param -- the eye reading loses its self-ignore (cold review F2) and a member probed at its own live actor reads 'eye inside solid' from its own collision."
    }
    if ($footprint -notmatch 'IsSpotEnclosed\([^{]*IgnoreActor2\)\s*const\s*\r?\n\{') {
        $fails += "FAIL: $footprintFile's IsSpotEnclosed definition no longer takes IgnoreActor2 as its last parameter -- the declaration and the definition have drifted apart, which does not compile."
    }
    if ($footprint -notmatch 'if\s*\(IgnoreActor2\)\s*\{\s*EyeParams\.AddIgnoredActor\(IgnoreActor2\);\s*\}') {
        $fails += "FAIL: $footprintFile no longer adds IgnoreActor2 to EyeParams when it is non-null -- the eye-reading parameter is declared but inert (cold review F2): callers can pass an actor and it changes nothing, which compiles clean and fails silently in the game."
    }
    if ($footprint -notmatch 'if\s*\(IgnoreActor2\)\s*\{\s*EncParams\.AddIgnoredActor\(IgnoreActor2\);\s*\}') {
        $fails += "FAIL: $footprintFile no longer adds IgnoreActor2 to EncParams when it is non-null -- the parameter is declared but inert: callers can pass an actor and it changes nothing, which compiles clean and fails silently in the game."
    }
    # The original IgnoreActor line must survive alongside it -- this is an ADDITION, not a replacement.
    if ($footprint -notmatch 'if\s*\(IgnoreActor\)\s*\{\s*EncParams\.AddIgnoredActor\(IgnoreActor\);\s*\}') {
        $fails += "FAIL: $footprintFile no longer adds the original IgnoreActor (the T36 pawn exclusion) to EncParams -- ns-t66 must ADD a second exclusion, not replace the first; NodeShuffle.Here's own capsule would start blocking its rays again."
    }

    # ---- HALF 2: NODEPROBE's gate leg passes its own Subject as the second ignore actor ---------------
    $nodeGateCall = Get-AllLineNumbers ($nodeProbe -split "`n") 'IsSpotEnclosed\(E\.Location, Blocked, Total, &Rays, &Threshold, Pawn'
    if ($nodeGateCall.Count -eq 0) {
        $fails += "FAIL: $nodeProbeFile no longer calls IsSpotEnclosed with the recorder+threshold+pawn shape at all -- half 2 is vacuous."
    } elseif ($nodeProbe -notmatch 'IsSpotEnclosed\(E\.Location, Blocked, Total, &Rays, &Threshold, Pawn, Subject\)') {
        $fails += "FAIL: $nodeProbeFile's gate-leg call no longer passes Subject as the 7th argument. This is THE T66 FIX ITSELF: without it, a probe at an entry with a live actor self-hits that actor at 0 cm on every ray again, exactly the defect measured 2026-08-11 (8/8 blocked on the probed entry's own actor)."
    }
    if ($nodeProbe -notmatch 'IgnoredOwnActorTok\s*=\s*Subject\s*\?\s*\*SubjectName\s*:\s*TEXT\("none"\)') {
        $fails += "FAIL: $nodeProbeFile's ignoredOwnActor readback is no longer computed as Subject ? *SubjectName : 'none' -- either it has been hardcoded (a stated cause instead of a measured one) or decoupled from the Subject pointer the gate call actually used, so the log field could disagree with what was really excluded."
    }
    if ($nodeProbe -notmatch 'ignoredOwnActor=%s') {
        $fails += "FAIL: $nodeProbeFile's ENCLOSURE GATE verdict line no longer carries an ignoredOwnActor=%s field -- the probe output no longer says what this run excluded."
    }

    # ---- HALF 3: WELLPROBE's per-member gate leg passes its own live actor as the second ignore actor -
    if ($wellProbe -notmatch 'GateSelfIgnore\s*=\s*IsValid\(LiveActor\)\s*\?\s*LiveActor\s*:\s*nullptr') {
        $fails += "FAIL: $wellProbeFile no longer derives GateSelfIgnore from IsValid(LiveActor) ? LiveActor : nullptr -- either the source actor changed (e.g. to the pawn, which would be a silent duplicate of the existing pawn exclusion and not a self-ignore at all) or the IsValid guard was dropped (a stale/pending-kill pointer handed to the trace)."
    }
    if ($wellProbe -notmatch 'IsSpotEnclosed\(MemberLoc, Blocked, Total, &Rays, &Threshold, Pawn, GateSelfIgnore\)') {
        $fails += "FAIL: $wellProbeFile's per-member gate-leg call no longer passes GateSelfIgnore as the 7th argument. This is THE T66 FIX for WELLPROBE: without it, a member probed at its own live actor's transform self-hits that actor at 0 cm on every ray again -- the P2 exposure the T66 packet measured and covered."
    }
    if ($wellProbe -notmatch 'GateSelfIgnoreName\s*=\s*GateSelfIgnore\s*\r?\n\s*\?\s*GateSelfIgnore->GetName\(\)\s*\r?\n\s*:\s*FString\(TEXT\("none"\)\)') {
        $fails += "FAIL: $wellProbeFile's GateSelfIgnoreName is no longer computed from the SAME GateSelfIgnore pointer the gate call was given -- if it drifts to a different source the log's ignoredOwnActor field can disagree with what was actually excluded this run."
    }
    # T66 cold review F2 aftermath: the file now carries TWO ignoredOwnActor=%s fields (gate verdict +
    # eye reading), so a bare existence check is satisfiable by either alone and a delete-one mutant
    # slips through (M12 MISSED, caught 2026-08-11). Anchor each field to its own sentence context.
    if ($wellProbe -notmatch 'ignoredOwnActor=%s\. The threshold') {
        $fails += "FAIL: $wellProbeFile's per-member ENCLOSURE GATE verdict line no longer carries an ignoredOwnActor=%s field -- the probe output no longer says what this run excluded."
    }
    if ($wellProbe -notmatch '\(pawn, ignoredOwnActor=%s\)') {
        $fails += "FAIL: $wellProbeFile's probe-eye line no longer states its ignore list (pawn, ignoredOwnActor=%s) -- the eye overlap's claim about what it excluded is gone, so a self-hit eye reading could mislead exactly as the pre-T66 gate did (cold review F2)."
    }
    if ($wellProbe -notmatch '\*GateSelfIgnoreName\)') {
        $fails += "FAIL: $wellProbeFile's verdict UE_LOG no longer passes *GateSelfIgnoreName as an argument -- the ignoredOwnActor=%s token would either fail to compile or print the wrong value."
    }

    # ---- HALF 4: the PLACEMENT call sites stay untouched -- the packet's explicit boundary ------------
    # "The gate ITSELF (placement path) is untouched." Both stay exactly as many arguments as before
    # T66, so the placement-time behaviour is provably bit-identical.
    if ($subsystem -notmatch 'const bool bEnclosed = IsSpotEnclosed\(At, Blocked, Total\);') {
        $fails += "FAIL: $subsystemFile's solid-node placement call site (EnsureNewNodeSpawned's IsEnclosed lambda) no longer reads as the exact 3-arg 'IsSpotEnclosed(At, Blocked, Total);'. The T66 packet's explicit boundary is that the placement-path gate is untouched -- any change here, including adding the new self-ignore or the pawn 'for consistency', changes real placement behaviour that has never excluded anything."
    }
    if ($footprint -notmatch 'if\s*\(IsSpotEnclosed\(OutLoc, Blocked, Total\)\)') {
        $fails += "FAIL: $footprintFile's well-placement gate six call site no longer reads as the exact 3-arg 'IsSpotEnclosed(OutLoc, Blocked, Total)'. Same boundary as the solid-node site above: this call must stay bit-identical to before T66."
    }

    # ---- HALF 5: the AUDIT call site keeps the self-ignore (cold review F1 + supersession addendum) --
    # Added 2026-08-11 ~02:0x after the chip session's addendum measured that a one-line revert of the
    # audit fix passed the then-15/15 suite -- this file was never loaded, so the fix was unpinned.
    if ($audit -notmatch 'IsSpotEnclosed\(E\.Location, Blocked, Total, nullptr, &Threshold, Pawn, LiveActor\);') {
        $fails += "FAIL: $auditFile's reading-3 gate call no longer passes LiveActor as the 7th argument -- every audited entry with a live node at its centre self-hits 8-of-8 again and the offender ranking is poisoned by the exact wrong-population signal this command exists to prevent (T66 cold review F1)."
    }

    return ,$fails
}

function Get-AllLineNumbers([string[]]$lines, [string]$pattern) {
    $out = @()
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match $pattern) { $out += ($i + 1) }
    }
    return ,$out
}

$hRaw          = Get-Content -Raw $hPath
$footprintRaw  = Get-Content -Raw $footprintPath
$nodeProbeRaw  = Get-Content -Raw $nodeProbePath
$wellProbeRaw  = Get-Content -Raw $wellProbePath
$subsystemRaw  = Get-Content -Raw $subsystemPath
$auditRaw      = Get-Content -Raw $auditPath

if (-not $MutationTest) {
    $f = Get-T66Failures $hRaw $footprintRaw $nodeProbeRaw $wellProbeRaw $subsystemRaw $auditRaw
    if ($f.Count -gt 0) { $f | ForEach-Object { Write-Host $_ }; exit 1 }
    exit 0
}

# ---- mutation mode: prove each pin actually catches its edit ----
$control = Get-T66Failures $hRaw $footprintRaw $nodeProbeRaw $wellProbeRaw $subsystemRaw $auditRaw
Write-Host ("CONTROL (unmutated tree): {0}" -f $(if ($control.Count -eq 0) { 'PASS (0 failures) -- as required' } else { "UNEXPECTEDLY FAILING with $($control.Count):`n  " + ($control -join "`n  ") }))

function Replace-First([string]$text, [string]$find, [string]$repl) {
    $i = $text.IndexOf($find)
    if ($i -lt 0) { return $null }
    return $text.Substring(0, $i) + $repl + $text.Substring($i + $find.Length)
}

# No mutant embeds a line ending in its Find string: the tree is CRLF and a `\n` would silently turn
# every such mutant into "NOT APPLIED", i.e. a test that proves nothing (check_t59_lint's note).
$mutants = @(
    @{ Name = 'M1 drop the 7th arg (Subject) from NODEPROBE gate call, log field left in place'; File = 'nodeProbe';
       Find = 'IsSpotEnclosed(E.Location, Blocked, Total, &Rays, &Threshold, Pawn, Subject);';
       Repl = 'IsSpotEnclosed(E.Location, Blocked, Total, &Rays, &Threshold, Pawn);' },
    @{ Name = 'M2 drop the 7th arg (GateSelfIgnore) from WELLPROBE gate call, log field left in place'; File = 'wellProbe';
       Find = 'IsSpotEnclosed(MemberLoc, Blocked, Total, &Rays, &Threshold, Pawn, GateSelfIgnore);';
       Repl = 'IsSpotEnclosed(MemberLoc, Blocked, Total, &Rays, &Threshold, Pawn);' },
    @{ Name = 'M3 delete the EncParams.AddIgnoredActor(IgnoreActor2) statement'; File = 'footprint';
       Find = 'if (IgnoreActor2) { EncParams.AddIgnoredActor(IgnoreActor2); }'; Repl = '' },
    @{ Name = 'M4 delete IgnoreActor2 from the shared predicate declaration'; File = 'h';
       Find = "                        const AActor* IgnoreActor = nullptr,`r`n                        const AActor* IgnoreActor2 = nullptr) const;";
       Repl = "                        const AActor* IgnoreActor = nullptr) const;" },
    @{ Name = 'M5 hardcode the NODEPROBE ignoredOwnActor readback instead of sourcing it from Subject'; File = 'nodeProbe';
       Find = 'const TCHAR* IgnoredOwnActorTok = Subject ? *SubjectName : TEXT("none");';
       Repl = 'const TCHAR* IgnoredOwnActorTok = TEXT("none");' },
    @{ Name = 'M6 source WELLPROBE self-ignore from Pawn instead of LiveActor'; File = 'wellProbe';
       Find = 'const AActor* GateSelfIgnore = IsValid(LiveActor) ? LiveActor : nullptr;';
       Repl = 'const AActor* GateSelfIgnore = Pawn;' },
    @{ Name = 'M7 touch the solid-node PLACEMENT call site (add the pawn "for consistency")'; File = 'subsystem';
       Find = 'const bool bEnclosed = IsSpotEnclosed(At, Blocked, Total);';
       Repl = 'const bool bEnclosed = IsSpotEnclosed(At, Blocked, Total, nullptr, nullptr, nullptr);' },
    @{ Name = 'M8 touch the well PLACEMENT call site (add the pawn "for consistency")'; File = 'footprint';
       Find = 'if (IsSpotEnclosed(OutLoc, Blocked, Total))'; Repl = 'if (IsSpotEnclosed(OutLoc, Blocked, Total, nullptr, nullptr, nullptr))' },
    @{ Name = 'M9 change the no-live-actor fallback token away from "none" (NODEPROBE)'; File = 'nodeProbe';
       Find = 'const TCHAR* IgnoredOwnActorTok = Subject ? *SubjectName : TEXT("none");';
       Repl = 'const TCHAR* IgnoredOwnActorTok = Subject ? *SubjectName : TEXT("n/a");' },
    @{ Name = 'M10 change the no-live-actor fallback token away from "none" (WELLPROBE)'; File = 'wellProbe';
       Find = ': FString(TEXT("none"));'; Repl = ': FString(TEXT("n/a"));' },
    @{ Name = 'M11 delete the ignoredOwnActor field from the NODEPROBE verdict legend'; File = 'nodeProbe';
       Find = 'TEXT("or more blocked, so the verdict for this entry is %s. ignoredOwnActor=%s. The threshold and ")';
       Repl = 'TEXT("or more blocked, so the verdict for this entry is %s. The threshold and ")' },
    @{ Name = 'M12 delete the ignoredOwnActor field from the WELLPROBE verdict legend'; File = 'wellProbe';
       Find = 'TEXT("ignoredOwnActor=%s. The threshold and the ray count here were read back from the ")';
       Repl = 'TEXT("The threshold and the ray count here were read back from the ")' },
    @{ Name = 'M13 delete the ignore-list statement from the WELLPROBE eye line (cold review F2)'; File = 'wellProbe';
       Find = 'TEXT("the same ignore list as those rays (pawn, ignoredOwnActor=%s); it returned %d ")';
       Repl = 'TEXT("the pawn on the ignore list; it returned %d ")' },
    # M14's Find is single-line on purpose: a multi-line Find embeds this .ps1's own line endings, which
    # need not match the target header's (it was NOT APPLIED that way, 2026-08-11). Closing the param
    # list at Out leaves the orphaned IgnoreActor2 line behind in the mutated text, which is fine --
    # mutation is in-memory, never compiled, and the anchored pin cannot cross the inserted semicolon.
    @{ Name = 'M14 delete IgnoreActor2 from the eye-diag declaration (cold review F2)'; File = 'h';
       Find = 'FNodeShuffleProbeEyeReading& Out,';
       Repl = 'FNodeShuffleProbeEyeReading& Out) const;' },
    @{ Name = 'M15 delete the EyeParams.AddIgnoredActor(IgnoreActor2) statement (cold review F2)'; File = 'footprint';
       Find = 'if (IgnoreActor2) { EyeParams.AddIgnoredActor(IgnoreActor2); }'; Repl = '' },
    @{ Name = 'M16 revert the audit-site 7th arg (supersession addendum: the fix was unpinned)'; File = 'audit';
       Find = 'IsSpotEnclosed(E.Location, Blocked, Total, nullptr, &Threshold, Pawn, LiveActor);';
       Repl = 'IsSpotEnclosed(E.Location, Blocked, Total, nullptr, &Threshold, Pawn);' }
)

$missed = 0
foreach ($m in $mutants) {
    $hh = $hRaw; $ff = $footprintRaw; $np = $nodeProbeRaw; $wp = $wellProbeRaw; $ss = $subsystemRaw; $aa = $auditRaw
    $applied = $null
    switch ($m.File) {
        'h'          { $hh = Replace-First $hRaw         $m.Find $m.Repl; $applied = $hh }
        'footprint'  { $ff = Replace-First $footprintRaw $m.Find $m.Repl; $applied = $ff }
        'nodeProbe'  { $np = Replace-First $nodeProbeRaw $m.Find $m.Repl; $applied = $np }
        'wellProbe'  { $wp = Replace-First $wellProbeRaw $m.Find $m.Repl; $applied = $wp }
        'subsystem'  { $ss = Replace-First $subsystemRaw $m.Find $m.Repl; $applied = $ss }
        'audit'      { $aa = Replace-First $auditRaw     $m.Find $m.Repl; $applied = $aa }
    }
    if ($null -eq $applied) {
        Write-Host ("{0}: NOT APPLIED -- the text it edits was not found. The mutation test is stale, which means it is proving nothing." -f $m.Name)
        $missed++
        continue
    }
    $f = Get-T66Failures $hh $ff $np $wp $ss $aa
    $new = @($f | Where-Object { $control -notcontains $_ })
    if ($new.Count -gt 0) {
        Write-Host ("{0}: CAUGHT ({1} new failure(s)) -- first: {2}" -f $m.Name, $new.Count, $new[0])
    } else {
        Write-Host ("{0}: MISSED -- the lint does not detect this edit." -f $m.Name)
        $missed++
    }
}
if ($missed -gt 0 -or $control.Count -gt 0) { exit 1 }
exit 0
