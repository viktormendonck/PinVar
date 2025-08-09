// SPinVarPanel.cpp
#include "SPinVarPanel.h"

#include "ISinglePropertyView.h"
#include "Blueprint/BlueprintSupport.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Text/STextBlock.h"
#include "PropertyHandle.h"
#include "PinVarSubsystem.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetData.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "BlueprintEditorModule.h"
#include "Editor.h"
#include "PinVarEditorUtils.h"


void SPinVarPanel::Construct(const FArguments& InArgs)
{
	OnRefreshRequested = InArgs._OnRefreshRequested;

	ChildSlot
	[
		SNew(SVerticalBox)

		// Toolbar row
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f)
		[
			SNew(SHorizontalBox)

			// Refresh button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
				.ToolTipText(FText::FromString(TEXT("Refresh pinned variables list")))
				.OnClicked_Lambda([this]()
				{
					Refresh();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Refresh")))
				]
			]
			// Add BP Variable button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(6.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
				.OnClicked(this, &SPinVarPanel::OnAddBlueprintVariableClicked)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Add BP Variable")))
				]
			]
			// Remove BP Variable button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(6.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
				.OnClicked(this, &SPinVarPanel::OnRemoveBlueprintVariableClicked)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Remove BP Variable")))
				]
			]
		]
		// Scrollable list
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(RootBox, SVerticalBox)
			]
		]
	];

	Refresh();
}



void SPinVarPanel::Refresh()
{
	if (OnRefreshRequested.IsBound())
	{
		OnRefreshRequested.Execute();
	}
	Grouped.Reset();
	Rebuild();
}

void SPinVarPanel::Rebuild()
{
	RootBox->ClearChildren();

	GatherPinnedProperties();

	if (Grouped.Num() == 0)
	{
		RootBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("No pinned variables found. Add meta=(PinnedGroup=\"...\"), rebuild and/or click Refresh.")))
		];
		return;
	}

	for (auto& KV : Grouped)
	{
		const FName Group = KV.Key;
		TArray<FEntry>& Entries = KV.Value;

		TSharedRef<SVerticalBox> ListVB = SNew(SVerticalBox);

		RootBox->AddSlot()
		.AutoHeight()
		.Padding(0.f, 4.f)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(true)
			.HeaderContent()[ SNew(STextBlock).Text(FText::FromName(Group)) ]
			.BodyContent()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ SNew(SSeparator) ]
				+ SVerticalBox::Slot().AutoHeight()[ ListVB ]
			]
		];

		for (const FEntry& E : Entries)
		{
			ListVB->AddSlot()
			.AutoHeight()
			.Padding(6.f, 6.f)
			[
				E.Widget
			];
		}
	}
}
static bool IsSkelOrReinst(const UClass* C)
{
	if (!C) return false;
	const FString N = C->GetName();
	return N.StartsWith(TEXT("SKEL_")) || N.StartsWith(TEXT("REINST_"));
}

