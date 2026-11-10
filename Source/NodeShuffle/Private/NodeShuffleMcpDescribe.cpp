// mcp-contract-1 (2026-11-09): McpDescribe -- the contract-v1 manifest (_team/mcp-mod-contract-v1.md). STATIC METADATA ONLY: it reads
// no config, no world and no member, so it answers identically on the CDO, in the menu and in any net mode. Every "note" below is a
// claim about this mod's source and was written from the reader sites cited in the mcp-contract-1 handoff
// (_team/test-logs/2026-11-09-nodeshuffle-mcp-contract-handoff.md). When a reading was ambiguous the MORE restrictive class was chosen.
// COMPLETENESS is linted: every key registered on the config root, every NodeShuffle.* console command and every NodeShuffle.* console
// variable registered in Source/ must appear in the tables below (tools/check_mcpcontract_lint.ps1).

#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMcpCommon.h"
#include "NodeShuffleBuildMarkers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    struct FNsMcpKeyRow { const TCHAR* Key; const TCHAR* Live; bool bTouchesSave; const TCHAR* Note; };

    // live: tick | roll | world-load | inert | never.  persists: cfg for every key (SML config file NodeShuffle.cfg).
    const FNsMcpKeyRow GNsMcpConfigKeys[] = {
        { TEXT("Enabled"), TEXT("tick"), true,
          TEXT("Read each RefreshTick. When false, RefreshTick returns before the first roll, the re-roll and ApplyLayout (spawning, hiding, late scans). That return writes nothing and undoes no stored layout entry.") },
        { TEXT("SeedOverride"), TEXT("roll"), false,
          TEXT("Read only where a roll starts: the first roll and the Re-roll toggle in RefreshTick, and the RWG legacy re-roll in RwgPipelineMayRun. 0 means a seed from FPlatformTime::Cycles().") },
        { TEXT("RerollNow"), TEXT("tick"), true,
          TEXT("THE RE-ROLL TRIGGER. Read each RefreshTick; an off-to-on change re-rolls the whole layout (RollLayout as a re-roll) once IsWorldReadyForReroll passes, and the mod then writes it back to false in config (ClearRerollNowFlag).") },
        { TEXT("ActivePercent"), TEXT("roll"), true,
          TEXT("Read by RollLayout and by each late-original scan (CaptureLateOriginalNodes), which appends layout entries; a changed value shapes the next scan's appended entries as well as the next roll.") },
        { TEXT("NewNodeCount"), TEXT("roll"), false,
          TEXT("Read only by RollLayout (the new-location quota).") },
        { TEXT("MinNodesPerResource"), TEXT("roll"), true,
          TEXT("Read by RollLayout and by each late-original scan (CaptureLateOriginalNodes), which appends layout entries.") },
        { TEXT("MinNodesPerModdedResource"), TEXT("roll"), true,
          TEXT("Read by RollLayout and by each late-original scan (CaptureLateOriginalNodes), which appends layout entries.") },
        { TEXT("RandomizePurity"), TEXT("roll"), true,
          TEXT("Read by RollLayout and by each late-original scan (CaptureLateOriginalNodes), which appends layout entries with the purity it picks.") },
        { TEXT("EnableStarterNodes"), TEXT("roll"), false,
          TEXT("Read by RollLayout on a first roll only (not a re-roll), and only while starter nodes were not yet placed and a player start was captured.") },
        { TEXT("StarterNodeRadiusMeters"), TEXT("roll"), false,
          TEXT("Read only by AppendStarterNodes, which runs inside that same first-roll branch.") },
        { TEXT("SpawnRadiusMeters"), TEXT("tick"), true,
          TEXT("Read each ApplyLayout pass: the distance from a player within which new-location entries get a live node actor spawned (code floor 100 m). A larger value spawns actors for entries that would otherwise stay data.") },
        { TEXT("RwgDensityBudget"), TEXT("roll"), false,
          TEXT("Read only by RollLayout (RwgDensityBudgetBegin). Random World Generator maps only.") },
        { TEXT("IncludeModdedNodes"), TEXT("roll"), true,
          TEXT("Read by RollLayout (node eligibility), by each late-original scan (it gates the scan, which appends layout entries) and by RwgEnsureCaveCheck.") },
        { TEXT("UnlockModdedKnowledge"), TEXT("world-load"), true,
          TEXT("Read by UnlockModdedScannerKnowledge, which runs until it completes once per load and once more after each roll. When true it adds managed modded resources to the game's scanner-unlock list, which the code comment describes as SaveGame and replicated.") },
        { TEXT("ProtectOtherModsNodes"), TEXT("world-load"), true,
          TEXT("Read once per world init by ArmDestroyerVetoIfEnabled, unless the console variable NodeShuffle.DestroyerVeto was set this session, which then decides. It decides whether NodeShuffle answers other mods' node-destroyer checks.") },
        { TEXT("ProtectedForeignResources"), TEXT("world-load"), true,
          TEXT("Array of rows {Resource, Protected}. Rows are latched once per world init (LatchForeignResourceOptOutsFromConfig). The mod itself adds rows to the live config from ApplyLayout (SyncForeignResourceRowsToConfig).") },
        { TEXT("ModdedVanillaCountsOnRwg"), TEXT("roll"), false,
          TEXT("Read only by RollLayout (NodeShuffleRwgSeedGas::DecideAndApply). Random World Generator maps only.") },
        { TEXT("ModdedVanillaCountRows"), TEXT("roll"), false,
          TEXT("Array of rows {ResourcePath, NodeClassPath, VanillaCount}, read from the live config tree by NodeShuffleRwgSeedGas at roll time.") },
        { TEXT("EnableDiagnostics"), TEXT("tick"), false,
          TEXT("Pushed to the module's diagnostics gate each ApplyLayout pass; gates extra log output only.") },
    };

    struct FNsMcpConsoleRow { const TCHAR* Name; const TCHAR* Effect; bool bTouchesSave; const TCHAR* Note; };

    // effect: read = the handler only reads and logs. Handlers that change any member state (session latches, a time-sliced job, a
    // persisted store) are graded write even when the change is diagnostic.
    const FNsMcpConsoleRow GNsMcpConsole[] = {
        { TEXT("NodeShuffle.Here"), TEXT("write"), false,
          TEXT("Logs a census within 300 m of the player. On RWG it reads RwgRejectV3, whose session latches it may grade; writes no layout field.") },
        { TEXT("NodeShuffle.NearestCave"), TEXT("read"), false,
          TEXT("Logs the nearest RWG cave mouth and nearest cave entry (const handler). One line off RWG.") },
        { TEXT("NodeShuffle.SeedHere"), TEXT("write"), true,
          TEXT("Traces up from the player and adds a cell to the cave store (CaveFloors). That store is written to a file outside the save, and later deals can draw from it.") },
        { TEXT("NodeShuffle.AuditPlacements"), TEXT("write"), false,
          TEXT("Starts a time-sliced audit job across ticks (member state) that runs traces over every layout entry and logs counts. Can hitch.") },
        { TEXT("NodeShuffle.DumpExtractors"), TEXT("read"), false,
          TEXT("Logs extractor classes and node groups. Loads asset-registry-named classes (LoadObject), so it can hitch.") },
        { TEXT("NodeShuffle.DumpLayout"), TEXT("write"), false,
          TEXT("RWG only. Logs every entry and a 1000 m grid in one frame (hitches) and posts one chat line. Reads RwgRejectV3, whose session latches it may grade; writes no layout field.") },
        { TEXT("NodeShuffle.ProbeNearestNode"), TEXT("write"), false,
          TEXT("Logs gate readings at the nearest shuffled node's centre. Handler is non-const; not traced to a member write, so graded write.") },
        { TEXT("NodeShuffle.PointAtHere"), TEXT("read"), false,
          TEXT("Traces from the camera and logs enclosure and slope readings at the hit (const handler).") },
        { TEXT("NodeShuffle.DumpWells"), TEXT("read"), false,
          TEXT("Logs a census of every loaded resource well.") },
        { TEXT("NodeShuffle.WellProbe"), TEXT("read"), false,
          TEXT("Logs gate readings for each member of the nearest relocated well group (const handler).") },
        { TEXT("NodeShuffle.WhereCaveNodes"), TEXT("read"), false,
          TEXT("Logs every cave-flagged layout entry (const handler).") },
    };

    // Registered console VARIABLES (not commands). The manifest marks them read-only for the MCP.
    const TCHAR* const GNsMcpCvars[] = {
        TEXT("NodeShuffle.DestroyerVeto"),
        TEXT("NodeShuffle.InsideGateRefuse"),
        TEXT("NodeShuffle.RwgSpawnAllAtRoll"),
        TEXT("NodeShuffle.RwgProvisionalBatchPerPass"),
        TEXT("NodeShuffle.RwgProvisionalBatchMs"),
        TEXT("NodeShuffle.RwgRingRadiusM"),
    };

    TSharedPtr<FJsonValue> NsMcpStr(const TCHAR* S) { return MakeShared<FJsonValueString>(FString(S)); }
}

