// RWG 0.1.12 gate lane (2026-10-26) -- see NodeShuffleRwgGateNotice.h for what this file is and why it exists.
// Every line here either reports a measurement (which export, which byte, which world) or posts the composed body;
// nothing here decides whether the API is usable -- TryBind (NodeShuffleMapModeHeightApi.cpp) does, and calls in.
#include "NodeShuffleRwgGateNotice.h"
#include "NodeShuffle.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
    // ---- cave masks: a COPY of NodeShuffleMapModeCave.cpp's five (names and bytes). check_rwg019_lint.ps1 parses both files
    // and fails when any token differs, so the two cannot drift silently. Build-705 disassembly; -1 = wildcard (IsInsideAt's
    // movsd RIP displacement and call rel32 only). CarvedCaves +0x28 / Num +0x08 / AnyNear and FloorCmAt +0x48 are disp8
    // MEMBER OFFSETS pinned here exactly as the cave file pins them (identical on 705/745/802 -- the 0.1.12 export diff);
    // the cave file's author chose fail-closed on a moved field, and this copy follows the original, not the HeightApi rule.
    const char* const GateSymCaves = "?CarvedCaves@FPWDensityField@@QEBAAEBVFPWCarvedCaves@@XZ";
    const char* const GateSymNum = "?Num@FPWCarvedCaves@@QEBAHXZ";
    const char* const GateSymAnyNear = "?AnyNear@FPWCarvedCaves@@QEBA_NNNN@Z";
    const char* const GateSymFloor = "?FloorCmAt@FPWCarvedCaves@@QEBA_NNNNAEAN@Z";
    const char* const GateSymInside = "?IsInsideAt@FPWCarvedCaves@@QEBA_NAEBU?$TVector@N@Math@UE@@@Z";
    const int16 GateSigCaves[] = { 0x48,0x8D,0x41,0x28,0xC3 };
    const int16 GateSigNum[] = { 0x8B,0x41,0x08,0xC3 };
    const int16 GateSigAnyNear[] = { 0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x57,0x48,0x83,0xEC,0x70,0x8B,0x59,0x48,0x48,0x8B,0xF1 };
    const int16 GateSigFloor[] = { 0x4C,0x8B,0xDC,0x53,0x41,0x55,0x48,0x81,0xEC,0xF8,0x00,0x00,0x00,0x8B,0x59,0x48,0x4C,0x8B,0xE9 };
    const int16 GateSigInside[] = { 0x48,0x83,0xEC,0x28,0xF2,0x0F,0x10,0x15,-1,-1,-1,-1,0xE8,-1,-1,-1,-1,0x0F,0x57,0xC9,0x66,0x0F,0x2F,0xC8,0x0F,0x97,0xC0 };
    // rwg-0-2-0-1 (2026-11-13): GENERATION B (RWG 0.2.0 build 1063, sha256 6AEF57A9652D77AD), a COPY of NodeShuffleMapModeCave.cpp's
    // SigCaves1063 / SigAnyNear1063 / SigFloor1063 (check_rwg020_lint.ps1 pins the pairs token-equal). Member offsets are PINNED to their
    // measured 1063 values, as the generation-A masks above pin theirs (orchestrator decision D2: a moved field fails closed):
    // CarvedCaves disp8 @3 = 0x38 (A 0x28); AnyNear disp8 @22 = 0x50 (A 0x48); FloorCmAt has a new prologue, its disp8 @30 = 0x50 pinned.
    const int16 GateSigCaves1063[] = { 0x48,0x8D,0x41,0x38,0xC3 };
    const int16 GateSigAnyNear1063[] = { 0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x57,0x48,0x83,0xEC,0x70,0x8B,0x59,0x50,0x48,0x8B,0xF1 };
    const int16 GateSigFloor1063[] = { 0x48,0x8B,0xC4,0xF2,0x0F,0x11,0x58,0x20,0xF2,0x0F,0x11,0x50,0x18,0xF2,0x0F,0x11,0x48,0x10,0x53,0x41,0x55,0x48,0x81,0xEC,0x18,0x01,0x00,0x00,0x8B,0x59,0x50,0x4C,0x8B,0xE9 };

    // ---- notice state. Per process, like the gate itself (the RWG DLL never unloads, TryBind runs once).
    constexpr int32 MaxNoticeAttempts = 8; // the drain runs <= 11 times per world (latch + every 60 s while SinceReady <= RwgReprobeWindowTicks = 120), one spent on the hold tick; 8 keeps "abandoned" reachable when a pawn exists by SinceReady = 24, later the window ends first
    struct FRwgGateNotice
    {
        bool bQueued = false;
        FString Kind;                        // "refused" | "unlistedVerified" -- printed, never branched on
        FString Body;
        TWeakObjectPtr<UWorld> PostedWorld;  // the world the body was delivered to (or abandoned for); a new world posts again
        TWeakObjectPtr<UWorld> AttemptWorld; // the world the counters below belong to
        int32 GateTicks = -1;                // -1 = player not yet seen; 0 = seen, hold one more tick
        int32 Attempts = 0;
    };
    FRwgGateNotice& Notice() { static FRwgGateNotice N; return N; }

    void Queue(const TCHAR* Kind, FString Body)
    {
        FRwgGateNotice& N = Notice();
        N.bQueued = true;
        N.Kind = Kind;
        N.Body = MoveTemp(Body);
        N.PostedWorld = nullptr;
        N.AttemptWorld = nullptr;
        N.GateTicks = -1;
        N.Attempts = 0;
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGGATENOTICE: queued kind=%s bodyChars=%d; posted to chat once per world from RwgLogHeightApi (readiness + reprobe ticks) after a player pawn exists"),
            Kind, N.Body.Len());
    }
}

