// RWG P2a (2026-10-17) -- RWGHEIGHTAPI: an OBSERVE-ONLY probe of RWG's native height/water query.
// Brief: _team/nodeshuffle-rwg-p2-spec.md "Addendum 2"; research: _team/rwg-map-persistence-research.md §4/§7.
// Reached only from RwgLogReprobes (NodeShuffleMapModeR1.cpp), i.e. only off vanilla, only after the v2 latch.
// It resolves six RWG exports by DECORATED name at runtime (GetModuleHandleW + GetProcAddress, no link
// dependency, so nothing here enters the import table but KERNEL32), refuses to call unless the RWG .uplugin
// next to the bound DLL names the accepted build, and compares the API's surface height with a line trace at
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

class FPWDensityField; // RWG's type. Never completed here: only an opaque pointer and a TSharedPtr slot are used.

namespace
{
    // ACCEPTED RWG BUILD -- a code constant, deliberately NOT a config row: the six signatures below were
    // measured against exactly this build (dumpbin /EXPORTS on FactoryGameSteam-ProceduralWorld-Win64-Shipping.dll,
    // RWG 0.1.7 build 705, sha256 EE60A003A44A4ABC..., 2026-10-17). Any other build is refused, never called.
    // Bump both only after re-measuring the exports AND re-running the RWGHEIGHTAPI census on that build.
    constexpr int32 RwgHeightApiAcceptedBuild = 705;
    const TCHAR* const RwgHeightApiAcceptedSemVer = TEXT("0.1.7");

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

    // Decorated names, copied from the dumpbin output of the build above (the research file lists the same).
    const char* const SymGet = "?Get@APWWorldGenerator@@SAPEAV1@PEAVUWorld@@@Z";
    const char* const SymGetField = "?GetField@APWWorldGenerator@@QEBA?AV?$TSharedPtr@$$CBVFPWDensityField@@$00@@XZ";
    const char* const SymGetSpliced = "?GetSpliced@FPWDensityField@@QEBAAEBVFPWSplicedWorld@@XZ";
    const char* const SymSurface = "?SurfaceHeightCm@FPWDensityField@@QEBANNN@Z";
    const char* const SymOutside = "?IsOutsideWorld@FPWDensityField@@QEBA_NNN@Z";
    const char* const SymWater = "?WaterLevelCmAt@FPWSplicedWorld@@QEBANNNH@Z";
    const char* const SymGMin = "?GlobalMinSurfaceCm@FPWDensityField@@QEBANXZ"; // optional: range + plausibility
    const char* const SymGMax = "?GlobalMaxSurfaceCm@FPWDensityField@@QEBANXZ"; // optional: range + plausibility

    struct FRwgHeightApi
    {
        bool bTried = false;
        bool bUsable = false;
        FString Status = TEXT("notTried");
        FString Module = TEXT("<none>");
        FString Uplugin = TEXT("<none>");
        FString SemVer = TEXT("<unread>");
        int32 Build = -1;
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
        const bool bVersion = A.Build == RwgHeightApiAcceptedBuild && A.SemVer == RwgHeightApiAcceptedSemVer;
        if (!bVersion) { A.Status = TEXT("versionMismatch"); return; }
        if (A.Required != 6) { A.Status = TEXT("exportMissing"); return; }
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
            TEXT("RWGHEIGHTAPI: bind status=%s module='%s' uplugin='%s' semVer='%s' build=%d acceptedSemVer='%s' acceptedBuild=%d requiredResolved=%d of 6 optionalResolved=%d of 2 missing='%s' callsAllowed=%d"),
            *A.Status, *A.Module, *A.Uplugin, *A.SemVer, A.Build, RwgHeightApiAcceptedSemVer, RwgHeightApiAcceptedBuild,
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
