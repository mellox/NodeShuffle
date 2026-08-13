# T72 (ns-t72-sam-visual-fallback, 2026-08-13) -- THE VISUAL-RESOLUTION LINT.
#
# THE DEFECT THIS PINS. NodeMeshCache / NodeMaterialCache are WEAK caches, and until T72 a cache HIT
# returned `Cached->Get()` with no retry. Once an entry's asset went away, the null was TERMINAL for the
# session: every later dress of that resource fell to the quartz placeholder and never self-healed.
# MEASURED: SAM rendered quartz on 4/4 dresses after any in-session re-roll while the other eight
# authored resources were 74/74 correct, and a save RELOAD restored SAM to correct until the next
# re-roll (session-scoped, cleared by restart -- FactoryGame.log 2026-08-13, lines 2643/2655 correct,
# 5098/6319/6323/7403 quartz, 11347/11359 correct again after reload).
#
# WHAT IS PINNED, AND WHY EACH IS A LINT RATHER THAN A COMMENT:
#   H1  The one-line revert. `return <ptr>->Get();` must appear NOWHERE in the subsystem -- that single
#       statement IS the bug, in both the resource resolver and the quartz accessor, and it is exactly
#       what someone writes back while "simplifying" the worker.
#   H2  ORDERING: the retry LoadObject must come BEFORE the load-failed branch. "Fall back only when the
#       load itself fails" is an ORDER, not a presence -- a fallback counted or returned ahead of the
#       retry passes every presence check while restoring the defect.
#   H3  The material staleness predicate tests BOTH shapes (length disagreement AND a dead slot).
#       Testing only length is the half-fix: SAM's row has one slot, so a dead slot is the ONLY shape
#       that can occur for it (SYMMETRY -- the same question asked of both cache types).
#   H4  The keep-alive is a UPROPERTY and is fed on EVERY successful load. It is the half of the fix
#       that PREVENTS the assumed cause; the retry CORRECTS whatever the cause really is. The forensics
#       graded the unload mechanism ASSUMED, so shipping only one half is shipping a guess.
#   H5  The census exists, carries all ten UNIQUE field names, is emitted from the apply pass, and its
#       legend contains NO name=value token of its own (this repo has twice shipped a legend that made
#       a grep of its own field return garbage; the tell was 384 matches over 128 lines).
#   H6  Both quartz accessors route through the shared workers. Two copies of the resolve is how the
#       placeholder path silently keeps the defect after the resource path is fixed.
#   H7  The SAM row is still in the table under its exact key. Every claim above is about SAM resolving;
#       a renamed/removed row makes the whole fix vacuous while every other pin stays green.
#
# Exit 0 = all hold. Exit 1 = at least one does not. Exit 2 = a pinned FILE is missing, i.e. the suite
# could not load what it claims to check (this repo's named vacuous-pass class).

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Source'

$files = @{
    subsys  = 'NodeShuffle\Private\NodeShuffleSubsystem.cpp'
    subsysh = 'NodeShuffle\Public\NodeShuffleSubsystem.h'
    assets  = 'NodeShuffle\Private\NodeShuffleNodeAssets.cpp'
}

# EVERY pinned file is loaded up front; a missing one is a HARD exit, never a skipped check.
$text = @{}
foreach ($k in $files.Keys) {
    $p = Join-Path $src $files[$k]
    if (-not (Test-Path $p)) {
        Write-Host "FAIL(load): pinned file '$($files[$k])' does not exist -- this suite cannot check what it cannot read."
        exit 2
    }
    $text[$k] = Get-Content -Raw -Path $p
}

$fails = @()

# The ten census columns. Named once, used by the presence pin, the uniqueness pin and the grader, so
# the three cannot drift apart.
$censusFields = @('meshFresh', 'meshHit', 'meshStale', 'meshLoadFail', 'meshNoEntry',
                  'matFresh', 'matHit', 'matStale', 'matLoadFail', 'matNoEntry', 'matEmptyRow',
                  'capFresh', 'capHit', 'capStale', 'capLoadFail', 'capNoCapture')