void SPinVarPanel::GatherPinnedProperties()
{
    if (!GEditor) return;
    UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>();
    if (!Subsystem) return;

    FPropertyEditorModule& PropEd = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

    // Track which classes already got a header per group
    TMap<FName, TSet<FName>> LabeledClassesByGroup;

    for (const TPair<FName, TArray<FPinnedVariable>>& Pair : Subsystem->PinnedGroups)
    {
        const FName ClassName = Pair.Key;
        UClass* Cls = FindObject<UClass>(ANY_PACKAGE, *ClassName.ToString());
        if (!Cls || IsSkelOrReinst(Cls)) continue;

        UObject* CDO = Cls->GetDefaultObject(true);
        if (!CDO) continue;

        const FName ClassFName = Cls->GetFName();
        const FText ClassLabel = FText::FromString(Cls->GetName());

        for (const FPinnedVariable& Pinned : Pair.Value)
        {
            const FName VarName = Pinned.VariableName;
            FProperty* FoundProp = FindFProperty<FProperty>(Cls, VarName);
            if (!FoundProp) continue;

            // Split "Core|Debug"
            TArray<FString> Tokens;
            const FString GroupStr = Pinned.GroupName.ToString();
            GroupStr.ParseIntoArray(Tokens, TEXT("|"), true);
            if (Tokens.Num() == 0) { Tokens.Add(GroupStr); }

            for (const FString& Tok : Tokens)
            {
                const FName GroupName(*Tok.TrimStartAndEnd());
                if (GroupName.IsNone()) continue;

                // Fresh property view per group
                FSinglePropertyParams Params;
                TSharedPtr<ISinglePropertyView> View =
                    PropEd.CreateSingleProperty(CDO, VarName, Params);
                if (!View.IsValid()) continue;

                // Only show class header once per group
                bool bFirstForThisClassInGroup = !LabeledClassesByGroup.FindOrAdd(GroupName).Contains(ClassFName);

                TSharedRef<SWidget> RowWidget =
                    bFirstForThisClassInGroup
                    ? StaticCastSharedRef<SWidget>(
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(STextBlock)
                            .Text(ClassLabel)
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
                        [
                            StaticCastSharedRef<SWidget>(View.ToSharedRef())
                        ])
                    : StaticCastSharedRef<SWidget>(View.ToSharedRef());

                Grouped.FindOrAdd(GroupName).Add({ GroupName, RowWidget });

                if (bFirstForThisClassInGroup)
                {
                    LabeledClassesByGroup.FindOrAdd(GroupName).Add(ClassFName);
                }
            }
        }
    }
}

FReply SPinVarPanel::OnAddBlueprintVariableClicked()
{
	FAssetPickerConfig PickerConfig;
	PickerConfig.Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	PickerConfig.SelectionMode = ESelectionMode::Single;
	PickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SPinVarPanel::OnBlueprintPicked);

	TSharedRef<SWindow> PickerWindow = SNew(SWindow)
		.Title(FText::FromString("Select Blueprint"))
		.ClientSize(FVector2D(600, 400))
		[
			FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser")
			.Get()
			.CreateAssetPicker(PickerConfig)
		];

	FSlateApplication::Get().AddWindow(PickerWindow);

	return FReply::Handled();
}

void SPinVarPanel::OnBlueprintPicked(const FAssetData& AssetData)
{
	if (!AssetData.IsValid())
		return;

	UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset());
	if (!BP)
		return;

	ShowVariableGroupDialog(BP);
}


FReply SPinVarPanel::OnRemoveBlueprintVariableClicked()
{
	ShowRemoveStagedDialog();
	return FReply::Handled();
}

