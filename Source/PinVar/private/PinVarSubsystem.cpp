
#include "PinVarSubsystem.h"

#include "Interfaces/IPluginManager.h"


static bool ContainsTriple(const TArray<FPinnedVariable>& Arr, FName Var, FName Group)
{
    for (const FPinnedVariable& E : Arr)
        if (E.VariableName == Var && E.GroupName == Group)
            return true;
    return false;
}

void UPinVarSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    LoadFromDisk();
}

void UPinVarSubsystem::PinVariable(FName ClassName, FName VariableName, FName GroupName)
{
    TArray<FPinnedVariable>& Vars = PinnedGroups.FindOrAdd(ClassName);
    if (!ContainsTriple(Vars, VariableName, GroupName))
    {
        Vars.Add(FPinnedVariable(VariableName, GroupName));
    }
}

void UPinVarSubsystem::StagePinVariable(FName ClassName, FName VariableName, FName GroupName)
{
    TArray<FPinnedVariable>& Vars = StagedPinnedGroups.FindOrAdd(ClassName);
    if (!ContainsTriple(Vars, VariableName, GroupName))
    {
        Vars.Add(FPinnedVariable(VariableName, GroupName));
    }
    SaveToDisk();

}

void UPinVarSubsystem::GetAllStaged(TArray<TTuple<FName,FName,FName>>& OutTriples) const
{
    OutTriples.Reset();
    for (const auto& Pair : StagedPinnedGroups)
    {
        const FName ClassName = Pair.Key;
        for (const FPinnedVariable& E : Pair.Value)
        {
            OutTriples.Add(MakeTuple(ClassName, E.VariableName, E.GroupName));
        }
    }
}


bool UPinVarSubsystem::UnstagePinVariable(FName ClassName, FName VariableName, FName GroupName)
{
    if (TArray<FPinnedVariable>* Vars = StagedPinnedGroups.Find(ClassName))
    {
        const int32 Removed = Vars->RemoveAll(
            [VariableName, GroupName](const FPinnedVariable& E)
            { return E.VariableName == VariableName && E.GroupName == GroupName; });
        if (Vars->Num() == 0)
        {
            StagedPinnedGroups.Remove(ClassName);
        }
        SaveToDisk();
        return Removed > 0;
    }
    return false;
}

void UPinVarSubsystem::MergeStagedIntoPinned()
{
    for (const auto& Pair : StagedPinnedGroups)
    {
        const FName ClassName = Pair.Key;
        const TArray<FPinnedVariable>& Staged = Pair.Value;

        TArray<FPinnedVariable>& Dest = PinnedGroups.FindOrAdd(ClassName);
        Dest.Append(Staged);
    }
}

const TArray<FPinnedVariable>* UPinVarSubsystem::GetPinnedVariables(FName ClassName) const
{
    return PinnedGroups.Find(ClassName);
}


FString UPinVarSubsystem::GetPinsFilePath() const
{
    // …Saved/PinVar/Pinned.json
    const FString Dir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("PinVar"))->GetBaseDir(), TEXT("PinVar"));
    IFileManager::Get().MakeDirectory(*Dir, /*Tree*/true);
    return FPaths::Combine(Dir, TEXT("Pinned.json"));
}

