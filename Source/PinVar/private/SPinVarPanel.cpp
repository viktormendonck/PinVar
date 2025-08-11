// SPinVarPanel.cpp
#include "SPinVarPanel.h"

#include "ISinglePropertyView.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "SSearchableComboBox.h"
#include "Widgets/Input/SSuggestionTextBox.h"

#include "Widgets/Input/SSegmentedControl.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"

#include "PinVarSubsystem.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/ActorComponent.h"
#include "UObject/UObjectIterator.h"
#include "Blueprint/BlueprintSupport.h"
#include "Engine/BlueprintGeneratedClass.h"

UObject* SPinVarPanel::FindComponentTemplate(UClass* Class, FName TemplateName)
{
	if (!Class || TemplateName.IsNone()) return nullptr;

	const FName Alt(*(TemplateName.ToString() + TEXT("_GEN_VARIABLE")));

	// Walk this class and all supers
	for (UClass* C = Class; C; C = C->GetSuperClass())
	{
		if (UObject* CDO = C->GetDefaultObject(true))
		{
			if (UObject* T = CDO->GetDefaultSubobjectByName(TemplateName)) return T;
			if (UObject* T2 = CDO->GetDefaultSubobjectByName(Alt))         return T2;

			TArray<UObject*> Subs;
			GetObjectsWithOuter(CDO, Subs, true);
			for (UObject* O : Subs)
			{
				if (!O) continue;
				const FName N = O->GetFName();
				if (N == TemplateName || N == Alt)
					return O;
			}
		}
	}

	// Last‑ditch global sweep (editor only): archetype components under any CDO in the chain
	for (TObjectIterator<UActorComponent> It; It; ++It)
	{
		UActorComponent* Comp = *It;
		if (!Comp || !Comp->HasAnyFlags(RF_ArchetypeObject)) continue;

		// Check whether this component ultimately lives under one of our class CDOs
		UObject* Outer = Comp->GetOuter();
		while (Outer && !Outer->IsA<UClass>())
		{
			if (Outer->GetFName() == TemplateName || Outer->GetFName() == Alt) return Comp;
			Outer = Outer->GetOuter();
		}
	}
	return nullptr;
}

void SPinVarPanel::BuildComponentOptions(UBlueprint* BP, UClass* Class,TArray<TSharedPtr<FCompOption>>& Out)
{
	Out.Reset();
	if (!Class) return;

	TSet<FName> Seen;
	for (UClass* C = Class; C; C = C->GetSuperClass())
	{
		if (UObject* CDO = C->GetDefaultObject(true))
		{
			TArray<UObject*> Subs;
			GetObjectsWithOuter(CDO, Subs, false);

			for (UObject* O : Subs)
			{
				UActorComponent* Comp = Cast<UActorComponent>(O);
				if (!Comp) continue;

				const FName TmplName = Comp->GetFName();
				if (Seen.Contains(TmplName)) continue;

				Seen.Add(TmplName);

				TSharedRef<FCompOption> Opt = MakeShared<SPinVarPanel::FCompOption>();
				Opt->Label        = TmplName;   
				Opt->TemplateName = TmplName;   
				Opt->Template     = Comp;       
				Out.Add(Opt);
			}
		}
	}

	for (UClass* C = Class; C; C = C->GetSuperClass())
	{
		UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(C);
		if (!BPGC) continue;
		UBlueprint* OwnerBP = Cast<UBlueprint>(BPGC->ClassGeneratedBy);
		if (!OwnerBP) continue;
		USimpleConstructionScript* SCS = OwnerBP->SimpleConstructionScript;
		if (!SCS) continue;
		
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (!Node || !C) return;
			UActorComponent* ActualTemplate = BPGC ? Node->GetActualComponentTemplate(BPGC) : nullptr;
			if (!ActualTemplate)
			{
				ActualTemplate = Node->ComponentTemplate;
			}

			const FName Pretty = Node->GetVariableName();
			FName TemplateKey = ActualTemplate ? ActualTemplate->GetFName()
											   : FName(*(Pretty.ToString() + TEXT("_GEN_VARIABLE")));

			for (auto& Opt : Out)
			{
				if (Opt->TemplateName == TemplateKey)
				{
					Opt->Label = Pretty;
					if (ActualTemplate) { Opt->Template = ActualTemplate; }
					return;
				}
			}

			// Otherwise add it now
			if (!Seen.Contains(TemplateKey))
			{
				Seen.Add(TemplateKey);
				TSharedRef<FCompOption> Opt = MakeShared<SPinVarPanel::FCompOption>();
				Opt->Label        = Pretty;
				Opt->TemplateName = TemplateKey;
				Opt->Template     = ActualTemplate; // may be null (rare)
				Out.Add(Opt);
			}
		}
	}

	Out.StableSort([](const TSharedPtr<SPinVarPanel::FCompOption>& A,
	                  const TSharedPtr<SPinVarPanel::FCompOption>& B)
	{
		return A->Label.LexicalLess(B->Label);
	});
}