void SPinVarPanel::ShowRemoveStagedDialog()
{
    if (!GEditor) return;
    UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>();
    if (!Subsystem) return;

    // ---- Persistent dialog state on the heap ----
    struct FRemoveState : public TSharedFromThis<FRemoveState>
    {
        // what the subsystem returns: (Class, Var, Group)
        struct FItem
        {
            FName ClassName;
            FName VarName;
            FName GroupName;
            FString Label;
        };

        // Data that must outlive the dialog widgets
        TArray<TSharedPtr<FItem>> Items;
        TSharedPtr<FItem> Selected;
    };

    TSharedRef<FRemoveState> State = MakeShared<FRemoveState>();

    // Fill Items from the subsystem (requires you to have these helpers)
    {
        TArray<TTuple<FName,FName,FName>> Triples;
        Subsystem->GetAllStaged(Triples); // <-- make sure you implemented this

        if (Triples.Num() == 0)
        {
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("No staged BP variables to remove.")));
            return;
        }

        State->Items.Reserve(Triples.Num());

        for (const auto& T : Triples)
        {
            const FName ClassName = T.Get<0>();
            const FName VarName   = T.Get<1>();
            const FName GroupName = T.Get<2>();

            // Pretty class name if possible
            FString ClassStr = ClassName.ToString();
            if (UClass* C = FindObject<UClass>(nullptr, *ClassStr)) // avoid ANY_PACKAGE
            {
                ClassStr = C->GetName();
            }

            TSharedPtr<FRemoveState::FItem> Item = MakeShared<FRemoveState::FItem>();
            Item->ClassName = ClassName;
            Item->VarName   = VarName;
            Item->GroupName = GroupName;
            Item->Label     = FString::Printf(TEXT("%s :: %s  —  %s"),
                                *ClassStr, *VarName.ToString(), *GroupName.ToString());
            State->Items.Add(Item);
        }

        State->Selected = State->Items[0];
    }

    // ---- Build dialog UI ----
    TSharedRef<SWindow> Dialog = SNew(SWindow)
        .Title(FText::FromString(TEXT("Remove BP Variable (staged)")))
        .SupportsMaximize(false)
        .SupportsMinimize(false)
        .ClientSize(FVector2D(560, 180));

    Dialog->SetContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(12,10,12,8)
        [
            SNew(STextBlock).Text(FText::FromString(TEXT("Select an entry to remove:")))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(12,0,12,8)
        [
            SNew(SComboBox<TSharedPtr<FRemoveState::FItem>>)
            .OptionsSource(&State->Items) // safe: Items lives in State
            .OnGenerateWidget_Lambda([](TSharedPtr<FRemoveState::FItem> In)
            {
                return SNew(STextBlock).Text(FText::FromString(In.IsValid() ? In->Label : TEXT("")));
            })
            .OnSelectionChanged_Lambda([State](TSharedPtr<FRemoveState::FItem> In, ESelectInfo::Type)
            {
                State->Selected = In;
            })
            .InitiallySelectedItem(State->Selected)
            [
                SNew(STextBlock)
                .Text_Lambda([State]()
                {
                    return FText::FromString(State->Selected.IsValid() ? State->Selected->Label : TEXT(""));
                })
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(12,0,12,12)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
            [
                SNew(SButton)
            	.ButtonStyle(FAppStyle::Get(), "Button")
				.ForegroundColor(FLinearColor::Red)
                .Text(FText::FromString(TEXT("Remove")))
                .OnClicked_Lambda([this, Dialog, State, Subsystem]() -> FReply
                {
                    if (State->Selected.IsValid())
                    {
                        const FName ClassName = State->Selected->ClassName;
                        const FName VarName   = State->Selected->VarName;
                        const FName GroupName = State->Selected->GroupName;

                        // Remove from staged (your subsystem helper)
                        Subsystem->UnstagePinVariable(ClassName, VarName, GroupName);
                        Refresh();
                    }
                    Dialog->RequestDestroyWindow();
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Cancel")))
                .OnClicked_Lambda([Dialog]() -> FReply
                {
                    Dialog->RequestDestroyWindow();
                    return FReply::Handled();
                })
            ]
        ]
    );

    FSlateApplication::Get().AddWindow(Dialog);
}


void SPinVarPanel::ShowVariableGroupDialog(UBlueprint* BP)
{
    if (!BP) return;

    struct FDialogState : public TSharedFromThis<FDialogState>
    {
        UBlueprint* BP = nullptr;
        TArray<TSharedPtr<FName>> VarOptions;
        TSharedPtr<FName> SelectedVar;
        FString GroupName;
    };
    TSharedRef<FDialogState> State = MakeShared<FDialogState>();
    State->BP = BP;

    for (const FBPVariableDescription& VarDesc : BP->NewVariables)
    {
        if (!VarDesc.VarName.IsNone())
            State->VarOptions.Add(MakeShared<FName>(VarDesc.VarName));
    }
    if (State->VarOptions.Num() == 0)
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("This Blueprint has no local variables.")));
        return;
    }
    State->SelectedVar = State->VarOptions[0];

    TSharedRef<SWindow> Dialog = SNew(SWindow)
        .Title(FText::FromString(TEXT("Select Variable & Group")))
        .SupportsMaximize(false)
        .SupportsMinimize(false)
        .ClientSize(FVector2D(420, 210));

    Dialog->SetContent(
        SNew(SVerticalBox)

        + SVerticalBox::Slot().AutoHeight().Padding(12,10,12,4)
        [ SNew(STextBlock).Text(FText::FromString(TEXT("Variable:"))) ]

        + SVerticalBox::Slot().AutoHeight().Padding(12,0,12,8)
        [
            SNew(SComboBox<TSharedPtr<FName>>)
            .OptionsSource(&State->VarOptions)
            .OnGenerateWidget_Lambda([](TSharedPtr<FName> InItem)
            {
                return SNew(STextBlock).Text(FText::FromName(InItem.IsValid()? *InItem : NAME_None));
            })
            .OnSelectionChanged_Lambda([State](TSharedPtr<FName> NewSel, ESelectInfo::Type)
            {
                State->SelectedVar = NewSel;
            })
            .InitiallySelectedItem(State->SelectedVar)
            [
                SNew(STextBlock)
                .Text_Lambda([State]()
                {
                    return State->SelectedVar.IsValid()
                        ? FText::FromName(*State->SelectedVar)
                        : FText::FromString(TEXT("None"));
                })
            ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(12,0,12,4)
        [ SNew(STextBlock).Text(FText::FromString(TEXT("Group Name:"))) ]

        + SVerticalBox::Slot().AutoHeight().Padding(12,0,12,12)
        [
            SNew(SEditableTextBox)
            .Text_Lambda([State]() { return FText::FromString(State->GroupName); })
            .OnTextChanged_Lambda([State](const FText& NewText)
            {
                State->GroupName = NewText.ToString();
            })
            .HintText(FText::FromString(TEXT("e.g. Core or Core|Debug")))
        ]

        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(12,0,12,12)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
            [
                SNew(SButton)
                .ButtonStyle(FAppStyle::Get(), "PrimaryButton")
                .Text(FText::FromString(TEXT("Add")))
                .OnClicked_Lambda([this, Dialog, State]()
                {
                	if (State->BP && State->SelectedVar.IsValid() && !State->GroupName.IsEmpty())
                	{
                		// Normalize separators
						FString GroupStr = State->GroupName;
						GroupStr.ReplaceCharInline(TEXT(','), TEXT('|'));

						// Persist to BP metadata (so it survives editor sessions)
						PinVarEditorUtils::AddBPVarPinnedGroupToken(State->BP, *State->SelectedVar, GroupStr);

						// Stage immediately so the UI shows it even if the class/asset isn’t reloaded yet
						if (GEditor)
						{
							if (UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>())
							{
								const FName ClassName = State->BP->GeneratedClass ? State->BP->GeneratedClass->GetFName() : NAME_None;
								if (!ClassName.IsNone())
								{
									// Support multiple tokens in one go
									FString Temp = GroupStr;
									Temp.ReplaceCharInline(TEXT('|'), TEXT(','));
									TArray<FString> Groups; Temp.ParseIntoArray(Groups, TEXT(","), true);
									for (FString& G : Groups)
									{
										G = G.TrimStartAndEnd();
										if (!G.IsEmpty())
										{
											Subsystem->StagePinVariable(ClassName, *State->SelectedVar, FName(*G));
										}
									}
								}
							}
						}

						// Refresh => ScanPinnedVariables() will repopulate from metadata,
						// then merge staged entries so you see it instantly.
						Refresh();
                	}
                	return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Cancel")))
                .OnClicked_Lambda([Dialog]()
                {
                    Dialog->RequestDestroyWindow();
                    return FReply::Handled();
                })
            ]
        ]
    );

    FSlateApplication::Get().AddWindow(Dialog);
}
