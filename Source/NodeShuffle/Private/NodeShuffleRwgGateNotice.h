#pragma once

// RWG 0.1.12 gate lane (2026-10-26): the two things the RWG height-API GATE needed that do not belong in
// NodeShuffleMapModeHeightApi.cpp (527 lines, over the 500-line rule already):
//   1. the UNLISTED-VERSION TRIAL's cave half -- an RWG build whose {SemVersion, PWBuildNumber} is not on the accept
//      list is TRIED, not refused on its label (user decision 2026-10-26, _team/NodeShuffle-state.md): all 16 bound
//      functions must match their byte masks. Eleven are checked in TryBind; the five cave masks live in
//      NodeShuffleMapModeCave.cpp's lazy bind, which runs only AFTER the height API is usable, so the trial re-declares
//      them here. check_rwg019_lint.ps1 pins the two declarations byte-equal.
//   2. the IN-GAME NOTICE every refusal and every unlisted-verified run owes the player (the 0.1.12 refusal on R18 was
//      log-only; the user saw nothing). Route: FNodeShuffleModule::PostChatNotice (AFGChatManager::BroadcastChatMessage,
//      both symbols already in tools/imports-baseline.txt, so this adds no import). VISIBILITY IS ASSUMED, NOT MEASURED:
//      no record exists of a NodeShuffle chat notice having been SEEN on screen (T55 STATUS, docs/TECH-DEBT.md); the
//      fallback the existing emitter names is AddChatMessageToReceived (NOT in the import baseline -- a measured step).
//      The notice is QUEUED where the gate decides (TryBind, which can run from the roll before a player exists) and
//      DRAINED from RwgLogHeightApi (readiness + every reprobe tick for 10 min), once per world, through the same gates
//      the two existing notices use: player pawn present, one further tick, bounded retries.
#include "CoreMinimal.h"

class UWorld;

namespace NodeShuffleRwgGate
{
    // Unlisted trial, cave half: resolves the five FPWCarvedCaves-group exports from the bound RWG module and compares
    // their leading bytes with the masks (a copy of NodeShuffleMapModeCave.cpp's). Returns the number of exports whose
    // PINNED bytes differed; OutMissing / OutMismatched carry the export names. A missing export is not a mismatch.
    int32 CheckCaveSignatures(void* Hmod, FString& OutMissing, FString& OutMismatched);

    // Queue the player notice for a REFUSAL (callsAllowed=0): Status is the refusal status the log prints, Detail the
    // measured list behind it (mismatched exports, missing exports). Replaces any earlier queued body (the gate binds once
    // per process; the latest verdict is the one to show).
    void QueueRefusalNotice(const FString& SemVer, int32 Build, const FString& Status, const FString& Detail);
    // Queue the player notice for an UNLISTED build that passed the full check (callsAllowed=1).
    void QueueUnlistedVerifiedNotice(const FString& SemVer, int32 Build, const FString& Accepted);
    // Drain: posts the queued body to chat once per World (a second save loaded in the same process gets it again, because
    // the gate verdict still holds there). Cheap when nothing is queued. Game thread.
    void TickNotice(UWorld* World);
}
