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
    constexpr int32 NsMcpDefaultCount = 40;
    constexpr int32 NsMcpMaxCount = 80;
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

FString FNodeShuffleMcpReader::ErrorJson(const TCHAR* Kind, const FString& Message)
{
    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    AddEnvelope(Root);
    Root->SetStringField(TEXT("errorKind"), Kind);
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
    AddEnvelope(Root);
    Root->SetStringField(TEXT("section"), TEXT("help"));
    const TSharedRef<FJsonObject> Sections = MakeShared<FJsonObject>();
    Sections->SetStringField(TEXT("help"), TEXT("this object; also returned when no section is given. An unknown section or argument name is a bad-args error."));
    Sections->SetStringField(TEXT("near"),
        TEXT("layout entries within radiusM of a point, nearest first. Point: xCm + yCm (numbers), or else player (whole number from 0, default 0: ")
        TEXT("the index among player controllers that hold a valid pawn). Optional: radiusM (default 300, cap 2000), resource (at most 64 characters, ")
        TEXT("case-insensitive substring of the assigned or original resource short name), count (default 40, cap 80), offset (default 0). ")
        TEXT("Distance is horizontal (XY). Top-level: contract, mod, buildMarker, ready, section, pointSource, xCm, yCm, radiusM, radiusCapped, ")
        TEXT("resourceFilter, count, countCapped, offset, distanceMode, layoutEntries, inRadius, nonFiniteSkipped (Layout entries whose ")
        TEXT("location has a non-finite X, Y or Z, counted over the whole Layout before the radius and resource filters; never returned as rows), returned, truncated (rows dropped by the 64 KB cap), ")
        TEXT("hasMore, cappedBy, rows. Row: guid, assignedResource, originalResource, nodeClass, xCm, yCm, zCm, distM, active, pinned, rayCasted, ")
        TEXT("isNewNode, underground, rwgCave, spawnedActor, spawnedClass, originalObject. spawnedActor: a valid actor is held in SpawnedNodes ")
        TEXT("for this entry's guid (new-location entries only; an original kept in place, isNewNode false, is never held there)."));
    Sections->SetStringField(TEXT("summary"),
        TEXT("totals counted now from members (no replay of any roll). Top-level: contract, mod, buildMarker, ready, section, mapMode, layoutGenerated, ")
        TEXT("seed (only when layoutGenerated), layoutVersion, currentLayoutVersion, rwgLayoutVersion, layout{...}, spawnedNodes{...}, hideRecords{...}, ")
        TEXT("wells{...}, session{...}, saved{...}, activeByResource[{resource, active}] (top 40), resourceKindsActive, resourcesTruncated."));
    Sections->SetStringField(TEXT("lastRoll"),
        TEXT("censuses the roll code copied as it printed them, during the latest entry into the roll routine in THIS session; never saved, so ")
        TEXT("it is filled only after the roll routine ran since this subsystem was created (while rollSeq is 0, every family reads captured false). Never not-ready. ")
        TEXT("Top-level: contract, mod, buildMarker, section, captured (rollSeq above 0), rollSeq (entries into the roll routine this session, ")
        TEXT("including one that aborted), note (only while rollSeq is 0), seed and isReroll (only when captured: the arguments of that entry), ")
        TEXT("rollDensity{...}. Each family object carries captured (its copy ran, right after its census log statement, during entry ")
        TEXT("rollSeq); a family whose copy did not run in that roll reads captured false and carries no other field. rollDensity: the ROLLDENSITY ")
        TEXT("line's fields (printed on Random World Generator maps only), without cellsAtCap; flags as booleans; cellCm and landSampleMs ")
        TEXT("rounded to whole numbers and left out when not finite, counted in nonFiniteOmitted. Not served: REROLLPOOL-CARRY, PURITYCENSUS, ")
        TEXT("RWGDEAL-RES and MODQUOTA-CFG."));
    Root->SetObjectField(TEXT("sections"), Sections);
    Root->SetStringField(TEXT("errors"), TEXT("every failure is an object with errorKind (bad-args, no-world, no-player, not-ready, too-large) and error; not-ready and no-world also carry ready false, reason and layoutGenerated"));
    return Serialize(Root);
}

