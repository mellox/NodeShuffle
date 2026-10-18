"""Compile the production allocation policy and check safety-critical integration seams.

These are offline checks, not an Unreal runtime/save-serialization test.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'Source/NodeShuffle/Private/NodeShuffleLateNodes.cpp').read_text(encoding='utf-8')
subsystem = (root / 'Source/NodeShuffle/Private/NodeShuffleSubsystem.cpp').read_text(encoding='utf-8')
header = (root / 'Source/NodeShuffle/Public/NodeShuffleSubsystem.h').read_text(encoding='utf-8')
policy = (root / 'Source/NodeShuffle/Private/NodeShuffleLateNodePolicy.h').read_text(encoding='utf-8')
policy_tests = (root / 'tools/test_late_node_policy.cpp').read_text(encoding='utf-8')
apply = subsystem.split('void ANodeShuffleSubsystem::ApplyLayout()', 1)[1].split('\nvoid ', 1)[0]
assert apply.index('AdoptRestoredSpawnedNodes();') < apply.index('MaybeCaptureLateOriginalNodes();')
assert apply.index('MaybeCaptureLateOriginalNodes();') < apply.index('EnsureNewNodeSpawned(Entry, bChangedWorld);')
schedule = source.split('void ANodeShuffleSubsystem::MaybeCaptureLateOriginalNodes()', 1)[1].split('\nvoid ', 1)[0]
assert schedule.index('NodeShuffleLateNodePolicy::ScanDue(') < schedule.index('IsWorldReadyForReroll()') < schedule.index('CaptureLateOriginalNodes();')
assert schedule.index('CaptureLateOriginalNodes();') < schedule.index('++LateNodeScansCompleted;')
assert 'durationMs=' in schedule and 'addedEntries=' in schedule and 'addedHideRecords=' in schedule
# RWG P2g (2026-10-18): periodic schedule + session growth cap (the 2-scan window is gone).
assert 'totalAddedSession=' in schedule and 'scheduleExhausted' not in schedule
assert schedule.index('GrowthCapReached(LateNodeTotalAddedSession') < schedule.index('NodeShuffleLateNodePolicy::ScanDue(')
assert 'ScanDue(LateNodeScansCompleted, Now, LastLateNodeScanTime,' in schedule
assert schedule.index('CaptureLateOriginalNodes();') < schedule.index('LastLateNodeScanTime = Now;') < schedule.index('LateNodeTotalAddedSession += AddedEntries;')
assert 'return !GrowthCapReached(TotalAddedSession, Cap) && (CompletedScans == 0 || Now - LastScanTime >= Period);' in policy
assert 'constexpr double ScanPeriodSeconds = 60.0;' in policy and 'constexpr int GrowthCapEntries = 2048;' in policy
begin_play = subsystem.split('void ANodeShuffleSubsystem::BeginPlay()', 1)[1].split('\nvoid ', 1)[0]
assert 'LateNodeScansCompleted = 0;' in begin_play and 'FirstLateNodeScanTime = 0.0;' in begin_play
assert subsystem.count('LateNodeScansCompleted = 0;') == 1  # rerolls never rearm the session schedule
assert 'LastLateNodeScanTime = 0.0;' in begin_play and 'LateNodeTotalAddedSession = 0;' in begin_play
assert subsystem.count('LateNodeTotalAddedSession = 0;') == 1  # rerolls never reset the session cap
assert 'UPROPERTY(SaveGame) int32 LateNodeTotalAddedSession' not in header
# Visible-original census brackets the hide pass in ApplyLayout.
assert apply.index('RehideVisibleOriginalsPre();') < apply.index('SuppressOriginalNodes();') < apply.index('RehideVisibleOriginalsPost();')
assert 'UPROPERTY(SaveGame) int32 LateNodeScansCompleted' not in header
assert 'bChangedWorld || SuppressChangesLastPass > 0' in apply
assert 'RollLayout(' not in source and 'Layout.Reset(' not in source and 'Layout.Empty(' not in source
assert 'OriginalNodeRecord.Reset(' not in source
assert 'FNodeShuffleModule::IsManagedSpawnedNode(Node)' in source
assert 'UNodeShuffleNodeComponent::Find(Node)' in source
assert 'Node->IsA<ANodeShuffleResourceNode>()' in source
assert 'IsNodeOccupiedAnyway(SolidNode)' in source
assert 'Node->IsNetStartupActor()' not in source  # no late level-actor blind spot
assert 'if (Locations.Num() != Active)' in source
assert source.index('if (Locations.Num() != Active)') < source.index('OriginalNodeRecord.Add(Rec)')
assert 'bKnowledgeUnlockDone = false;' in source and 'bAutoAllowExtractorsDone = false;' in source
assert 'UPROPERTY(SaveGame) FString SpawnNodeClassPath;' in header
assert 'UPROPERTY(SaveGame) FString OriginalNodeClassPath;' in header
assert 'E.SpawnNodeClassPath = Old.SpawnNodeClassPath;' in subsystem
assert 'Legacy->EntryGuid = Entry.EntryGuid;' in subsystem
assert 'NodeShuffleLateNodePolicy::AdditionalActive(' in source
rematch = subsystem.split('AFGResourceNodeBase* ANodeShuffleSubsystem::TryRematchStaleRecord(', 1)[1].split('\nvoid ', 1)[0]
assert rematch.index('NodeShuffleLateNodePolicy::MatchesOriginalIdentity(') < rematch.index('MatchEntry = &E;')

compilers = list(Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC').glob('*/bin/Hostx64/x64/cl.exe'))
assert compilers, 'MSVC compiler not found for production policy checks'
with tempfile.TemporaryDirectory(prefix='nodeshuffle-policy-') as temp:
    result = subprocess.run([str(sorted(compilers)[-1]), '/nologo', '/c', '/std:c++17',
                             str(root / 'tools/test_late_node_policy.cpp'), '/Fo' + temp + '/policy.obj'],
                            capture_output=True, text=True)
    print(result.stdout, end='')
    print(result.stderr, end='')
    assert result.returncode == 0, 'production policy tests failed to compile'
cases = len(re.findall(r'(?m)^static_assert\(', policy_tests))
assert cases >= 36, f'policy cases dropped: {cases}'
print(f'PASS: {cases} production C++ policy cases and late-node integration guards (offline only)')