bool UPinVarSubsystem::SaveToDisk() const
{
    const FString FilePath = GetPinsFilePath();
    const FString Dir = FPaths::GetPath(FilePath);

    // Make sure directory exists
    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    if (!PF.DirectoryExists(*Dir))
    {
        if (!PF.CreateDirectoryTree(*Dir))
        {
            UE_LOG(LogTemp, Error, TEXT("PinVar: SaveToDisk - failed to create dir: %s"), *Dir);
            return false;
        }
    }

    // Build JSON
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    for (const auto& Pair : StagedPinnedGroups)
    {
        const FString ClassKey = Pair.Key.ToString();
        const TArray<FPinnedVariable>& Arr = Pair.Value;

        TArray<TSharedPtr<FJsonValue>> JArr;
        JArr.Reserve(Arr.Num());
        for (const FPinnedVariable& E : Arr)
        {
            // Skip empties just in case
            if (E.VariableName.IsNone() || E.GroupName.IsNone())
                continue;

            TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
            J->SetStringField(TEXT("Var"),   E.VariableName.ToString());
            J->SetStringField(TEXT("Group"), E.GroupName.ToString());
            JArr.Add(MakeShared<FJsonValueObject>(J));
        }
        Root->SetArrayField(ClassKey, JArr);
    }

    // Serialize
    FString OutStr;
    {
        TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);

        const bool bOk = FJsonSerializer::Serialize(Root, Writer);
        if (!bOk)
        {
            UE_LOG(LogTemp, Error, TEXT("PinVar: SaveToDisk - Serialize failed (root had %d keys). Trying sanity test..."), Root->Values.Num());

            // Sanity test: serialize a tiny object to see if the writer setup is the issue
            FString Sanity;
            TSharedRef<TJsonWriter<>> SanityWriter = TJsonWriterFactory<>::Create(&Sanity);
            TSharedRef<FJsonObject> Tiny = MakeShared<FJsonObject>();
            Tiny->SetStringField(TEXT("ok"), TEXT("true"));
            const bool bSanity = FJsonSerializer::Serialize(Tiny, SanityWriter);
            UE_LOG(LogTemp, Error, TEXT("PinVar: Sanity serialize %s. Output: %s"),
                   bSanity ? TEXT("SUCCEEDED") : TEXT("FAILED"),
                   bSanity ? *Sanity : TEXT("<empty>"));
            return false;
        }
    }

    // Save
    const bool bSaved = FFileHelper::SaveStringToFile(OutStr, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if (!bSaved)
    {
        UE_LOG(LogTemp, Error, TEXT("PinVar: SaveToDisk - SaveStringToFile failed: %s\nContent(%d bytes): %s"),
               *FilePath, OutStr.Len(), *OutStr);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("PinVar: SaveToDisk OK -> %s (%d bytes)"), *FilePath, OutStr.Len());
    }
    return bSaved;
}

bool UPinVarSubsystem::LoadFromDisk()
{
    const FString FilePath = GetPinsFilePath();
    FString InStr;
    if (!FPaths::FileExists(FilePath) || !FFileHelper::LoadFileToString(InStr, *FilePath))
    {
        UE_LOG(LogTemp, Log, TEXT("PinVar: LoadFromDisk - no file at %s"), *FilePath);
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InStr);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("PinVar: LoadFromDisk - JSON parse failed: %s"), *FilePath);
        return false;
    }

    StagedPinnedGroups.Empty();

    int32 ClassesLoaded = 0;
    for (const auto& KVP : Root->Values) // KVP.Key = class name, KVP.Value = array
    {
        const FString& ClassKey = KVP.Key;
        const TSharedPtr<FJsonValue>& Value = KVP.Value;

        const TArray<TSharedPtr<FJsonValue>>* JArr = nullptr;
        if (!Value.IsValid() || !Value->TryGetArray(JArr) || !JArr)
            continue;

        TArray<FPinnedVariable>& Arr = StagedPinnedGroups.FindOrAdd(FName(*ClassKey));
        for (const TSharedPtr<FJsonValue>& JV : *JArr)
        {
            const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
            if (!JV.IsValid() || !JV->TryGetObject(ObjPtr) || !ObjPtr || !ObjPtr->IsValid())
                continue;

            FString VarStr, GroupStr;
            (*ObjPtr)->TryGetStringField(TEXT("Var"), VarStr);
            (*ObjPtr)->TryGetStringField(TEXT("Group"), GroupStr);
            if (!VarStr.IsEmpty() && !GroupStr.IsEmpty())
            {
                Arr.Add(FPinnedVariable(FName(*VarStr), FName(*GroupStr)));
            }
        }

        if (Arr.Num() > 0) ++ClassesLoaded;
    }

    UE_LOG(LogTemp, Log, TEXT("PinVar: LoadFromDisk - loaded %d classes, total entries"),ClassesLoaded);
    MergeStagedIntoPinned();
    return true;
}