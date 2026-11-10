#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class ANodeShuffleSubsystem;

// mcp-contract-1 (2026-11-09): the read side of the MCP <-> mod contract v1 (_team/mcp-mod-contract-v1.md).
// ANodeShuffleSubsystem befriends this struct. Every function that touches the subsystem takes it as a CONST reference, so a member
// write cannot compile; check_mcpcontract_lint.ps1 also forbids const_cast, GetMapMode( (it latches a mutable member), loads, traces,
// spawns and config/save writes in NodeShuffleMcp*.cpp. Nothing here includes, names or depends on the MCP mod.
struct FNodeShuffleMcpReader
{
    // Contract: "at most 64 KB". Measured as UTF-8 bytes of the serialized text (the MCP transports UTF-8).
    static constexpr int32 MaxOutputBytes = 64 * 1024;

    // NodeShuffleMcpDescribe.cpp -- static manifest; reads no instance state.
    static FString Describe();

    // NodeShuffleMcpSnapshot.cpp -- OutBranch names the branch taken, for the caller's single Verbose log line.
    static FString Snapshot(const ANodeShuffleSubsystem& Sub, const FString& ArgsJson, FString& OutBranch);

    // Shared helpers (NodeShuffleMcpSnapshot.cpp).
    static FString Serialize(const TSharedRef<FJsonObject>& Root);
    static int32 Utf8Bytes(const FString& Text);
    static FString ErrorJson(const FString& Message);
    static FString ShortObjectName(const FString& Path);

private:
    static FString Help();
    static FString Near(const ANodeShuffleSubsystem& Sub, const TSharedPtr<FJsonObject>& Args, FString& OutBranch);
    static FString Summary(const ANodeShuffleSubsystem& Sub, FString& OutBranch);
    static bool NotReady(const ANodeShuffleSubsystem& Sub, FString& OutJson, FString& OutBranch);
};
