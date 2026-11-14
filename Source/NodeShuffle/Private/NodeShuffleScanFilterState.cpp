// ns-scanfilter-4 round 3 (2026-11-14, user decision "One box, follows game"): the two states of a row's 'Show in scanner'
// box and the small record file that remembers them. Design and grades: the ns-scanfilter-4 handoff, "Round 3".
//   AUTO        the player has never changed this row's box. The hook leaves the game's entry alone (an All Minable one is kept
//               out), the pick override does not apply, and each pass sets the box to whether the GAME'S OWN scannable list
//               (the unlock subsystem's two views, as read by Tick) contains the resource; an All Minable row is set OFF.
//   PLAYER-SET  the box's value differed from the value last recorded here: from then on ON adds and OFF removes, as before.
// The record lives in Configs/NodeShuffle_ScannerRows.txt beside the mod's config file (the cave store's location rule), never in
// the row and never in a config property. It is read once per process and written by Tick only, after the config save, only when
// something changed. Nothing here writes 'Protected', the path, a PLAYER-SET row's box, the save, or any unlock.
#include "NodeShuffleScanFilter.h"
#include "NodeShuffleScanFilterInternal.h"
#include "NodeShuffle.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Resources/FGItemDescriptor.h"
#include "Resources/FGResourceDescriptor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"

namespace NodeShuffleScanFilter
{
    namespace
    {
        struct FRowRecord { bool bPlayerSet = false; bool bLastValue = false; };
        TMap<FString, FRowRecord> GRecords;   // row path -> state; per process (the file is per install). Game thread only.
        bool GRecordsLoaded = false;
        bool GMissingResolved = false;       // the missing/unreadable case was decided once, in the first pass past the carry-over wait
        bool GRecordsDirty = false;
        const TCHAR* const RecordHeader = TEXT("# NodeShuffle scanner-row record v1: playerSet<TAB>lastValue<TAB>path");

        FString RecordFilePath() { return FPaths::Combine(FPaths::ProjectDir(), TEXT("Configs"), TEXT("NodeShuffle_ScannerRows.txt")); }

        // Touch detection. Runs BEFORE the mirror: a box whose value differs from the value last recorded makes its row PLAYER-SET.
        int32 DetectTouches(const TArray<FShowRow>& Shows)
        {
            int32 Became = 0;
            for (const FShowRow& Row : Shows)
            {
                FRowRecord* Rec = GRecords.Find(Row.Path);
                if (!Rec || Rec->bPlayerSet) { continue; } // no record yet = a new row (AUTO); already player-set = nothing to detect
                if (Row.Show->Value == Rec->bLastValue) { continue; }
                Rec->bPlayerSet = true;
                Rec->bLastValue = Row.Show->Value;
                GRecordsDirty = true;
                ++Became;
                // Arity hand-counted: 3 format specifiers, 3 arguments.
                UE_LOG(LogNodeShuffle, Display, TEXT("SCANFILTER-STATE playerSet resource=%s value=%d recordedBefore=%d: the box differs from the recorded value; the row is player-set from now on"),
                    *Row.Path, Row.Show->Value ? 1 : 0, Row.Show->Value ? 0 : 1);
            }
            return Became;
        }

        // The mirror. Writes ONLY the box of a row that is not player-set, ONLY to AutoShowValue, ONLY when that value is known.
        int32 MirrorAutoRows(const UObject* WorldContext, const TArray<FShowRow>& Shows, int32& OutUnknown)
        {
            int32 Mirrored = 0;
            for (const FShowRow& Row : Shows)
            {
                const FRowRecord* Rec = GRecords.Find(Row.Path);
                if (Rec && Rec->bPlayerSet) { continue; } // a PLAYER-SET row is never written
                const TCHAR* Basis = nullptr;
                bool bKnown = false;
                const bool bMirror = AutoShowValue(WorldContext, Row.Path, &Basis, &bKnown);
                if (!bKnown) { ++OutUnknown; continue; } // the game's list was not read: the box keeps what it says
                if (Row.Show->Value == bMirror) { continue; }
                const bool bOld = Row.Show->Value;
                Row.Show->Value = bMirror;
                ++Mirrored;
                // Arity hand-counted: 4 format specifiers, 4 arguments.
                UE_LOG(LogNodeShuffle, Display, TEXT("SCANFILTER-STATE mirrored resource=%s old=%d new=%d basis=%s"),
                    *Row.Path, bOld ? 1 : 0, bMirror ? 1 : 0, Basis ? Basis : TEXT("<none>"));
            }
            return Mirrored;
        }
    }

