// RWG P2a (2026-10-17) -- RWGHEIGHTAPI: an OBSERVE-ONLY probe of RWG's native height/water query.
// Brief: _team/nodeshuffle-rwg-p2-spec.md "Addendum 2"; research: _team/rwg-map-persistence-research.md §4/§7.
// Reached only from RwgLogReprobes (NodeShuffleMapModeR1.cpp), i.e. only off vanilla, only after the v2 latch.
// It resolves six RWG exports by DECORATED name at runtime (GetModuleHandleW + GetProcAddress, no link
// dependency, so nothing here enters the import table but KERNEL32), refuses to call unless the RWG .uplugin
// next to the bound DLL names an accepted build, and compares the API's surface height with a line trace at
// the same XY. It gates nothing, writes nothing and changes no node: every result is a log line.
#include "NodeShuffleSubsystem.h"
#include "NodeShuffleMapMode.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Templates/SharedPointer.h"
#include "Components/PrimitiveComponent.h"
#include "UObject/UnrealType.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
#include <limits>

class FPWDensityField; // RWG's type. Never completed here: only an opaque pointer and a TSharedPtr slot are used.

namespace
{
    // ACCEPTED RWG BUILDS -- code constants, deliberately NOT a config row. Each {SemVersion, PWBuildNumber} pair was measured
    // (dumpbin /EXPORTS + /DISASM on FactoryGameSteam-ProceduralWorld-Win64-Shipping.dll):
    //   0.1.7 build 705, sha256 EE60A003A44A4ABC..., 2026-10-17;
    //   0.1.9 build 745, sha256 93516E7F09E60F27..., 2026-10-18 (_team/test-logs/2026-10-18-rwg-0.1.9-export-diff.md: 14 of 16
    //   bound prefixes byte-identical; GetField + GetKillboxTop differ ONLY in member displacements, which the masks below wildcard).
    // SemVersion AND build must both equal ONE entry; anything else is refused, never called. Add an entry only after re-measuring
    // every bound export on that build AND re-running the RWGHEIGHTAPI census on it.
    struct FRwgAcceptedBuild { const TCHAR* SemVer; int32 Build; };
    const FRwgAcceptedBuild RwgHeightApiAccepted[] = { { TEXT("0.1.7"), 705 }, { TEXT("0.1.9"), 745 } };
    // RWG 0.1.9 (2026-10-18): the accept list as printed by the census and the refusals, e.g. {0.1.7,705} {0.1.9,745}.
    FString RwgAcceptedList()
    {
        FString S;
        for (const FRwgAcceptedBuild& E : RwgHeightApiAccepted) { S += FString::Printf(TEXT("%s{%s,%d}"), S.IsEmpty() ? TEXT("") : TEXT(" "), E.SemVer, E.Build); }
        return S;
    }

    // x64 MSVC MEMBER ABI -- ASSUMED, graded by the census below (|dZ| vs line trace), never by reasoning:
    // a non-static member function is called as a free function with `this` as the first argument (RCX);
    // double arguments then take XMM1/XMM2 (positional), an int the next integer slot; a member returning a
    // class by value (GetField's TSharedPtr) takes a hidden return-slot pointer as the SECOND argument (RDX)
    // and returns that pointer. `$00` in GetField's name = ESPMode 1 = ThreadSafe. Static Get takes no `this`.
    using FFieldPtr = TSharedPtr<const FPWDensityField, ESPMode::ThreadSafe>;
    using FnGet = void* (*)(UWorld*);
    using FnGetField = FFieldPtr* (*)(const void* This, FFieldPtr* ReturnSlot);
    using FnGetSpliced = const void* (*)(const void* This);
    using FnXY = double (*)(const void* This, double X, double Y);
    using FnOutside = bool (*)(const void* This, double X, double Y);
    using FnWater = double (*)(const void* This, double X, double Y, int32 Arg3);
    using FnScalar = double (*)(const void* This);
    using FnInSea = bool (*)(const void* This, double X, double Y, double Z); // RWG P2e: this RCX, X/Y/Z in XMM1..3 (disassembled, build 705)
    using FnVoidAt = bool (*)(const void* This, double X, double Y);          // RWG P2f: this RCX (spliced world), X XMM1, Y XMM2 (disassembled, build 705)
    using FnKillboxTop = bool (*)(const void* This, double* OutZ);            // RWG P2f: this RCX (generator), OutZ RDX (double&) (disassembled, build 705)

    // Decorated names, copied from the dumpbin output of the build above (the research file lists the same).
    const char* const SymGet = "?Get@APWWorldGenerator@@SAPEAV1@PEAVUWorld@@@Z";
    const char* const SymGetField = "?GetField@APWWorldGenerator@@QEBA?AV?$TSharedPtr@$$CBVFPWDensityField@@$00@@XZ";
    const char* const SymGetSpliced = "?GetSpliced@FPWDensityField@@QEBAAEBVFPWSplicedWorld@@XZ";
    const char* const SymSurface = "?SurfaceHeightCm@FPWDensityField@@QEBANNN@Z";
    const char* const SymOutside = "?IsOutsideWorld@FPWDensityField@@QEBA_NNN@Z";
    const char* const SymWater = "?WaterLevelCmAt@FPWSplicedWorld@@QEBANNNH@Z";
    const char* const SymGMin = "?GlobalMinSurfaceCm@FPWDensityField@@QEBANXZ"; // optional: range + plausibility
    const char* const SymGMax = "?GlobalMaxSurfaceCm@FPWDensityField@@QEBANXZ"; // optional: range + plausibility
    const char* const SymInSea = "?InSeaVolume@FPWSplicedWorld@@QEBA_NNNN@Z"; // RWG P2e: optional; its term counts only after RWGSEACHECK grades it
    const char* const SymVoidAt = "?VoidAt@FPWSplicedWorld@@QEBA_NNN@Z";       // RWG P2f: optional; its term counts only after RWGVOIDCHECK grades it
    const char* const SymKillboxTop = "?GetKillboxTop@APWWorldGenerator@@QEBA_NAEAN@Z"; // RWG P2f: optional; compared with the measured volume top
    // RWG P2f: RWG's integer-index classifiers (per-sample iy,ix) are NEVER bound -- they `int 3` on an out-of-range index in Shipping.