# ---- H1: the one-line revert -------------------------------------------------------------------
# Regex, not a literal: the mutant everyone writes uses a different local name (`C`, `Found`, `Hit`).
# MEASURED: zero legitimate occurrences of this shape exist in the file today, so the pin has no
# false-positive population to erode it.
function Test-H1OnText([string]$Text) {  # $true when the DEFECT is present
    return ($Text -match 'return\s+\w+->Get\(\)\s*;')
}
if (Test-H1OnText $text.subsys) {
    $fails += "FAIL(H1): $($files.subsys) returns a weak pointer's Get() directly -- that is the pre-T72 defect: a dead cached pointer becomes a terminal null and every later dress of that resource falls to quartz for the rest of the session."
}
foreach ($anchor in @('UStaticMesh* Reloaded = LoadObject<UStaticMesh>(nullptr, AuthoredPath);',
                      'UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, MatPath); // keep slot order')) {
    if (-not $text.subsys.Contains($anchor)) {
        $fails += "FAIL(H1): $($files.subsys) no longer contains the retry statement '$anchor' -- without it a stale cache entry is never re-resolved, which is the whole packet."
    }
}

# ---- H2: fallback ONLY on a load failure, i.e. the retry comes FIRST ----------------------------
# Anchored on the mesh worker's own cache-probe line (unique to that function), then on the two
# statements whose ORDER carries the contract. A bare "is the retry present" test cannot see a fallback
# hoisted above it.
function Test-H2OnText([string]$Text) {  # $true when the ORDER HOLDS
    $ls = @($Text -split "`r?`n")
    $probe = -1; $retry = -1; $fail = -1
    for ($i = 0; $i -lt $ls.Count; $i++) {
        if ($probe -lt 0 -and $ls[$i].Contains('TWeakObjectPtr<UStaticMesh>* Cached = NodeMeshCache.Find(CacheKey)')) { $probe = $i }
        if ($probe -ge 0 -and $retry -lt 0 -and $ls[$i].Contains('UStaticMesh* Reloaded = LoadObject<UStaticMesh>(nullptr, AuthoredPath);')) { $retry = $i }
        if ($probe -ge 0 -and $fail  -lt 0 -and $ls[$i].Contains('T72MeshLoadFailed++;')) { $fail = $i }
    }
    if ($probe -lt 0 -or $retry -lt 0 -or $fail -lt 0) { return $false }
    return ($probe -lt $retry -and $retry -lt $fail)
}
if (-not (Test-H2OnText $text.subsys)) {
    $fails += "FAIL(H2): in $($files.subsys) the mesh worker's cache probe -> LoadObject retry -> load-failed fallback no longer appear in that order (or one of the three is gone). The contract is an ORDER: the placeholder may be reached only AFTER the single retry has actually been attempted."
}

# ---- H3: the material staleness predicate tests BOTH shapes ------------------------------------
foreach ($half in @('bool bStale = (Cached->Num() != AuthoredCount);', 'bStale = bStale || (DeadSlots > 0);')) {
    if (-not $text.subsys.Contains($half)) {
        $fails += "FAIL(H3): $($files.subsys) no longer tests '$half' -- staleness has two shapes (the row's length changed, or a slot's weak pointer died) and SAM's one-slot row can only ever exhibit the second."
    }
}