FString FNodeShuffleMcpReader::Describe()
{
    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("contract"), 1);
    Root->SetStringField(TEXT("mod"), TEXT("NodeShuffle"));
    Root->SetStringField(TEXT("buildMarker"),
        FString(NodeShuffleBuildMarkers::PacketMarkers[NodeShuffleBuildMarkers::PacketMarkerCount - 1]));
    Root->SetStringField(TEXT("loadedLineTag"), FString(NodeShuffleBuildMarkers::LoadedLineTag));
    {
        TArray<TSharedPtr<FJsonValue>> Markers;
        for (int32 I = 0; I < NodeShuffleBuildMarkers::PacketMarkerCount; ++I) { Markers.Add(NsMcpStr(NodeShuffleBuildMarkers::PacketMarkers[I])); }
        Root->SetArrayField(TEXT("markers"), Markers);
    }
    Root->SetArrayField(TEXT("logCategories"), TArray<TSharedPtr<FJsonValue>>{ NsMcpStr(TEXT("LogNodeShuffle")) });

    {
        const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetStringField(TEXT("section"), TEXT("help | near | summary (missing or unknown returns help)"));
        Args->SetStringField(TEXT("xCm"), TEXT("number, near: world X in cm (with yCm), or use usePlayer"));
        Args->SetStringField(TEXT("yCm"), TEXT("number, near: world Y in cm"));
        Args->SetStringField(TEXT("usePlayer"), TEXT("bool, near: true uses the first player pawn's location"));
        Args->SetStringField(TEXT("radiusM"), TEXT("number, near: horizontal radius in m, default 300, cap 2000"));
        Args->SetStringField(TEXT("resource"), TEXT("string, near: case-insensitive substring of the assigned or original resource short name"));
        Args->SetStringField(TEXT("limit"), TEXT("integer, near: entries returned, default 40, cap 80"));
        Root->SetObjectField(TEXT("snapshotArgs"), Args);
    }

    {
        const TSharedRef<FJsonObject> Config = MakeShared<FJsonObject>();
        Config->SetStringField(TEXT("kind"), TEXT("sml-config"));
        Config->SetStringField(TEXT("id"), TEXT("NodeShuffle"));
        const TSharedRef<FJsonObject> Keys = MakeShared<FJsonObject>();
        for (const FNsMcpKeyRow& Row : GNsMcpConfigKeys)
        {
            const TSharedRef<FJsonObject> K = MakeShared<FJsonObject>();
            K->SetStringField(TEXT("live"), Row.Live);
            K->SetStringField(TEXT("persists"), TEXT("cfg"));
            K->SetBoolField(TEXT("touchesSave"), Row.bTouchesSave);
            K->SetStringField(TEXT("note"), Row.Note);
            Keys->SetObjectField(FString(Row.Key), K);
        }
        Config->SetObjectField(TEXT("keys"), Keys);
        Root->SetObjectField(TEXT("config"), Config);
    }

    {
        const TSharedRef<FJsonObject> Cvars = MakeShared<FJsonObject>();
        Cvars->SetStringField(TEXT("prefix"), TEXT("NodeShuffle."));
        Cvars->SetBoolField(TEXT("writable"), false);
        TArray<TSharedPtr<FJsonValue>> Names;
        for (const TCHAR* Name : GNsMcpCvars) { Names.Add(NsMcpStr(Name)); }
        Cvars->SetArrayField(TEXT("names"), Names);
        Root->SetObjectField(TEXT("cvars"), Cvars);
    }

    {
        TArray<TSharedPtr<FJsonValue>> Console;
        for (const FNsMcpConsoleRow& Row : GNsMcpConsole)
        {
            const TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();
            C->SetStringField(TEXT("name"), Row.Name);
            C->SetStringField(TEXT("effect"), Row.Effect);
            C->SetBoolField(TEXT("touchesSave"), Row.bTouchesSave);
            C->SetStringField(TEXT("note"), Row.Note);
            Console.Add(MakeShared<FJsonValueObject>(C));
        }
        Root->SetArrayField(TEXT("console"), Console);
    }

    Root->SetArrayField(TEXT("actions"), TArray<TSharedPtr<FJsonValue>>());

    const FString Out = Serialize(Root);
    // The manifest is fixed text; this guard only fires if a future edit grows it past the contract's cap.
    return Utf8Bytes(Out) <= MaxOutputBytes ? Out : ErrorJson(TEXT("manifest exceeds the 64 KB contract cap"));
}

FString ANodeShuffleSubsystem::McpDescribe() const
{
    const FString Out = FNodeShuffleMcpReader::Describe();
    UE_LOG(LogNodeShuffle, Verbose, TEXT("MCPDESCRIBE: manifest returned, %d chars, cdo %d"), Out.Len(), IsTemplate() ? 1 : 0);
    return Out;
}