    struct FRwgHeightApi
    {
        bool bTried = false;
        bool bUsable = false;
        FString Status = TEXT("notTried");
        FString Module = TEXT("<none>");
        FString Uplugin = TEXT("<none>");
        FString SemVer = TEXT("<unread>");
        int32 Build = -1;
        FString Matched = TEXT("none");       // RWG 0.1.9: the accept-list entry SemVer+Build matched, {semver,build}; "none" = refused
        int32 Required = 0;
        int32 Optional = 0;
        FString Missing;
        FnGet Get = nullptr;
        FnGetField GetField = nullptr;
        FnGetSpliced GetSpliced = nullptr;
        FnXY Surface = nullptr;
        FnOutside Outside = nullptr;
        FnWater Water = nullptr;
        FnScalar GMin = nullptr;
        FnScalar GMax = nullptr;
        FnInSea InSea = nullptr;          // RWG P2e: null unless resolved AND its byte signature matched
        FString SeaStatus = TEXT("notTried");
        FnVoidAt VoidAt = nullptr;            // RWG P2f: null unless resolved AND its byte signature matched
        FnKillboxTop KillboxTop = nullptr;    // RWG P2f: same
        FString VoidStatus = TEXT("notTried");
        FString KillboxStatus = TEXT("notTried");
        void* Hmod = nullptr;                 // RWG P5: the bound module, set only together with bUsable (the cave group binds from it)
    };
    FRwgHeightApi& Api() { static FRwgHeightApi A; return A; } // per process: the RWG DLL never unloads

