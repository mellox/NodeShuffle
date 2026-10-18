#include "../Source/NodeShuffle/Private/NodeShuffleLateNodePolicy.h"
using NodeShuffleLateNodePolicy::AdditionalActive;
using NodeShuffleLateNodePolicy::MatchesOriginalIdentity;
using NodeShuffleLateNodePolicy::ScanDue;

using NodeShuffleLateNodePolicy::GrowthCapReached;
using NodeShuffleLateNodePolicy::ScanPeriodSeconds;
using NodeShuffleLateNodePolicy::GrowthCapEntries;
constexpr double P = ScanPeriodSeconds;
constexpr int Cap = GrowthCapEntries;

// RWG P2g (2026-10-18): the schedule is periodic; the old "two scans exhaust" cases are inverted below, not deleted.
static_assert(ScanDue(0, 10., 0., P, 0, Cap), "first ready scan runs");
static_assert(ScanDue(0, 300., 0., P, 0, Cap), "slow loading does not consume the first scan");
static_assert(!ScanDue(1, 300., 300., P, 0, Cap), "first scan cannot immediately repeat");
static_assert(!ScanDue(1, 359.999, 300., P, 0, Cap), "follow-up waits a full period from the last scan");
static_assert(ScanDue(1, 360., 300., P, 0, Cap), "follow-up is due at exactly one period");
static_assert(ScanDue(1, 365., 300., P, 0, Cap), "a delayed refresh still runs the follow-up");
static_assert(!ScanDue(2, 365., 360., P, 0, Cap), "the period restarts at the scan that just ran");
static_assert(ScanDue(2, 420., 360., P, 0, Cap), "a third scan is due one period after the second (no exhaustion)");
static_assert(ScanDue(0, 10., 0., P, 0, Cap), "a new session with reset state starts again");
static_assert(ScanDue(500, 99999., 99939., P, 37, Cap), "discovery continues hours into the session");
static_assert(!ScanDue(500, 99998.9, 99939., P, 37, Cap), "hours in, the period still gates the next scan");
static_assert(ScanPeriodSeconds == 60.0, "period is 60 s (design input, dated in the header)");
static_assert(GrowthCapEntries == 2048, "growth cap is 2048 entries (design input, dated in the header)");
static_assert(ScanDue(9, 999., 0., P, Cap - 1, Cap), "one entry below the cap still scans");
static_assert(!ScanDue(9, 999., 0., P, Cap, Cap), "reaching the cap stops scanning");
static_assert(!ScanDue(9, 999., 0., P, Cap + 500, Cap), "a scan that overshot the cap stops scanning");
static_assert(!ScanDue(0, 10., 0., P, Cap, Cap), "the cap also blocks a first scan");
static_assert(GrowthCapReached(Cap, Cap) && !GrowthCapReached(Cap - 1, Cap), "cap boundary is inclusive");
static_assert(ScanDue(1, 330., 300., 30., 0, Cap), "a shorter configured period fires sooner");
static_assert(!ScanDue(1, 330., 300., 60., 0, Cap), "the same moment is not due under the 60 s period");

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
