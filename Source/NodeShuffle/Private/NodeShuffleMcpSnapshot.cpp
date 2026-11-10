// mcp-contract-1 (2026-11-09): McpSnapshot -- the READ-ONLY tier-2 snapshot of the MCP <-> mod contract v1 (_team/mcp-mod-contract-v1.md).
// Sections: help (default), near, summary. HARD PROPERTIES (tools/check_mcpcontract_lint.ps1 pins each): the subsystem is reached only
// through a CONST reference; no object/class loading, no traces, sweeps or overlaps, no spawning, no config or save write; no call of
// GetMapMode (it latches the mutable MapMode member -- the raw member is read instead); one Verbose log line per call (in McpSnapshot);
// output capped at FNodeShuffleMcpReader::MaxOutputBytes by dropping tail entries. Every field reports a value read now; none states a cause.

#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMcpCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace
{
    constexpr double NsMcpDefaultRadiusM = 300.0;
    constexpr double NsMcpMaxRadiusM = 2000.0;
    constexpr int32 NsMcpDefaultLimit = 40;
    constexpr int32 NsMcpMaxLimit = 80;
    constexpr int32 NsMcpTopResources = 40;

    const TCHAR* NsMcpMapModeName(ENodeShuffleMapMode Mode)
    {
        switch (Mode)
        {
        case ENodeShuffleMapMode::Unlatched: return TEXT("unlatched");
        case ENodeShuffleMapMode::Vanilla: return TEXT("vanilla");
        case ENodeShuffleMapMode::Rwg: return TEXT("rwg");
        case ENodeShuffleMapMode::Unknown: return TEXT("unknown");
        default: return TEXT("unrecognised-enum-value");
        }
    }

    // Typed argument readers: absent is fine (bPresent false); present with the wrong JSON type sets Err and returns false.
    bool NsMcpArgNumber(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, bool& bPresent, double& Out, FString& Err)
    {
        TSharedPtr<FJsonValue> V;
        if (Args.IsValid()) { V = Args->TryGetField(Name); }
        bPresent = V.IsValid() && V->Type != EJson::Null;
        if (!bPresent) { return true; }
        if (V->Type != EJson::Number) { Err = FString::Printf(TEXT("argument %s must be a number"), Name); return false; }
        Out = V->AsNumber();
        if (!FMath::IsFinite(Out)) { Err = FString::Printf(TEXT("argument %s must be finite"), Name); return false; }
        return true;
    }

    bool NsMcpArgBool(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, bool& bPresent, bool& Out, FString& Err)
    {
        TSharedPtr<FJsonValue> V;
        if (Args.IsValid()) { V = Args->TryGetField(Name); }
        bPresent = V.IsValid() && V->Type != EJson::Null;
        if (!bPresent) { return true; }
        if (V->Type != EJson::Boolean) { Err = FString::Printf(TEXT("argument %s must be true or false"), Name); return false; }
        Out = V->AsBool();
        return true;
    }

    bool NsMcpArgString(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, bool& bPresent, FString& Out, FString& Err)
    {
        TSharedPtr<FJsonValue> V;
        if (Args.IsValid()) { V = Args->TryGetField(Name); }
        bPresent = V.IsValid() && V->Type != EJson::Null;
        if (!bPresent) { return true; }
        if (V->Type != EJson::String) { Err = FString::Printf(TEXT("argument %s must be a string"), Name); return false; }
        Out = V->AsString();
        return true;
    }

    double NsMcpRound1(double V) { return FMath::RoundToDouble(V * 10.0) / 10.0; }
}

FString FNodeShuffleMcpReader::Serialize(const TSharedRef<FJsonObject>& Root)
{
    FString Out;
    const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
    FJsonSerializer::Serialize(Root, Writer);
    return Out;
}

int32 FNodeShuffleMcpReader::Utf8Bytes(const FString& Text)
{
    const FTCHARToUTF8 Utf8(*Text, Text.Len());
    return Utf8.Length();
}

FString FNodeShuffleMcpReader::ErrorJson(const FString& Message)
{
    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("error"), Message);
    return Serialize(Root);
}

