#include "PinVarSubsystem.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"
#include "Interfaces/IPluginManager.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Blueprint/BlueprintSupport.h"
#include "Engine/BlueprintGeneratedClass.h"

bool UPinVarSubsystem::ContainsTriple(const TArray<FPinnedVariable>& Arr, FName Var, FName Group, FName Comp)
{
	for (const FPinnedVariable& E : Arr)
	{
		if (E.VariableName == Var && E.GroupName == Group && E.ComponentTemplateName == Comp)
			return true;
	}
	return false;
}

void UPinVarSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	LoadFromDisk();
	MergeStagedIntoPinned();
	RepopulateSessionCacheAll();
}


void UPinVarSubsystem::StagePinVariable(FName ClassName, FName VariableName, FName GroupName,
                                        FName ComponentTemplateName)
{
	TArray<FPinnedVariable>& Vars = StagedPinnedGroups.FindOrAdd(ClassName);
	if (!ContainsTriple(Vars, VariableName, GroupName, ComponentTemplateName))
	{
		Vars.Add(FPinnedVariable(VariableName, GroupName, ComponentTemplateName));
	}
	
	
}


void UPinVarSubsystem::StagePinVariableWithTemplate(FName ClassName, FName VariableName, FName GroupName,
                                                    FName ComponentTemplateName, UObject* TemplatePtr,
                                                    FName ComponentVariablePrettyName)
{
	TArray<FPinnedVariable>& Vars = StagedPinnedGroups.FindOrAdd(ClassName);
	if (!ContainsTriple(Vars, VariableName, GroupName, ComponentTemplateName))
	{
		FPinnedVariable E(VariableName, GroupName, ComponentTemplateName, ComponentVariablePrettyName);
		E.ResolvedTemplate = TemplatePtr;
		Vars.Add(MoveTemp(E));
	}
	
}

bool UPinVarSubsystem::UnstagePinVariable(FName ClassName, FName VariableName, FName GroupName,
                                          FName ComponentTemplateName)
{
	if (TArray<FPinnedVariable>* Vars = StagedPinnedGroups.Find(ClassName))
	{
		const int32 Removed = Vars->RemoveAll(
			[VariableName, GroupName, ComponentTemplateName](const FPinnedVariable& E)
			{
				return E.VariableName == VariableName
					&& E.GroupName == GroupName
					&& E.ComponentTemplateName == ComponentTemplateName;
			});

		if (Vars->Num() == 0)
		{
			StagedPinnedGroups.Remove(ClassName);
		}
		return Removed > 0;
	}
	return false;
}

void UPinVarSubsystem::MergeStagedIntoPinned()
{
	const TMap<FName, TArray<FPinnedVariable>> Original = StagedPinnedGroups; // keep a copy
	StagedPinnedGroups.Empty();

	for (const TPair<FName, TArray<FPinnedVariable>>& Pair : Original)
	{
		const FName ClassName = Pair.Key;

		for (const FPinnedVariable& E : Pair.Value)
		{
			// Data Asset entry
			if (!E.AssetPath.IsNull())
			{
				UObject* Instance = E.AssetPath.ResolveObject();
				if (!Instance) { Instance = E.AssetPath.TryLoad(); }
				if (Instance)
				{
					StagePinVariableForDataAsset(ClassName, E.VariableName, E.GroupName, Instance);
				}
				continue;
			}

			// Component entry (template or pretty name)
			if (!E.ComponentTemplateName.IsNone() || E.ResolvedTemplate.IsValid())
			{
				StagePinVariableWithTemplate(
					ClassName,
					E.VariableName,
					E.GroupName,
					E.ComponentTemplateName,
					E.ResolvedTemplate.Get(),
					E.ComponentVariablePrettyName
				);
				continue;
			}

			// Class default (no component/asset)
			StagePinVariable(ClassName, E.VariableName, E.GroupName, NAME_None);
		}
	}

	// Mirror the freshly-restaged set into PinnedGroups
	PinnedGroups = StagedPinnedGroups;

	// Restore original staged map
	StagedPinnedGroups = Original;
}