    void EnsureRowStateRecordLoaded()
    {
        if (GRecordsLoaded) { return; }
        GRecordsLoaded = true;
        const FString Path = RecordFilePath();
        FString Content;
        if (!FPaths::FileExists(Path))
        {
            UE_LOG(LogNodeShuffle, Display, TEXT("SCANFILTER-STATE fileMissing path=%s"), *Path);
            return;
        }
        TArray<FString> Lines;
        const bool bLoaded = FFileHelper::LoadFileToString(Content, *Path);
        if (bLoaded) { Content.ParseIntoArrayLines(Lines); }
        if (!bLoaded || Lines.Num() == 0 || Lines[0].TrimStartAndEnd() != RecordHeader)
        {
            // Arity hand-counted: 2 format specifiers, 2 arguments.
            UE_LOG(LogNodeShuffle, Warning, TEXT("SCANFILTER-STATE fileUnreadable path=%s linesRead=%d: the file could not be read or its first line is not the record header"), *Path, Lines.Num());
            return;
        }
        int32 Malformed = 0; int32 PlayerSet = 0;
        for (int32 i = 1; i < Lines.Num(); ++i)
        {
            TArray<FString> Parts;
            Lines[i].ParseIntoArray(Parts, TEXT("\t"), false);
            if (Parts.Num() != 3 || (Parts[0] != TEXT("0") && Parts[0] != TEXT("1")) || (Parts[1] != TEXT("0") && Parts[1] != TEXT("1")) || Parts[2].TrimStartAndEnd().IsEmpty()) { ++Malformed; continue; }
            FRowRecord& Rec = GRecords.FindOrAdd(Parts[2].TrimStartAndEnd());
            Rec.bPlayerSet = Parts[0] == TEXT("1");
            Rec.bLastValue = Parts[1] == TEXT("1");
            if (Rec.bPlayerSet) { ++PlayerSet; }
        }
        GMissingResolved = true; // the file was read: a row with no record is a new row (AUTO)
        // Arity hand-counted: 4 format specifiers, 4 arguments.
        UE_LOG(LogNodeShuffle, Display, TEXT("SCANFILTER-STATE fileRead path=%s records=%d playerSet=%d malformedLines=%d"), *Path, GRecords.Num(), PlayerSet, Malformed);
    }

    void NoteCarriedRow(const FString& Path, bool bValue)
    {
        FRowRecord& Rec = GRecords.FindOrAdd(Path); // a carried-over row is PLAYER-SET with its old switch value
        Rec.bPlayerSet = true;
        Rec.bLastValue = bValue;
        GRecordsDirty = true;
    }

    void ResolveMissingRecordOnce(const TSet<FString>& RowsBeforeAdds, bool bCarriedThisPass)
    {
        if (GMissingResolved || !GRecordsLoaded) { return; }
        GMissingResolved = true;
        GRecordsDirty = true; // a fresh file is written at the end of this pass, even with no record in it
        int32 Made = 0; int32 LeftAuto = 0;
        for (const FString& Path : RowsBeforeAdds)
        {
            FRowRecord* Rec = GRecords.Find(Path);
            if (Rec && Rec->bPlayerSet) { continue; } // carried over this pass
            if (bCarriedThisPass) { ++LeftAuto; continue; } // first run after this update: no row had a box the player could set
            FRowRecord& NewRec = GRecords.FindOrAdd(Path);
            NewRec.bPlayerSet = true; // the record was lost: every existing row keeps what its box says
            ++Made;
        }
        // Arity hand-counted: 4 format specifiers, 4 arguments.
        UE_LOG(LogNodeShuffle, Warning, TEXT("SCANFILTER-STATE noRecordFile carriedThisPass=%d rowsBefore=%d rowsMadePlayerSet=%d rowsLeftAuto=%d"),
            bCarriedThisPass ? 1 : 0, RowsBeforeAdds.Num(), Made, LeftAuto);
    }

