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
    const char* const SymCaves = "?CarvedCaves@FPWDensityField@@QEBAAEBVFPWCarvedCaves@@XZ";
    const char* const SymNum = "?Num@FPWCarvedCaves@@QEBAHXZ";
    const char* const SymAnyNear = "?AnyNear@FPWCarvedCaves@@QEBA_NNNN@Z";
    const char* const SymFloor = "?FloorCmAt@FPWCarvedCaves@@QEBA_NNNNAEAN@Z";
    const char* const SymInside = "?IsInsideAt@FPWCarvedCaves@@QEBA_NAEBU?$TVector@N@Math@UE@@@Z";
    const int16 SigCaves[] = { 0x48,0x8D,0x41,0x28,0xC3 };
    const int16 SigNum[] = { 0x8B,0x41,0x08,0xC3 };
    const int16 SigAnyNear[] = { 0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x57,0x48,0x83,0xEC,0x70,0x8B,0x59,0x48,0x48,0x8B,0xF1 };
    const int16 SigFloor[] = { 0x4C,0x8B,0xDC,0x53,0x41,0x55,0x48,0x81,0xEC,0xF8,0x00,0x00,0x00,0x8B,0x59,0x48,0x4C,0x8B,0xE9 };
    const int16 SigInside[] = { 0x48,0x83,0xEC,0x28,0xF2,0x0F,0x10,0x15,-1,-1,-1,-1,0xE8,-1,-1,-1,-1,0x0F,0x57,0xC9,0x66,0x0F,0x2F,0xC8,0x0F,0x97,0xC0 };

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

int32 NodeShuffleRwgGate::CheckCaveSignatures(void* Hmod, FString& OutMissing, FString& OutMismatched)
{
    int32 Bad = 0;
#if PLATFORM_WINDOWS
    HMODULE H = static_cast<HMODULE>(Hmod);
    if (!H) { OutMissing = TEXT("<module null>"); return 0; }
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
    Check(SymCaves, SigCaves, UE_ARRAY_COUNT(SigCaves), TEXT("CarvedCaves"));
    Check(SymNum, SigNum, UE_ARRAY_COUNT(SigNum), TEXT("Num"));
    Check(SymAnyNear, SigAnyNear, UE_ARRAY_COUNT(SigAnyNear), TEXT("AnyNear"));
    Check(SymFloor, SigFloor, UE_ARRAY_COUNT(SigFloor), TEXT("FloorCmAt"));
    Check(SymInside, SigInside, UE_ARRAY_COUNT(SigInside), TEXT("IsInsideAt"));
#else
    OutMissing = TEXT("<notWindows>");
#endif
    UE_LOG(LogNodeShuffle, Display, TEXT("RWGGATE-CAVESIG: unlisted trial, cave half: mismatched=%d missing='%s' mismatchedList='%s' (each item is the export and the offset of its first differing pinned byte; the cave bind re-checks the same five masks later)"),
        Bad, *OutMissing, *OutMismatched);
    return Bad;
}

// ---- bodies. PLAYER-FACING COPY: every sentence is graded in the comment above it. Nothing names a cause ("RWG updated",
// "layout moved") -- the gate measured bytes and a label, and that is all the text asserts.
void NodeShuffleRwgGate::QueueRefusalNotice(const FString& SemVer, int32 Build, const FString& Status, const FString& Detail)
{
    // MEASURED: SemVer/Build are read from the .uplugin beside the bound DLL; Status is the refusal the log prints; Detail is
    // the measured export list behind it. MEASURED: the heading prints " (build N)" only when Build > 0, i.e. a build number was
    // read from the .uplugin; with Build <= 0 (the modulePathUnread / upluginUnread refusals, where nothing was read) the heading
    // names no build at all -- it does not claim build 0. MEASURED: callsAllowed=0 => A.bUsable false => FRwgGroundQuery unusable (no
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

void NodeShuffleRwgGate::QueueUnlistedVerifiedNotice(const FString& SemVer, int32 Build, const FString& Accepted)
{
    // MEASURED: SemVer/Build from the .uplugin; "not on the tested list" = the accept-list walk matched no entry; "all 16
    // functions matched" = this run resolved all 16 exports (bAllSixteenResolved) and the 11 HeightApi + 5 cave masks returned no
    // pinned-byte mismatch -- that is the only path that queues this body. "height API is in use" -- MEASURED: callsAllowed=1. The
    // last sentence is a request, not a claim.
    Queue(TEXT("unlistedVerified"), FString::Printf(
        TEXT("NODE SHUFFLE - Random World Generator %s (build %d) is not on the tested list\n\n")
        TEXT("Tested versions: %s. All 16 RWG functions Node Shuffle uses matched their expected signatures on this build, so the\n")
        TEXT("RWG height API is in use this session. If nodes land in water or over void, include the RWG version when reporting."),
        *SemVer, Build, *Accepted));
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