    int32 JsonInt(const FString& J, const TCHAR* Key)
    {
        const int32 K = J.Find(FString::Printf(TEXT("\"%s\""), Key));
        const int32 C = K < 0 ? INDEX_NONE : J.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, K);
        if (C < 0) { return -1; }
        int32 I = C + 1;
        while (I < J.Len() && FChar::IsWhitespace(J[I])) { ++I; }
        int32 V = 0, Digits = 0;
        while (I < J.Len() && FChar::IsDigit(J[I]) && Digits < 9) { V = V * 10 + (J[I] - TEXT('0')); ++I; ++Digits; }
        return Digits > 0 ? V : -1;
    }

    FString JsonStr(const FString& J, const TCHAR* Key)
    {
        const int32 K = J.Find(FString::Printf(TEXT("\"%s\""), Key));
        const int32 C = K < 0 ? INDEX_NONE : J.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, K);
        const int32 Q1 = C < 0 ? INDEX_NONE : J.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, C);
        const int32 Q2 = Q1 < 0 ? INDEX_NONE : J.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Q1 + 1);
        return Q2 < 0 ? FString(TEXT("<unread>")) : J.Mid(Q1 + 1, Q2 - Q1 - 1);
    }

    void TryBind(FRwgHeightApi& A)
    {
        A.bTried = true;
#if PLATFORM_WINDOWS
        static const TCHAR* const Names[] = {
            TEXT("FactoryGameSteam-ProceduralWorld-Win64-Shipping.dll"), TEXT("FactoryGameEGS-ProceduralWorld-Win64-Shipping.dll") };
        HMODULE H = nullptr;
        for (const TCHAR* N : Names) { if ((H = ::GetModuleHandleW(N)) != nullptr) { A.Module = N; break; } }
        if (!H) { A.Status = TEXT("moduleNotLoaded"); return; }
        // The version gate reads the .uplugin of the DLL actually bound: <plugin>/Binaries/Win64/<dll>.
        WCHAR Path[1024] = {};
        const auto Len = ::GetModuleFileNameW(H, Path, UE_ARRAY_COUNT(Path));
        if (Len == 0 || Len >= UE_ARRAY_COUNT(Path)) { A.Status = TEXT("modulePathUnread"); return; }
        A.Uplugin = FPaths::Combine(FPaths::GetPath(FPaths::GetPath(FPaths::GetPath(FString(Path)))), TEXT("RandomWorldGenerator.uplugin"));
        FString Json;
        if (!FFileHelper::LoadFileToString(Json, *A.Uplugin)) { A.Status = TEXT("upluginUnread"); return; }
        A.Build = JsonInt(Json, TEXT("PWBuildNumber"));
        A.SemVer = JsonStr(Json, TEXT("SemVersion"));
        const auto Resolve = [&A, H](const char* Sym, bool bRequired) -> void*
        {
            void* P = reinterpret_cast<void*>(::GetProcAddress(H, Sym));
            if (P) { ++(bRequired ? A.Required : A.Optional); }
            else { A.Missing += (A.Missing.IsEmpty() ? TEXT("") : TEXT(" ")) + FString(ANSI_TO_TCHAR(Sym)); }
            return P;
        };
        A.Get = reinterpret_cast<FnGet>(Resolve(SymGet, true));
        A.GetField = reinterpret_cast<FnGetField>(Resolve(SymGetField, true));
        A.GetSpliced = reinterpret_cast<FnGetSpliced>(Resolve(SymGetSpliced, true));
        A.Surface = reinterpret_cast<FnXY>(Resolve(SymSurface, true));
        A.Outside = reinterpret_cast<FnOutside>(Resolve(SymOutside, true));
        A.Water = reinterpret_cast<FnWater>(Resolve(SymWater, true));
        A.GMin = reinterpret_cast<FnScalar>(Resolve(SymGMin, false));
        A.GMax = reinterpret_cast<FnScalar>(Resolve(SymGMax, false));
        bool bVersion = false;
        for (const FRwgAcceptedBuild& E : RwgHeightApiAccepted)
        {
            if (A.Build == E.Build && A.SemVer == E.SemVer) { bVersion = true; A.Matched = FString::Printf(TEXT("{%s,%d}"), E.SemVer, E.Build); break; }
        }
        if (!bVersion) { A.Status = TEXT("versionMismatch"); UE_LOG(LogNodeShuffle, Warning, TEXT("RWGHEIGHTAPI-REFUSED: status=versionMismatch semVer='%s' build=%d accepted='%s' uplugin='%s' callsAllowed=0"), *A.SemVer, A.Build, *RwgAcceptedList(), *A.Uplugin); return; }
        if (A.Required != 6) { A.Status = TEXT("exportMissing"); UE_LOG(LogNodeShuffle, Warning, TEXT("RWGHEIGHTAPI-REFUSED: status=exportMissing semVer='%s' build=%d matchedEntry='%s' requiredResolved=%d of 6 missing='%s' callsAllowed=0"), *A.SemVer, A.Build, *A.Matched, A.Required, *A.Missing); return; }
        // RWG P2b BYTE-SIGNATURE GATE (P2a cold review alternative 2): each export's leading bytes must equal the
        // build-705 disassembly (dumpbin /DISASM, 2026-10-17); -1 = wildcard. Mismatch = fail closed.
        // Wildcards: IsOutsideWorld's RIP displacement; SINCE RWG 0.1.9 (2026-10-18) the 4-byte member displacement of GetField
        // (bytes 3..6 of `mov rax,[rcx+disp32]`) -- measured F40h on build 705, 1008h on build 745. The opcode/ModRM bytes
        // around it (48 8B 81 / 48 89 02) stay pinned; only the displacement value is not compared.
        static const int16 SigGet[] = { 0x48,0x83,0xEC,0x28,0x48,0x85,0xC9 };
        static const int16 SigField[] = { 0x48,0x8B,0x81,-1,-1,-1,-1,0x48,0x89,0x02 }; // disp32 @3..6: 705 = 0x00000F40, 745 = 0x00001008
        static const int16 SigSpliced[] = { 0x48,0x8B,0x81,0x90,0x00,0x00,0x00,0xC3 };
        static const int16 SigSurface[] = { 0x40,0x53,0x48,0x83,0xEC,0x40,0x0F,0x29,0x74,0x24,0x30,0x48,0x8B,0xD9 };
        static const int16 SigOutside[] = { 0xF2,0x0F,0x10,0x1D,-1,-1,-1,-1,0xF2,0x0F,0x10,0x41,0x08,0x0F,0x54,0xCB };
        static const int16 SigWater[] = { 0x48,0x8B,0xC4,0x44,0x89,0x48,0x20,0x48,0x89,0x48,0x08 };
        static const int16 SigGMin[] = { 0xF2,0x0F,0x10,0x41,0x18,0xC3 };
        static const int16 SigGMax[] = { 0xF2,0x0F,0x10,0x41,0x20,0xC3 };
        // RWG 0.1.9: SigOk also records the index of the first PINNED byte that differed (LastSigDiff; -1 = every pinned byte matched)
        // so each signature refusal can print it.
        int32 LastSigDiff = -1;
        const auto SigOk = [&LastSigDiff](const void* Fn, const int16* Sig, int32 N) -> bool
        {
            const uint8* B = static_cast<const uint8*>(Fn);
            LastSigDiff = -1;
            for (int32 I = 0; I < N; ++I) { if (Sig[I] >= 0 && B[I] != static_cast<uint8>(Sig[I])) { LastSigDiff = I; return false; } }
            return true;
        };
        // RWG 0.1.9: one Warning per refused export (required or optional), measured values only.
        const auto WarnSigRefused = [&A, &LastSigDiff](const TCHAR* Name, bool bRequired)
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("RWGSIGREFUSED: export=%s required=%d firstDiffByte=%d semVer='%s' build=%d matchedEntry='%s'"),
                Name, bRequired ? 1 : 0, LastSigDiff, *A.SemVer, A.Build, *A.Matched);
        };
        FString SigBad;
        bool bRequiredSigBad = false;
        const auto Check = [&](const void* Fn, const int16* Sig, int32 N, const TCHAR* Name)
        {
            if (!SigOk(Fn, Sig, N)) { bRequiredSigBad = true; SigBad += FString::Printf(TEXT("%s%s@+%d"), SigBad.IsEmpty() ? TEXT("") : TEXT(" "), Name, LastSigDiff); WarnSigRefused(Name, true); }
        };
        Check(A.Get, SigGet, UE_ARRAY_COUNT(SigGet), TEXT("Get"));
        Check(A.GetField, SigField, UE_ARRAY_COUNT(SigField), TEXT("GetField"));
        Check(A.GetSpliced, SigSpliced, UE_ARRAY_COUNT(SigSpliced), TEXT("GetSpliced"));
        Check(A.Surface, SigSurface, UE_ARRAY_COUNT(SigSurface), TEXT("SurfaceHeightCm"));
        Check(A.Outside, SigOutside, UE_ARRAY_COUNT(SigOutside), TEXT("IsOutsideWorld"));
        Check(A.Water, SigWater, UE_ARRAY_COUNT(SigWater), TEXT("WaterLevelCmAt"));
        if (A.GMin && !SigOk(A.GMin, SigGMin, UE_ARRAY_COUNT(SigGMin))) { A.GMin = nullptr; SigBad += FString::Printf(TEXT(" GlobalMinSurfaceCm@+%d(optional,dropped)"), LastSigDiff); WarnSigRefused(TEXT("GlobalMinSurfaceCm"), false); }
        if (A.GMax && !SigOk(A.GMax, SigGMax, UE_ARRAY_COUNT(SigGMax))) { A.GMax = nullptr; SigBad += FString::Printf(TEXT(" GlobalMaxSurfaceCm@+%d(optional,dropped)"), LastSigDiff); WarnSigRefused(TEXT("GlobalMaxSurfaceCm"), false); }
        // RWG 0.1.9: the member displacement GetField actually carries (bytes 3..6, little-endian) -- read, never compared.
        uint32 FieldDisp = 0;
        FMemory::Memcpy(&FieldDisp, static_cast<const uint8*>(reinterpret_cast<const void*>(A.GetField)) + 3, sizeof(FieldDisp));
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGHEIGHTAPI: signature check mismatched='%s' matchedEntry='%s' optionalResolved=%d getFieldDisp=0x%X (empty list = every resolved export matched its mask; the GetField value is its bytes 3..6 read as a little-endian uint32, a displacement only while GetField is absent from the list; each item is the export and the offset of its first differing pinned byte)"),
            *SigBad, *A.Matched, A.Optional, FieldDisp);
        if (bRequiredSigBad) { UE_LOG(LogNodeShuffle, Warning, TEXT("RWGHEIGHTAPI-REFUSED: status=signatureMismatch semVer='%s' build=%d matchedEntry='%s' accepted='%s' mismatched='%s' callsAllowed=0"), *A.SemVer, A.Build, *A.Matched, *RwgAcceptedList(), *SigBad); }
        if (bRequiredSigBad) { A.Status = TEXT("signatureMismatch"); return; }
        // RWG P2e (2026-10-18): InSeaVolume, OPTIONAL like GMin/GMax -- resolved only after the version, export and required-signature
        // gates passed; a missing export or a byte mismatch leaves it null and the old water predicate stays in place. Build-705
        // disassembly (dumpbin /DISASM, 2026-10-18): `this` RCX, X XMM1, Y XMM2, Z XMM3; Z >= 0 returns false before the grid lookup
        // (comisd xmm3,0 / jae), so callers pass the surface Z. These leading bytes carry no RIP displacement (no wildcard).
        static const int16 SigInSea[] = { 0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x30,0x0F,0x57,0xC0,0x0F,0x28,0xEA,0x66,0x0F,0x2F,0xD8,0x48,0x8B,0xF9 };
        A.InSea = reinterpret_cast<FnInSea>(::GetProcAddress(H, SymInSea));
        if (!A.InSea) { A.SeaStatus = TEXT("exportMissing"); }
        else if (!SigOk(reinterpret_cast<const void*>(A.InSea), SigInSea, UE_ARRAY_COUNT(SigInSea))) { A.InSea = nullptr; A.SeaStatus = TEXT("signatureMismatch"); WarnSigRefused(TEXT("InSeaVolume"), false); }
        else { A.SeaStatus = TEXT("bound"); }
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGSEABIND: status=%s export='%s' optional=1; its term counts only after RWGSEACHECK grades it"), *A.SeaStatus, ANSI_TO_TCHAR(SymInSea));
        if (!A.InSea) { UE_LOG(LogNodeShuffle, Warning, TEXT("RWGSEABIND-OFF: status=%s; the sea term is off this session"), *A.SeaStatus); }
        // RWG P2f (2026-10-18): VoidAt + GetKillboxTop, OPTIONAL like InSeaVolume -- same position (after the version, export and required-
        // signature gates, before A.bUsable = true); missing or byte-mismatched = null, its term stays off. Build-705 disassembly (dumpbin
        // /DISASM, 2026-10-18): VoidAt = this RCX, X XMM1, Y XMM2, returns false when the void count [+0x2E8] is 0 and on a negative or
        // >= N sample index (its own bounds check); GetKillboxTop = mov rax,[rcx+0C58h] / mov [rdx],rax / cmp dword [rcx+0C48h],0 / setg al.
        // SigVoidAt is pinned in full (its +0x2E8 displacement included). SINCE RWG 0.1.9 (2026-10-18) SigKillboxTop wildcards exactly
        // its two 4-byte member displacements (bytes 3..6 of `mov rax,[rcx+disp32]`, bytes 12..15 of `cmp dword [rcx+disp32],0`):
        // measured C58h/C48h on build 705, C60h/C50h on build 745. Opcode/ModRM bytes, the cmp imm8 (byte 16) and setg/ret stay pinned.
        static const int16 SigVoidAt[] = { 0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x83,0xB9,0xE8,0x02,0x00,0x00,0x00,0x0F,0x28,0xE2,0x0F,0x28,0xC1,0x48,0x8B,0xD9,0x0F,0x84 };
        static const int16 SigKillboxTop[] = { 0x48,0x8B,0x81,-1,-1,-1,-1,0x48,0x89,0x02,0x83,0xB9,-1,-1,-1,-1,0x00,0x0F,0x9F,0xC0,0xC3 }; // disp32 @3..6: 705 = 0xC58, 745 = 0xC60; disp32 @12..15: 705 = 0xC48, 745 = 0xC50
        A.VoidAt = reinterpret_cast<FnVoidAt>(::GetProcAddress(H, SymVoidAt));
        if (!A.VoidAt) { A.VoidStatus = TEXT("exportMissing"); }
        else if (!SigOk(reinterpret_cast<const void*>(A.VoidAt), SigVoidAt, UE_ARRAY_COUNT(SigVoidAt))) { A.VoidAt = nullptr; A.VoidStatus = TEXT("signatureMismatch"); WarnSigRefused(TEXT("VoidAt"), false); }
        else { A.VoidStatus = TEXT("bound"); }
        A.KillboxTop = reinterpret_cast<FnKillboxTop>(::GetProcAddress(H, SymKillboxTop));
        uint32 KillboxDispA = 0, KillboxDispB = 0; // RWG 0.1.9: the two displacements the export carries (read, never compared); 0 = export missing
        if (A.KillboxTop)
        {
            FMemory::Memcpy(&KillboxDispA, reinterpret_cast<const uint8*>(A.KillboxTop) + 3, sizeof(KillboxDispA));
            FMemory::Memcpy(&KillboxDispB, reinterpret_cast<const uint8*>(A.KillboxTop) + 12, sizeof(KillboxDispB));
        }
        if (!A.KillboxTop) { A.KillboxStatus = TEXT("exportMissing"); }
        else if (!SigOk(reinterpret_cast<const void*>(A.KillboxTop), SigKillboxTop, UE_ARRAY_COUNT(SigKillboxTop))) { A.KillboxTop = nullptr; A.KillboxStatus = TEXT("signatureMismatch"); WarnSigRefused(TEXT("GetKillboxTop"), false); }
        else { A.KillboxStatus = TEXT("bound"); }
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGVOIDBIND: voidAtStatus=%s killboxTopStatus=%s optional=1 killboxTopDisps=0x%X/0x%X; the void term counts only after RWGVOIDCHECK grades it, the killbox floor reads the damage volumes first"),
            *A.VoidStatus, *A.KillboxStatus, KillboxDispA, KillboxDispB);
        if (!A.VoidAt) { UE_LOG(LogNodeShuffle, Warning, TEXT("RWGVOIDBIND-OFF: voidAtStatus=%s; the void term is off this session (the killbox floor still applies when measured)"), *A.VoidStatus); }
        A.Hmod = H; // RWG P5: published only on the path that also sets bUsable
        A.bUsable = true;
        A.Status = TEXT("bound");