int32 NodeShuffleRwgGate::CheckCaveSignatures(void* Hmod, int32 Gen, FString& OutMissing, FString& OutMismatched)
{
    int32 Bad = 0;
#if PLATFORM_WINDOWS
    HMODULE H = static_cast<HMODULE>(Hmod);
    if (!H) { OutMissing = TEXT("<module null>"); return 0; }
    // rwg-0-2-0-1: the generation the height-API bind accepted picks the three generation-specific masks; Num / IsInsideAt are shared.
    const bool bGenB = Gen == GenB;
    const int16* const CavesSig = bGenB ? GateSigCaves1063 : GateSigCaves;
    const int32 CavesN = bGenB ? static_cast<int32>(UE_ARRAY_COUNT(GateSigCaves1063)) : static_cast<int32>(UE_ARRAY_COUNT(GateSigCaves));
    const int16* const AnyNearSig = bGenB ? GateSigAnyNear1063 : GateSigAnyNear;
    const int32 AnyNearN = bGenB ? static_cast<int32>(UE_ARRAY_COUNT(GateSigAnyNear1063)) : static_cast<int32>(UE_ARRAY_COUNT(GateSigAnyNear));
    const int16* const FloorSig = bGenB ? GateSigFloor1063 : GateSigFloor;
    const int32 FloorN = bGenB ? static_cast<int32>(UE_ARRAY_COUNT(GateSigFloor1063)) : static_cast<int32>(UE_ARRAY_COUNT(GateSigFloor));
    const auto Check = [&](const char* Sym, const int16* Sig, int32 N, const TCHAR* Name)
    {
        const uint8* B = reinterpret_cast<const uint8*>(::GetProcAddress(H, Sym));
        if (!B) { OutMissing += FString(OutMissing.IsEmpty() ? TEXT("") : TEXT(" ")) + Name; return; }
        for (int32 I = 0; I < N; ++I)
        {
            if (Sig[I] >= 0 && B[I] != static_cast<uint8>(Sig[I]))
            {
                ++Bad;
                OutMismatched += FString::Printf(TEXT("%s%s@+%d"), OutMismatched.IsEmpty() ? TEXT("") : TEXT(" "), Name, I);
                return;
            }
        }
    };
    Check(GateSymCaves, CavesSig, CavesN, TEXT("CarvedCaves"));
    Check(GateSymNum, GateSigNum, UE_ARRAY_COUNT(GateSigNum), TEXT("Num"));
    Check(GateSymAnyNear, AnyNearSig, AnyNearN, TEXT("AnyNear"));
    Check(GateSymFloor, FloorSig, FloorN, TEXT("FloorCmAt"));
    Check(GateSymInside, GateSigInside, UE_ARRAY_COUNT(GateSigInside), TEXT("IsInsideAt"));
#else
    OutMissing = TEXT("<notWindows>");
#endif
    UE_LOG(LogNodeShuffle, Display, TEXT("RWGGATE-CAVESIG: unlisted trial, cave half: generation=%s mismatched=%d missing='%s' mismatchedList='%s' (each item is the export and the offset of its first differing pinned byte; the cave bind re-checks the same five masks later)"),
        Gen == GenB ? TEXT("B") : TEXT("A"), Bad, *OutMissing, *OutMismatched);
    return Bad;
}