bool FNodeShuffleMcpReader::NotReady(const ANodeShuffleSubsystem& Sub, FString& OutJson, FString& OutBranch)
{
    const TCHAR* Reason = nullptr;
    const TCHAR* Kind = TEXT("not-ready");
    if (Sub.IsTemplate()) { Reason = TEXT("IsTemplate() is true: this is the class default object"); Kind = TEXT("no-world"); }
    else if (Sub.GetWorld() == nullptr) { Reason = TEXT("GetWorld() returned null"); Kind = TEXT("no-world"); }
    else if (Sub.Layout.Num() == 0) { Reason = TEXT("Layout.Num() is 0"); }
    if (!Reason) { return false; }
    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    AddEnvelope(Root);
    Root->SetBoolField(TEXT("ready"), false);
    Root->SetStringField(TEXT("reason"), Reason);
    Root->SetBoolField(TEXT("layoutGenerated"), Sub.bLayoutGenerated);
    Root->SetStringField(TEXT("errorKind"), Kind);
    Root->SetStringField(TEXT("error"), Reason);
    OutJson = Serialize(Root);
    OutBranch = FString::Printf(TEXT("%s notready: %s"), *OutBranch, Reason);
    return true;
}

FString FNodeShuffleMcpReader::Snapshot(const ANodeShuffleSubsystem& Sub, const FString& ArgsJson, FString& OutBranch)
{
    if (ArgsJson.Len() > 8192) { OutBranch = TEXT("error: ArgsJson over 8192 chars"); return ErrorJson(TEXT("bad-args"), TEXT("ArgsJson must be at most 8192 characters")); }
    TSharedPtr<FJsonObject> Args;
    const FString Trimmed = ArgsJson.TrimStartAndEnd();
    if (!Trimmed.IsEmpty())
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
        if (!FJsonSerializer::Deserialize(Reader, Args) || !Args.IsValid())
        {
            OutBranch = TEXT("error: ArgsJson did not parse as a JSON object");
            return ErrorJson(TEXT("bad-args"), TEXT("ArgsJson must be a JSON object, e.g. {\"section\":\"summary\"}"));
        }
    }
    static const TCHAR* const NsMcpKnownArgs[] = { TEXT("section"), TEXT("xCm"), TEXT("yCm"), TEXT("player"), TEXT("radiusM"), TEXT("resource"), TEXT("count"), TEXT("offset") };
    if (Args.IsValid()) { for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Args->Values) { bool bKnown = false;
      for (const TCHAR* K : NsMcpKnownArgs) { if (KV.Key.Equals(K, ESearchCase::IgnoreCase)) { bKnown = true; break; } }
      if (!bKnown) { OutBranch = TEXT("error: unknown argument"); return ErrorJson(TEXT("bad-args"), FString::Printf(TEXT("unknown argument '%s'; known: section, xCm, yCm, player, radiusM, resource, count, offset"), *KV.Key.Left(40))); } } }
    FString Section;
    bool bHasSection = false;
    FString Err;
    if (!NsMcpArgString(Args, TEXT("section"), bHasSection, Section, Err)) { OutBranch = TEXT("error: section type"); return ErrorJson(TEXT("bad-args"), Err); }
    if (Section.Equals(TEXT("near"), ESearchCase::IgnoreCase)) { OutBranch = TEXT("near"); return Near(Sub, Args, OutBranch); }
    if (Section.Equals(TEXT("summary"), ESearchCase::IgnoreCase)) { OutBranch = TEXT("summary"); return Summary(Sub, OutBranch); }
    if (Section.Equals(TEXT("lastRoll"), ESearchCase::IgnoreCase)) { OutBranch = TEXT("lastRoll"); return LastRoll(Sub, OutBranch); } // mcp-lastroll-1
    if (!bHasSection) { OutBranch = TEXT("help: no section given"); return Help(); }
    if (Section.Equals(TEXT("help"), ESearchCase::IgnoreCase)) { OutBranch = TEXT("help: requested"); return Help(); }
    OutBranch = TEXT("error: unknown section");
    return ErrorJson(TEXT("bad-args"), FString::Printf(TEXT("section '%s' is not help, near, summary or lastRoll"), *Section.Left(40)));
}