FString FNodeShuffleMcpReader::ShortObjectName(const FString& Path)
{
    int32 Dot = INDEX_NONE;
    int32 Colon = INDEX_NONE;
    Path.FindLastChar(TCHAR('.'), Dot);
    Path.FindLastChar(TCHAR(':'), Colon);
    const int32 Cut = FMath::Max(Dot, Colon);
    return Cut == INDEX_NONE ? Path : Path.Mid(Cut + 1);
}

FString FNodeShuffleMcpReader::Help()
{
    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("section"), TEXT("help"));
    const TSharedRef<FJsonObject> Sections = MakeShared<FJsonObject>();
    Sections->SetStringField(TEXT("help"), TEXT("this object; also returned for a missing or unknown section"));
    Sections->SetStringField(TEXT("near"),
        TEXT("layout entries within radiusM of a point, nearest first. Point: xCm + yCm (numbers), or usePlayer true (first player pawn). ")
        TEXT("Optional: radiusM (default 300, cap 2000), resource (case-insensitive substring of the assigned or original resource short name), ")
        TEXT("limit (default 40, cap 80). Distance is horizontal (XY). Top-level: ready, section, pointSource, xCm, yCm, radiusM, radiusCapped, ")
        TEXT("resourceFilter, limit, distanceMode, layoutEntries, inRadius, returned, truncated, cappedBy, entries. Entry: guid, assignedResource, ")
        TEXT("originalResource, nodeClass, xCm, yCm, zCm, distM, active, pinned, rayCasted, isNewNode, underground, rwgCave, live, liveClass, originalObject."));
    Sections->SetStringField(TEXT("summary"),
        TEXT("totals counted now from members (no replay of any roll). Top-level: ready, section, mapMode, layoutGenerated, seed (only when ")
        TEXT("layoutGenerated), layoutVersion, currentLayoutVersion, rwgLayoutVersion, layout{...}, spawnedNodes{...}, hideRecords{...}, wells{...}, ")
        TEXT("session{...}, saved{...}, activeByResource[{resource, active}] (top 40), resourceKindsActive, resourcesTruncated."));
    Root->SetObjectField(TEXT("sections"), Sections);
    Root->SetStringField(TEXT("notReady"), TEXT("near and summary return ready false with the failing predicate while there is no world, on the class default object, or while the layout array is empty"));
    return Serialize(Root);
}

bool FNodeShuffleMcpReader::NotReady(const ANodeShuffleSubsystem& Sub, FString& OutJson, FString& OutBranch)
{
    const TCHAR* Reason = nullptr;
    if (Sub.IsTemplate()) { Reason = TEXT("IsTemplate() is true: this is the class default object"); }
    else if (Sub.GetWorld() == nullptr) { Reason = TEXT("GetWorld() returned null"); }
    else if (Sub.Layout.Num() == 0) { Reason = TEXT("Layout.Num() is 0"); }
    if (!Reason) { return false; }
    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("ready"), false);
    Root->SetStringField(TEXT("reason"), Reason);
    Root->SetBoolField(TEXT("layoutGenerated"), Sub.bLayoutGenerated);
    OutJson = Serialize(Root);
    OutBranch = FString::Printf(TEXT("%s notready: %s"), *OutBranch, Reason);
    return true;
}

FString FNodeShuffleMcpReader::Snapshot(const ANodeShuffleSubsystem& Sub, const FString& ArgsJson, FString& OutBranch)
{
    TSharedPtr<FJsonObject> Args;
    const FString Trimmed = ArgsJson.TrimStartAndEnd();
    if (!Trimmed.IsEmpty())
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
        if (!FJsonSerializer::Deserialize(Reader, Args) || !Args.IsValid())
        {
            OutBranch = TEXT("error: ArgsJson did not parse as a JSON object");
            return ErrorJson(TEXT("ArgsJson must be a JSON object, e.g. {\"section\":\"summary\"}"));
        }
    }
    FString Section;
    bool bHasSection = false;
    FString Err;
    if (!NsMcpArgString(Args, TEXT("section"), bHasSection, Section, Err)) { OutBranch = TEXT("error: section type"); return ErrorJson(Err); }
    if (Section.Equals(TEXT("near"), ESearchCase::IgnoreCase)) { OutBranch = TEXT("near"); return Near(Sub, Args, OutBranch); }
    if (Section.Equals(TEXT("summary"), ESearchCase::IgnoreCase)) { OutBranch = TEXT("summary"); return Summary(Sub, OutBranch); }
    if (!bHasSection) { OutBranch = TEXT("help: no section given"); }
    else if (Section.Equals(TEXT("help"), ESearchCase::IgnoreCase)) { OutBranch = TEXT("help: requested"); }
    else { OutBranch = FString::Printf(TEXT("help: section '%s' is not near, summary or help"), *Section.Left(40)); }
    return Help();
}

