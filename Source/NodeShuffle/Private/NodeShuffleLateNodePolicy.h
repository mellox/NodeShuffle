#pragma once

namespace NodeShuffleLateNodePolicy
{
    // RWG P2g (2026-10-18): periodic session-only schedule, replacing the two-scan window (t=0, +60 s) of 201a764.
    // Design inputs, not physics: one scan per period for the whole session, stopped for good once the session has
    // appended GrowthCapEntries layout entries (the unbounded-growth guard the 2026-10-14 review asked for).
    constexpr double ScanPeriodSeconds = 60.0;
    constexpr int GrowthCapEntries = 2048;

    constexpr bool GrowthCapReached(int TotalAddedSession, int Cap)
    {
        return TotalAddedSession >= Cap;
    }

    // A completed empty scan still restarts the period; nothing here retries faster than one period.
    constexpr bool ScanDue(int CompletedScans, double Now, double LastScanTime, double Period, int TotalAddedSession, int Cap)
    {
        return !GrowthCapReached(TotalAddedSession, Cap) && (CompletedScans == 0 || Now - LastScanTime >= Period);
    }

    constexpr bool MatchesOriginalIdentity(bool ClassMatches, bool ResourceMatches, double DistanceSquared, double RadiusSquared)
    {
        return ClassMatches && ResourceMatches && DistanceSquared < RadiusSquared;
    }

    // Cumulative target; existing active slots and newly protected originals already count.
    // Additional slots may exceed the late-original count to satisfy a new resource's floor.
    constexpr int AdditionalActive(int Originals, int ExistingActive, int NewlyPinned, int Percent, int Minimum)
    {
        const int ClampedPercent = Percent < 1 ? 1 : (Percent > 100 ? 100 : Percent);
        const int RoundedShare = (Originals * ClampedPercent + 50) / 100;
        const int Floor = Minimum < 0 ? 0 : Minimum;
        const int Target = RoundedShare > Floor ? RoundedShare : Floor;
        const int Missing = Target - ExistingActive - NewlyPinned;
        return Missing > 0 ? Missing : 0;
    }
}