FString FNodeShuffleMcpReader::Near(const ANodeShuffleSubsystem& Sub, const TSharedPtr<FJsonObject>& Args, FString& OutBranch)
{
    FString Err;
    bool bHasX = false, bHasY = false, bHasPlayer = false, bHasRadius = false, bHasResource = false, bHasCount = false, bHasOffset = false;
    double XCm = 0.0, YCm = 0.0, PlayerD = 0.0, RadiusM = NsMcpDefaultRadiusM, CountD = NsMcpDefaultCount, OffsetD = 0.0;
    FString Resource;
    if (!NsMcpArgNumber(Args, TEXT("xCm"), bHasX, XCm, Err) || !NsMcpArgNumber(Args, TEXT("yCm"), bHasY, YCm, Err)
        || !NsMcpArgNumber(Args, TEXT("player"), bHasPlayer, PlayerD, Err) || !NsMcpArgNumber(Args, TEXT("radiusM"), bHasRadius, RadiusM, Err)
        || !NsMcpArgString(Args, TEXT("resource"), bHasResource, Resource, Err) || !NsMcpArgNumber(Args, TEXT("count"), bHasCount, CountD, Err)
        || !NsMcpArgNumber(Args, TEXT("offset"), bHasOffset, OffsetD, Err))
    {
        OutBranch += TEXT(" error: argument type");
        return ErrorJson(TEXT("bad-args"), Err);
    }
    if (Resource.Len() > 64) { OutBranch += TEXT(" error: resource length"); return ErrorJson(TEXT("bad-args"), TEXT("resource must be at most 64 characters")); }
    if (bHasPlayer && (bHasX || bHasY)) { OutBranch += TEXT(" error: both point forms"); return ErrorJson(TEXT("bad-args"), TEXT("give either xCm and yCm, or player, not both")); }
    if (bHasX != bHasY) { OutBranch += TEXT(" error: half a point"); return ErrorJson(TEXT("bad-args"), TEXT("xCm and yCm must be given together")); }
    if (!(RadiusM > 0.0)) { OutBranch += TEXT(" error: radius"); return ErrorJson(TEXT("bad-args"), TEXT("radiusM must be greater than 0")); }
    if (CountD < 1.0 || CountD != FMath::FloorToDouble(CountD)) { OutBranch += TEXT(" error: count"); return ErrorJson(TEXT("bad-args"), TEXT("count must be a whole number of at least 1")); }
    if (OffsetD < 0.0 || OffsetD != FMath::FloorToDouble(OffsetD) || OffsetD > 1.0e9) { OutBranch += TEXT(" error: offset"); return ErrorJson(TEXT("bad-args"), TEXT("offset must be a whole number from 0 to 1e9")); }
    if (PlayerD < 0.0 || PlayerD != FMath::FloorToDouble(PlayerD) || PlayerD > 1.0e6) { OutBranch += TEXT(" error: player"); return ErrorJson(TEXT("bad-args"), TEXT("player must be a whole number from 0 to 1e6")); }
    const bool bRadiusCapped = RadiusM > NsMcpMaxRadiusM;
    RadiusM = FMath::Min(RadiusM, NsMcpMaxRadiusM);
    const bool bCountCapped = CountD > NsMcpMaxCount;
    const int32 Count = static_cast<int32>(FMath::Min(CountD, static_cast<double>(NsMcpMaxCount)));
    const int32 Offset = static_cast<int32>(OffsetD);
    const int32 PlayerIndex = static_cast<int32>(PlayerD);

    FString NotReadyJson;
    if (NotReady(Sub, NotReadyJson, OutBranch)) { return NotReadyJson; }

    const TCHAR* PointSource = TEXT("xy");
    if (!bHasX)
    {
        const APawn* FoundPawn = nullptr;
        int32 Seen = 0;
        for (FConstPlayerControllerIterator It = Sub.GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            const APlayerController* Pc = It->Get();
            const APawn* Pawn = Pc ? Pc->GetPawn() : nullptr;
            if (!IsValid(Pawn)) { continue; }
            if (Seen == PlayerIndex) { FoundPawn = Pawn; break; }
            ++Seen;
        }
        if (!FoundPawn) { OutBranch += TEXT(" error: no player pawn"); return ErrorJson(TEXT("no-player"), FString::Printf(TEXT("player %d: only %d player controller(s) hold a valid pawn"), PlayerIndex, Seen)); }
        const FVector P = FoundPawn->GetActorLocation();
        if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y)) { OutBranch += TEXT(" error: pawn location not finite"); return ErrorJson(TEXT("no-player"), FString::Printf(TEXT("player %d: the pawn location has a non-finite X or Y component"), PlayerIndex)); }
        XCm = P.X;
        YCm = P.Y;
        PointSource = TEXT("player");
    }

    const double RadiusCm = RadiusM * 100.0;
    const double RadiusSq = RadiusCm * RadiusCm;
    TArray<TPair<double, int32>> Hits; // (horizontal distance squared cm, Layout index)
    int32 NonFiniteSkipped = 0; // contract R4.4: replies carry finite numbers only
    for (int32 I = 0; I < Sub.Layout.Num(); ++I)
    {
        const FNodeShuffleEntry& E = Sub.Layout[I];
        if (!FMath::IsFinite(E.Location.X) || !FMath::IsFinite(E.Location.Y) || !FMath::IsFinite(E.Location.Z)) { ++NonFiniteSkipped; continue; }
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
    const int32 Start = FMath::Min(Offset, Hits.Num());
    const int32 Wanted = FMath::Min(Count, Hits.Num() - Start);
    for (int32 H = Start; H < Start + Wanted; ++H)
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
        O->SetBoolField(TEXT("spawnedActor"), LiveNode != nullptr);
        O->SetStringField(TEXT("spawnedClass"), LiveNode ? LiveNode->GetClass()->GetName() : FString());
        O->SetStringField(TEXT("originalObject"), ShortObjectName(E.VanillaNodePath));
        Entries.Add(MakeShared<FJsonValueObject>(O));
    }

    // 64 KB cap: serialize, measure; when over, shrink the kept rows in proportion to the overshoot (always by at least one) and retry.
    int32 Keep = Entries.Num();
    FString Out;
    for (;;)
    {
        const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        AddEnvelope(Root);
        Root->SetBoolField(TEXT("ready"), true);
        Root->SetStringField(TEXT("section"), TEXT("near"));
        Root->SetStringField(TEXT("pointSource"), PointSource);
        Root->SetNumberField(TEXT("xCm"), FMath::RoundToDouble(XCm));
        Root->SetNumberField(TEXT("yCm"), FMath::RoundToDouble(YCm));
        Root->SetNumberField(TEXT("radiusM"), RadiusM);
        Root->SetBoolField(TEXT("radiusCapped"), bRadiusCapped);
        Root->SetStringField(TEXT("resourceFilter"), Resource);
        Root->SetNumberField(TEXT("count"), Count);
        Root->SetBoolField(TEXT("countCapped"), bCountCapped);
        Root->SetStringField(TEXT("distanceMode"), TEXT("xy"));
        Root->SetNumberField(TEXT("layoutEntries"), Sub.Layout.Num());
        Root->SetNumberField(TEXT("inRadius"), Hits.Num());
        Root->SetNumberField(TEXT("nonFiniteSkipped"), NonFiniteSkipped);
        Root->SetNumberField(TEXT("offset"), Start);
        Root->SetNumberField(TEXT("returned"), Keep);
        Root->SetBoolField(TEXT("hasMore"), Start + Keep < Hits.Num());
        Root->SetBoolField(TEXT("truncated"), Keep < Wanted); // rows dropped by the byte cap
        Root->SetStringField(TEXT("cappedBy"), Keep < Wanted ? TEXT("bytes") : (Start + Wanted < Hits.Num() ? TEXT("count") : TEXT("none")));
        Root->SetArrayField(TEXT("rows"), TArray<TSharedPtr<FJsonValue>>(Entries.GetData(), Keep));
        Out = Serialize(Root);
        const int32 Bytes = Utf8Bytes(Out);
        if (Bytes <= MaxOutputBytes) { break; }
        if (Keep == 0) { OutBranch += TEXT(" error: over cap with no rows"); return ErrorJson(TEXT("too-large"), TEXT("near: output exceeds the 64 KB cap even with no rows")); }
        Keep = FMath::Min(Keep - 1, static_cast<int32>(static_cast<int64>(Keep) * MaxOutputBytes / Bytes));
    }
    OutBranch += FString::Printf(TEXT(" point %s inRadius %d nonFiniteSkipped %d offset %d returned %d"), PointSource, Hits.Num(), NonFiniteSkipped, Start, Keep);
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
        AddEnvelope(Root);
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
        L->SetNumberField(TEXT("withSpawnedActor"), LiveEntries);
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
        const int32 Bytes = Utf8Bytes(Out);
        if (Bytes <= MaxOutputBytes) { break; }
        if (Keep == 0) { OutBranch += TEXT(" error: over cap"); return ErrorJson(TEXT("too-large"), TEXT("summary: output exceeds the 64 KB cap")); }
        Keep = FMath::Min(Keep - 1, static_cast<int32>(static_cast<int64>(Keep) * MaxOutputBytes / Bytes));
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