FString FNodeShuffleMcpReader::Near(const ANodeShuffleSubsystem& Sub, const TSharedPtr<FJsonObject>& Args, FString& OutBranch)
{
    FString Err;
    bool bHasX = false, bHasY = false, bHasPlayer = false, bHasRadius = false, bHasResource = false, bHasLimit = false;
    double XCm = 0.0, YCm = 0.0, RadiusM = NsMcpDefaultRadiusM, LimitD = NsMcpDefaultLimit;
    bool bUsePlayer = false;
    FString Resource;
    if (!NsMcpArgNumber(Args, TEXT("xCm"), bHasX, XCm, Err) || !NsMcpArgNumber(Args, TEXT("yCm"), bHasY, YCm, Err)
        || !NsMcpArgBool(Args, TEXT("usePlayer"), bHasPlayer, bUsePlayer, Err) || !NsMcpArgNumber(Args, TEXT("radiusM"), bHasRadius, RadiusM, Err)
        || !NsMcpArgString(Args, TEXT("resource"), bHasResource, Resource, Err) || !NsMcpArgNumber(Args, TEXT("limit"), bHasLimit, LimitD, Err))
    {
        OutBranch += TEXT(" error: argument type");
        return ErrorJson(Err);
    }
    if (bUsePlayer && (bHasX || bHasY)) { OutBranch += TEXT(" error: both point forms"); return ErrorJson(TEXT("give either xCm and yCm, or usePlayer true, not both")); }
    if (!bUsePlayer && !(bHasX && bHasY)) { OutBranch += TEXT(" error: no point"); return ErrorJson(TEXT("section near needs a point: xCm and yCm, or usePlayer true")); }
    if (!(RadiusM > 0.0)) { OutBranch += TEXT(" error: radius"); return ErrorJson(TEXT("radiusM must be greater than 0")); }
    if (LimitD < 1.0 || LimitD != FMath::FloorToDouble(LimitD)) { OutBranch += TEXT(" error: limit"); return ErrorJson(TEXT("limit must be a whole number of at least 1")); }
    const bool bRadiusCapped = RadiusM > NsMcpMaxRadiusM;
    RadiusM = FMath::Min(RadiusM, NsMcpMaxRadiusM);
    const int32 Limit = static_cast<int32>(FMath::Min(LimitD, static_cast<double>(NsMcpMaxLimit)));

    FString NotReadyJson;
    if (NotReady(Sub, NotReadyJson, OutBranch)) { return NotReadyJson; }

    const TCHAR* PointSource = TEXT("xy");
    if (bUsePlayer)
    {
        const APawn* FoundPawn = nullptr;
        for (FConstPlayerControllerIterator It = Sub.GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            const APlayerController* Pc = It->Get();
            const APawn* Pawn = Pc ? Pc->GetPawn() : nullptr;
            if (IsValid(Pawn)) { FoundPawn = Pawn; break; }
        }
        if (!FoundPawn) { OutBranch += TEXT(" error: no player pawn"); return ErrorJson(TEXT("usePlayer: no player controller with a valid pawn was found")); }
        const FVector P = FoundPawn->GetActorLocation();
        XCm = P.X;
        YCm = P.Y;
        PointSource = TEXT("player");
    }

    const double RadiusCm = RadiusM * 100.0;
    const double RadiusSq = RadiusCm * RadiusCm;
    TArray<TPair<double, int32>> Hits; // (horizontal distance squared cm, Layout index)
    for (int32 I = 0; I < Sub.Layout.Num(); ++I)
    {
        const FNodeShuffleEntry& E = Sub.Layout[I];
        const double Dx = E.Location.X - XCm;
        const double Dy = E.Location.Y - YCm;
        const double D2 = Dx * Dx + Dy * Dy;
        if (D2 > RadiusSq) { continue; }
        if (bHasResource && !Resource.IsEmpty()
            && !ShortObjectName(E.AssignedResourceClassPath).Contains(Resource, ESearchCase::IgnoreCase)
            && !ShortObjectName(E.OriginalResourceClassPath).Contains(Resource, ESearchCase::IgnoreCase)) { continue; }
        Hits.Emplace(D2, I);
    }
    Hits.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });

    TArray<TSharedPtr<FJsonValue>> Entries;
    const int32 Wanted = FMath::Min(Limit, Hits.Num());
    for (int32 H = 0; H < Wanted; ++H)
    {
        const FNodeShuffleEntry& E = Sub.Layout[Hits[H].Value];
        AFGResourceNode* const* Slot = Sub.SpawnedNodes.Find(E.EntryGuid);
        const AFGResourceNode* LiveNode = (Slot && IsValid(*Slot)) ? *Slot : nullptr;
        const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("guid"), E.EntryGuid.ToString(EGuidFormats::DigitsWithHyphens));
        O->SetStringField(TEXT("assignedResource"), ShortObjectName(E.AssignedResourceClassPath));
        O->SetStringField(TEXT("originalResource"), ShortObjectName(E.OriginalResourceClassPath));
        O->SetStringField(TEXT("nodeClass"), ShortObjectName(E.NodeClassPath));
        O->SetNumberField(TEXT("xCm"), FMath::RoundToDouble(E.Location.X));
        O->SetNumberField(TEXT("yCm"), FMath::RoundToDouble(E.Location.Y));
        O->SetNumberField(TEXT("zCm"), FMath::RoundToDouble(E.Location.Z));
        O->SetNumberField(TEXT("distM"), NsMcpRound1(FMath::Sqrt(Hits[H].Key) / 100.0));
        O->SetBoolField(TEXT("active"), E.bActive);
        O->SetBoolField(TEXT("pinned"), E.bPinned);
        O->SetBoolField(TEXT("rayCasted"), E.bRayCasted);
        O->SetBoolField(TEXT("isNewNode"), E.bIsNewNode);
        O->SetBoolField(TEXT("underground"), E.bUnderground);
        O->SetBoolField(TEXT("rwgCave"), E.bRwgCave);
        O->SetBoolField(TEXT("live"), LiveNode != nullptr);
        O->SetStringField(TEXT("liveClass"), LiveNode ? LiveNode->GetClass()->GetName() : FString());
        O->SetStringField(TEXT("originalObject"), ShortObjectName(E.VanillaNodePath));
        Entries.Add(MakeShared<FJsonValueObject>(O));
    }

    // 64 KB cap: serialize, measure, drop tail entries until it fits.
    int32 Keep = Entries.Num();
    FString Out;
    for (;;)
    {
        const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetBoolField(TEXT("ready"), true);
        Root->SetStringField(TEXT("section"), TEXT("near"));
        Root->SetStringField(TEXT("pointSource"), PointSource);
        Root->SetNumberField(TEXT("xCm"), FMath::RoundToDouble(XCm));
        Root->SetNumberField(TEXT("yCm"), FMath::RoundToDouble(YCm));
        Root->SetNumberField(TEXT("radiusM"), RadiusM);
        Root->SetBoolField(TEXT("radiusCapped"), bRadiusCapped);
        Root->SetStringField(TEXT("resourceFilter"), Resource);
        Root->SetNumberField(TEXT("limit"), Limit);
        Root->SetStringField(TEXT("distanceMode"), TEXT("xy"));
        Root->SetNumberField(TEXT("layoutEntries"), Sub.Layout.Num());
        Root->SetNumberField(TEXT("inRadius"), Hits.Num());
        Root->SetNumberField(TEXT("returned"), Keep);
        Root->SetBoolField(TEXT("truncated"), Keep < Hits.Num());
        Root->SetStringField(TEXT("cappedBy"), Keep < Entries.Num() ? TEXT("bytes") : (Entries.Num() < Hits.Num() ? TEXT("limit") : TEXT("none")));
        Root->SetArrayField(TEXT("entries"), TArray<TSharedPtr<FJsonValue>>(Entries.GetData(), Keep));
        Out = Serialize(Root);
        if (Utf8Bytes(Out) <= MaxOutputBytes) { break; }
        if (Keep == 0) { OutBranch += TEXT(" error: over cap with no entries"); return ErrorJson(TEXT("near: output exceeds the 64 KB cap even with no entries")); }
        --Keep;
    }
    OutBranch += FString::Printf(TEXT(" point %s inRadius %d returned %d"), PointSource, Hits.Num(), Keep);
    return Out;
}

