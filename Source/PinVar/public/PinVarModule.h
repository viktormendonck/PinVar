#pragma once

#include "Modules/ModuleManager.h"

class SDockTab;

class FPinVarModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedRef<SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);

    void RegisterMenus();
    void UnregisterMenus();

    // Refreshes from JSON (no class/property scanning)
    void ScanPinnedVariables();

private:
    FDelegateHandle MenuRegHandle;
    static const FName PinVarTabName;
};