void UPinVarSubsystem::RepopulateSessionCacheAll()
{
	for (auto& Pair : StagedPinnedGroups)
	{
		const FName ClassName = Pair.Key;
		UClass* Cls = FindFirstObjectSafe<UClass>(*ClassName.ToString());
		if (!Cls) continue;

		for (FPinnedVariable& E : Pair.Value)
		{
			if (!E.ComponentTemplateName.IsNone())
			{
				UObject* Found = nullptr;

				// Try CDO first
				if (UObject* CDO = Cls->GetDefaultObject(true))
				{
					Found = CDO->GetDefaultSubobjectByName(E.ComponentTemplateName);
				}

				// Try SCS pretty name
				if (!Found && !E.ComponentVariablePrettyName.IsNone())
				{
					for (UClass* C = Cls; C; C = C->GetSuperClass())
					{
						UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(C);
						if (!BPGC) continue;

						UBlueprint* OwnerBP = Cast<UBlueprint>(BPGC->ClassGeneratedBy);
						if (!OwnerBP) continue;

						USimpleConstructionScript* SCS = OwnerBP->SimpleConstructionScript;
						if (!SCS) continue;

						for (USCS_Node* Node : SCS->GetAllNodes())
						{
							if (!Node || Node->GetVariableName() != E.ComponentVariablePrettyName) continue;

							Found = Node->GetActualComponentTemplate(BPGC);
							if (!Found)
							{
								Found = Node->ComponentTemplate;
							}
							break;
						}
						if (Found) break;
					}
				}

				E.ResolvedTemplate = Found;
			}
		}
	}
}

FString UPinVarSubsystem::GetPinsFilePath()
{
	const FString Dir = FPaths::Combine(FPaths::ProjectDir(), TEXT("PinVar"));
	IFileManager::Get().MakeDirectory(*Dir, /*Tree*/true);
	return FPaths::Combine(Dir, TEXT("Pinned.json"));
}

bool UPinVarSubsystem::SaveToDisk() const
{
	UE_LOG(LogTemp, Display, TEXT("SaveToDisk"));
	const FString FilePath = GetPinsFilePath();
	const FString Dir = FPaths::GetPath(FilePath);

	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!PF.DirectoryExists(*Dir))
	{
		if (!PF.CreateDirectoryTree(*Dir))
		{
			UE_LOG(LogTemp, Error, TEXT("PinVar: SaveToDisk - failed to create dir: %s"), *Dir);
			return false;
		}
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	for (const auto& Pair : StagedPinnedGroups)
	{
		const FString ClassKey = Pair.Key.ToString();
		const TArray<FPinnedVariable>& Arr = Pair.Value;

		TArray<TSharedPtr<FJsonValue>> JArr;
		JArr.Reserve(Arr.Num());
		for (const FPinnedVariable& E : Arr)
		{
			if (E.VariableName.IsNone() || E.GroupName.IsNone())
				continue;

			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("Var"), E.VariableName.ToString());
			J->SetStringField(TEXT("Group"), E.GroupName.ToString());
			if (!E.ComponentTemplateName.IsNone())
			{
				J->SetStringField(TEXT("Comp"), E.ComponentTemplateName.ToString());
			}
			if (!E.ComponentVariablePrettyName.IsNone())
			{
				J->SetStringField(TEXT("CompVar"), E.ComponentVariablePrettyName.ToString());
			}
			if (!E.AssetPath.IsNull())
			{
				J->SetStringField(TEXT("Asset"), E.AssetPath.ToString());
			}
			JArr.Add(MakeShared<FJsonValueObject>(J));
		}
		Root->SetArrayField(ClassKey, JArr);
	}

	FString OutStr;
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);

		if (!FJsonSerializer::Serialize(Root, Writer))
		{
			UE_LOG(LogTemp, Error, TEXT("PinVar: SaveToDisk - Serialize failed (root had %d keys)."),
			       Root->Values.Num());
			return false;
		}
	}

	// --- Source Control: checkout before saving if the file exists; add after saving if it's new ---
	const bool bFileExists = PF.FileExists(*FilePath);
	const bool bSCEnabled  = ISourceControlModule::Get().IsEnabled();
	ISourceControlProvider* Provider = bSCEnabled ? &ISourceControlModule::Get().GetProvider() : nullptr;

	if (bSCEnabled && Provider && bFileExists)
	{
		Provider->Execute(ISourceControlOperation::Create<FUpdateStatus>(), FilePath);
		if (FSourceControlStatePtr State = Provider->GetState(FilePath, EStateCacheUsage::Use))
		{
			if (!State->IsCheckedOut())
			{
				if (State->CanCheckout())
				{
					if (!Provider->Execute(ISourceControlOperation::Create<FCheckOut>(), FilePath))
					{
						UE_LOG(LogTemp, Warning, TEXT("PinVar: SaveToDisk - Perforce checkout failed for %s"), *FilePath);
					}
				} else 
				{
					UE_LOG(LogTemp, Warning, TEXT("PinVar: SaveToDisk - Cant check out file %s"), *FilePath);
				}
			}
		}
	}
	const bool bSaved = FFileHelper::SaveStringToFile(OutStr, *FilePath,
	                                                  FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (!bSaved)
	{
		UE_LOG(LogTemp, Error, TEXT("PinVar: SaveToDisk - SaveStringToFile failed: %s"), *FilePath);
	}
	return bSaved;
}

