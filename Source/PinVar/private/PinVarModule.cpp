#include "PinVarModule.h"

#include "BlueprintEditorModule.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"
#include "Editor.h"
#include "PinVarSubsystem.h"
#include "SPinVarPanel.h"
#include "PropertyEditorModule.h"

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
	UnregisterMenus();
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PinVarTabName);
}

TSharedRef<SDockTab> FPinVarModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	// Sync from disk on open
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

	if (UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>())
	{
		// Source of truth = JSON (staged). We show staged immediately.
		Subsystem->LoadFromDisk();            // refresh staged
		Subsystem->MergeStagedIntoPinned();   // mirror for display
		UE_LOG(LogTemp, Log, TEXT("PinVar: Refreshed from disk."));
	}
}

IMPLEMENT_MODULE(FPinVarModule, PinVar)