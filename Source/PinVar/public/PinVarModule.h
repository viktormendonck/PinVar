#pragma once

#include "SPinVarPanel.h"
#include "Modules/ModuleManager.h"

class SDockTab;
class SPinVarPanel;

class FPinVarModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedRef<SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);

	void RegisterMenus();
	void UnregisterMenus();
	void ScanPinnedVariables();

	bool InitialRefreshOnce(float);

private:
	FDelegateHandle MenuRegHandle;
	static const FName PinVarTabName;

	TWeakPtr<SPinVarPanel> PanelWeak;
};
