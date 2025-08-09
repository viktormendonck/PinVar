#pragma once

#include "Modules/ModuleManager.h"

class SDockTab;

class FPinVarModule : public IModuleInterface
{
public:
    // IModuleInterface
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    // Spawns our dockable tab
    TSharedRef<SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);

    // Adds "Pinned Variables Tool" under Window → Custom Tools
    void RegisterMenus();
    void UnregisterMenus();

    // Populates the subsystem by scanning for meta=(PinnedGroup="...")
    void ScanPinnedVariables();

private:
    FDelegateHandle MenuRegHandle;
    static const FName PinVarTabName;
};