FString FNodeShuffleMcpReader::Summary(const ANodeShuffleSubsystem& Sub, FString& OutBranch)
{
    FString NotReadyJson;
    if (NotReady(Sub, NotReadyJson, OutBranch)) { return NotReadyJson; }

    int32 Active = 0, Pinned = 0, NewNodes = 0, RayCasted = 0, Underground = 0, RwgCave = 0, LiveEntries = 0;
    TMap<FString, int32> ActiveByResource;
    for (const FNodeShuffleEntry& E : Sub.Layout)
    {
        if (E.bActive) { ++Active; ++ActiveByResource.FindOrAdd(ShortObjectName(E.AssignedResourceClassPath)); }
        if (E.bPinned) { ++Pinned; }
        if (E.bIsNewNode) { ++NewNodes; }
        if (E.bRayCasted) { ++RayCasted; }
        if (E.bUnderground) { ++Underground; }
        if (E.bRwgCave) { ++RwgCave; }
        AFGResourceNode* const* Slot = Sub.SpawnedNodes.Find(E.EntryGuid);
        if (Slot && IsValid(*Slot)) { ++LiveEntries; }
    }
    int32 SpawnedValid = 0;
    for (const TPair<FGuid, AFGResourceNode*>& Pair : Sub.SpawnedNodes) { if (IsValid(Pair.Value)) { ++SpawnedValid; } }
    int32 ModdedOrigin = 0;
    for (const FNodeShuffleSuppressedOriginal& R : Sub.OriginalNodeRecord) { if (R.bModdedOrigin) { ++ModdedOrigin; } }
    int32 WManaged = 0, WPinned = 0, WRelocate = 0, WDealt = 0, WPlaced = 0, WFailed = 0, WUnhide = 0;
    for (const FNodeShuffleWellEntry& W : Sub.WellLayout)
    {
        WManaged += W.bManaged ? 1 : 0; WPinned += W.bPinned ? 1 : 0; WRelocate += W.bRelocate ? 1 : 0; WDealt += W.bDestDealt ? 1 : 0;
        WPlaced += W.bGroupPlaced ? 1 : 0; WFailed += W.bRelocationFailed ? 1 : 0; WUnhide += W.bUnhidePending ? 1 : 0;
    }
    TArray<TPair<FString, int32>> Ranked;
    for (const TPair<FString, int32>& P : ActiveByResource) { Ranked.Emplace(P.Key, P.Value); }
    Ranked.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) { return A.Value != B.Value ? A.Value > B.Value : A.Key < B.Key; });

    int32 Keep = FMath::Min(NsMcpTopResources, Ranked.Num());
    FString Out;
    for (;;)
    {
        const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetBoolField(TEXT("ready"), true);
        Root->SetStringField(TEXT("section"), TEXT("summary"));
        Root->SetStringField(TEXT("mapMode"), NsMcpMapModeName(Sub.MapMode)); // raw member; GetMapMode() would latch it
        Root->SetBoolField(TEXT("layoutGenerated"), Sub.bLayoutGenerated);
        if (Sub.bLayoutGenerated) { Root->SetNumberField(TEXT("seed"), Sub.SavedSeed); }
        Root->SetNumberField(TEXT("layoutVersion"), Sub.LayoutVersion);
        Root->SetNumberField(TEXT("currentLayoutVersion"), ANodeShuffleSubsystem::CurrentLayoutVersion);
        Root->SetNumberField(TEXT("rwgLayoutVersion"), Sub.RwgLayoutVersion);

        const TSharedRef<FJsonObject> L = MakeShared<FJsonObject>();
        L->SetNumberField(TEXT("entries"), Sub.Layout.Num());
        L->SetNumberField(TEXT("active"), Active);
        L->SetNumberField(TEXT("pinned"), Pinned);
        L->SetNumberField(TEXT("isNewNode"), NewNodes);
        L->SetNumberField(TEXT("rayCasted"), RayCasted);
        L->SetNumberField(TEXT("underground"), Underground);
        L->SetNumberField(TEXT("rwgCave"), RwgCave);
        L->SetNumberField(TEXT("withLiveActor"), LiveEntries);
        Root->SetObjectField(TEXT("layout"), L);

        const TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
        S->SetNumberField(TEXT("slots"), Sub.SpawnedNodes.Num());
        S->SetNumberField(TEXT("validActors"), SpawnedValid);
        Root->SetObjectField(TEXT("spawnedNodes"), S);

        const TSharedRef<FJsonObject> H = MakeShared<FJsonObject>();
        H->SetNumberField(TEXT("records"), Sub.OriginalNodeRecord.Num());
        H->SetNumberField(TEXT("moddedOrigin"), ModdedOrigin);
        Root->SetObjectField(TEXT("hideRecords"), H);

        const TSharedRef<FJsonObject> W = MakeShared<FJsonObject>();
        W->SetBoolField(TEXT("wellLayoutRolled"), Sub.bWellLayoutRolled);
        W->SetNumberField(TEXT("entries"), Sub.WellLayout.Num());
        W->SetNumberField(TEXT("managed"), WManaged);
        W->SetNumberField(TEXT("pinned"), WPinned);
        W->SetNumberField(TEXT("relocate"), WRelocate);
        W->SetNumberField(TEXT("destDealt"), WDealt);
        W->SetNumberField(TEXT("groupPlaced"), WPlaced);
        W->SetNumberField(TEXT("relocationFailed"), WFailed);
        W->SetNumberField(TEXT("unhidePending"), WUnhide);
        Root->SetObjectField(TEXT("wells"), W);

        // Plain (non-SaveGame) members: values for THIS world session only.
        const TSharedRef<FJsonObject> Ses = MakeShared<FJsonObject>();
        Ses->SetNumberField(TEXT("lateNodeScansCompleted"), Sub.LateNodeScansCompleted);
        Ses->SetNumberField(TEXT("lateNodeEntriesAppended"), Sub.LateNodeTotalAddedSession);
        Ses->SetBoolField(TEXT("knowledgeUnlockDone"), Sub.bKnowledgeUnlockDone);
        Ses->SetBoolField(TEXT("adoptedRestoredNodes"), Sub.bAdoptedRestoredNodes);
        Ses->SetBoolField(TEXT("didInitialApply"), Sub.bDidInitialApply);
        Root->SetObjectField(TEXT("session"), Ses);

        // SaveGame members.
        const TSharedRef<FJsonObject> Sav = MakeShared<FJsonObject>();
        Sav->SetBoolField(TEXT("starterNodesPlaced"), Sub.bStarterNodesPlaced);
        Sav->SetBoolField(TEXT("playerStartCaptured"), Sub.bPlayerStartCaptured);
        Root->SetObjectField(TEXT("saved"), Sav);

        TArray<TSharedPtr<FJsonValue>> Res;
        for (int32 I = 0; I < Keep; ++I)
        {
            const TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
            R->SetStringField(TEXT("resource"), Ranked[I].Key);
            R->SetNumberField(TEXT("active"), Ranked[I].Value);
            Res.Add(MakeShared<FJsonValueObject>(R));
        }
        Root->SetArrayField(TEXT("activeByResource"), Res);
        Root->SetNumberField(TEXT("resourceKindsActive"), Ranked.Num());
        Root->SetBoolField(TEXT("resourcesTruncated"), Keep < Ranked.Num());
        Out = Serialize(Root);
        if (Utf8Bytes(Out) <= MaxOutputBytes) { break; }
        if (Keep == 0) { OutBranch += TEXT(" error: over cap"); return ErrorJson(TEXT("summary: output exceeds the 64 KB cap")); }
        --Keep;
    }
    OutBranch += FString::Printf(TEXT(" entries %d active %d resources %d"), Sub.Layout.Num(), Active, Keep);
    return Out;
}

FString ANodeShuffleSubsystem::McpSnapshot(const FString& ArgsJson)
{
    FString Branch;
    const FString Out = FNodeShuffleMcpReader::Snapshot(static_cast<const ANodeShuffleSubsystem&>(*this), ArgsJson, Branch);
    UE_LOG(LogNodeShuffle, Verbose, TEXT("MCPSNAPSHOT: args %d chars, branch [%s], returned %d chars"), ArgsJson.Len(), *Branch, Out.Len());
    return Out;
}