FString SPinVarPanel::PrettyBlueprintDisplayName(const UClass* Cls)
{
	if (!Cls) return TEXT("");
	FString N = Cls->GetName();
	N.RemoveFromEnd(TEXT("_C"), ESearchCase::CaseSensitive);
	return N;
}

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

			// Refresh
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
					SNew(STextBlock).Text(FText::FromString(TEXT("Refresh")))
				]
			]

			// Add Variable (BP / Parent C++ / Component)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(6.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
				.OnClicked(this, &SPinVarPanel::OnAddBlueprintVariableClicked)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Add Variable")))
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

	if (GEditor)
	{
		if (UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>())
		{
			Subsystem->RepopulateSessionCacheAll();
		}
	}

	Grouped.Reset();
	Rebuild();
}

void SPinVarPanel::Rebuild()
{
	for (auto& Pair : GroupAreaWidgets)
	{
		const FName GroupName = Pair.Key;
		if (TSharedPtr<SExpandableArea> Area = Pair.Value.Pin())
		{
			GroupExpandedState.Add(GroupName, Area->IsExpanded());
		}
	}
	GroupAreaWidgets.Empty();

	RootBox->ClearChildren();
	GatherPinnedProperties();

	if (Grouped.Num() == 0)
	{
		RootBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("No pinned variables yet. Use “Add Variable” to stage entries.")))
		];

		GroupExpandedState.Empty();
		return;
	}

	for (auto& KV : Grouped)
	{
		const FName Group = KV.Key;
		TArray<FEntry>& Entries = KV.Value;

		TSharedRef<SVerticalBox> ListVB = SNew(SVerticalBox);

		// Create the group area (collapsed by default), then apply remembered state
		TSharedRef<SExpandableArea> Area =
			SNew(SExpandableArea)
			.InitiallyCollapsed(true)
			.HeaderContent()[ SNew(STextBlock).Text(FText::FromName(Group)) ]
			.BodyContent()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ SNew(SSeparator) ]
				+ SVerticalBox::Slot().AutoHeight()[ ListVB ]
			];

		// Restore expansion if we remembered it
		const bool* Remembered = GroupExpandedState.Find(Group);
		if (Remembered && *Remembered)
		{
			Area->SetExpanded(true);
		}

		// Track this area so we can snapshot its state next rebuild
		GroupAreaWidgets.Add(Group, Area);

		RootBox->AddSlot()
		.AutoHeight()
		.Padding(0.f, 4.f)
		[
			Area
		];

		// Populate the body with the class sections we already assembled
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

bool SPinVarPanel::IsSkelOrReinst(const UClass* C)
{
	if (!C) return false;
	const FString N = C->GetName();
	return N.StartsWith(TEXT("SKEL_")) || N.StartsWith(TEXT("REINST_"));
}

bool SPinVarPanel::IsEditableProperty(const FProperty* P)
{
	if (!P) return false;

	// Editable on class defaults if it has CPF_Edit (covers EditAnywhere + EditDefaultsOnly + EditInstanceOnly)
	const bool bHasEdit = P->HasAnyPropertyFlags(CPF_Edit);

	// Hide if read-only in editor or not editable on templates (CDOs)
	const bool bReadOnlyInEditor  = P->HasAnyPropertyFlags(CPF_EditConst);
	const bool bHiddenOnTemplates = P->HasAnyPropertyFlags(CPF_DisableEditOnTemplate);

	// We don't expose transient or delegate properties
	const bool bTransient = P->HasAnyPropertyFlags(CPF_Transient);
	const bool bIsDelegate =
		P->IsA(FMulticastDelegateProperty::StaticClass()) ||
		P->IsA(FDelegateProperty::StaticClass());

	return bHasEdit && !bReadOnlyInEditor && !bHiddenOnTemplates && !bTransient && !bIsDelegate;
}

void SPinVarPanel::GetAllGroups(TSharedRef<FState> S)
{
	TSet<FString> Unique;

	if (GEditor)
	{
		if (UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>())
		{
			// Pull from pinned mirror
			for (const auto& Pair : Subsystem->PinnedGroups)                          // Map<FName ClassName, TArray<FPinnedVariable>>
			{
				for (const FPinnedVariable& E : Pair.Value)
				{
					if (!E.GroupName.IsNone())
					{
						Unique.Add(E.GroupName.ToString());
					}
				}
			}

			// (Optional) also include staged-only entries, if any
			for (const auto& Pair : Subsystem->StagedPinnedGroups)
			{
				for (const FPinnedVariable& E : Pair.Value)
				{
					if (!E.GroupName.IsNone())
					{
						Unique.Add(E.GroupName.ToString());
					}
				}
			}
		}
	}

	S->AllGroups.Reserve(Unique.Num());
	for (const FString& G : Unique) { S->AllGroups.Add(G); }
	S->AllGroups.Sort();
	for (const FString& G : S->AllGroups)
	{
		S->ExistingGroupOpts.Add(MakeShared<FString>(G));
	}
}