#else
        A.Status = TEXT("notWindows");
#endif
    }

    // GenerationParams.WorldSizeMeters by reflection (type-checked); <= 0 = unread.
    double ReadWorldSizeMeters(UObject* Gen)
    {
        if (!IsValid(Gen)) { return -1.0; }
        for (TFieldIterator<FProperty> It(Gen->GetClass()); It; ++It)
        {
            const FStructProperty* SP = CastField<FStructProperty>(*It);
            if (!SP || !SP->Struct || It->GetName() != TEXT("GenerationParams")) { continue; }
            const void* SPtr = SP->ContainerPtrToValuePtr<void>(Gen);
            for (TFieldIterator<FProperty> In(SP->Struct.Get()); In; ++In)
            {
                const FNumericProperty* N = CastField<FNumericProperty>(*In);
                if (!N || In->GetName() != TEXT("WorldSizeMeters")) { continue; }
                const void* V = N->ContainerPtrToValuePtr<void>(SPtr);
                return N->IsFloatingPoint() ? N->GetFloatingPointPropertyValue(V) : static_cast<double>(N->GetSignedIntPropertyValue(V));
            }
        }
        return -1.0;
    }

    double PctOf(const TArray<double>& Sorted, double P)
    {
        return Sorted.Num() == 0 ? 0.0 : Sorted[FMath::Clamp(FMath::RoundToInt(P * (Sorted.Num() - 1)), 0, Sorted.Num() - 1)];
    }
}

