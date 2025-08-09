#include "PinVarModule.h"

#include "BlueprintEditorModule.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "Editor.h"
#include "PinVarSubsystem.h"
#include "SPinVarPanel.h"
#include "PropertyEditorModule.h"
#include "BPVariableDescriptionCustomization.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "PinVarEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Subsystems/AssetEditorSubsystem.h" 


const FName FPinVarModule::PinVarTabName("PinVar");


void FPinVarModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		PinVarTabName,
		FOnSpawnTab::CreateRaw(this, &FPinVarModule::OnSpawnPluginTab))
		.SetDisplayName(FText::FromString("Pinned Variables Tool"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	MenuRegHandle = UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPinVarModule::RegisterMenus));
	FPropertyEditorModule& PropEd =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	// Hook FBPVariableDescription (Blueprint variable row) with our extra field
	PropEd.RegisterCustomPropertyTypeLayout(
		"BPVariableDescription",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FBPVariableDescriptionCustomization::MakeInstance));
}

void FPinVarModule::ShutdownModule()
{
	if (GEditor)
	{
		if (UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>())
		{
			Subsystem->SaveToDisk();
		}
	}
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropEd =
			FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropEd.UnregisterCustomPropertyTypeLayout("BPVariableDescription");
	}
	UnregisterMenus();
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PinVarTabName);
}

TSharedRef<SDockTab> FPinVarModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	// (Re)scan on open
	ScanPinnedVariables();

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SPinVarPanel)
			.OnRefreshRequested(FSimpleDelegate::CreateRaw(this, &FPinVarModule::ScanPinnedVariables))
		];
}



void FPinVarModule::RegisterMenus()
{
	// Keep your Window menu entry
	if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window"))
	{
		FToolMenuSection& Section = Menu->AddSection("PinVarSection", FText::FromString("Custom Tools"));
		Section.AddMenuEntry(
			"OpenPinVarTool",
			FText::FromString("Pinned Variables Tool"),
			FText::FromString("Opens the PinVar Tool tab"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				FGlobalTabmanager::Get()->TryInvokeTab(PinVarTabName);
			}))
		);
	}
	
}

void FPinVarModule::UnregisterMenus()
{
	if (MenuRegHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(MenuRegHandle);
		MenuRegHandle.Reset();
	}
}

void FPinVarModule::ScanPinnedVariables()
{
	if (!GEditor) return;

	UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>();
	if (!Subsystem) return;

	Subsystem->PinnedGroups.Empty();

	// ---------- Helper: parse a group string and add deduped entries ----------
	auto ParseAndPin = [Subsystem](const FName ClassName, const FName VarName, FString GroupStr)
	{
		if (GroupStr.IsEmpty()) return;

		// Allow both "A,B" and "A|B"
		GroupStr.ReplaceCharInline(TEXT('|'), TEXT(','));

		TArray<FString> RawGroups;
		GroupStr.ParseIntoArray(RawGroups, TEXT(","), /*CullEmpty*/ true);

		TSet<FName> UniqueGroups;
		for (FString& G : RawGroups)
		{
			G = G.TrimStartAndEnd();
			if (!G.IsEmpty())
			{
				UniqueGroups.Add(FName(*G));
			}
		}

		for (const FName& GroupName : UniqueGroups)
		{
			if (!GroupName.IsNone())
			{
				Subsystem->PinVariable(ClassName, VarName, GroupName);
			}
		}
	};

	for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			const FString N = Class->GetName();
			const bool bIsBP = Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
			if (!Class || N.StartsWith(TEXT("SKEL_")) || N.StartsWith(TEXT("REINST_")))
			{
				continue;
			}

			bool bAnyPinned = false;

			for (TFieldIterator<FProperty> PropIt(Class, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				if (!Prop || !Prop->HasMetaData(TEXT("PinnedGroup")) || !bIsBP)
				{
					continue;
				}

				FString GroupStr = Prop->GetMetaData(TEXT("PinnedGroup"));
				if (GroupStr.IsEmpty())
				{
					continue;
				}

				bAnyPinned = true;
				const FName ClassFName = Class->GetFName();
				const FName VarFName = FName(*Prop->GetName());

				ParseAndPin(ClassFName, VarFName, GroupStr);
			}

			if (bAnyPinned)
			{
				UE_LOG(LogTemp, Log, TEXT("PinVar: found pinned vars in class %s"), *Class->GetName());
			}
		}
	
	Subsystem->MergeStagedIntoPinned();
	

}

IMPLEMENT_MODULE(FPinVarModule, PinVar)