bool SPinVarPanel::IsBPDeclared(const FProperty* P)
{
	const UClass* OwnerClass = Cast<UClass>(P ? P->GetOwnerStruct() : nullptr);
	return (OwnerClass && OwnerClass->ClassGeneratedBy != nullptr);
}

bool SPinVarPanel::IsNativeDeclared(const FProperty* P)
{
	const UClass* OwnerClass = Cast<UClass>(P ? P->GetOwnerStruct() : nullptr);
	return (OwnerClass && OwnerClass->ClassGeneratedBy == nullptr);
}

void SPinVarPanel::GatherPinnedProperties()
{
	if (!GEditor) return;
	UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>();
	if (!Subsystem) return;

	FPropertyEditorModule& PropEd = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	
	struct FClassBuckets
	{
		FName   ClassName;
		FText   ClassLabel;

		TArray<FName> BPVars;
		TArray<FName> NativeVars;

		TMap<FName, TArray<FName>> ComponentVarsByName;          // CompLabel -> Var list
		TMap<FName, TWeakObjectPtr<UObject>> ComponentTemplates; // CompLabel -> template obj
	};

	TMap<FName, TMap<FName, FClassBuckets>> Build; // Group -> (ClassName -> Buckets)

	// -------- Collect --------
	for (const TPair<FName, TArray<FPinnedVariable>>& Pair : Subsystem->PinnedGroups)
	{
		const FName ClassName = Pair.Key;
		UClass* Cls = FindFirstObjectSafe<UClass>(*ClassName.ToString());
		if (!Cls || IsSkelOrReinst(Cls)) continue;

		UObject* CDO = Cls->GetDefaultObject(true);
		if (!CDO) continue;

		const FName ClassFName = Cls->GetFName();
		const FText ClassLabel = FText::FromString(PrettyBlueprintDisplayName(Cls)); // display-only prettified name. 

		for (const FPinnedVariable& Pinned : Pair.Value)
		{
			// Resolve target object (class defaults or component template)
			UObject* TargetObj = CDO;
			const bool bIsComponent = !Pinned.ComponentTemplateName.IsNone();
			if (bIsComponent)
			{
				TargetObj = Pinned.ResolvedTemplate.IsValid()
					? Pinned.ResolvedTemplate.Get()
					: FindComponentTemplate(Cls, Pinned.ComponentTemplateName);

				if (!TargetObj)
				{
					UE_LOG(LogTemp, Warning, TEXT("PinVar.Panel: component template '%s' not resolved on %s"),
						*Pinned.ComponentTemplateName.ToString(), *CDO->GetName());
					continue;
				}
			}

			// Check property exists and is editable
			FProperty* Found = FindFProperty<FProperty>(TargetObj->GetClass(), Pinned.VariableName);
			if (!Found || !IsEditableProperty(Found)) continue;  // uses your IsEditableProperty() 

			// Groups can be split by ','
			TArray<FString> Tokens;
			const FString Group = Pinned.GroupName.ToString();
			Group.ParseIntoArray(Tokens, TEXT(","), /*CullEmpty*/true);
			if (Tokens.Num() == 0) Tokens.Add(GroupStr);

			for (const FString& Tok : Tokens)
			{
				const FName GroupName(*Tok.TrimStartAndEnd());
				if (GroupName.IsNone()) continue;

				FClassBuckets& B = Build.FindOrAdd(GroupName).FindOrAdd(ClassFName);
				B.ClassName  = ClassFName;
				B.ClassLabel = ClassLabel;

				if (!bIsComponent)
				{
					// Class defaults: BP vs Native by owner
					if (IsBPDeclared(Found))          { B.BPVars.Add(Pinned.VariableName); }
					else if (IsNativeDeclared(Found)) { B.NativeVars.Add(Pinned.VariableName); }
					else                               { B.BPVars.Add(Pinned.VariableName); } // fallback
				}
				else
				{
					// Component: show pretty label if present, else template name
					const FName CompLabel = !Pinned.ComponentVariablePrettyName.IsNone()
						? Pinned.ComponentVariablePrettyName
						: Pinned.ComponentTemplateName;

					B.ComponentVarsByName.FindOrAdd(CompLabel).Add(Pinned.VariableName);
					if (!B.ComponentTemplates.Contains(CompLabel))
					{
						B.ComponentTemplates.Add(CompLabel, TargetObj);
					}
				}
			}
		}
	}

	// -------- Emit UI into Grouped  --------
	for (auto& GroupKV : Build)
	{
		const FName Group = GroupKV.Key;
		TMap<FName, FClassBuckets>& Classes = GroupKV.Value;

		// Sort classes by their label (case-insensitive)
		TArray<FName> ClassOrder;
		Classes.GenerateKeyArray(ClassOrder);
		ClassOrder.Sort([&Classes](const FName& A, const FName& B)
		{
			return Classes[A].ClassLabel.ToString().Compare(
				Classes[B].ClassLabel.ToString(), ESearchCase::IgnoreCase) < 0;
		});

		for (const FName& CN : ClassOrder)
		{
			FClassBuckets& B = Classes[CN];

			// Sort buckets
			B.BPVars.Sort(FNameLexicalLess());
			B.NativeVars.Sort(FNameLexicalLess());

			TArray<FName> CompNames;
			B.ComponentVarsByName.GenerateKeyArray(CompNames);
			CompNames.Sort(FNameLexicalLess());
			for (auto& CKV : B.ComponentVarsByName) { CKV.Value.Sort(FNameLexicalLess()); }

			TSharedRef<SVerticalBox> ClassVB = SNew(SVerticalBox);

			ClassVB->AddSlot().AutoHeight().Padding(6.f, 8.f, 6.f, 4.f)
			[
				SNew(STextBlock)
				.Text(B.ClassLabel)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
			];

			auto EmitPropOnly = [&](UObject* Target, const FName Var) -> TSharedRef<SWidget>
			{
				FSinglePropertyParams Params;
				TSharedPtr<ISinglePropertyView> View = PropEd.CreateSingleProperty(Target, Var, Params);
				return View.IsValid()
					? StaticCastSharedRef<SWidget>(View.ToSharedRef())
					: SNew(STextBlock).Text(FText::FromString(Var.ToString()));
			};

			auto EmitPropWithDelete = [&](UObject* Target,
			                              const FName Var,
			                              const FName GroupName,
			                              const FName ClassName,
			                              const FName CompNameForRemoval) -> TSharedRef<SWidget>
			{
				return SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					EmitPropOnly(Target, Var)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton")
					.ContentPadding(FMargin(4.f, 2.f))
					.ToolTipText(FText::FromString(TEXT("Remove this variable from the list")))
					.OnClicked(this, &SPinVarPanel::OnRemovePinned, ClassName, Var, GroupName, CompNameForRemoval)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("X")))
						.ColorAndOpacity(FLinearColor::Red)
					]
				];
			};

			// Class defaults (BP then Native)
			if (B.BPVars.Num() || B.NativeVars.Num())
			{
				if (UClass* ClsForCDO = FindFirstObjectSafe<UClass>(*B.ClassName.ToString()))
				{
					if (UObject* CDO = ClsForCDO->GetDefaultObject(true))
					{
						for (const FName& Var : B.BPVars)
						{
							ClassVB->AddSlot().AutoHeight().Padding(16.f, 2.f)
							[ EmitPropWithDelete(CDO, Var, Group, B.ClassName, NAME_None) ];
						}
						for (const FName& Var : B.NativeVars)
						{
							ClassVB->AddSlot().AutoHeight().Padding(16.f, 2.f)
							[ EmitPropWithDelete(CDO, Var, Group, B.ClassName, NAME_None) ];
						}
					}
				}
			}

			// Components
			for (const FName& CompLabel : CompNames)
			{
				ClassVB->AddSlot().AutoHeight().Padding(10.f, 8.f, 6.f, 2.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("Component: %s"), *CompLabel.ToString())))
					.ColorAndOpacity(FLinearColor(0.8f,0.8f,0.8f,1))
				];

				UObject* Tmpl = B.ComponentTemplates.FindRef(CompLabel).Get();
				if (!Tmpl) continue;

				const FName CompNameForRemoval = Tmpl->GetFName(); // remove by template key

				for (const FName& Var : B.ComponentVarsByName[CompLabel])
				{
					ClassVB->AddSlot().AutoHeight().Padding(16.f, 2.f)
					[
						EmitPropWithDelete(Tmpl, Var, Group, B.ClassName, CompNameForRemoval)
					];
				}
			}

			// One entry per class for this group
			Grouped.FindOrAdd(Group).Add({ Group, ClassVB });
		}
	}
}