# ---- H4: the keep-alive, declared strong and fed at every successful load -----------------------
# F7: TObjectPtr, not a raw pointer -- every other reflected pointer member in this mod uses TObjectPtr and
# UE5 gates raw pointer members behind the target's NativePointerMemberBehavior (unread, uncompiled here).
if ($text.subsysh -notmatch 'UPROPERTY\(\)\s*TArray<TObjectPtr<UObject>>\s*VisualAssetKeepAlive;') {
    $fails += "FAIL(H4): $($files.subsysh) no longer declares VisualAssetKeepAlive as a UPROPERTY TArray<TObjectPtr<UObject>> -- without the UPROPERTY it is not a strong reference at all and the prevention half of the fix is decorative; with a raw UObject* it may not even compile against this engine fork's NativePointerMemberBehavior."
}
# SIX sites since F1: mesh fresh, mesh retry, material rebuild, material fresh, capture mesh, capture materials.
$keepAliveFeeds = ([regex]::Matches($text.subsys, [regex]::Escape('VisualAssetKeepAlive.AddUnique('))).Count
if ($keepAliveFeeds -lt 6) {
    $fails += "FAIL(H4): $($files.subsys) feeds VisualAssetKeepAlive at only $keepAliveFeeds site(s); all SIX successful-load sites (mesh fresh, mesh retry, material rebuild, material fresh, and the two capture loads added by F1) must add, or the asset that unloads is the one nobody rooted."
}
# F2: the four load-failed Display lines are LATCHED per key. Unlatched, they fire per node per pass -- the
# 153k-line/35MB failure this repo has already shipped once.
# R1 (scoped re-pass): the latch is keyed by (key, ASSET KIND). The two workers are handed the SAME
# CacheKey for a resource, so a bare-key latch let the first failing kind suppress the other kind's line
# for the session -- and made "at most one line per key per session" unfailable as an acceptance test.
function Test-LatchOnText([string]$Text) {   # $true when the latch pins HOLD
    if (([regex]::Matches($Text, [regex]::Escape('if (!T72LoadFailLogged.Contains(FailKey))'))).Count -lt 4) { return $false }
    if (([regex]::Matches($Text, [regex]::Escape('CacheKey + TEXT("|mesh")'))).Count -ne 2) { return $false }
    if (([regex]::Matches($Text, [regex]::Escape('CacheKey + TEXT("|mat")'))).Count -ne 2) { return $false }
    if ($Text.Contains('T72LoadFailLogged.Contains(CacheKey)') -or $Text.Contains('T72LoadFailLogged.Add(CacheKey)')) { return $false }
    return $true
}
if (-not (Test-LatchOnText $text.subsys)) {
    $fails += "FAIL(H4/F2/R1): $($files.subsys) does not latch all four load-failed Display lines on a (key, asset-kind) FailKey -- two '|mesh' sites and two '|mat' sites, and never the bare CacheKey. Unlatched, a line fires per node per pass (98-node population on file); latched on the bare key, the first failing kind silently suppresses the other kind's line for the whole session."
}
if ($text.subsysh -notmatch 'TSet<FString>\s*T72LoadFailLogged;') {
    $fails += "FAIL(H4/F2): $($files.subsysh) no longer declares the T72LoadFailLogged latch."
}

# ---- H5: the census -----------------------------------------------------------------------------
if ($text.subsys -notmatch 'T72DRESS:') {
    $fails += "FAIL(H5): $($files.subsys) no longer emits a T72DRESS line -- a resolver whose branch mix nobody can count is a resolver nobody can debug."
}
# The census function body, isolated, so the field-uniqueness test cannot be satisfied (or broken) by
# unrelated text elsewhere in a 5000-line file.
function Get-CensusBody([string]$Text) {
    $ls = @($Text -split "`r?`n")
    $start = -1
    for ($i = 0; $i -lt $ls.Count; $i++) {
        if ($ls[$i].Contains('void ANodeShuffleSubsystem::EmitT72DressCensus()')) { $start = $i; break }
    }
    if ($start -lt 0) { return $null }
    for ($j = $start + 1; $j -lt $ls.Count; $j++) {
        if ($ls[$j] -match '^\}') { return ($ls[$start..$j] -join "`n") }
    }
    return $null
}
function Test-H5OnText([string]$Text) {  # $true when the census pin HOLDS
    $body = Get-CensusBody $Text
    if ($null -eq $body) { return $false }
    foreach ($f in $censusFields) {
        # EXACTLY ONE occurrence of each `field=` token: present as a column, and absent from the legend.
        # A legend that exemplifies its own field is what made two earlier greps in this repo return
        # every line regardless of value.
        $n = ([regex]::Matches($body, [regex]::Escape($f + '='))).Count
        if ($n -ne 1) { return $false }
    }
    return $true
}
if (-not (Test-H5OnText $text.subsys)) {
    $fails += "FAIL(H5): the T72DRESS census body in $($files.subsys) does not carry each of the ten field names ($($censusFields -join ', ')) exactly once. Either a column is missing/renamed, two columns share a name, or the trailing legend exemplifies a field in the same form the field itself prints -- which makes a grep of that field match the legend too."
}
# The census must be CALLED from the apply pass, not merely defined.
$censusCalls = ([regex]::Matches($text.subsys, '(?m)^\s+EmitT72DressCensus\(\);')).Count
if ($censusCalls -lt 1) {
    $fails += "FAIL(H5): $($files.subsys) defines EmitT72DressCensus but never calls it from the pass -- a census nothing invokes is the vacuous pass wearing a census's hat."
}
# The decision branches must each be nameable in the log, PER WORKER. A file-wide presence test is
# satisfiable by the OTHER worker's copy of the same token -- measured: M12 deletes the mesh worker's
# stale-branch name and a file-wide test still passed because the material worker's line carried it.
# That is this repo's "a pin satisfied by a sibling occurrence" class, so each worker is scoped by its
# own signature line and asked separately.
function Get-FunctionBody([string]$Text, [string]$SignatureFragment) {
    $ls = @($Text -split "`r?`n")
    $start = -1
    for ($i = 0; $i -lt $ls.Count; $i++) {
        if ($ls[$i].Contains($SignatureFragment)) { $start = $i; break }
    }
    if ($start -lt 0) { return $null }
    for ($j = $start + 1; $j -lt $ls.Count; $j++) {
        if ($ls[$j] -match '^\}') { return ($ls[$start..$j] -join "`n") }
    }
    return $null
}
# Mesh worker: five literal branch names. Material worker: the same five, with cache-hit/no-table-entry
# selected by a ternary into a %s (so its two literals are pinned in that form).
$branchPins = @(
    @{ Sig = 'ANodeShuffleSubsystem::ResolveNodeMeshForKey'; Label = 'mesh';
       Tokens = @('branch=cache-hit', 'branch=no-table-entry', 'branch=cache-stale-reloaded',
                  'branch=load-failed-fallback', 'branch=fresh-loaded') },
    @{ Sig = 'ANodeShuffleSubsystem::ResolveNodeMaterialsForKey'; Label = 'material';
       Tokens = @('TEXT("cache-hit")', 'TEXT("no-table-entry")', 'branch=cache-stale-reloaded',
                  'branch=load-failed-fallback', 'branch=fresh-loaded', 'branch=no-table-entry') }
)
function Test-BranchPinsOnText([string]$Text, $Pins) {  # $true when ALL hold
    foreach ($p in $Pins) {
        $body = Get-FunctionBody $Text $p.Sig
        if ($null -eq $body) { return $false }
        foreach ($t in $p.Tokens) { if (-not $body.Contains($t)) { return $false } }
    }
    return $true
}
foreach ($p in $branchPins) {
    $body = Get-FunctionBody $text.subsys $p.Sig
    if ($null -eq $body) {
        $fails += "FAIL(H5): $($files.subsys) has no $($p.Label) worker ($($p.Sig)) to scan -- this half would pass by reading nothing."
        continue
    }
    foreach ($t in $p.Tokens) {
        if (-not $body.Contains($t)) {
            $fails += "FAIL(H5): the $($p.Label) worker in $($files.subsys) no longer names '$t' -- the forensics' explicit gap was that no line distinguished 'no table entry' from 'LoadObject failed' from 'the cached pointer went stale', and a branch named in the OTHER worker does not answer it here."
        }
    }
}