    FRowStateCounts UpdateRowStates(const UObject* WorldContext, const TArray<FShowRow>& Shows)
    {
        FRowStateCounts C;
        C.BecamePlayerSet = DetectTouches(Shows);
        C.Mirrored = MirrorAutoRows(WorldContext, Shows, C.MirrorUnknown);
        TSet<FString> Present;
        for (const FShowRow& Row : Shows) // record what every box says NOW (a new row gets an AUTO record here)
        {
            Present.Add(Row.Path);
            FRowRecord* Rec = GRecords.Find(Row.Path);
            if (!Rec) { Rec = &GRecords.Add(Row.Path, FRowRecord()); Rec->bLastValue = Row.Show->Value; GRecordsDirty = true; }
            else if (Rec->bLastValue != Row.Show->Value) { Rec->bLastValue = Row.Show->Value; GRecordsDirty = true; }
            if (Rec->bPlayerSet) { ++C.PlayerSetRows; } else { ++C.AutoRows; }
        }
        for (TMap<FString, FRowRecord>::TIterator It = GRecords.CreateIterator(); It; ++It) // a deleted row's record goes: re-created = AUTO
        {
            if (Present.Contains(It.Key())) { continue; }
            // Arity hand-counted: 2 format specifiers, 2 arguments.
            UE_LOG(LogNodeShuffle, Display, TEXT("SCANFILTER-STATE recordDropped resource=%s wasPlayerSet=%d: no row has this path now"), *It.Key(), It.Value().bPlayerSet ? 1 : 0);
            It.RemoveCurrent();
            GRecordsDirty = true;
            ++C.Dropped;
        }
        return C;
    }

    // Moved from NodeShuffleScanFilterRows.cpp in round 3 (500-line rule); unchanged.
    bool InGameNameIfLoaded(const FString& Path, FString& OutName)
    {
        UClass* Cls = Path.IsEmpty() ? nullptr : FSoftClassPath(Path).ResolveClass();
        if (!Cls || !Cls->IsChildOf(UFGResourceDescriptor::StaticClass())) { return false; }
        OutName = UFGItemDescriptor::GetItemName(TSubclassOf<UFGItemDescriptor>(Cls)).ToString();
        return !OutName.IsEmpty();
    }

    bool IsRowPlayerSet(const FString& Path)
    {
        const FRowRecord* Rec = GRecords.Find(Path);
        return Rec != nullptr && Rec->bPlayerSet;
    }

    void FlushRowStateRecordIfDirty()
    {
        if (!GRecordsDirty || !GRecordsLoaded) { return; }
        FString Out = FString(RecordHeader) + TEXT("\n");
        int32 PlayerSet = 0;
        for (const TPair<FString, FRowRecord>& Pair : GRecords)
        {
            Out += FString::Printf(TEXT("%d\t%d\t%s\n"), Pair.Value.bPlayerSet ? 1 : 0, Pair.Value.bLastValue ? 1 : 0, *Pair.Key);
            if (Pair.Value.bPlayerSet) { ++PlayerSet; }
        }
        const FString Path = RecordFilePath();
        if (!FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            UE_LOG(LogNodeShuffle, Warning, TEXT("SCANFILTER-STATE fileWriteFailed path=%s"), *Path);
            return; // stays dirty: the next pass tries again
        }
        GRecordsDirty = false;
        // Arity hand-counted: 3 format specifiers, 3 arguments.
        UE_LOG(LogNodeShuffle, Display, TEXT("SCANFILTER-STATE fileWritten path=%s records=%d playerSet=%d"), *Path, GRecords.Num(), PlayerSet);
    }
}