void ANodeShuffleSubsystem::RwgLogHeightApi(bool bFull)
{
    FRwgHeightApi& A = Api();
    if (!A.bTried) { TryBind(A); }
    if (bFull)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RWGHEIGHTAPI: bind status=%s module='%s' uplugin='%s' semVer='%s' build=%d accepted='%s' matchedEntry='%s' requiredResolved=%d of 6 optionalResolved=%d of 2 missing='%s' callsAllowed=%d"),
            *A.Status, *A.Module, *A.Uplugin, *A.SemVer, A.Build, *RwgAcceptedList(), *A.Matched,
            A.Required, A.Optional, *A.Missing, A.bUsable ? 1 : 0);
    }
    UWorld* World = GetWorld();
    APawn* Pawn = World ? NodeShuffleMapMode::FirstPlayerPawn(World) : nullptr;
    if (!A.bUsable || !Pawn)
    {
        UE_LOG(LogNodeShuffle, Display, TEXT("RWGHEIGHTAPI: census skipped; callsAllowed=%d pawnFound=%d status=%s"),
            A.bUsable ? 1 : 0, Pawn ? 1 : 0, *A.Status);
        return;
    }
    void* Gen = A.Get(World);
    if (!Gen) { UE_LOG(LogNodeShuffle, Display, TEXT("RWGHEIGHTAPI: unavailable; Get(World) returned null")); return; }
    const int32 SameGen = (Gen == static_cast<void*>(RwgGenerator.Get())) ? 1 : 0;
    FFieldPtr Field; // holds a strong reference for this call only; released at return, never cached
    A.GetField(Gen, &Field);
    const void* F = Field.Get();
    if (!F) { UE_LOG(LogNodeShuffle, Display, TEXT("RWGHEIGHTAPI: unavailable; GetField returned an empty pointer sameGeneratorAsReflected=%d"), SameGen); return; }
    const void* S = A.GetSpliced(F);
    const FVector P = Pawn->GetActorLocation();
    const double GMin = A.GMin ? A.GMin(F) : TNumericLimits<double>::Max();
    const double GMax = A.GMax ? A.GMax(F) : TNumericLimits<double>::Lowest();
    const double PawnApiZ = A.Surface(F, P.X, P.Y);
    // Plausibility before the batch (bounds are 3x the .key file's floor -60000 / ceiling 120000 cm): a value
    // outside them means the assumed ABI or semantics do not hold, and no further call is made this run.
    const bool bRangeOk = !A.GMin || !A.GMax || (FMath::IsFinite(GMin) && FMath::IsFinite(GMax) && GMin < GMax && GMin > -200000.0 && GMax < 360000.0);
    const bool bPawnOk = FMath::IsFinite(PawnApiZ) && PawnApiZ > -200000.0 && PawnApiZ < 360000.0;
    if (!bRangeOk || !bPawnOk || !S)
    {
        UE_LOG(LogNodeShuffle, Display,
            TEXT("RWGHEIGHTAPI: plausibility failed; globalSurfaceCm=%.1f..%.1f pawnApiZ=%.1f pawnZ=%.0f splicedFound=%d sameGeneratorAsReflected=%d; batch not run"),
            GMin, GMax, PawnApiZ, P.Z, S ? 1 : 0, SameGen);
        return;
    }
    const double Top = FMath::Max(static_cast<double>(RwgZMax) + 30000.0, A.GMax ? GMax + 10000.0 : -1.0e9);
    const double Bottom = FMath::Min(static_cast<double>(RwgZMin) - 30000.0, A.GMin ? GMin - 10000.0 : 1.0e9);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(NodeShuffleRwgHeightApi), true);
    Params.AddIgnoredActor(Pawn);
    static const float RadiiM[8] = { 25.f, 75.f, 125.f, 175.f, 250.f, 325.f, 400.f, 475.f };
    TArray<double> AbsDz, SignedDz, TerrainAbsDz;
    int32 Candidates = 0, Listed = 0, WaterAboveIn = 0, WaterAboveOut = 0, WaterBelowIn = 0, WaterBelowOut = 0;
    double ApiSec = 0.0;
    int32 ApiCalls = 0;
    for (int32 RI = 0; RI < 8; ++RI)
    {
        for (int32 K = 0; K < 16; ++K)
        {
            ++Candidates;
            const float Ang = (K + 0.5f * (RI % 2)) * (2.f * PI / 16.f);
            const double X = P.X + FMath::Cos(Ang) * RadiiM[RI] * 100.0;
            const double Y = P.Y + FMath::Sin(Ang) * RadiiM[RI] * 100.0;
            FHitResult Hit;
            if (!World->LineTraceSingleByChannel(Hit, FVector(X, Y, Top), FVector(X, Y, Bottom), ECC_WorldStatic, Params)) { continue; }
            const double T0 = FPlatformTime::Seconds();
            const double ApiZ = A.Surface(F, X, Y);
            const double WaterZ = A.Water(S, X, Y, 0); // third int: meaning unmeasured; 0 passed
            ApiSec += FPlatformTime::Seconds() - T0;
            ApiCalls += 2;
            const double TraceZ = Hit.ImpactPoint.Z;
            const double Dz = ApiZ - TraceZ;
            const bool bIn = IsPointInWater(Hit.ImpactPoint);
            const bool bWaterAbove = WaterZ > TraceZ;
            WaterAboveIn += (bWaterAbove && bIn) ? 1 : 0;
            WaterAboveOut += (bWaterAbove && !bIn) ? 1 : 0;
            WaterBelowIn += (!bWaterAbove && bIn) ? 1 : 0;
            WaterBelowOut += (!bWaterAbove && !bIn) ? 1 : 0;
            const AActor* HA = Hit.GetActor();
            const UPrimitiveComponent* HC = Hit.GetComponent();
            const FString HitClass = HA ? HA->GetClass()->GetName() : FString(TEXT("<no actor>"));
            const FString HitComp = HC ? HC->GetName() : FString(TEXT("<no component>"));
            // Terrain subset keyed on the hit COMPONENT: APWTerrainChunk also owns WaterMesh and foliage/tree components.
            const bool bTerrainMesh = HitClass.Contains(TEXT("TerrainChunk")) && HitComp.Contains(TEXT("Terrain"));
            AbsDz.Add(FMath::Abs(Dz));
            SignedDz.Add(Dz);
            if (bTerrainMesh) { TerrainAbsDz.Add(FMath::Abs(Dz)); }
            const bool bDisagree = FMath::Abs(Dz) >= 50.0 || bWaterAbove != bIn;
            if (bFull || (bDisagree && Listed < 16))
            {
                ++Listed;
                UE_LOG(LogNodeShuffle, Display,
                    TEXT("RWGHEIGHTAPI point %d: x=%.0f y=%.0f traceZ=%.1f apiZ=%.1f dZ=%.1f hitClass='%s' hitComponent='%s' terrainMesh=%d waterLevelApi=%.1f inVolumeAtHit=%d"),
                    AbsDz.Num(), X, Y, TraceZ, ApiZ, Dz, *HitClass, *HitComp, bTerrainMesh ? 1 : 0, WaterZ, bIn ? 1 : 0);
            }
        }
    }
    AbsDz.Sort();
    SignedDz.Sort();
    TerrainAbsDz.Sort();
    const auto Under50 = [](const TArray<double>& V) { int32 C = 0; for (double D : V) { C += D < 50.0 ? 1 : 0; } return V.Num() ? double(C) / V.Num() : 0.0; };
    const double Frac = Under50(AbsDz);
    const double TerrainFrac = Under50(TerrainAbsDz);
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGHEIGHTAPI: census n=%d targetN=64 candidates=%d absDzUnder50Frac=%.3f absDzMedian=%.1f absDzP95=%.1f signedDzMedian=%.1f terrainMeshHitN=%d terrainMeshAbsDzUnder50Frac=%.3f terrainMeshAbsDzMedian=%.1f terrainMeshAbsDzP95=%.1f terrainMeshNAtLeast64AndUnder50FracAtLeast95=%d waterAboveHitInVolume=%d waterAboveHitNotInVolume=%d waterNotAboveHitInVolume=%d waterNotAboveHitNotInVolume=%d globalSurfaceCm=%.1f..%.1f traceWindowZ=%.0f..%.0f pawn=(%.0f,%.0f,%.0f) pawnApiZ=%.1f sameGeneratorAsReflected=%d apiUsPerCall=%.2f linesListed=%d"),
        AbsDz.Num(), Candidates, Frac, PctOf(AbsDz, 0.5), PctOf(AbsDz, 0.95), PctOf(SignedDz, 0.5),
        TerrainAbsDz.Num(), TerrainFrac, PctOf(TerrainAbsDz, 0.5), PctOf(TerrainAbsDz, 0.95),
        (TerrainAbsDz.Num() >= 64 && TerrainFrac >= 0.95) ? 1 : 0,
        WaterAboveIn, WaterAboveOut, WaterBelowIn, WaterBelowOut, GMin, GMax, Top, Bottom, P.X, P.Y, P.Z, PawnApiZ, SameGen,
        ApiCalls > 0 ? ApiSec * 1.0e6 / ApiCalls : 0.0, Listed);
    if (!bFull) { return; }
    // IsOutsideWorld at 8 points 500 m past the map edge, plus the pawn as the inside control (a stub that
    // answers true everywhere fails the control).
    const double WorldM = ReadWorldSizeMeters(RwgGenerator.Get());
    const double Half = WorldM > 0.0 ? WorldM * 50.0 : 1500000.0;
    const double O = Half + 50000.0;
    static const int32 Sx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static const int32 Sy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    int32 OutsideTrue = 0;
    FString Pts;
    for (int32 I = 0; I < 8; ++I)
    {
        const bool bOut = A.Outside(F, Sx[I] * O, Sy[I] * O);
        OutsideTrue += bOut ? 1 : 0;
        Pts += FString::Printf(TEXT("%s%.0f,%.0f:%d,%.0f"), I ? TEXT("; ") : TEXT(""), Sx[I] * O, Sy[I] * O, bOut ? 1 : 0, A.Surface(F, Sx[I] * O, Sy[I] * O));
    }
    UE_LOG(LogNodeShuffle, Display,
        TEXT("RWGHEIGHTAPI: edge halfExtentCm=%.0f edgeSource=%s outsideTrue=%d of 8 pawnControlOutside=%d [%s]"),
        Half, WorldM > 0.0 ? TEXT("GenerationParams.WorldSizeMeters") : TEXT("researchConstant"), OutsideTrue,
        A.Outside(F, P.X, P.Y) ? 1 : 0, *Pts);
}