int32 NodeShuffleRwgGate::ClassifyCaveGenerations(void* Hmod, FString& OutDetail)
{
    int32 Bits = 0;
#if PLATFORM_WINDOWS
    HMODULE H = static_cast<HMODULE>(Hmod);
    if (!H) { OutDetail = TEXT("<module null>"); return 0; }
    const auto Matches = [](const uint8* B, const int16* Sig, int32 N) -> bool
    {
        for (int32 I = 0; I < N; ++I) { if (Sig[I] >= 0 && B[I] != static_cast<uint8>(Sig[I])) { return false; } }
        return true;
    };
    const auto One = [&](const char* Sym, const int16* SigGenA, int32 NA, const int16* SigGenB, int32 NB, const TCHAR* Name)
    {
        const uint8* B = reinterpret_cast<const uint8*>(::GetProcAddress(H, Sym));
        const bool bA = B != nullptr && Matches(B, SigGenA, NA);
        const bool bB = B != nullptr && Matches(B, SigGenB, NB);
        const TCHAR* Label = !B ? TEXT("missing") : ((bA && bB) ? TEXT("both") : (bA ? TEXT("A") : (bB ? TEXT("B") : TEXT("none"))));
        if (bA && !bB) { Bits |= (1 << GenA); }
        if (bB && !bA) { Bits |= (1 << GenB); }
        OutDetail += FString::Printf(TEXT("%s%s:%s"), OutDetail.IsEmpty() ? TEXT("") : TEXT(" "), Name, Label);
    };
    One(GateSymCaves, GateSigCaves, UE_ARRAY_COUNT(GateSigCaves), GateSigCaves1063, UE_ARRAY_COUNT(GateSigCaves1063), TEXT("CarvedCaves"));
    One(GateSymAnyNear, GateSigAnyNear, UE_ARRAY_COUNT(GateSigAnyNear), GateSigAnyNear1063, UE_ARRAY_COUNT(GateSigAnyNear1063), TEXT("AnyNear"));
    One(GateSymFloor, GateSigFloor, UE_ARRAY_COUNT(GateSigFloor), GateSigFloor1063, UE_ARRAY_COUNT(GateSigFloor1063), TEXT("FloorCmAt"));
#else
    OutDetail = TEXT("<notWindows>");
#endif
    return Bits;
}

// ---- bodies. PLAYER-FACING COPY: every sentence is graded in the comment above it. Nothing names a cause ("RWG updated",
// "layout moved") -- the gate measured bytes and a label, and that is all the text asserts.
void NodeShuffleRwgGate::QueueRefusalNotice(const FString& SemVer, int32 Build, const FString& Status, const FString& Detail)
{
    // MEASURED: SemVer/Build are read from the .uplugin beside the bound DLL; Status is the refusal the log prints; Detail is
    // the measured export list behind it. MEASURED: the heading prints " (build N)" only when Build > 0. Build <= 0 means no build number
    // was read: the modulePathUnread / upluginUnread refusals (no .uplugin read), or a .uplugin whose PWBuildNumber is absent or not a number
    // (JsonInt -1); the heading then names no build -- it does not claim build 0 or -1.
    // MEASURED: callsAllowed=0 => A.bUsable false => FRwgGroundQuery unusable (no
    // RWG height-API call) and RwgHeightApiBoundModule null (caves never bind); ROLLCENSUS deal=fallback. NOT CLAIMED: which other NodeShuffle checks
    // (water volumes, killbox actors) still guard the fallback; why the bytes differ.
    const FString BuildClause = Build > 0 ? FString::Printf(TEXT(" (build %d)"), Build) : FString();
    Queue(TEXT("refused"), FString::Printf(
        TEXT("NODE SHUFFLE - Random World Generator %s%s not verified\n\n")
        TEXT("Node Shuffle could not verify the RWG functions it uses: %s (%s).\n")
        TEXT("Node Shuffle will not use RWG's height API (heights, water, void, killbox top, caves) this session; node placement on\n")
        TEXT("this world uses its fallback (line traces). The log line RWGHEIGHTAPI-REFUSED has the details."),
        *SemVer, *BuildClause, *Status, Detail.IsEmpty() ? TEXT("no detail") : *Detail));
}