void UPinVarSubsystem::StagePinVariableForDataAsset(FName ClassName, FName VariableName, FName GroupName,
                                                    UObject* DataAssetInstance)
{
	if (!DataAssetInstance || VariableName.IsNone() || GroupName.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("PinVar: Stage DA — invalid input."));
		return;
	}
	TArray<FPinnedVariable>& Bucket = StagedPinnedGroups.FindOrAdd(ClassName);

	const FString PathStr = DataAssetInstance->GetPathName();

	for (const FPinnedVariable& E : Bucket)
	{
		if (E.VariableName == VariableName &&
			E.GroupName == GroupName &&
			E.AssetPath.ToString() == PathStr)
		{
			return;
		}
	}

	FPinnedVariable NewEntry(VariableName, GroupName);
	NewEntry.AssetPath = FSoftObjectPath(DataAssetInstance);

	Bucket.Add(MoveTemp(NewEntry));
}

bool UPinVarSubsystem::LoadFromDisk()
{
	const FString FilePath = GetPinsFilePath();
	FString InStr;
	if (!FPaths::FileExists(FilePath) || !FFileHelper::LoadFileToString(InStr, *FilePath)) { return false; }

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InStr);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("PinVar: LoadFromDisk - JSON parse failed: %s"), *FilePath);
		return false;
	}

	StagedPinnedGroups.Empty();

	int32 ClassesLoaded = 0;
	for (const auto& KVP : Root->Values)
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

			FString VarStr, GroupStr, CompStr, CompVarStr, AssetStr;
			(*ObjPtr)->TryGetStringField(TEXT("Var"), VarStr);
			(*ObjPtr)->TryGetStringField(TEXT("Group"), GroupStr);
			(*ObjPtr)->TryGetStringField(TEXT("Comp"), CompStr);
			(*ObjPtr)->TryGetStringField(TEXT("CompVar"), CompVarStr);
			(*ObjPtr)->TryGetStringField(TEXT("Asset"), AssetStr);

			if (!VarStr.IsEmpty() && !GroupStr.IsEmpty())
			{
				Arr.Add(FPinnedVariable(
					FName(*VarStr),
					FName(*GroupStr),
					CompStr.IsEmpty() ? NAME_None : FName(*CompStr),
					CompVarStr.IsEmpty() ? NAME_None : FName(*CompVarStr),
					FSoftObjectPath(AssetStr)
				));
			}
		}

		if (Arr.Num() > 0) ++ClassesLoaded;
	}

	MergeStagedIntoPinned();
	RepopulateSessionCacheAll();
	return true;
}
