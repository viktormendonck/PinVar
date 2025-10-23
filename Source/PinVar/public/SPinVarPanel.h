// SPinVarPanel.h
#pragma once

#include "Widgets/SCompoundWidget.h"

class SSearchableComboBox;
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
    SLATE_BEGIN_ARGS(SPinVarPanel)
    {
    }

    SLATE_EVENT(FSimpleDelegate, OnRefreshRequested)
    SLATE_END_ARGS()

    static FString PrettyBlueprintDisplayName(const UClass* Cls);
    void Construct(const FArguments& InArgs);
    void Refresh();

private:
    // Toolbar actions
    FReply OnAddBlueprintVariableClicked();
    void OnBlueprintPicked(const FAssetData& AssetData);

    // Unified “Add” dialog (Blueprint local / Parent C++ / Component)
    void ShowAddDialog(UBlueprint* BP);

    // Data gathering helpers for the Add dialog
    void GatherLocalVars(UBlueprint* BP, TArray<FName>& OutVars) const;
    void GatherNativeProps(UClass* Class, TArray<FName>& OutProps) const;
    void GatherComponentPropsByTemplate(UObject* CompTemplate, TArray<FName>& OutProps) const;

    // UI build
    void Rebuild();
    void GatherPinnedProperties();

    FReply OnRemovePinned(FName ClassName, FName VarName, FName GroupName, FName CompName);
    // Small utils
    static bool IsSkelOrReinst(const UClass* C);
    static bool IsEditableProperty(const FProperty* P);
    void OnAnyAssetPicked(const FAssetData& AssetData);
    void ShowAddDialogForDataAsset(UObject* DataAssetInstance);

    FSimpleDelegate OnRefreshRequested;
    TSharedPtr<SVerticalBox> RootBox;

    FName ClassName{}; // weird bypass for illegal fname in func

    struct FEntry
    {
        FName Group;
        TSharedRef<SWidget> Widget;
    };

    TMap<FName, TArray<FEntry>> Grouped;

    // Track currently open popups so we can close them when needed
    TWeakPtr<class SWindow> SelectBlueprintWindow;
    TWeakPtr<class SWindow> AddVariableWindow;

    // add stuff to allow groups to remain open between
    TMap<FName, bool> GroupExpandedState;
    TMap<FName, TWeakPtr<class SExpandableArea>> GroupAreaWidgets;

public:
    struct FCompOption
    {
        FName Label;
        FName TemplateName;
        TWeakObjectPtr<UActorComponent> Template;
    };

    struct FState : public TSharedFromThis<FState>
    {
        UBlueprint* BP = nullptr;
        UClass* Class = nullptr;
        bool bIsDataAssetClass = false;
        TWeakObjectPtr<UObject> DataAssetInstance;

        enum class ESourceType : uint8
        {
            LocalBPVar,
            LocalCppVar,
            ComponentVar,
        };

        ESourceType SourceType = ESourceType::LocalBPVar;

        TArray<TSharedPtr<FString>> LocalVarOpts;
        TSharedPtr<FString> LocalVarSel;

        TArray<TSharedPtr<FString>> NativePropOpts;
        TSharedPtr<FString> NativePropSel;

        TArray<TSharedPtr<FString>> CompOptLabels;
        TMap<FString, TSharedPtr<FCompOption>> LabelToCompOpt;
        TSharedPtr<FCompOption> CompSel;

        TArray<TSharedPtr<FString>> CompPropOpts;
        TSharedPtr<FString> CompPropSel;
        TSharedPtr<SSearchableComboBox> CompPropCombo;

        TArray<TSharedPtr<FCompOption>> CompOpts;

        FString GroupStr;
        TArray<FString> AllGroups;

        TArray<TSharedPtr<FString>> ExistingGroupOpts;
        TSharedPtr<FString> ExistingGroupSel;
    };

private:
    void GetAllGroups(TSharedRef<FState> S);
    static bool IsBPDeclared(const FProperty* P);
    static bool IsNativeDeclared(const FProperty* P);
    static bool IsSimpleStruct(const UScriptStruct* SS);
    UClass* ResolveGeneratedClassByShortName();
    static bool IsComplexStructContainer(const FProperty* P);
    static bool IsContainerProperty(const FProperty* P);
    static UObject* FindComponentTemplate(UClass* Class, FName TemplateName);
    static void BuildComponentOptions(UBlueprint* BP, UClass* Class, TArray<TSharedPtr<FCompOption>>& Out);
    FString GroupStr;
};
