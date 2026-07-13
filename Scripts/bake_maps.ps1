# bakedmaps-2: regenerate the EMBEDDED baked-knowledge header from the live learned stores.
# The SML/UAT packaging pipeline ships ONLY .uplugin + Binaries + cooked Paks (verified 2026-07-13:
# FilterPlugin.ini is ignored; no loose Config files reach the zip or the deployed plugin), so the
# baked water/cave maps ride INSIDE the DLL as chunked string literals instead.
#
# Run BEFORE packaging a release (release checklist): it snapshots the developer's current learned
# maps so fresh installs start with them. Then rebuild.
param(
    [string]$ConfigsDir = "C:\Program Files (x86)\Steam\steamapps\common\Satisfactory\FactoryGame\Configs",
    [string]$OutHeader = "$PSScriptRoot\..\Source\NodeShuffle\Private\NodeShuffleBakedData.h"
)

$ErrorActionPreference = 'Stop'
# MSVC caps a single string literal around 16380 bytes; stay far under it per chunk.
$ChunkSize = 8000

function Convert-ToChunkedLiterals([string]$Json, [string]$ArrayName) {
    # Cold review BLOCKER fix: UE's default TJsonWriter is PRETTY-PRINTED (multi-line, tabs) — raw
    # control chars inside a C string literal are C2001. Re-serialize COMPACT first, then escape
    # backslash and quote, then HARD-FAIL if any control char survives.
    $Json = ($Json | ConvertFrom-Json | ConvertTo-Json -Compress -Depth 10)
    $escaped = $Json.Replace('\', '\\').Replace('"', '\"')
    if ($escaped -match "[`r`n`t]") { throw "control characters survived compaction for $ArrayName — aborting bake" }
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.AppendLine("    static const TCHAR* const $ArrayName[] = {")
    for ($i = 0; $i -lt $escaped.Length; $i += $ChunkSize) {
        $len = [Math]::Min($ChunkSize, $escaped.Length - $i)
        # Never split in the middle of an escape pair.
        while ($len -gt 1 -and $escaped[$i + $len - 1] -eq '\') { $len-- }
        $chunk = $escaped.Substring($i, $len)
        [void]$sb.AppendLine("        TEXT(`"$chunk`"),")
        if ($len -lt $ChunkSize) { $i -= ($ChunkSize - $len) } # account for the shortened chunk
    }
    [void]$sb.AppendLine("    };")
    return $sb.ToString()
}

$water = Get-Content (Join-Path $ConfigsDir "NodeShuffle_WaterGrid.json") -Raw
$caves = Get-Content (Join-Path $ConfigsDir "NodeShuffle_CaveFloors.json") -Raw
# Sanity: both must parse as JSON before we bake them into the binary.
$null = $water | ConvertFrom-Json
$null = $caves | ConvertFrom-Json

$stamp = Get-Date -Format 'yyyy-MM-dd HH:mm'
$header = @"
// GENERATED FILE — DO NOT EDIT. Regenerate with Scripts/bake_maps.ps1 (reads the live learned
// stores from FactoryGame/Configs and snapshots them here). Baked: $stamp.
//
// bakedmaps-2: the packaging pipeline ships only Binaries + cooked Paks, so the shipped water/cave
// knowledge is EMBEDDED in the DLL as chunked string literals (MSVC limits a single literal to
// ~16 KB). NodeShuffleSubsystem assembles + parses these through the same merge path as the local
// learned files (local always wins per cell).
#pragma once

#include "CoreMinimal.h"

namespace NodeShuffleBakedData
{
$(Convert-ToChunkedLiterals $water 'WaterGridChunks')
    static const int32 WaterGridChunkCount = sizeof(WaterGridChunks) / sizeof(WaterGridChunks[0]);

$(Convert-ToChunkedLiterals $caves 'CaveFloorsChunks')
    static const int32 CaveFloorsChunkCount = sizeof(CaveFloorsChunks) / sizeof(CaveFloorsChunks[0]);

    inline FString Assemble(const TCHAR* const* Chunks, int32 Count)
    {
        FString Out;
        for (int32 i = 0; i < Count; i++) { Out += Chunks[i]; }
        return Out;
    }
}
"@
Set-Content -Path $OutHeader -Value $header -Encoding UTF8
$wKB = [Math]::Round($water.Length / 1KB, 1); $cKB = [Math]::Round($caves.Length / 1KB, 1)
Write-Host "baked: water $wKB KB + caves $cKB KB -> $OutHeader"
