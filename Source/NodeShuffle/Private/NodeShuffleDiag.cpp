// ns-quietlog-1 (2026-11-20): topic switches for diagnostic log lines. See NodeShuffleDiag.h.
#include "NodeShuffleDiag.h"
#include "NodeShuffle.h"
#include "Misc/ConfigCacheIni.h"

static bool GNsDiagTopicOn[(int32)ENsDiag::Count] = {};
static int32 GNsDiagLastMaster = -1; // -1 = not noted yet

bool NsDiag(ENsDiag Topic)
{
    return FNodeShuffleModule::AreDiagnosticsEnabled() || ((uint8)Topic < (uint8)ENsDiag::Count && GNsDiagTopicOn[(uint8)Topic]);
}

const TCHAR* NsDiagTopics::Name(ENsDiag T)
{
    static const TCHAR* Names[] = { TEXT("Void"), TEXT("Status"), TEXT("Placement"), TEXT("Gates"), TEXT("Spawn"), TEXT("Rocks"),
        TEXT("Visuals"), TEXT("Wells"), TEXT("Hide"), TEXT("Scanner"), TEXT("Hologram"), TEXT("Veto"), TEXT("Roll") };
    static_assert(UE_ARRAY_COUNT(Names) == (int32)ENsDiag::Count, "one name per topic");
    return (uint8)T < (uint8)ENsDiag::Count ? Names[(uint8)T] : TEXT("?");
}

void NsDiagTopics::ReadOnce()
{
    FString Raw;
    const bool bFound = GConfig && GConfig->GetString(TEXT("NodeShuffle.Diagnostics"), TEXT("Topics"), Raw, GEngineIni);
    TArray<FString> Parts;
    Raw.ParseIntoArray(Parts, TEXT(","), /*InCullEmpty=*/true);
    int32 Unknown = 0;
    for (FString P : Parts)
    {
        P.TrimStartAndEndInline();
        if (P.IsEmpty()) { continue; }
        if (P.Equals(TEXT("All"), ESearchCase::IgnoreCase))
        {
            for (int32 i = 0; i < (int32)ENsDiag::Count; ++i) { GNsDiagTopicOn[i] = true; }
            continue;
        }
        bool bMatched = false;
        for (int32 i = 0; i < (int32)ENsDiag::Count; ++i)
        {
            if (P.Equals(Name((ENsDiag)i), ESearchCase::IgnoreCase)) { GNsDiagTopicOn[i] = true; bMatched = true; }
        }
        if (!bMatched) { ++Unknown; }
    }
    FString On;
    for (int32 i = 0; i < (int32)ENsDiag::Count; ++i)
    {
        if (GNsDiagTopicOn[i]) { On += (On.IsEmpty() ? TEXT("") : TEXT(",")); On += Name((ENsDiag)i); }
    }
    // Startup topics line, always on, once per module start. topics: the topic names switched on from the list read (none when
    // empty); unknownNames: list entries that matched no topic name; source: engineIni when the key was found in GEngineIni,
    // absent when it was not; raw: the string as read. master is not read here: the config is read later, at BeginPlay.
    UE_LOG(LogNodeShuffle, Display, TEXT("NSDIAG: topics=%s unknownNames=%d source=%s section=NodeShuffle.Diagnostics key=Topics raw='%s' master=unknownAtModuleStart"),
        On.IsEmpty() ? TEXT("none") : *On, Unknown, bFound ? TEXT("engineIni") : TEXT("absent"), *Raw);
}

void NsDiagTopics::NoteMaster(bool bNow, const TCHAR* From)
{
    const int32 Now = bNow ? 1 : 0;
    if (Now == GNsDiagLastMaster) { return; }
    // Master-switch change line, always on, printed only when the pushed value differs from the one last noted here.
    // previous is unknown on the first push of the module's life.
    UE_LOG(LogNodeShuffle, Display, TEXT("NSDIAG-MASTER: master=%d previous=%s from=%s"),
        Now, GNsDiagLastMaster < 0 ? TEXT("unknown") : (GNsDiagLastMaster ? TEXT("1") : TEXT("0")), From);
    GNsDiagLastMaster = Now;
}