# ---- H6: both quartz accessors route through the shared workers ---------------------------------
foreach ($route in @('return ResolveNodeMeshForKey(QuartzKey.ToString(), QuartzKey, TEXT("quartz-placeholder"));',
                     'return ResolveNodeMaterialsForKey(QuartzKey.ToString(), QuartzKey, TEXT("quartz-placeholder"));')) {
    if (-not $text.subsys.Contains($route)) {
        $fails += "FAIL(H6): $($files.subsys) no longer routes a quartz placeholder accessor through the shared worker ('$route') -- the placeholder used the SAME weak cache with the SAME no-retry hit, so a second copy of the resolve means the FALLBACK can go stale while the resource path is fixed."
    }
}

# ---- H7: the SAM row is still in the table under its exact key ----------------------------------
if ($text.assets -notmatch [regex]::Escape('M.Add("Desc_SAM_C"')) {
    $fails += "FAIL(H7): $($files.assets) no longer adds the 'Desc_SAM_C' row -- T72's entire subject is SAM resolving to its authored mesh; without the row every other pin here stays green while the node renders quartz by design."
}
if ($text.assets -notmatch [regex]::Escape('SM_SAM_Node_01')) {
    $fails += "FAIL(H7): $($files.assets) no longer names SM_SAM_Node_01 -- that is the mesh whose absence this packet exists to make impossible."
}