void NodeShuffleRwgGate::QueueUnlistedVerifiedNotice(const FString& SemVer, int32 Build, const FString& Accepted, int32 Checked)
{
    // MEASURED: SemVer/Build from the .uplugin; "not on the tested list" = the accept-list walk matched no entry; "all N
    // functions matched" = this run resolved all N exports (bAllSixteenResolved, plus on generation B the two edge getters) and their
    // masks (11 HeightApi + 5 cave, + 2 getters on generation B) returned no pinned-byte mismatch -- that is the only path that queues
    // this body; N is passed by the caller (rwg-0-2-0-1: 16 on generation A, 18 on B). rwg-0-2-0-2 F5 (review §5 text, one word changed):
    // "was switched on for this session" -- MEASURED: queued only on the path that sets bUsable=true two lines later; past tense because a
    // later edge-getter refusal can turn it off (that refusal queues its own notice). "relies on" instead of the review's "uses": the four
    // readiness getters (item 4) are LOG-ONLY and NOT in N, so "uses" would overstate N. The last sentence is a request, not a claim.
    const FString BuildClause = Build > 0 ? FString::Printf(TEXT(" (build %d)"), Build) : FString();
    Queue(TEXT("unlistedVerified"), FString::Printf(
        TEXT("NODE SHUFFLE - Random World Generator %s%s is not on the tested list\n\n")
        TEXT("Tested versions: %s. All %d RWG functions Node Shuffle relies on matched their expected signatures on this build, so the\n")
        TEXT("RWG height API was switched on for this session. If nodes land in water or over void, include the RWG version when reporting."),
        *SemVer, *BuildClause, *Accepted, Checked));
}

void NodeShuffleRwgGate::TickNotice(UWorld* World)
{
    FRwgGateNotice& N = Notice();
    if (!N.bQueued || !World) { return; } // the common case on every reprobe tick: one bool
    if (N.PostedWorld.Get() == World) { return; } // delivered (or abandoned) for this world already
    if (N.AttemptWorld.Get() != World) { N.AttemptWorld = World; N.GateTicks = -1; N.Attempts = 0; } // a new world: fresh gates
    const APlayerController* PC = World->GetFirstPlayerController();
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!PC || !Pawn)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGGATENOTICE: held kind=%s pc=%d pawn=%d; the body waits for a player pawn"), *N.Kind, PC ? 1 : 0, Pawn ? 1 : 0);
        return;
    }
    if (N.GateTicks < 0)
    {
        N.GateTicks = 0; // the other two notices hold one tick after the pawn appears (toast timing, ASSUMED); same here
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGGATENOTICE: player pawn present; holding kind=%s one more tick before posting"), *N.Kind);
        return;
    }
    ++N.Attempts;
    if (FNodeShuffleModule::PostChatNotice(World, N.Body, TEXT("RWGGATE")))
    {
        N.PostedWorld = World;
        UE_LOG(LogNodeShuffle, Warning, TEXT("RWGGATENOTICE: posted kind=%s attempt=%d world=%s (CHATNOTICE[RWGGATE] logs the broadcast CALL only; chat-history delivery is not measured and on-screen visibility is not measured by any log line)"),
            *N.Kind, N.Attempts, *World->GetName());
        return;
    }
    if (N.Attempts >= MaxNoticeAttempts)
    {
        N.PostedWorld = World; // stop retrying for this world; a later world tries again
        UE_LOG(LogNodeShuffle, Error, TEXT("RWGGATENOTICE: abandoned kind=%s after %d attempts world=%s; the chat manager never returned an object. The RWGHEIGHTAPI lines above carry the same facts"),
            *N.Kind, N.Attempts, *World->GetName());
    }
}