FReply SPinVarPanel::OnRemovePinned(FName ClassName, FName VarName, FName GroupName, FName CompName)
{
	if (GEditor)
	{
		if (UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>())
		{
			Subsystem->UnstagePinVariable(ClassName, VarName, GroupName, CompName);
			Refresh(); // rebuild UI
		}
	}
	return FReply::Handled();
}

FReply SPinVarPanel::OnAddBlueprintVariableClicked()
{
	if (TSharedPtr<SWindow> W = SelectBlueprintWindow.Pin()) { W->RequestDestroyWindow(); }
	if (TSharedPtr<SWindow> W = AddVariableWindow.Pin())     { W->RequestDestroyWindow(); }
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

	SelectBlueprintWindow = PickerWindow;
	FSlateApplication::Get().AddWindow(PickerWindow);
	return FReply::Handled();
}

void SPinVarPanel::OnBlueprintPicked(const FAssetData& AssetData)
{
	if (TSharedPtr<SWindow> W = AddVariableWindow.Pin())     { W->RequestDestroyWindow(); }
	if (!AssetData.IsValid())
		return;

	UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset());
	if (!BP)
		return;

	ShowAddDialog(BP);
}

void SPinVarPanel::ShowAddDialog(UBlueprint* BP)
{
	if (!BP) return;

	// Ensure a generated class exists
	if (!BP->GeneratedClass)
	{
		FKismetEditorUtilities::CompileBlueprint(BP);
	}
	UClass* TargetClass = BP->GeneratedClass ? BP->GeneratedClass : BP->SkeletonGeneratedClass;
	if (!TargetClass)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Blueprint has no generated class yet.")));
		return;
	}
	TSharedRef<FState> S = MakeShared<FState>();
	S->BP    = BP;
	S->Class = TargetClass;
	if (!GroupStr.IsEmpty())
	{
		S->GroupStr = GroupStr;
	}
	// Local BP variables
	
	TArray<FName> TmpVar;
	GatherLocalVars(BP, TmpVar);
	for (const FName& N : TmpVar) S->LocalVarOpts.Add(MakeShared<FString>(N.ToString()));
	if (S->LocalVarOpts.Num()) S->LocalVarSel = S->LocalVarOpts[0];
	

	// Parent C++ properties
	
	TArray<FName> TmpProp;
	GatherNativeProps(TargetClass, TmpProp);
	for (const FName& N : TmpProp) S->NativePropOpts.Add(MakeShared<FString>(N.ToString()));
	if (S->NativePropOpts.Num()) S->NativePropSel = S->NativePropOpts[0];
	

	BuildComponentOptions(BP, TargetClass, S->CompOpts);
	GetAllGroups(S);
	if (S->ExistingGroupOpts.Num())
	{
		S->ExistingGroupSel = S->ExistingGroupOpts[0];
	}
	if (S->CompOpts.Num())
	{
		for (const auto& Opt : S->CompOpts)
		{
			const FString LabelStr = Opt->Label.ToString();
			S->CompOptLabels.Add(MakeShared<FString>(LabelStr));
			S->LabelToCompOpt.Add(LabelStr, Opt);
		}
		S->CompSel = S->CompOpts[0];

		if (S->CompSel.IsValid())
		{
			UActorComponent* Template =
				S->CompSel->Template.IsValid()
					? S->CompSel->Template.Get()
					: Cast<UActorComponent>(FindComponentTemplate(S->Class, S->CompSel->TemplateName));

			if (Template)
			{
				TArray<FName> Props;
				GatherComponentPropsByTemplate(Template, Props);
				for (const FName& N : Props) S->CompPropOpts.Add(MakeShared<FString>(N.ToString()));
				if (S->CompPropOpts.Num()) S->CompPropSel = S->CompPropOpts[0];
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("PinVar: (init) Template not found for '%s' (key '%s') on %s"),
					*S->CompSel->Label.ToString(), *S->CompSel->TemplateName.ToString(), *S->Class->GetName());
			}
		}
	}

	// --- Build dialog UI ---

	TSharedRef<SWindow> Dialog = SNew(SWindow)
		.Title(FText::FromString(TEXT("Add Variable to Group")))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.ClientSize(FVector2D(520, 380));

	AddVariableWindow = Dialog;

	// Helper for searchable combos over FString arrays
	auto MakeStringCombo = [](TArray<TSharedPtr<FString>>& Source, TSharedPtr<FString>& Sel, TFunction<void(TSharedPtr<FString>)> OnSel)
	{
		return SNew(SSearchableComboBox)
			.OptionsSource(&Source)
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> It)
			{
				return SNew(STextBlock).Text(FText::FromString(It.IsValid() ? *It : TEXT("None")));
			})
			.OnSelectionChanged_Lambda([&Sel, OnSel](TSharedPtr<FString> NewSel, ESelectInfo::Type)
			{
				Sel = NewSel;
				if (OnSel) OnSel(NewSel);
			})
			.InitiallySelectedItem(Sel)
			[
				SNew(STextBlock)
				.Text_Lambda([&Sel]()
				{
					return Sel.IsValid() ? FText::FromString(*Sel) : FText::FromString(TEXT("None"));
				})
			];
	};

	Dialog->SetContent(
		SNew(SVerticalBox)

		// Source segment
		+ SVerticalBox::Slot().AutoHeight().Padding(12,12,12,6)
		[
			SNew(SSegmentedControl<int32>)
			.Value_Lambda([S](){ return static_cast<int>(S->SourceType); })
			.OnValueChanged_Lambda([S](int32 NewIdx){ S->SourceType = static_cast<FState::ESourceType>(NewIdx); })
			+ SSegmentedControl<int32>::Slot(0).Text(FText::FromString("Blueprint local"))
			+ SSegmentedControl<int32>::Slot(1).Text(FText::FromString("Parent C++"))
			+ SSegmentedControl<int32>::Slot(2).Text(FText::FromString("Component"))
		]
		// Local BP
		+ SVerticalBox::Slot().AutoHeight().Padding(12,6,12,4)
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([S](){ return S->SourceType == FState::ESourceType::LocalBPVar ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Text(FText::FromString("Variable:")) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,2,0,0)
			[
				MakeStringCombo(S->LocalVarOpts, S->LocalVarSel, nullptr)
			]
		]
		
		// Parent C++
		+ SVerticalBox::Slot().AutoHeight().Padding(12,6,12,4)
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([S](){ return S->SourceType==FState::ESourceType::LocalCppVar ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Text(FText::FromString("Property:")) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,2,0,0)
			[
				MakeStringCombo(S->NativePropOpts, S->NativePropSel, nullptr)
			]
		]
		
		// Component
		+ SVerticalBox::Slot().AutoHeight().Padding(12,6,12,4)
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([S](){ return S->SourceType==FState::ESourceType::ComponentVar ? EVisibility::Visible : EVisibility::Collapsed; })
		
			+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Text(FText::FromString("Component:")) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,2,0,4)
			[
				SNew(SSearchableComboBox)
				.OptionsSource(&S->CompOptLabels)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> It)
				{
					return SNew(STextBlock).Text(FText::FromString(It.IsValid() ? *It : TEXT("None")));
				})
				.OnSelectionChanged_Lambda([S,this](TSharedPtr<FString> NewSel, ESelectInfo::Type)
				{
					S->CompSel.Reset();
					S->CompPropOpts.Reset();
					S->CompPropSel.Reset();
		
					if (!S->Class || !NewSel.IsValid()) { if (S->CompPropCombo.IsValid()) S->CompPropCombo->RefreshOptions(); return; }
		
					// Resolve label -> option
					if (TSharedPtr<FCompOption>* FoundPtr = S->LabelToCompOpt.Find(*NewSel))
					{
						S->CompSel = *FoundPtr;
		
						UActorComponent* Tmpl =
							S->CompSel->Template.IsValid()
								? S->CompSel->Template.Get()
								: Cast<UActorComponent>(FindComponentTemplate(S->Class, S->CompSel->TemplateName));
		
						if (Tmpl)
						{
							TArray<FName> P;
							GatherComponentPropsByTemplate(Tmpl, P);
							for (const FName& N : P) S->CompPropOpts.Add(MakeShared<FString>(N.ToString()));
							if (S->CompPropOpts.Num()) S->CompPropSel = S->CompPropOpts[0];
						}
						else
						{
							UE_LOG(LogTemp, Warning, TEXT("PinVar: Template not found for '%s' (key '%s') on %s"),
								*S->CompSel->Label.ToString(), *S->CompSel->TemplateName.ToString(), *S->Class->GetName());
						}
					}
		
					if (S->CompPropCombo.IsValid())
					{
						S->CompPropCombo->RefreshOptions();
						S->CompPropCombo->SetSelectedItem(S->CompPropSel);
					}
				})
				[
					SNew(STextBlock)
					.Text_Lambda([S]()
					{
						return S->CompSel.IsValid()
							? FText::FromName(S->CompSel->Label)
							: FText::FromString(TEXT("None"));
					})
				]
			]
		
			+ SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
			[
				SNew(STextBlock).Text(FText::FromString("Property:"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,2,0,0)
			[
				SAssignNew(S->CompPropCombo, SSearchableComboBox)
				.OptionsSource(&S->CompPropOpts)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> It)
				{
					return SNew(STextBlock).Text(FText::FromString(It.IsValid()? *It : TEXT("None")));
				})
				.OnSelectionChanged_Lambda([S](TSharedPtr<FString> NewSel, ESelectInfo::Type)
				{
					S->CompPropSel = NewSel;
				})
				.InitiallySelectedItem(S->CompPropSel)
				[
					SNew(STextBlock)
					.Text_Lambda([S]()
					{
						return S->CompPropSel.IsValid()? FText::FromString(*S->CompPropSel) : FText::FromString(TEXT("None"));
					})
				]
			]
		]

		// Group
		+ SVerticalBox::Slot().AutoHeight().Padding(12,10,12,8)
		[
			SNew(STextBlock).Text(FText::FromString("Group Name (A or A,B):"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(12,0,12,12)
		[
			SNew(SEditableTextBox)
			.Text_Lambda([S]() { return FText::FromString(S->GroupStr); })
			.OnTextChanged_Lambda([S](const FText& T){ S->GroupStr = T.ToString(); })
		]


		// Buttons
		+ SVerticalBox::Slot().AutoHeight().Padding(12,0,12,12)
[
    SNew(SHorizontalBox)

    // LEFT: pick an existing group
    + SHorizontalBox::Slot()
    .AutoWidth()
    .VAlign(VAlign_Center)
    [
        SNew(SSearchableComboBox)
        .OptionsSource(&S->ExistingGroupOpts)
        .OnGenerateWidget_Lambda([](TSharedPtr<FString> It)
        {
            return SNew(STextBlock).Text(FText::FromString(It.IsValid() ? *It : TEXT("None")));
        })
        .OnSelectionChanged_Lambda([S](TSharedPtr<FString> NewSel, ESelectInfo::Type)
        {
            S->ExistingGroupSel = NewSel;
        })
        .InitiallySelectedItem(S->ExistingGroupSel)
        [
            SNew(STextBlock)
            .Text_Lambda([S]()
            {
                return S->ExistingGroupSel.IsValid()
                    ? FText::FromString(*S->ExistingGroupSel)
                    : FText::FromString(TEXT("Select group…"));
            })
        ]
    ]

    // LEFT: "Add to existing group" button
    + SHorizontalBox::Slot()
    .AutoWidth()
    .Padding(8,0,0,0)
    .VAlign(VAlign_Center)
    [
        SNew(SButton)
        .IsEnabled_Lambda([S](){ return S->ExistingGroupSel.IsValid() && !S->ExistingGroupSel->IsEmpty(); })
        .Text(FText::FromString("Add to existing group"))
        .OnClicked_Lambda([this, S]()
        {
            if (!GEditor || !S->Class || !S->ExistingGroupSel.IsValid() || S->ExistingGroupSel->IsEmpty())
                return FReply::Handled();

            const FName GroupName = FName(**S->ExistingGroupSel);
            FName VarName = NAME_None;

            // Common helper to stage non-component vars
            auto StageSimple = [&](const FName& InVar)
            {
                if (UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>())
                {
                    Subsystem->StagePinVariable(S->Class->GetFName(), InVar, GroupName, NAME_None);
                    Subsystem->SaveToDisk();
                    Subsystem->MergeStagedIntoPinned();
                }
            };

            switch (S->SourceType)
            {
                case FState::ESourceType::LocalBPVar: // Blueprint local
                    if (S->LocalVarSel.IsValid()) VarName = FName(**S->LocalVarSel);
                    if (!VarName.IsNone()) StageSimple(VarName);
                    break;

                case FState::ESourceType::LocalCppVar: // Parent C++
                    if (S->NativePropSel.IsValid()) VarName = FName(**S->NativePropSel);
                    if (!VarName.IsNone()) StageSimple(VarName);
                    break;

                case FState::ESourceType::ComponentVar: // Component
                    if (S->CompSel.IsValid() && S->CompPropSel.IsValid())
                    {
                        UActorComponent* Tmpl =
                            S->CompSel->Template.IsValid()
                                ? S->CompSel->Template.Get()
                                : Cast<UActorComponent>(FindComponentTemplate(S->Class, S->CompSel->TemplateName));

                        if (Tmpl)
                        {
                            const FName TemplateKey = Tmpl->GetFName();
                            const FName PrettyVar   = S->CompSel->Label;
                            VarName = FName(**S->CompPropSel);

                            if (UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>())
                            {
                                Subsystem->StagePinVariableWithTemplate(
                                    S->Class->GetFName(), VarName, GroupName, TemplateKey, Tmpl, PrettyVar);
                                Subsystem->SaveToDisk();
                                Subsystem->MergeStagedIntoPinned();
                            }
                        }
                    }
                    break;
            }

            Refresh(); // keep dialog open
            return FReply::Handled();
        })
    ]

    // SPACER
    + SHorizontalBox::Slot().FillWidth(1.f)

    // RIGHT: "Add" (from text box) and "Cancel"
    + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
    [
        SNew(SButton)
        .ButtonStyle(FAppStyle::Get(), "PrimaryButton")
        .Text(FText::FromString("Add"))
        .OnClicked_Lambda([this, S]()
        {
            if (!GEditor || !S->Class) return FReply::Handled();

            // Accept empty group -> "Default"
            FString GroupCsv = S->GroupStr.TrimStartAndEnd();
            if (GroupCsv.IsEmpty()) { GroupCsv = TEXT("Default"); }

            FName VarName = NAME_None;
            FName CompName = NAME_None;

            switch (S->SourceType)
            {
                case FState::ESourceType::LocalBPVar: if (S->LocalVarSel.IsValid())  VarName = FName(**S->LocalVarSel);  break;
                case FState::ESourceType::LocalCppVar: if (S->NativePropSel.IsValid()) VarName = FName(**S->NativePropSel); break;
                case FState::ESourceType::ComponentVar:
                {
                    if (S->CompSel.IsValid() && S->CompPropSel.IsValid())
                    {
                        UActorComponent* Tmpl =
                            S->CompSel->Template.IsValid()
                                ? S->CompSel->Template.Get()
                                : Cast<UActorComponent>(FindComponentTemplate(S->Class, S->CompSel->TemplateName));

                        const FName TemplateKey = Tmpl ? Tmpl->GetFName() : S->CompSel->TemplateName;
                        const FName PrettyVar   = S->CompSel->Label;
                        VarName = FName(**S->CompPropSel);

                        if (UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>())
                        {
                            TArray<FString> Groups;
                            GroupCsv.ParseIntoArray(Groups, TEXT(","), true);
                            for (FString& G : Groups)
                            {
                                G = G.TrimStartAndEnd();
                                if (G.IsEmpty()) continue;
                                Subsystem->StagePinVariableWithTemplate(
                                    S->Class->GetFName(), VarName, FName(*G), TemplateKey, Tmpl, PrettyVar);
                                GetAllGroups(S);
                            }
                            Subsystem->SaveToDisk();
                            Subsystem->MergeStagedIntoPinned();
                        }
                        Refresh();
                        return FReply::Handled();
                    }
                }
                break;
            }

            if (VarName.IsNone()) { UE_LOG(LogTemp, Warning, TEXT("PinVar: Add aborted — no variable selected.")); return FReply::Handled(); }

            if (UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>())
            {
                TArray<FString> Groups;
                GroupCsv.ParseIntoArray(Groups, TEXT(","), true);
                for (FString& G : Groups)
                {
                    G = G.TrimStartAndEnd();
                    if (!G.IsEmpty())
                    {
                        Subsystem->StagePinVariable(S->Class->GetFName(), VarName, FName(*G), CompName);
                        GetAllGroups(S);
                    }
                }
                Subsystem->SaveToDisk();
            }
            Refresh();
            return FReply::Handled();
        })
    ]
    + SHorizontalBox::Slot().AutoWidth()
    [
        SNew(SButton)
        .Text(FText::FromString("Cancel"))
        .OnClicked_Lambda([Dialog](){ Dialog->RequestDestroyWindow(); return FReply::Handled(); })
    ]
]
	);
	FSlateApplication::Get().AddWindow(Dialog);
	GroupStr = S->GroupStr;
}

void SPinVarPanel::GatherLocalVars(UBlueprint* BP, TArray<FName>& OutVars) const
{
	OutVars.Reset();
	if (!BP) return;
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		if (!Var.VarName.IsNone())
		{
			OutVars.Add(Var.VarName);
		}
	}
	OutVars.Sort(FNameLexicalLess());
}

void SPinVarPanel::GatherNativeProps(UClass* Class, TArray<FName>& OutProps) const
{
	OutProps.Reset();
	if (!Class) return;

	for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* P = *It;
		if (!IsEditableProperty(P)) continue;
		
		UStruct* OwnerStruct = P->GetOwnerStruct();
		UClass*  OwnerClass  = Cast<UClass>(OwnerStruct);
		
		const bool bDeclaredOnNative = (OwnerClass && OwnerClass->ClassGeneratedBy == nullptr);
		const bool bDeclaredOnThisBP = (OwnerClass == Class);

		if (bDeclaredOnNative && !bDeclaredOnThisBP)
		{
			OutProps.Add(P->GetFName());
		}
	}

	OutProps.Sort(FNameLexicalLess());
}

void SPinVarPanel::GatherComponentPropsByTemplate(UObject* CompTemplate, TArray<FName>& OutProps) const
{
	OutProps.Reset();
	if (!CompTemplate) return;

	for (TFieldIterator<FProperty> It(CompTemplate->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* P = *It;
		if (IsEditableProperty(P))
		{
			OutProps.Add(P->GetFName());
		}
	}
	OutProps.Sort(FNameLexicalLess());
}