# ---- H8: the write-back. The repair is not the LoadObject, it is storing what it returned. Deleting either
# assignment leaves every other pin green: the material one hands the caller back the OLD dead array (a full
# revert of that half), the mesh one re-loads on every dress forever. Measured by the cold review: both
# survived the 12-mutant suite as shipped.
foreach ($wb in @('*Cached = Reloaded;', '*Cached = MoveTemp(Rebuilt);')) {
    if (-not $text.subsys.Contains($wb)) {
        $fails += "FAIL(H8): $($files.subsys) no longer writes the re-resolved asset back with '$wb' -- without it the cache is never healed, and on the material side the caller is handed the OLD array of dead weak pointers, i.e. T72's material half is reverted with every other pin still green."
    }
}
# ---- H9: the material worker's ORDER, the same contract H2 pins for the mesh worker (SYMMETRY).
function Test-H9OnText([string]$Text) {   # $true when the ORDER HOLDS
    $body = Get-FunctionBody $Text 'ANodeShuffleSubsystem::ResolveNodeMaterialsForKey'
    if ($null -eq $body) { return $false }
    $ls = @($body -split "`r?`n")
    $rebuild = -1; $fail = -1
    for ($i = 0; $i -lt $ls.Count; $i++) {
        if ($rebuild -lt 0 -and $ls[$i].Contains('TArray<TWeakObjectPtr<UMaterialInterface>> Rebuilt;')) { $rebuild = $i }
        if ($fail    -lt 0 -and $ls[$i].Contains('T72MatLoadFailed++;')) { $fail = $i }
    }
    if ($rebuild -lt 0 -or $fail -lt 0) { return $false }
    return ($rebuild -lt $fail)
}
if (-not (Test-H9OnText $text.subsys)) {
    $fails += "FAIL(H9): in $($files.subsys) the material worker's rebuild no longer precedes its load-failed fallback (or one of the two is gone). H2 pins this order for the mesh worker; asking it of only one of two workers is the sibling-asymmetry class this repo keeps re-finding."
}

# ---- H10 (F1): the CAPTURED resolver gets the same discipline, in its own function ---------------
# The cold review found the identical no-retry weak cache serving the MODDED population, invisible to the
# census. The pin is scoped to that function -- a liveness test, the drop of the dead entry, and its own
# census branch -- because the mesh worker's copies of these ideas would otherwise satisfy a file-wide test.
function Test-H10OnText([string]$Text) {  # $true when the capture-path pins HOLD
    $body = Get-FunctionBody $Text 'ANodeShuffleSubsystem::ResolveCapturedVisual'
    if ($null -eq $body) { return $false }
    foreach ($t in @('const bool bMeshDead = !Cached->Mesh.IsValid();',
                     'if (!M.IsValid()) { bMatDead = true; break; }',
                     'ResolvedCaptureCache.Remove(ResourceClassName);',
                     'branch=cache-stale-reloaded',
                     'T72CapCacheHit++;', 'T72CapStaleReloaded++;',
                     'VisualAssetKeepAlive.AddUnique(',
                     # R3: the cap columns are DISJOINT per call -- staleness is a local spent in the
                     # success tail, so one call moves exactly one column like its eleven neighbours.
                     'bool bWasStale = false;', 'bWasStale = true;', 'else if (bWasStale)')) {
        if (-not $body.Contains($t)) { return $false }
    }
    # Exactly one increment site per cap column: a second T72CapStaleReloaded++ is the double-count R3 removed.
    foreach ($c in @('T72CapStaleReloaded++;', 'T72CapFreshLoaded++;', 'T72CapLoadFailed++;', 'T72CapCacheHit++;')) {
        if (([regex]::Matches($body, [regex]::Escape($c))).Count -ne 1) { return $false }
    }
    # ORDER, by character offset so a same-line insertion cannot slip past a line-index test: the liveness
    # test must come BEFORE any return of the cached entry. Presence alone is satisfied by a hoisted
    # `return Cached;` sitting above a liveness test that then never runs -- measured (mutant M16).
    $live = $body.IndexOf('const bool bMeshDead = !Cached->Mesh.IsValid();')
    $ret  = $body.IndexOf('return Cached;')
    if ($ret -lt 0 -or $live -lt 0 -or $ret -lt $live) { return $false }
    return $true
}
if (-not (Test-H10OnText $text.subsys)) {
    $fails += "FAIL(H10): the captured-visual resolver in $($files.subsys) no longer tests its cached entry for liveness, drops the dead entry, feeds the keep-alive, and counts its own census branches. That resolver serves the MODDED population -- the one the workspace's standing rule says a vanilla-only run never confirms -- and pre-F1 a dead entry there read as a clean census while the node wore quartz."
}

