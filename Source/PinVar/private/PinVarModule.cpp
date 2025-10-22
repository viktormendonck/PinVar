#include "PinVarModule.h"

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
    FTSTicker::GetCoreTicker().AddTicker(
    FTickerDelegate::CreateRaw(this, &FPinVarModule::InitialRefreshOnce),
    1.0f);
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
    ScanPinnedVariables();

    TSharedRef<SPinVarPanel> Panel = SNew(SPinVarPanel)
                                     .OnRefreshRequested(FSimpleDelegate::CreateRaw(this, &FPinVarModule::ScanPinnedVariables));

    PanelWeak = Panel;

    return SNew(SDockTab)
    .TabRole(NomadTab)
    [SNew(SPinVarPanel)
     .OnRefreshRequested(FSimpleDelegate::CreateRaw(this, &FPinVarModule::ScanPinnedVariables))];
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
        })));
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
        Subsystem->LoadFromDisk();
    }
}

bool FPinVarModule::InitialRefreshOnce(float)
{
    if (!GEditor) return true; // keep trying until editor is up

    if (TSharedPtr<SPinVarPanel> P = PanelWeak.Pin())
    {
        P->Refresh(); // Refresh() -> OnRefreshRequested.Execute()
    }
    else
    {
        // panel not open yet; run the bound work directly
        ScanPinnedVariables();
    }
    return false; // done
}

IMPLEMENT_MODULE(FPinVarModule, PinVar)
