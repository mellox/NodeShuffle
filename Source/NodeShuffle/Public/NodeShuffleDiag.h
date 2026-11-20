#pragma once

#include "CoreMinimal.h"

// ns-quietlog-1 (2026-11-20): per-topic diagnostic switches. A guarded log line prints when the master switch
// (the EnableDiagnostics config key, pushed through FNodeShuffleModule::SetDiagnosticsEnabled) is on OR its topic is listed
// under [NodeShuffle.Diagnostics] Topics= in Engine.ini. The topic list is read once, at module startup; the NSDIAG line at
// startup prints what was read. Not exported: the NodeShuffleVetoKBFL module never calls it (its lines stay always-on).
enum class ENsDiag : uint8 { Void, Status, Placement, Gates, Spawn, Rocks, Visuals, Wells, Hide, Scanner, Hologram, Veto, Roll, Count };

// True when the master switch is on or Topic is listed. Cheap: one bool read plus one array read.
bool NsDiag(ENsDiag Topic);

namespace NsDiagTopics
{
    // Reads [NodeShuffle.Diagnostics] Topics from GEngineIni and prints the NSDIAG line. First statement of StartupModule.
    void ReadOnce();
    // Prints NSDIAG-MASTER when the master value differs from the value last noted here (first call prints).
    void NoteMaster(bool bNow, const TCHAR* From);
    const TCHAR* Name(ENsDiag T);
}