# ---- H11 (F3): the material branch keys on ROW EXISTENCE, not on slot count ---------------------
# Three authored rows (the FicsitFarming dirt family) list zero material slots. Keying on AuthoredCount
# counted them as 'no table entry' -- a field counting what it does not name, which is what makes a reader
# stop looking.
# R2 (scoped re-pass): the split lives in TWO branches -- the one-line cache-hit form AND the multi-line
# FIRST-RESOLUTION form, which is the branch the 98-node FicsitFarming dirt population actually takes on a
# fresh load. Pinning only the one-line copy let half the fix be reverted with all pins green.
$f3Anchors = @('const bool bHasRow = (Visual != nullptr);',
               'if (!bHasRow) { T72MatNoTableEntry++; }',
               'else if (AuthoredCount == 0) { T72MatAuthoredEmptyRow++; }',
               'bHasRow ? TEXT("cache-hit") : TEXT("no-table-entry")',
               '// T72 F3 split: an authored row that lists zero material slots (the FicsitFarming dirt family).')
function Test-H11OnText([string]$Text) {
    foreach ($a in $f3Anchors) { if (-not $Text.Contains($a)) { return $false } }
    # BOTH increment sites, counted: one per branch. A count test is what distinguishes "the split exists"
    # from "the split exists in the branch the lint happened to anchor on".
    if (([regex]::Matches($Text, [regex]::Escape('T72MatAuthoredEmptyRow++;'))).Count -ne 2) { return $false }
    return $true
}
if (-not (Test-H11OnText $text.subsys)) {
    $fails += "FAIL(H11): $($files.subsys) no longer branches the material worker on whether a table ROW EXISTS (bHasRow) with a separate counter for authored-empty rows -- keying on the slot count files the three FicsitFarming dirt rows, which ARE covered, under 'no table entry'."
}

# ------------------------------------------------------------------------------------------------
# MUTATION TEST. Every pin must FAIL on a revert of the change it protects. A pin that survives its own
# mutant is decorative. Nothing on disk is touched -- each mutant is graded by re-running the matching
# pin against the mutated text in memory, calling THE PIN'S OWN helper where one exists (a
# re-implemented grader can pass while the real pin misses -- T68's R4 lesson).
# ------------------------------------------------------------------------------------------------
function Replace-First([string]$Text, [string]$Find, [string]$Repl) {
    $i = $Text.IndexOf($Find)
    if ($i -lt 0) { return $null }
    return $Text.Substring(0, $i) + $Repl + $Text.Substring($i + $Find.Length)
}