// ---------------------------------------------------------------- RWG P2b ground query ----
// Used by the RWG deal / redeal / spawn resolve (NodeShuffleMapModePlace.cpp). Same binding, same gates as the
// census above (version, exports, byte signatures), plus the plausibility range and the half extent; any refusal
// leaves F null so IsUsable() is false and the caller takes the trace-and-defer fallback.
NodeShuffleMapMode::FRwgGroundQuery::FRwgGroundQuery(UWorld* World, UObject* Generator)
{
    FRwgHeightApi& A = Api();
    if (!A.bTried) { TryBind(A); }
    if (!A.bUsable || !World) { StatusText = FString::Printf(TEXT("binding:%s"), *A.Status); return; }
    void* Gen = A.Get(World);
    if (!Gen) { StatusText = TEXT("getReturnedNull"); return; }
    A.GetField(Gen, &Field);
    const void* Fp = Field.Get();
    if (!Fp) { StatusText = TEXT("emptyField"); return; }
    S = A.GetSpliced(Fp);
    const double GMin = A.GMin ? A.GMin(Fp) : -1.0;
    const double GMax = A.GMax ? A.GMax(Fp) : 1.0;
    const bool bRangeOk = FMath::IsFinite(GMin) && FMath::IsFinite(GMax) && GMin < GMax && GMin > -200000.0 && GMax < 360000.0;
    const double WorldM = ReadWorldSizeMeters(Generator);
    if (!bRangeOk || !S || WorldM <= 0.0)
    {
        StatusText = FString::Printf(TEXT("refused range=%d spliced=%d worldSizeM=%.0f"), bRangeOk ? 1 : 0, S ? 1 : 0, WorldM);
        S = nullptr;
        return;
    }
    HalfExtentCm = WorldM * 50.0; // WorldSizeMeters is the full side; half of it, in cm (R2: 1,500,000)
    F = Fp;
    G = Gen; // RWG P2f: GetKillboxTop's this
    StatusText = TEXT("usable");
}

