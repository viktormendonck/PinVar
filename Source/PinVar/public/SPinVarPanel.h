// SPinVarPanel.h
#pragma once
#include "Widgets/SCompoundWidget.h"
#include "UObject/WeakObjectPtr.h"

class ISinglePropertyView;
class UBlueprint;
struct FAssetData;

class SPinVarPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPinVarPanel) {}
	SLATE_EVENT(FSimpleDelegate, OnRefreshRequested)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void Refresh();

private:
	FSimpleDelegate OnRefreshRequested;
	TSharedPtr<SVerticalBox> RootBox;

	FReply OnAddBlueprintVariableClicked();
	void OnBlueprintPicked(const FAssetData& AssetData);
	void ShowVariableGroupDialog(UBlueprint* BP);
	FReply OnRemoveBlueprintVariableClicked();
	void   ShowRemoveStagedDialog();

	struct FEntry { FName Group; TSharedRef<SWidget> Widget; };
	TMap<FName, TArray<FEntry>> Grouped;

	void Rebuild();
	void GatherPinnedProperties();
};