# No Find string ends at a line boundary: the tree is CRLF and a trailing `\n` would silently turn a
# mutant into "NOT APPLIED" -- a test that proves nothing (check_t59_lint's note, same trap).
$mutants = @(
    @{ Name = 'M1 THE PACKET REVERT: delete the retry, keep everything else'; Key = 'subsys';
       Find = 'UStaticMesh* Reloaded = LoadObject<UStaticMesh>(nullptr, AuthoredPath);';
       Repl = 'UStaticMesh* Reloaded = nullptr;' },
    @{ Name = 'M2 restore the pre-T72 dead-pointer return, under a different local name'; Key = 'subsys';
       Find = '        if (UStaticMesh* Live = Cached->Get())';
       Repl = '        if (const TWeakObjectPtr<UStaticMesh>* Hit = &(*Cached)) { return Hit->Get(); }' + [Environment]::NewLine + '        if (UStaticMesh* Live = Cached->Get())' },
    @{ Name = 'M3 (ORDER) take the fallback before the retry is attempted'; Key = 'subsys';
       Find = '        UStaticMesh* Reloaded = LoadObject<UStaticMesh>(nullptr, AuthoredPath);';
       Repl = '        T72MeshLoadFailed++;' + [Environment]::NewLine + '        UStaticMesh* Reloaded = LoadObject<UStaticMesh>(nullptr, AuthoredPath);' },
    @{ Name = 'M4 half-fix the material staleness predicate (length only, so SAM is unreachable)'; Key = 'subsys';
       Find = '        bStale = bStale || (DeadSlots > 0);';
       Repl = '        // length-only staleness' },
    @{ Name = 'M5 drop the keep-alive at the retry site'; Key = 'subsys';
       Find = '            VisualAssetKeepAlive.AddUnique(Reloaded);';
       Repl = '            // no keep-alive' },
    @{ Name = 'M6 (LEGEND COLLISION) exemplify a census field inside the census legend'; Key = 'subsys';
       Find = 'a stale column above zero means a cached ';
       Repl = 'meshStale=1 means a cached ' },
    @{ Name = 'M7 give two census columns the same name'; Key = 'subsys';
       Find = 'matHit=%d'; Repl = 'meshHit=%d' },
    @{ Name = 'M8 stop calling the census from the pass'; Key = 'subsys';
       Find = '    EmitT72DressCensus();'; Repl = '    // census call removed' },
    @{ Name = 'M9 let the quartz accessor keep its own no-retry cache probe'; Key = 'subsys';
       Find = '    return ResolveNodeMeshForKey(QuartzKey.ToString(), QuartzKey, TEXT("quartz-placeholder"));';
       Repl = '    if (const TWeakObjectPtr<UStaticMesh>* C = NodeMeshCache.Find(QuartzKey.ToString())) { return C->Get(); }' + [Environment]::NewLine + '    return nullptr;' },
    @{ Name = 'M10 downgrade the keep-alive to a plain (non-UPROPERTY) array'; Key = 'subsysh';
       Find = '    UPROPERTY() TArray<TObjectPtr<UObject>> VisualAssetKeepAlive;';
       Repl = '    TArray<TObjectPtr<UObject>> VisualAssetKeepAlive;' },
    @{ Name = 'M20 (F2) delete the latch declaration'; Key = 'subsysh';
       Find = '    TSet<FString> T72LoadFailLogged;'; Repl = '    // latch removed' },
    @{ Name = 'M11 rename the SAM table key (every other pin stays green)'; Key = 'assets';
       Find = 'M.Add("Desc_SAM_C"'; Repl = 'M.Add("Desc_SAM_Disabled_C"' },
    @{ Name = 'M12 delete one branch name from the resolver log'; Key = 'subsys';
       Find = 'branch=cache-stale-reloaded resolved'; Repl = 'branch=reloaded resolved' },
    # ADDED BY THE COLD REVIEW (F5). All three were MEASURED to survive the 12-mutant suite as shipped.
    @{ Name = 'M13 (F5) delete the material write-back -- reverts T72''s material half entirely'; Key = 'subsys';
       Find = '        *Cached = MoveTemp(Rebuilt);'; Repl = '        // write-back removed' },
    @{ Name = 'M14 (F5) delete the mesh write-back -- correct pixels, one LoadObject per dress forever'; Key = 'subsys';
       Find = '        *Cached = Reloaded;'; Repl = '        // write-back removed' },
    @{ Name = 'M15 (F5/SYMMETRY) hoist the material fallback above its rebuild'; Key = 'subsys';
       Find = '        TArray<TWeakObjectPtr<UMaterialInterface>> Rebuilt;';
       Repl = '        T72MatLoadFailed++;' + [Environment]::NewLine + '        TArray<TWeakObjectPtr<UMaterialInterface>> Rebuilt;' },
    # ADDED BY THIS ROUND (F1). The capture resolver reverting to its pre-F1 unconditional hit is the exact
    # defect the review found, in the population a vanilla run cannot test.
    @{ Name = 'M16 (F1) revert the capture resolver to an unconditional cache hit'; Key = 'subsys';
       Find = '        const bool bMeshDead = !Cached->Mesh.IsValid();';
       Repl = '        return Cached; const bool bMeshDead = !Cached->Mesh.IsValid();' },
    @{ Name = 'M17 (F1) stop dropping the dead capture entry'; Key = 'subsys';
       Find = '        ResolvedCaptureCache.Remove(ResourceClassName);'; Repl = '        // dead entry kept' },
    @{ Name = 'M18 (F2) unlatch one load-failed Display line'; Key = 'subsys';
       Find = '        if (!T72LoadFailLogged.Contains(FailKey))';
       Repl = '        if (true)' },
    @{ Name = 'M19 (F3) fold authored-empty material rows back into no-table-entry'; Key = 'subsys';
       Find = '            if (!bHasRow) { T72MatNoTableEntry++; }';
       Repl = '            if (AuthoredCount == 0) { T72MatNoTableEntry++; }' },
    # ADDED BY THE SCOPED RE-PASS (R1-R3), each reproducing that finding's exact revert.
    @{ Name = 'M21 (R1) collapse the latch key back to the bare CacheKey'; Key = 'subsys';
       Find = '        const FString FailKey = CacheKey + TEXT("|mesh");';
       Repl = '        const FString FailKey = CacheKey;' },
    @{ Name = 'M22 (R2) delete the FIRST-RESOLUTION half of the matEmptyRow split'; Key = 'subsys';
       # Single-line anchor on purpose: the FIRST-RESOLUTION copy is the only one where this increment
       # stands alone on its own 8-space-indented line (the cache-hit copy is inside a one-liner), and a
       # two-line Find would silently NOT APPLY on whichever line ending this tree happens to carry.
       Find = '        T72MatAuthoredEmptyRow++;';
       Repl = '        T72MatNoTableEntry++;' },
    @{ Name = 'M23 (R3) restore the cap double-increment (one call moving two columns)'; Key = 'subsys';
       Find = '        bWasStale = true; // R3: spent in the success tail below, never alongside capLoadFail';
       Repl = '        T72CapStaleReloaded++;' }
)