NodeShuffleMapMode::FRwgGroundQuery::~FRwgGroundQuery() = default;

bool NodeShuffleMapMode::FRwgGroundQuery::Outside(double X, double Y) const
{
    return F ? Api().Outside(F, X, Y) : true;
}

double NodeShuffleMapMode::FRwgGroundQuery::SurfaceZ(double X, double Y) const
{
    return F ? Api().Surface(F, X, Y) : 0.0;
}

bool NodeShuffleMapMode::FRwgGroundQuery::HasSea() const
{
    return F != nullptr && S != nullptr && Api().InSea != nullptr; // RWG P2e
}

bool NodeShuffleMapMode::FRwgGroundQuery::InSea(double X, double Y, double Z) const
{
    // RWG P2e: raw InSeaVolume(X, Y, Z) on the spliced world. Z must be the surface Z (the function answers false for Z >= 0).
    return HasSea() ? Api().InSea(S, X, Y, Z) : false;
}

double NodeShuffleMapMode::FRwgGroundQuery::GlobalMinZ() const
{
    return (F && Api().GMin) ? Api().GMin(F) : std::numeric_limits<double>::quiet_NaN(); // RWG P2e census floor; NaN = optional export dropped
}

bool NodeShuffleMapMode::FRwgGroundQuery::HasVoid() const
{
    return F != nullptr && S != nullptr && Api().VoidAt != nullptr; // RWG P2f
}

