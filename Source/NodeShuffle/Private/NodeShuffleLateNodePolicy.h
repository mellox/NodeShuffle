#pragma once

namespace NodeShuffleLateNodePolicy
{
    // Session-only schedule. A completed empty scan still consumes its slot.
    constexpr bool ScanDue(int CompletedScans, double Now, double FirstScanTime)
    {
        return CompletedScans == 0 || (CompletedScans == 1 && Now - FirstScanTime >= 60.0);
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
