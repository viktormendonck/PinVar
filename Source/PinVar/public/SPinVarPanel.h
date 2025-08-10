// SPinVarPanel.h
#pragma once

#include "Widgets/SCompoundWidget.h"

class ISinglePropertyView;
class UBlueprint;
class UClass;
class UObject;
class UActorComponent;
struct FAssetData;
class FProperty;

class SPinVarPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPinVarPanel) {}
	SLATE_EVENT(FSimpleDelegate, OnRefreshRequested)
SLATE_END_ARGS()

void Construct(const FArguments& InArgs);
	void Refresh();

private:
	// Toolbar actions
	FReply OnAddBlueprintVariableClicked();
	void   OnBlueprintPicked(const FAssetData& AssetData);
	FReply OnRemoveBlueprintVariableClicked();
	void   ShowRemoveStagedDialog();

	// Unified “Add” dialog (Blueprint local / Parent C++ / Component)
	void ShowAddDialog(UBlueprint* BP);

	// Data gathering helpers for the Add dialog
	void GatherLocalVars(UBlueprint* BP, TArray<FName>& OutVars) const;
	void GatherNativeProps(UClass* Class, TArray<FName>& OutProps) const;
	void GatherComponentPropsByTemplate(UObject* CompTemplate, TArray<FName>& OutProps) const;

	// UI build
	void Rebuild();
	void GatherPinnedProperties();

	// Small utils
	static bool IsSkelOrReinst(const UClass* C);
	static bool IsEditableProperty(const FProperty* P);

private:
	FSimpleDelegate             OnRefreshRequested;
	TSharedPtr<SVerticalBox>    RootBox;

	struct FEntry { FName Group; TSharedRef<SWidget> Widget; };
	TMap<FName, TArray<FEntry>> Grouped;

	// ----- Add dialog state helpers -----
public:
	
	struct FCompOption
	{
		FName Label;
		FName TemplateName;
		TWeakObjectPtr<UActorComponent> Template;
	};
};