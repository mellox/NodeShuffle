// mcp-contract-1 (2026-11-09): McpDescribe -- the contract-v1 manifest (_team/mcp-mod-contract-v1.md, incl. Revision 2). STATIC METADATA
// ONLY: it reads no config, no world and no member, so it answers identically on the CDO, in the menu and in any net mode. Every "note"
// below is a claim about this mod's source, written from the reader sites cited in the mcp-contract-1 handoff
// (_team/test-logs/2026-11-09-nodeshuffle-mcp-contract-handoff.md) and corrected by its cold review
// (_team/test-logs/2026-11-09-nodeshuffle-mcp-contract-review.md). When a reading was ambiguous the MORE restrictive class was chosen.
// COMPLETENESS and the (touchesSave, saveEffect) grades are linted: tools/check_mcpcontract_lint.ps1 (golden table).

#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMcpCommon.h"
#include "NodeShuffleBuildMarkers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    struct FNsMcpKeyRow { const TCHAR* Key; const TCHAR* Live; bool bTouchesSave; const TCHAR* SaveEffect; const TCHAR* Note; };

    // live: tick | roll | world-load | inert | never.  saveEffect (R2.1): none | gameplay | structural.  persists: cfg for every key.
    const FNsMcpKeyRow GNsMcpConfigKeys[] = {
        { TEXT("Enabled"), TEXT("tick"), true, TEXT("structural"),
          TEXT("Read each RefreshTick. When false, RefreshTick returns before the first roll, the re-roll and ApplyLayout (spawning, hiding, late scans). That return changes no layout entry and spawns or hides nothing.") },
        { TEXT("SeedOverride"), TEXT("roll"), false, TEXT("none"),
          TEXT("Read only where a roll starts: the first roll and the Re-roll toggle in RefreshTick, and the RWG legacy re-roll in RwgPipelineMayRun. 0 means a seed from FPlatformTime::Cycles().") },
        { TEXT("RerollNow"), TEXT("tick"), true, TEXT("structural"),
          TEXT("THE RE-ROLL TRIGGER. Read each RefreshTick; an off-to-on change re-rolls the whole layout (RollLayout as a re-roll) once IsWorldReadyForReroll passes, which can wait beyond the tick period, and the mod then writes it back to false in config (ClearRerollNowFlag).") },
        { TEXT("ActivePercent"), TEXT("roll"), true, TEXT("structural"),
          TEXT("Read by RollLayout and by each late-original scan (CaptureLateOriginalNodes), which appends layout entries; a changed value shapes the next scan's appended entries as well as the next roll.") },
        { TEXT("NewNodeCount"), TEXT("roll"), false, TEXT("none"),
          TEXT("Read only by RollLayout (the new-location quota).") },
        { TEXT("MinNodesPerResource"), TEXT("roll"), true, TEXT("structural"),
          TEXT("Read by RollLayout and by each late-original scan (CaptureLateOriginalNodes), which appends layout entries.") },
        { TEXT("MinNodesPerModdedResource"), TEXT("roll"), true, TEXT("structural"),
          TEXT("Read by RollLayout and by each late-original scan (CaptureLateOriginalNodes), which appends layout entries.") },
        { TEXT("RandomizePurity"), TEXT("roll"), true, TEXT("structural"),
          TEXT("Read by RollLayout and by each late-original scan (CaptureLateOriginalNodes), which appends layout entries with the purity it picks.") },
        { TEXT("EnableStarterNodes"), TEXT("roll"), false, TEXT("none"),
          TEXT("Read by RollLayout on a first roll only (not a re-roll), and only while starter nodes were not yet placed and a player start was captured.") },
        { TEXT("StarterNodeRadiusMeters"), TEXT("roll"), false, TEXT("none"),
          TEXT("Read only by AppendStarterNodes, which runs inside that same first-roll branch.") },
        { TEXT("SpawnRadiusMeters"), TEXT("tick"), true, TEXT("structural"),
          TEXT("Read each ApplyLayout pass: the distance from a player within which new-location entries get a live node actor spawned (code floor 100 m). A larger value spawns actors for entries that would otherwise stay data.") },
        { TEXT("RwgDensityBudget"), TEXT("roll"), false, TEXT("none"),
          TEXT("Read only by RollLayout (RwgDensityBudgetBegin).") },
        { TEXT("IncludeModdedNodes"), TEXT("roll"), true, TEXT("structural"),
          TEXT("Read by RollLayout (node eligibility), by each late-original scan (it gates the scan, which appends layout entries) and by RwgEnsureCaveCheck.") },
        { TEXT("UnlockModdedKnowledge"), TEXT("world-load"), true, TEXT("structural"),
          TEXT("Read by UnlockModdedScannerKnowledge, which runs until it completes once per load, and again after each roll and after each late-original scan that re-arms it. When true it adds managed modded resources to the game's scanner-unlock list, which the code comment describes as SaveGame and replicated.") },
        { TEXT("ScannerResourceVisibility"), TEXT("tick"), false, TEXT("none"),
          TEXT("Array of rows {ShowInScanner, Name, Mod, Resource}, read from the live config tree by NodeShuffleScanFilter::Tick on each RefreshTick while Enabled is true (which also adds rows to NodeShuffle.cfg). When that Tick has published a snapshot for the caller's world (authority only, Enabled true, rows reachable), the GetScannableResources hook removes from the list the scanner receives every modded class whose row is false, and every modded class with no row whose path starts with /AllMinable/; otherwise it returns the list unchanged. It never unlocks anything and writes nothing to the save.") },
        { TEXT("ProtectOtherModsNodes"), TEXT("world-load"), true, TEXT("structural"),
          TEXT("Read once per world init by ArmDestroyerVetoIfEnabled through NodeShuffleResolveDestroyerVetoRequested, which reads the console variable NodeShuffle.DestroyerVeto instead when that variable's set-by flag is not ECVF_SetByConstructor. It decides whether NodeShuffle answers other mods' node-destroyer checks.") },
        { TEXT("ProtectedForeignResources"), TEXT("world-load"), true, TEXT("structural"),
          TEXT("Array of rows {Resource, Protected}. Rows are latched once per world init (LatchForeignResourceOptOutsFromConfig). The mod itself adds rows to the live config from ApplyLayout (SyncForeignResourceRowsToConfig).") },
        { TEXT("ModdedVanillaCountsOnRwg"), TEXT("roll"), false, TEXT("none"),
          TEXT("Read only by RollLayout (NodeShuffleRwgSeedGas::DecideAndApply). Random World Generator maps only.") },
        { TEXT("ModdedVanillaCountRows"), TEXT("roll"), false, TEXT("none"),
          TEXT("Array of rows {ResourcePath, NodeClassPath, VanillaCount}, read from the live config tree by NodeShuffleRwgSeedGas at roll time.") },
        { TEXT("EnableDiagnostics"), TEXT("tick"), false, TEXT("none"),
          TEXT("Pushed to the module's diagnostics gate each ApplyLayout pass.") },
    };

    struct FNsMcpConsoleRow { const TCHAR* Name; const TCHAR* Effect; bool bTouchesSave; const TCHAR* SaveEffect; const TCHAR* Note; };

    // effect: read = the handler only reads and logs. Handlers that change any member state (session latches, a time-sliced job, a
    // persisted store) are graded write even when the change is diagnostic.
    // mcp-regrade-1 (2026-11-10; audit in _team/test-logs/2026-11-10-nodeshuffle-mcp-regrade-handoff.md): every grade below was traced
    // from the handler down to its leaf calls. A const method signature is not evidence: this class has mutable members that const
    // functions write (map-mode latch, lazy store loaders, probe counters). Grades hold on every map mode (worst case), and a command that
    // can cause a write to a cross-save store file that later rolls or deals read is touchesSave true / structural (review F1 rule).
    const FNsMcpConsoleRow GNsMcpConsole[] = {
        { TEXT("NodeShuffle.Here"), TEXT("write"), true, TEXT("structural"),
          TEXT("Logs a census within 300 m of the player. Its ground trace at the player's position records one cell in the persistent cross-save water grid (a file outside the save that later rolls and redeals read). On RWG it reads RwgRejectV3, whose session latches it may grade.") },
        { TEXT("NodeShuffle.NearestCave"), TEXT("write"), false, TEXT("none"),
          TEXT("Logs the nearest RWG cave mouth and nearest dealt cave entry; on RWG it reads the per-world cave file carved-caves-<seed>.json under Saved/PWMaps. One line off RWG. Graded write on every map: its first call is GetMapMode, which stores the map mode and map package name in the subsystem when they are not yet latched (BeginPlay latches them only with authority). No trace and no file write.") },
        { TEXT("NodeShuffle.SeedHere"), TEXT("write"), true, TEXT("structural"),
          TEXT("Traces up from the player and adds a cell to the cave store (CaveFloors). That store is written to a file outside the save, and later deals can draw from it.") },
        { TEXT("NodeShuffle.AuditPlacements"), TEXT("write"), true, TEXT("structural"),
          TEXT("Starts a time-sliced audit job across ticks (member state) that traces at every layout entry and logs counts. Its ground traces record no water-grid sample (grid learning is suppressed while a slice runs), but its known-water lookup loads the water grid when it is not yet loaded. On a vanilla map that load merges the embedded baked grid and marks the grid dirty when the merge added cells; the next save or EndPlay then writes Configs/NodeShuffle_WaterGrid.json, a file outside the save that later rolls and redeals read. Other maps write no file.") },
        { TEXT("NodeShuffle.DumpExtractors"), TEXT("write"), false, TEXT("none"),
          TEXT("Logs extractor classes, node groups and the extractor by node-group match matrix. Graded write on every map: it calls LoadObject for the SF+ allow-list asset and for asset-registry-named extractor classes that are not yet in memory, which adds those objects to memory; what such a load runs was not measured. It does not use the NodeShuffle subsystem, traces nothing and writes no file.") },
        { TEXT("NodeShuffle.DumpLayout"), TEXT("write"), false, TEXT("none"),
          TEXT("RWG only. Off RWG it prints one line after GetMapMode, which stores the map mode in the subsystem when it is not yet latched. On RWG it logs every entry and a 1000 m grid in one frame (a single-frame loop; its frame time was not measured), increments a per-process dump id, may build the water-volume cache, samples the grid through RwgRejectV3 only after its sea, void and killbox checks have latched, and broadcasts one chat line; whether chat is kept in the save was not measured. Writes no layout field and no store file.") },
        { TEXT("NodeShuffle.ProbeNearestNode"), TEXT("write"), false, TEXT("none"),
          TEXT("Logs enclosure-gate and containment readings at the nearest shuffled node's recorded centre. No member write was found in the handler (NodeShuffleNodeProbe.cpp) or in the callees read (IsSpotEnclosed in NodeShuffleWellFootprint.cpp, RunTotallyInsideProbe in NodeShuffleTotallyInside.cpp, LogTotallyInsideReading in NodeShuffleTotallyInsideLog.cpp). It runs line traces, sweeps and overlaps. Graded write because the engine-side effects of those queries were not measured.") },
        { TEXT("NodeShuffle.PointAtHere"), TEXT("write"), true, TEXT("structural"),
          TEXT("Traces from the camera and logs enclosure and slope readings at the hit. Its ground trace records one cell in the persistent cross-save water grid (a file outside the save that later rolls and redeals read).") },
        { TEXT("NodeShuffle.ChatProbe"), TEXT("write"), false, TEXT("none"),
          TEXT("ns-rocks-1: posts six labelled chat lines 4 s apart (custom, system, ADA and player message types, an instigator, a line break) through the chat manager, and once per session logs two chat limits read by reflection. Graded write: it arms a timer and sets member state. Whether chat is kept in the save was not measured. No trace, no layout field, no store file.") },
        { TEXT("NodeShuffle.DumpWells"), TEXT("read"), false, TEXT("none"),
          TEXT("Logs a census of every loaded resource well. It runs in one frame. Graded read on every map: the handler has no map-mode branch and never looks up the NodeShuffle subsystem; it iterates fracking cores, satellites and resource nodes, reads the mod's managed-node registry, and writes only its own locals. Assumed to be reads, not traced: the game's GetResourceClass and IsOccupied, actor iteration, actor location and component lookup. The header bodies of the other game accessors it calls return a stored member.") },
        { TEXT("NodeShuffle.WellProbe"), TEXT("write"), true, TEXT("structural"),
          TEXT("Logs gate readings for each member of the nearest relocated well group. Each member's ground trace records a cell in the persistent cross-save water grid (a file outside the save that later rolls and redeals read).") },
        { TEXT("NodeShuffle.WhereCaveNodes"), TEXT("write"), true, TEXT("structural"),
          TEXT("Logs every cave-flagged layout entry. Each row's cave-store lookup loads the cave store when it is not yet loaded. On a vanilla map that load reads Configs/NodeShuffle_CaveFloors.json, merges the embedded baked atlas and marks the store dirty when the merge added cells; the next save or EndPlay then writes that file, which is outside the save and which later deals read. On other maps the load sets the store's loaded flag and prints one CAVESTORE line, and no file is written. It may also store the map mode when not yet latched.") },
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

void FNodeShuffleMcpReader::AddEnvelope(const TSharedRef<FJsonObject>& Root)
{
    Root->SetNumberField(TEXT("contract"), 1);
    Root->SetStringField(TEXT("mod"), TEXT("NodeShuffle"));
    Root->SetStringField(TEXT("buildMarker"), FString(NodeShuffleBuildMarkers::PacketMarkers[NodeShuffleBuildMarkers::PacketMarkerCount - 1]));
}

FString FNodeShuffleMcpReader::Describe()
{
    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    AddEnvelope(Root);
    Root->SetStringField(TEXT("loadedLineTag"), FString(NodeShuffleBuildMarkers::LoadedLineTag));
    {
        TArray<TSharedPtr<FJsonValue>> Markers;
        for (int32 I = 0; I < NodeShuffleBuildMarkers::PacketMarkerCount; ++I) { Markers.Add(NsMcpStr(NodeShuffleBuildMarkers::PacketMarkers[I])); }
        Root->SetArrayField(TEXT("markers"), Markers);
    }
    Root->SetStringField(TEXT("authority"), TEXT("server"));
    Root->SetArrayField(TEXT("logCategories"), TArray<TSharedPtr<FJsonValue>>{ NsMcpStr(TEXT("LogNodeShuffle")) });

    {
        const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetStringField(TEXT("section"), TEXT("help | near | summary | lastRoll (missing returns help; unknown is a bad-args error; lastRoll is filled only in a session where a roll or re-roll ran)"));
        Args->SetStringField(TEXT("xCm"), TEXT("number, near: world X in cm (with yCm); when absent, player selects the point"));
        Args->SetStringField(TEXT("yCm"), TEXT("number, near: world Y in cm (with xCm)"));
        Args->SetStringField(TEXT("player"), TEXT("whole number from 0, near: index among player controllers holding a valid pawn, default 0; not with xCm/yCm"));
        Args->SetStringField(TEXT("radiusM"), TEXT("number, near: horizontal radius in m, default 300, cap 2000"));
        Args->SetStringField(TEXT("resource"), TEXT("string up to 64 chars, near: case-insensitive substring of the assigned or original resource short name"));
        Args->SetStringField(TEXT("count"), TEXT("whole number, near: rows per page, default 40, cap 80"));
        Args->SetStringField(TEXT("offset"), TEXT("whole number, near: rows to skip, default 0"));
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
            if (FCString::Strcmp(Row.Live, TEXT("tick")) == 0) { K->SetNumberField(TEXT("liveLatencySeconds"), 5); }
            K->SetStringField(TEXT("persists"), TEXT("cfg"));
            K->SetBoolField(TEXT("touchesSave"), Row.bTouchesSave);
            K->SetStringField(TEXT("saveEffect"), Row.SaveEffect);
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
            C->SetStringField(TEXT("saveEffect"), Row.SaveEffect);
            C->SetStringField(TEXT("note"), Row.Note);
            Console.Add(MakeShared<FJsonValueObject>(C));
        }
        Root->SetArrayField(TEXT("console"), Console);
    }

    Root->SetArrayField(TEXT("actions"), TArray<TSharedPtr<FJsonValue>>());

    const FString Out = Serialize(Root);
    // The manifest is fixed text; this guard only fires if a future edit grows it past the contract's cap.
    return Utf8Bytes(Out) <= MaxOutputBytes ? Out : ErrorJson(TEXT("too-large"), TEXT("manifest exceeds the 64 KB contract cap"));
}

FString ANodeShuffleSubsystem::McpDescribe() const
{
    const FString Out = FNodeShuffleMcpReader::Describe();
    UE_LOG(LogNodeShuffle, Verbose, TEXT("MCPDESCRIBE: manifest returned, %d chars, cdo %d"), Out.Len(), IsTemplate() ? 1 : 0);
    return Out;
}