$missed = 0
foreach ($m in $mutants) {
    $mutated = Replace-First $text[$m.Key] $m.Find $m.Repl
    if ($null -eq $mutated) {
        Write-Host "FAIL(mutation): '$($m.Name)' NOT APPLIED -- its anchor text is not present in $($files[$m.Key]). An unapplied mutant grades nothing."
        $missed++
        continue
    }
    $caught = $false
    switch ($m.Key) {
        'subsys' {
            if (Test-H1OnText $mutated) { $caught = $true }
            if (-not $mutated.Contains('UStaticMesh* Reloaded = LoadObject<UStaticMesh>(nullptr, AuthoredPath);')) { $caught = $true }
            if (-not (Test-H2OnText $mutated)) { $caught = $true }
            foreach ($half in @('bool bStale = (Cached->Num() != AuthoredCount);', 'bStale = bStale || (DeadSlots > 0);')) {
                if (-not $mutated.Contains($half)) { $caught = $true }
            }
            if (([regex]::Matches($mutated, [regex]::Escape('VisualAssetKeepAlive.AddUnique('))).Count -lt 6) { $caught = $true }
            if (-not (Test-LatchOnText $mutated)) { $caught = $true }
            foreach ($wb in @('*Cached = Reloaded;', '*Cached = MoveTemp(Rebuilt);')) {
                if (-not $mutated.Contains($wb)) { $caught = $true }
            }
            if (-not (Test-H9OnText  $mutated)) { $caught = $true }
            if (-not (Test-H10OnText $mutated)) { $caught = $true }
            if (-not (Test-H11OnText $mutated)) { $caught = $true }
            if (-not (Test-H5OnText $mutated)) { $caught = $true }
            if (([regex]::Matches($mutated, '(?m)^\s+EmitT72DressCensus\(\);')).Count -lt 1) { $caught = $true }
            if (-not (Test-BranchPinsOnText $mutated $branchPins)) { $caught = $true }
            foreach ($route in @('return ResolveNodeMeshForKey(QuartzKey.ToString(), QuartzKey, TEXT("quartz-placeholder"));',
                                 'return ResolveNodeMaterialsForKey(QuartzKey.ToString(), QuartzKey, TEXT("quartz-placeholder"));')) {
                if (-not $mutated.Contains($route)) { $caught = $true }
            }
        }
        'subsysh' {
            if ($mutated -notmatch 'UPROPERTY\(\)\s*TArray<TObjectPtr<UObject>>\s*VisualAssetKeepAlive;') { $caught = $true }
            if ($mutated -notmatch 'TSet<FString>\s*T72LoadFailLogged;') { $caught = $true }
        }
        'assets' {
            if ($mutated -notmatch [regex]::Escape('M.Add("Desc_SAM_C"')) { $caught = $true }
            if ($mutated -notmatch [regex]::Escape('SM_SAM_Node_01')) { $caught = $true }
        }
    }
    if (-not $caught) {
        Write-Host "FAIL(mutation): '$($m.Name)' SURVIVED -- the pin that should have caught it is decorative."
        $missed++
    }
}

foreach ($f in $fails) { Write-Host $f }
if ($fails.Count -gt 0 -or $missed -gt 0) {
    Write-Host "T72 lint: $($fails.Count) pin failure(s), $missed mutation failure(s)."
    exit 1
}
Write-Host "OK: T72 -- no weak-pointer Get() is returned as an answer; both workers' retry precedes their fallback; both write-backs stand; material staleness tests length AND dead slots and branches on row existence; the captured-visual resolver tests liveness before returning and drops its dead entry; the keep-alive is a UPROPERTY TObjectPtr array fed at all six load sites; the four load-failed lines are latched per key AND asset kind; the matEmptyRow split is pinned in BOTH its branches; the cap columns are disjoint per call; the T72DRESS census carries $($censusFields.Count) uniquely-named columns with a non-colliding legend and is called from the pass; both quartz accessors share the workers; the SAM row stands. $($mutants.Count)/$($mutants.Count) mutants caught."
exit 0
