#pragma once

#include "CoreMinimal.h"

// mcp-contract-1 (2026-11-09): THE ONE LIST of build-marker tags the MCP manifest reports (McpDescribe "buildMarker" / "markers").
// The UE_LOG marker lines in NodeShuffle.cpp StartupModule stay hand-written literals in their established shape (other packets'
// lints pin those lines); tools/check_mcpcontract_lint.ps1 fails unless every "packet marker (" tag and the LOADED tag found there
// appears below. When a packet adds a marker line, append its tag here, NEWEST LAST. When the LOADED line's tag changes, replace
// LoadedLineTag (one value, so the old tag leaves the DLL with it).
namespace NodeShuffleBuildMarkers
{
    // The tag inside NodeShuffle.cpp's "1.4.0 LOADED (...)" line.
    static const TCHAR* const LoadedLineTag = TEXT("2026-10-31-rwg-farspawn-1f");

    // Every "packet marker (...)" tag in NodeShuffle.cpp, in the order those lines appear: oldest first, NEWEST LAST.
    static const TCHAR* const PacketMarkers[] = {
        TEXT("2026-10-31-rwg-rerollwipe-1"),
        TEXT("2026-11-01-rwg-logflood-1"),
        TEXT("2026-11-01-rwg-rerolldup-1"),
        TEXT("2026-11-01-rwg-pinnedspawn-1"),
        TEXT("2026-11-01-rwg-density-1"),
        TEXT("2026-11-01-rwg-thoriumclass-1"),
        TEXT("2026-11-01-rwg-rerolldup-2"),
        TEXT("2026-11-01-rwg-thoriumclass-2"),
        TEXT("2026-11-09-mcp-contract-1"),
        TEXT("2026-11-10-mcp-lastroll-1"),
        TEXT("2026-11-10-mcp-regrade-1"),
        TEXT("2026-11-11-lead-restore-1"),
        TEXT("2026-11-11-thorium-half-1"),
        TEXT("2026-11-13-rwg-0-2-0-1"),
        TEXT("2026-11-13-rwg-0-2-0-2"),
        TEXT("2026-11-14-rollchat-1"),
        TEXT("2026-11-14-rocks-1"),
        TEXT("2026-11-14-quietchat-1"),
        TEXT("2026-11-14-scanfilter-1"),
        TEXT("2026-11-14-scanfilter-2"),
        TEXT("2026-11-14-scanfilter-3"),
        TEXT("2026-11-14-scanfilter-4"),
    };
    static constexpr int32 PacketMarkerCount = UE_ARRAY_COUNT(PacketMarkers);
}
