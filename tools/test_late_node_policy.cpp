#include "../Source/NodeShuffle/Private/NodeShuffleLateNodePolicy.h"
using NodeShuffleLateNodePolicy::AdditionalActive;
using NodeShuffleLateNodePolicy::MatchesOriginalIdentity;
using NodeShuffleLateNodePolicy::ScanDue;

static_assert(ScanDue(0, 10., 0.), "first ready scan runs");
static_assert(ScanDue(0, 300., 0.), "slow loading does not consume the first scan");
static_assert(!ScanDue(1, 300., 300.), "first scan cannot immediately repeat");
static_assert(!ScanDue(1, 359.999, 300.), "follow-up waits a full minute from the first scan");
static_assert(ScanDue(1, 360., 300.), "follow-up is due at exactly sixty seconds");
static_assert(ScanDue(1, 365., 300.), "a delayed refresh still runs the follow-up");
static_assert(!ScanDue(2, 365., 300.), "two scans exhaust the session");
static_assert(!ScanDue(2, 999999., 300.), "no ongoing discovery even hours later");
static_assert(ScanDue(0, 10., 0.), "a new session with reset state starts again");

// Compile-time tests execute the same policy used by the game, without loading the engine.
static_assert(AdditionalActive(1, 0, 0, 85, 2) == 2, "singleton needs a supplemental slot");
static_assert(AdditionalActive(2, 2, 0, 85, 2) == 0, "second arrival does not duplicate the floor");
static_assert(AdditionalActive(3, 2, 0, 85, 2) == 1, "incremental third arrival");
static_assert(AdditionalActive(4, 3, 0, 85, 2) == 0, "incremental fourth arrival");
static_assert(AdditionalActive(20, 0, 0, 85, 2) == 17, "batch percentage");
static_assert(AdditionalActive(20, 17, 0, 85, 2) == 0, "repeated batch has no deficit");
static_assert(AdditionalActive(20, 12, 5, 85, 2) == 0, "occupied originals contribute");
static_assert(AdditionalActive(20, 12, 2, 85, 2) == 3, "mixed occupancy");
static_assert(AdditionalActive(3, 9, 0, 85, 2) == 0, "existing bonus nodes never removed");
static_assert(AdditionalActive(100, 0, 0, 0, -1) == 1, "configuration clamps");
static_assert(AdditionalActive(100, 0, 0, 200, 0) == 100, "upper percentage clamp");
static_assert(MatchesOriginalIdentity(true, true, 0., 90000.), "renamed original at its anchor");
static_assert(!MatchesOriginalIdentity(true, true, 1000000., 90000.), "swapped actor names must not select the wrong entry");
static_assert(!MatchesOriginalIdentity(true, false, 0., 90000.), "co-located different resource");
static_assert(!MatchesOriginalIdentity(false, true, 0., 90000.), "co-located different class");
static_assert(!MatchesOriginalIdentity(true, true, 90000., 90000.), "strict radius boundary");