bool NodeShuffleMapMode::FRwgGroundQuery::VoidAt(double X, double Y) const
{
    // RWG P2f: our IsOutsideWorld test first (VoidAt also bounds-checks itself), then raw VoidAt(X, Y) on the spliced world.
    if (!HasVoid() || Outside(X, Y)) { return false; }
    return Api().VoidAt(S, X, Y);
}

bool NodeShuffleMapMode::FRwgGroundQuery::KillboxTopApi(double& OutZ) const
{
    // RWG P2f: GetKillboxTop(double&) on the generator; OutZ is written by RWG only when this returns true here.
    double Z = 0.0;
    if (!F || !G || !Api().KillboxTop || !Api().KillboxTop(G, &Z)) { return false; }
    OutZ = Z;
    return true;
}

FString NodeShuffleMapMode::RwgVoidBindStatus()
{
    const FRwgHeightApi& A = Api();
    if (!A.bTried) { return TEXT("notTried"); }
    return A.bUsable ? A.VoidStatus : FString::Printf(TEXT("notBound:%s"), *A.Status);
}

FString NodeShuffleMapMode::RwgKillboxBindStatus()
{
    const FRwgHeightApi& A = Api();
    if (!A.bTried) { return TEXT("notTried"); }
    return A.bUsable ? A.KillboxStatus : FString::Printf(TEXT("notBound:%s"), *A.Status);
}

void* NodeShuffleMapMode::RwgHeightApiBoundModule()
{
    FRwgHeightApi& A = Api();
    if (!A.bTried) { TryBind(A); }
    return A.bUsable ? A.Hmod : nullptr; // RWG P5: null = the version/export/signature gates did not pass; the cave group then stays unbound
}

FString NodeShuffleMapMode::RwgSeaBindStatus()
{
    const FRwgHeightApi& A = Api();
    if (!A.bTried) { return TEXT("notTried"); }
    return A.bUsable ? A.SeaStatus : FString::Printf(TEXT("notBound:%s"), *A.Status);
}

double NodeShuffleMapMode::FRwgGroundQuery::WaterLevelZ(double X, double Y) const
{
    // The int argument's meaning is UNVERIFIED (P2a); 0 is passed as in the census. Out-of-range XY exits early.
    return (F && S) ? Api().Water(S, X, Y, 0) : TNumericLimits<double>::Lowest();
}
