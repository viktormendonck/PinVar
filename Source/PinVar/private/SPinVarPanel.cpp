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
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/StructOnScope.h"
#include "Engine/Blueprint.h"

// Simple structs that should stay SinglePropertyView (no dropdown header)
bool SPinVarPanel::IsSimpleStruct(const UScriptStruct* SS)
{
	return SS == TBaseStructure<FVector>::Get()
		|| SS == TBaseStructure<FVector2D>::Get()
		|| SS == TBaseStructure<FVector4>::Get()
		|| SS == TBaseStructure<FRotator>::Get()
		|| SS == TBaseStructure<FQuat>::Get()
		|| SS == TBaseStructure<FLinearColor>::Get()
		|| SS == TBaseStructure<FColor>::Get();
}


UClass* SPinVarPanel::ResolveGeneratedClassByShortName()
{
	const FString InName = ClassName.ToString();
	const FString WantedGenName = InName.EndsWith(TEXT("_C")) ? InName : (InName + TEXT("_C"));

	// Already loaded?
	if (UClass* C = FindFirstObjectSafe<UClass>(*WantedGenName))
	{
		return C;
	}

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	FARFilter Filter;
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());

	TArray<FAssetData> Assets;
	ARM.Get().GetAssets(Filter, Assets);

	for (const FAssetData& AD : Assets)
	{
		// UE5 tag: GeneratedClassPath (string form works across versions)
		FString PathStr;
		if (AD.GetTagValue(FBlueprintTags::GeneratedClassPath, PathStr) && !PathStr.IsEmpty())
		{
			if (FPackageName::ObjectPathToObjectName(PathStr) == WantedGenName)
			{
				UClass* C = FindObject<UClass>(nullptr, *PathStr);
				if (!C) { C = LoadObject<UClass>(nullptr, *PathStr); }
				if (C) { return C; }
			}
		}
		// Fallback: load BP and use its GeneratedClass
		if (UObject* Obj = AD.GetAsset())
		{
			if (UBlueprint* BP = Cast<UBlueprint>(Obj))
			{
				if (BP->GeneratedClass && BP->GeneratedClass->GetName() == WantedGenName)
				{
					return BP->GeneratedClass;
				}
			}
		}
	}

	return nullptr;
}

bool SPinVarPanel::IsComplexStructContainer(const FProperty* P)
{
	if (!P) return false;

	if (const FArrayProperty* AP = CastField<FArrayProperty>(P))
	{
		if (const FStructProperty* SP = CastField<FStructProperty>(AP->Inner))
		{
			return SP->Struct && !SP->Struct->IsChildOf(TBaseStructure<FVector>::Get())
				&& !SP->Struct->IsChildOf(TBaseStructure<FVector2D>::Get())
				&& !SP->Struct->IsChildOf(TBaseStructure<FRotator>::Get())
				&& !SP->Struct->IsChildOf(TBaseStructure<FQuat>::Get())
				&& !SP->Struct->IsChildOf(TBaseStructure<FLinearColor>::Get())
				&& !SP->Struct->IsChildOf(TBaseStructure<FColor>::Get());
		}
		return false;
	}

	if (const FSetProperty* SetP = CastField<FSetProperty>(P))
	{
		if (const FStructProperty* SP = CastField<FStructProperty>(SetP->ElementProp))
		{
			return SP->Struct && !IsSimpleStruct(SP->Struct);
		}
		return false;
	}

	if (const FMapProperty* MP = CastField<FMapProperty>(P))
	{
		const FStructProperty* KeySP = CastField<FStructProperty>(MP->KeyProp);
		const FStructProperty* ValSP = CastField<FStructProperty>(MP->ValueProp);
		const bool bKeyComplex = KeySP && KeySP->Struct && !IsSimpleStruct(KeySP->Struct);
		const bool bValComplex = ValSP && ValSP->Struct && !IsSimpleStruct(ValSP->Struct);
		return bKeyComplex || bValComplex;
	}

	return false;
}

bool SPinVarPanel::IsContainerProperty(const FProperty* P)
{
	return P
		&& (P->IsA(FArrayProperty::StaticClass())
			|| P->IsA(FMapProperty::StaticClass())
			|| P->IsA(FSetProperty::StaticClass()));
}

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
			if (UObject* T2 = CDO->GetDefaultSubobjectByName(Alt)) return T2;

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

void SPinVarPanel::BuildComponentOptions(UBlueprint* BP, UClass* Class, TArray<TSharedPtr<FCompOption>>& Out)
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
				Opt->Label = TmplName;
				Opt->TemplateName = TmplName;
				Opt->Template = Comp;
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
			FName TemplateKey = ActualTemplate
				                    ? ActualTemplate->GetFName()
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
				Opt->Label = Pretty;
				Opt->TemplateName = TemplateKey;
				Opt->Template = ActualTemplate; // may be null (rare)
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
			.HeaderContent()
			[
				SNew(STextBlock).Text(FText::FromName(Group))
			]
			.BodyContent()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SSeparator)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					ListVB
				]
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

// SPinVarPanel.cpp
bool SPinVarPanel::IsEditableProperty(const FProperty* P)
{
	if (!P) return false;

	const bool bHasEdit = P->HasAnyPropertyFlags(CPF_Edit);
	const bool bReadOnlyInEditor = P->HasAnyPropertyFlags(CPF_EditConst);
	const bool bHiddenOnTemplates = P->HasAnyPropertyFlags(CPF_DisableEditOnTemplate);
	const bool bTransient = P->HasAnyPropertyFlags(CPF_Transient);
	const bool bIsDelegate =
		P->IsA(FMulticastDelegateProperty::StaticClass()) ||
		P->IsA(FDelegateProperty::StaticClass());

	const bool bIsStructWrapper = P->IsA(FStructProperty::StaticClass());

	// Allow struct wrappers even if DisableEditOnTemplate, but still block read‑only/transient/delegates.
	if (bIsStructWrapper)
	{
		return bHasEdit && !bReadOnlyInEditor && !bTransient && !bIsDelegate;
	}

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
			for (const auto& Pair : Subsystem->PinnedGroups) // Map<FName ClassName, TArray<FPinnedVariable>>
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

	// ---------- buckets per class ----------
	struct FClassBuckets
	{
		FName ClassName;
		FText ClassLabel;

		TArray<FName> BPVars;
		TArray<FName> NativeVars;

		TMap<FName, TArray<FName>> ComponentVarsByName; // PrettyLabel -> [Var]
		TMap<FName, TWeakObjectPtr<UObject>> ComponentTemplates; // PrettyLabel -> Template

		TMap<FName, TArray<FName>> AssetVarsByName; // AssetName -> [Var]
		TMap<FName, TWeakObjectPtr<UObject>> AssetsByName; // AssetName -> UObject
	};

	// ---------- tree for subcategories ----------
	struct FGroupNode
	{
		FName Segment; // e.g. "Combat"
		TMap<FName, TSharedPtr<FGroupNode>> Children;
	};

	// leaf full path -> (class -> buckets)
	TMap<FString, TMap<FName, FClassBuckets>> BuildByPath;
	// top-level segment -> node
	TMap<FName, TSharedPtr<FGroupNode>> Roots;

	// ---------- collect ----------
	for (const TPair<FName, TArray<FPinnedVariable>>& Pair : Subsystem->PinnedGroups)
	{
		ClassName = Pair.Key;
		UClass* Cls = FindFirstObjectSafe<UClass>(*ClassName.ToString());
		if (!Cls) Cls = ResolveGeneratedClassByShortName();
		if (!Cls || IsSkelOrReinst(Cls)) continue;

		UObject* CDO = Cls->GetDefaultObject(true);
		if (!CDO) continue;

		const FName ClassFName = Cls->GetFName();
		const FText ClassLabel = FText::FromString(PrettyBlueprintDisplayName(Cls));

		for (const FPinnedVariable& Pinned : Pair.Value)
		{
			UObject* Target = CDO;

			// asset target
			if (!Pinned.AssetPath.IsNull())
			{
				Target = Pinned.AssetPath.ResolveObject();
				if (!Target) Target = Pinned.AssetPath.TryLoad();
				if (!Target) continue;
			}

			// component target
			if (!Pinned.ComponentTemplateName.IsNone())
			{
				Target = Pinned.ResolvedTemplate.IsValid()
					         ? Pinned.ResolvedTemplate.Get()
					         : FindComponentTemplate(Cls, Pinned.ComponentTemplateName);
				if (!Target) continue;
			}

			FProperty* Found = FindFProperty<FProperty>(Target->GetClass(), Pinned.VariableName);
			if (!Found || !IsEditableProperty(Found)) continue;

			TArray<FString> Tokens;
			const FString GroupCsv = Pinned.GroupName.ToString();
			GroupCsv.ParseIntoArray(Tokens, TEXT(","), /*CullEmpty*/ true);
			if (Tokens.Num() == 0) Tokens.Add(GroupCsv);

			for (FString Tok : Tokens)
			{
				Tok = Tok.TrimStartAndEnd();
				if (Tok.IsEmpty()) continue;

				// split by '|' for subcategories
				TArray<FString> Segs;
				Tok.ParseIntoArray(Segs, TEXT("|"), true);
				for (FString& S : Segs) S = S.TrimStartAndEnd();
				if (Segs.Num() == 0) continue;

				const FString FullPath = FString::Join(Segs, TEXT("|"));
				TMap<FName, FClassBuckets>& ClassMap = BuildByPath.FindOrAdd(FullPath);
				FClassBuckets& B = ClassMap.FindOrAdd(ClassFName);
				B.ClassName = ClassFName;
				B.ClassLabel = ClassLabel;

				if (!Pinned.AssetPath.IsNull())
				{
					const FName AssetLabel(*Target->GetName());
					B.AssetVarsByName.FindOrAdd(AssetLabel).Add(Pinned.VariableName);
					B.AssetsByName.FindOrAdd(AssetLabel) = Target;
				}
				else if (Pinned.ComponentTemplateName.IsNone())
				{
					if (IsBPDeclared(Found)) B.BPVars.Add(Pinned.VariableName);
					else if (IsNativeDeclared(Found)) B.NativeVars.Add(Pinned.VariableName);
					else B.BPVars.Add(Pinned.VariableName);
				}
				else
				{
					const FName CompLabel = !Pinned.ComponentVariablePrettyName.IsNone()
						                        ? Pinned.ComponentVariablePrettyName
						                        : Pinned.ComponentTemplateName;

					B.ComponentVarsByName.FindOrAdd(CompLabel).Add(Pinned.VariableName);
					B.ComponentTemplates.FindOrAdd(CompLabel) = Target;
				}

				// build tree path
				const FName RootSeg(*Segs[0]);
				TSharedPtr<FGroupNode>& Root = Roots.FindOrAdd(RootSeg);
				if (!Root)
				{
					Root = MakeShared<FGroupNode>();
					Root->Segment = RootSeg;
				}

				TSharedPtr<FGroupNode> Cursor = Root;
				for (int32 i = 1; i < Segs.Num(); ++i)
				{
					const FName Seg(*Segs[i]);
					TSharedPtr<FGroupNode>& Child = Cursor->Children.FindOrAdd(Seg);
					if (!Child)
					{
						Child = MakeShared<FGroupNode>();
						Child->Segment = Seg;
					}
					Cursor = Child;
				}
			}
		}
	}

	// helper: make class sections for a leaf path
	auto MakeClassSectionsForPath = [&](const FString& FullPath) -> TSharedRef<SVerticalBox>
	{
		TSharedRef<SVerticalBox> VB = SNew(SVerticalBox);
		TMap<FName, FClassBuckets>* ClassesPtr = BuildByPath.Find(FullPath);
		if (!ClassesPtr) return VB;

		TArray<FName> ClassOrder;
		ClassesPtr->GenerateKeyArray(ClassOrder);
		ClassOrder.Sort([&](const FName& A, const FName& B)
		{
			return (*ClassesPtr)[A].ClassLabel.ToString().Compare((*ClassesPtr)[B].ClassLabel.ToString(),
			                                                      ESearchCase::IgnoreCase) < 0;
		});

		for (const FName& CN : ClassOrder)
		{
			FClassBuckets& B = (*ClassesPtr)[CN];
			B.BPVars.Sort(FNameLexicalLess());
			B.NativeVars.Sort(FNameLexicalLess());
			for (auto& It : B.ComponentVarsByName) { It.Value.Sort(FNameLexicalLess()); }

			TArray<FName> CompNames;
			B.ComponentVarsByName.GenerateKeyArray(CompNames);
			CompNames.Sort(FNameLexicalLess());
			TArray<FName> AssetNames;
			B.AssetVarsByName.GenerateKeyArray(AssetNames);
			AssetNames.Sort(FNameLexicalLess());

			VB->AddSlot().AutoHeight().Padding(6, 8, 6, 4)
			[
				SNew(STextBlock).Text(B.ClassLabel).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
			];

			// emitters
			auto EmitPropOnly = [&](UObject* Target, const FName Var)-> TSharedRef<SWidget>
			{
				FProperty* P = FindFProperty<FProperty>(Target->GetClass(), Var);

				// struct handling
				if (FStructProperty* SP = CastField<FStructProperty>(P))
				{
					UScriptStruct* SS = SP->Struct;
					if (SS && IsSimpleStruct(SS))
					{
						FSinglePropertyParams Params;
						TSharedPtr<ISinglePropertyView> View = PropEd.CreateSingleProperty(Target, Var, Params);
						TSharedRef<SWidget> Inner = View.IsValid()
							                            ? StaticCastSharedRef<SWidget>(View.ToSharedRef())
							                            : StaticCastSharedRef<SWidget>(
								                            SNew(STextBlock).Text(FText::FromName(Var)));
						return SNew(SBox).ToolTipText(
								FText::FromString(P ? P->GetCPPType(nullptr) : TEXT("Unknown Type")))
							[
								Inner
							];
					}
					if (SS)
					{
						void* ValuePtr = SP->ContainerPtrToValuePtr<void>(Target);
						if (ValuePtr)
						{
							TSharedRef<FStructOnScope> Scope = MakeShared<FStructOnScope>(
								SS, reinterpret_cast<uint8*>(ValuePtr));
							FDetailsViewArgs DArgs;
							DArgs.bAllowSearch = false;
							DArgs.bShowOptions = false;
							DArgs.bShowScrollBar = false;
							DArgs.bHideSelectionTip = true;
							DArgs.bShowObjectLabel = false;
							FStructureDetailsViewArgs SArgs;
							SArgs.bShowObjects = false;
							SArgs.bShowAssets = false;
							TSharedRef<IStructureDetailsView> SDV = PropEd.CreateStructureDetailView(
								DArgs, SArgs, nullptr);
							SDV->SetStructureData(Scope);
							return SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(STextBlock).Text(FText::FromName(Var))
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SDV->GetWidget().ToSharedRef()
								];
						}
					}
				}

				// containers
				if (IsContainerProperty(P))
				{
					FDetailsViewArgs DArgs;
					DArgs.bAllowSearch = false;
					DArgs.bShowOptions = false;
					DArgs.bShowScrollBar = false;
					DArgs.bHideSelectionTip = true;
					DArgs.bShowObjectLabel = false;
					DArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
					TSharedRef<IDetailsView> DV = PropEd.CreateDetailView(DArgs);
					DV->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda(
						[Var](const FPropertyAndParent& In)
						{
							if (In.Property.GetFName() == Var) return true;
							for (const FProperty* Parent : In.ParentProperties)
							{
								if (Parent && Parent->GetFName() == Var) return true;
							}
							return false;
						}));
					if (!IsComplexStructContainer(P))
					{
						const FString VarStr = Var.ToString();
						DV->SetIsCustomRowVisibleDelegate(FIsCustomRowVisible::CreateLambda(
							[VarStr](FName, FName PropName)
							{
								if (PropName.IsNone()) return true;
								const FString N = PropName.ToString();
								return N == VarStr || N.StartsWith(VarStr + TEXT(".")) || N.StartsWith(
									VarStr + TEXT("_"));
							}));
					}
					DV->SetObject(Target);
					return SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(FText::FromName(Var))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							DV
						];
				}

				// simple single row
				FSinglePropertyParams Params;
				TSharedPtr<ISinglePropertyView> View = PropEd.CreateSingleProperty(Target, Var, Params);
				TSharedRef<SWidget> Inner = View.IsValid()
					                            ? StaticCastSharedRef<SWidget>(View.ToSharedRef())
					                            : StaticCastSharedRef<SWidget>(
						                            SNew(STextBlock).Text(FText::FromName(Var)));
				return SNew(SBox).ToolTipText(FText::FromString(P ? P->GetCPPType(nullptr) : TEXT("Unknown Type")))
					[
						Inner
					];
			};

			auto EmitPropWithDelete = [&](UObject* Target, const FName Var, const FName ClassName,
			                              const FName CompNameForRemoval)-> TSharedRef<SWidget>
			{
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						EmitPropOnly(Target, Var)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(6, 2, 0, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton")
						.ContentPadding(FMargin(4, 2))
						.ToolTipText(FText::FromString(TEXT("Remove this variable from the list")))
						.OnClicked(this, &SPinVarPanel::OnRemovePinned, ClassName, Var, FName(*FullPath),
						           CompNameForRemoval)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("X"))).ColorAndOpacity(FLinearColor::Red)
						]
					];
			};

			// class defaults
			if (UClass* C = FindFirstObjectSafe<UClass>(*B.ClassName.ToString()))
			{
				if (UObject* CDO = C->GetDefaultObject(true))
				{
					for (const FName& V : B.BPVars)
						VB->AddSlot().AutoHeight().Padding(16, 2)[EmitPropWithDelete(
							CDO, V, B.ClassName, NAME_None)];
					for (const FName& V : B.NativeVars)
						VB->AddSlot().AutoHeight().Padding(16, 2)[EmitPropWithDelete(
							CDO, V, B.ClassName, NAME_None)];
				}
			}

			// components
			{
				TArray<FName> CompLabels;
				B.ComponentVarsByName.GenerateKeyArray(CompLabels);
				CompLabels.Sort(FNameLexicalLess());
				for (const FName& CompLabel : CompLabels)
				{
					VB->AddSlot().AutoHeight().Padding(10, 8, 6, 2)
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("Component: %s"), *CompLabel.ToString())))
						.ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.8f, 1))
					];
					UObject* Tmpl = B.ComponentTemplates.FindRef(CompLabel).Get();
					if (!Tmpl) continue;
					const FName CompNameForRemoval = Tmpl->GetFName();
					for (const FName& V : B.ComponentVarsByName[CompLabel])
					{
						VB->AddSlot().AutoHeight().Padding(16, 2)[EmitPropWithDelete(
							Tmpl, V, B.ClassName, CompNameForRemoval)];
					}
				}
			}

			// assets
			{
				TArray<FName> AssetLabels;
				B.AssetVarsByName.GenerateKeyArray(AssetLabels);
				AssetLabels.Sort(FNameLexicalLess());

				for (const FName& AName : AssetLabels)
				{
					UObject* Obj = B.AssetsByName.FindRef(AName).Get();
					const FString ParentClass = Obj ? Obj->GetClass()->GetName() : FString();

					// Two-line header: AssetName (big) + ParentClass (small, grey)
					VB->AddSlot().AutoHeight().Padding(10, 8, 6, 2)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromName(AName))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(ParentClass))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f))
						]
					];

					if (Obj)
					{
						for (const FName& V : B.AssetVarsByName[AName])
						{
							VB->AddSlot().AutoHeight().Padding(16, 2)
							[
								EmitPropWithDelete(Obj, V, B.ClassName, NAME_None)
							];
						}
					}
				}
			}
		}

		return VB;
	};

	// small helper to register expand state per path
	auto RegisterArea = [&](const FString& FullPath, const TSharedRef<SExpandableArea>& Area)
	{
		const FName Key(*FullPath);
		if (const bool* Remembered = GroupExpandedState.Find(Key)) { Area->SetExpanded(*Remembered); }
		GroupAreaWidgets.Add(Key, Area);
	};

	// recursive builder
	TFunction<TSharedRef<SWidget>(const FString&, const TSharedPtr<FGroupNode>&)> BuildNode =
		[&](const FString& ParentPath, const TSharedPtr<FGroupNode>& Node)-> TSharedRef<SWidget>
	{
		const FString FullPath = ParentPath.IsEmpty()
			                         ? Node->Segment.ToString()
			                         : ParentPath + TEXT("|") + Node->Segment.ToString();

		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

		// sections at this exact path
		Body->AddSlot().AutoHeight().Padding(6, 6)[MakeClassSectionsForPath(FullPath)];

		// children
		TArray<FName> ChildKeys;
		Node->Children.GenerateKeyArray(ChildKeys);
		ChildKeys.Sort(FNameLexicalLess());
		for (const FName& ChildSeg : ChildKeys)
		{
			const TSharedPtr<FGroupNode>& Child = Node->Children[ChildSeg];
			TSharedRef<SExpandableArea> ChildArea =
				SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.HeaderContent()
				[
					SNew(STextBlock).Text(FText::FromName(ChildSeg))
				]
				.BodyContent()
				[
					BuildNode(FullPath, Child)
				];

			RegisterArea(FullPath + TEXT("|") + ChildSeg.ToString(), ChildArea);
			Body->AddSlot().AutoHeight().Padding(12, 4, 0, 0)[ChildArea];
		}
		return Body;
	};

	// ---------- emit to Grouped (top-level only) ----------
	Grouped.Reset();

	TArray<FName> RootNames;
	Roots.GenerateKeyArray(RootNames);
	RootNames.Sort(FNameLexicalLess());
	for (const FName& RootSeg : RootNames)
	{
		const TSharedPtr<FGroupNode>& Root = Roots[RootSeg];
		if (!Root) continue;

		// composite body that Rebuild() will put inside the top-level area
		TSharedRef<SVerticalBox> RootBody = SNew(SVerticalBox);

		// sections pinned directly to the root
		RootBody->AddSlot().AutoHeight().Padding(6, 6)[MakeClassSectionsForPath(RootSeg.ToString())];

		// children areas
		TArray<FName> ChildKeys;
		Root->Children.GenerateKeyArray(ChildKeys);
		ChildKeys.Sort(FNameLexicalLess());
		for (const FName& ChildSeg : ChildKeys)
		{
			const TSharedPtr<FGroupNode>& Child = Root->Children[ChildSeg];
			TSharedRef<SExpandableArea> Area =
				SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.HeaderContent()
				[
					SNew(STextBlock).Text(FText::FromName(ChildSeg))
				]
				.BodyContent()
				[
					BuildNode(RootSeg.ToString(), Child)
				];

			RegisterArea(RootSeg.ToString() + TEXT("|") + ChildSeg.ToString(), Area);
			RootBody->AddSlot().AutoHeight().Padding(12, 4, 0, 0)[Area];
		}

		Grouped.FindOrAdd(RootSeg).Add({RootSeg, RootBody});
	}
}


void SPinVarPanel::OnAnyAssetPicked(const FAssetData& AssetData)
{
	if (TSharedPtr<SWindow> W = SelectBlueprintWindow.Pin())
	{
		W->RequestDestroyWindow();
	}

	if (!AssetData.IsValid()) return;

	UObject* Asset = AssetData.GetAsset();
	if (!Asset) return;

	// If it's a Blueprint, reuse your existing path.
	if (UBlueprint* BP = Cast<UBlueprint>(Asset))
	{
		OnBlueprintPicked(AssetData);
		return;
	}

	// Data Asset instance picked (Primary or plain)
	if (Asset->IsA(UPrimaryDataAsset::StaticClass()) || Asset->IsA(UDataAsset::StaticClass()))
	{
		ShowAddDialogForDataAsset(Asset); // <-- use the instance dialog
	}
}

FReply SPinVarPanel::OnRemovePinned(FName Class, FName VarName, FName GroupName, FName CompName)
{
	if (GEditor)
	{
		if (UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>())
		{
			Subsystem->UnstagePinVariable(Class, VarName, GroupName, CompName);
			Refresh(); // rebuild UI
		}
	}
	return FReply::Handled();
}

FReply SPinVarPanel::OnAddBlueprintVariableClicked()
{
	if (TSharedPtr<SWindow> W = SelectBlueprintWindow.Pin()) { W->RequestDestroyWindow(); }
	if (TSharedPtr<SWindow> W = AddVariableWindow.Pin()) { W->RequestDestroyWindow(); }
	FAssetPickerConfig PickerConfig;
	PickerConfig.Filter.bRecursiveClasses = true;
	PickerConfig.Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	PickerConfig.Filter.ClassPaths.Add(UPrimaryDataAsset::StaticClass()->GetClassPathName());
	PickerConfig.Filter.ClassPaths.Add(UDataAsset::StaticClass()->GetClassPathName());
	PickerConfig.SelectionMode = ESelectionMode::Single;
	PickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SPinVarPanel::OnAnyAssetPicked);

	TSharedRef<SWindow> PickerWindow = SNew(SWindow)
		.Title(FText::FromString("Select Asset"))
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
	if (TSharedPtr<SWindow> W = SelectBlueprintWindow.Pin()) { W->RequestDestroyWindow(); }
	if (TSharedPtr<SWindow> W2 = AddVariableWindow.Pin()) { W2->RequestDestroyWindow(); }

	if (!AssetData.IsValid()) return;
	if (UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset()))
	{
		ShowAddDialog(BP);
	}
}

void SPinVarPanel::ShowAddDialogForDataAsset(UObject* DataAssetInstance)
{
	if (!DataAssetInstance) return;

	UClass* TargetClass = DataAssetInstance->GetClass();

	TSharedRef<FState> S = MakeShared<FState>();
	S->BP = nullptr;
	S->Class = TargetClass;
	S->bIsDataAssetClass = true;
	S->DataAssetInstance = DataAssetInstance; // remember the instance we picked
	if (!GroupStr.IsEmpty()) { S->GroupStr = GroupStr; }

	// --- Gather Parent C++ props ---
	{
		TArray<FName> TmpProp;
		GatherNativeProps(TargetClass, TmpProp);
		for (const FName& N : TmpProp)
			S->NativePropOpts.Add(MakeShared<FString>(N.ToString()));
		if (S->NativePropOpts.Num())
			S->NativePropSel = S->NativePropOpts[0];
	}

	// --- If Blueprint DataAsset, gather its local BP variables ---
	if (UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(TargetClass))
	{
		if (UBlueprint* OwnerBP = Cast<UBlueprint>(BPGC->ClassGeneratedBy))
		{
			S->BP = OwnerBP;
			TArray<FName> TmpVar;
			GatherLocalVars(OwnerBP, TmpVar);
			for (const FName& N : TmpVar)
				S->LocalVarOpts.Add(MakeShared<FString>(N.ToString()));
			if (S->LocalVarOpts.Num())
				S->LocalVarSel = S->LocalVarOpts[0];
		}
	}

	// Prefer BP locals if present, otherwise parent C++
	S->SourceType = (S->BP && S->LocalVarOpts.Num() > 0)
		                ? FState::ESourceType::LocalBPVar
		                : FState::ESourceType::LocalCppVar;

	// Groups
	GetAllGroups(S);
	if (S->ExistingGroupOpts.Num())
		S->ExistingGroupSel = S->ExistingGroupOpts[0];

	TSharedRef<SWindow> Dialog = SNew(SWindow)
		.Title(FText::FromString(TEXT("Add Variable to Group")))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.ClientSize(FVector2D(520, 320));

	AddVariableWindow = Dialog;

	auto MakeStringCombo = [](TArray<TSharedPtr<FString>>& Source, TSharedPtr<FString>& Sel)
	{
		return SNew(SSearchableComboBox)
			.OptionsSource(&Source)
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> It)
			{
				return SNew(STextBlock).Text(FText::FromString(It.IsValid() ? *It : TEXT("None")));
			})
			.OnSelectionChanged_Lambda([&Sel](TSharedPtr<FString> NewSel, ESelectInfo::Type)
			{
				Sel = NewSel;
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

		// Source segment (BP local / Parent C++)
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 12, 12, 6)
		[
			SNew(SSegmentedControl<int32>)
			.Value_Lambda([S]()
			{
				return S->BP
					       ? static_cast<int32>(S->SourceType)
					       : static_cast<int32>(FState::ESourceType::LocalCppVar);
			})
			.OnValueChanged_Lambda([S](int32 NewIdx)
			{
				if (!S->BP && NewIdx == static_cast<int32>(FState::ESourceType::LocalBPVar))
					NewIdx = static_cast<int32>(FState::ESourceType::LocalCppVar);
				if (NewIdx > static_cast<int32>(FState::ESourceType::LocalCppVar))
					NewIdx = static_cast<int32>(FState::ESourceType::LocalCppVar);
				S->SourceType = static_cast<FState::ESourceType>(NewIdx);
			})
			+ SSegmentedControl<int32>::Slot(0).Text(FText::FromString("Blueprint local"))
			+ SSegmentedControl<int32>::Slot(1).Text(FText::FromString("Parent C++"))
		]

		// Local BP
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 6, 12, 4)
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([S]()
			{
				const bool bShow = (S->SourceType == FState::ESourceType::LocalBPVar) && (S->BP != nullptr);
				return bShow ? EVisibility::Visible : EVisibility::Collapsed;
			})
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString("Variable:"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
			[
				MakeStringCombo(S->LocalVarOpts, S->LocalVarSel)
			]
		]

		// Parent C++
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 6, 12, 4)
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([S]()
			{
				return (S->SourceType == FState::ESourceType::LocalCppVar)
					       ? EVisibility::Visible
					       : EVisibility::Collapsed;
			})
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString("Property:"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
			[
				MakeStringCombo(S->NativePropOpts, S->NativePropSel)
			]
		]

		// Group name
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 10, 12, 8)
		[
			SNew(STextBlock).Text(FText::FromString("Group Name (`|` for subcategories):"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 0, 12, 12)
		[
			SNew(SEditableTextBox)
			.Text_Lambda([S]() { return FText::FromString(S->GroupStr); })
			.OnTextChanged_Lambda([S](const FText& T) { S->GroupStr = T.ToString(); })
		]

		// Buttons row
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 0, 12, 12)
		[
			SNew(SHorizontalBox)

			// Existing group dropdown
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
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

			// Add to existing group
			+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0).VAlign(VAlign_Center)
			[
				SNew(SButton)
				.IsEnabled_Lambda([S]() { return S->ExistingGroupSel.IsValid() && !S->ExistingGroupSel->IsEmpty(); })
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				.Text(FText::FromString("Add to existing group"))
				.OnClicked_Lambda([this, S]()
				{
					if (!GEditor || !S->Class || !S->ExistingGroupSel.IsValid() || S->ExistingGroupSel->IsEmpty())
						return FReply::Handled();

					const FName GroupName = FName(**S->ExistingGroupSel);
					FName VarName = NAME_None;

					switch (S->SourceType)
					{
					case FState::ESourceType::LocalBPVar:
						if (S->LocalVarSel.IsValid()) VarName = FName(**S->LocalVarSel);
						break;
					case FState::ESourceType::LocalCppVar:
						if (S->NativePropSel.IsValid()) VarName = FName(**S->NativePropSel);
						break;
					default: break;
					}

					if (!VarName.IsNone() && S->DataAssetInstance.IsValid())
					{
						if (UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>())
						{
							Subsystem->StagePinVariableForDataAsset(
								S->Class->GetFName(),
								VarName,
								GroupName,
								S->DataAssetInstance.Get()
							);
							Subsystem->SaveToDisk();
							Subsystem->MergeStagedIntoPinned();
						}
					}

					Refresh();
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot().FillWidth(1.f)

			// Add from text box
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				.Text(FText::FromString("Add"))
				.OnClicked_Lambda([this, S]()
				{
					if (!GEditor || !S->Class) return FReply::Handled();

					FString GroupCsv = S->GroupStr.TrimStartAndEnd();
					if (GroupCsv.IsEmpty()) { GroupCsv = TEXT("Default"); }

					FName VarName = NAME_None;
					switch (S->SourceType)
					{
					case FState::ESourceType::LocalBPVar:
						if (S->LocalVarSel.IsValid()) VarName = FName(**S->LocalVarSel);
						break;
					case FState::ESourceType::LocalCppVar:
						if (S->NativePropSel.IsValid()) VarName = FName(**S->NativePropSel);
						break;
					default: break;
					}

					if (VarName.IsNone() || !S->DataAssetInstance.IsValid())
					{
						UE_LOG(LogTemp, Warning,
						       TEXT("PinVar: Add aborted — no variable selected or instance invalid."));
						return FReply::Handled();
					}

					if (UPinVarSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPinVarSubsystem>())
					{
						TArray<FString> Groups;
						GroupCsv.ParseIntoArray(Groups, TEXT(","), true);
						for (FString& G : Groups)
						{
							G = G.TrimStartAndEnd();
							if (!G.IsEmpty())
							{
								Subsystem->StagePinVariableForDataAsset(
									S->Class->GetFName(),
									VarName,
									FName(*G),
									S->DataAssetInstance.Get()
								);
								GetAllGroups(S);
							}
						}
						Subsystem->SaveToDisk();
						Subsystem->MergeStagedIntoPinned();
					}
					Refresh();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString("Cancel"))
				.OnClicked_Lambda([Dialog]()
				{
					Dialog->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
		]
	);

	FSlateApplication::Get().AddWindow(Dialog);
	GroupStr = S->GroupStr;
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
	S->BP = BP;
	S->Class = TargetClass;
	S->bIsDataAssetClass = TargetClass->IsChildOf(UPrimaryDataAsset::StaticClass());
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
	auto MakeStringCombo = [](TArray<TSharedPtr<FString>>& Source, TSharedPtr<FString>& Sel,
	                          TFunction<void(TSharedPtr<FString>)> OnSel)
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
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 12, 12, 6)
		[
			SNew(SSegmentedControl<int32>)
			.Value_Lambda([S]() { return static_cast<int>(S->SourceType); })
			.OnValueChanged_Lambda([S](int32 NewIdx) { S->SourceType = static_cast<FState::ESourceType>(NewIdx); })
			+ SSegmentedControl<int32>::Slot(0).Text(FText::FromString("Blueprint local"))
			+ SSegmentedControl<int32>::Slot(1).Text(FText::FromString("Parent C++"))
			+ SSegmentedControl<int32>::Slot(2).Text(FText::FromString("Component"))
		]
		// Local BP
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 6, 12, 4)
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([S]()
			{
				return S->SourceType == FState::ESourceType::LocalBPVar
					       ? EVisibility::Visible
					       : EVisibility::Collapsed;
			})
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString("Variable:"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
			[
				MakeStringCombo(S->LocalVarOpts, S->LocalVarSel, nullptr)
			]
		]

		// Parent C++
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 6, 12, 4)
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([S]()
			{
				return S->SourceType == FState::ESourceType::LocalCppVar
					       ? EVisibility::Visible
					       : EVisibility::Collapsed;
			})
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString("Property:"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
			[
				MakeStringCombo(S->NativePropOpts, S->NativePropSel, nullptr)
			]
		]

		// Component
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 6, 12, 4)
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([S]()
			{
				const bool bShow = (S->SourceType == FState::ESourceType::ComponentVar) && !S->bIsDataAssetClass;
				return bShow ? EVisibility::Visible : EVisibility::Collapsed;
			})


			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString("Component:"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 4)
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

					if (!S->Class || !NewSel.IsValid())
					{
						if (S->CompPropCombo.IsValid()) S->CompPropCombo->RefreshOptions();
						return;
					}

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
							       *S->CompSel->Label.ToString(), *S->CompSel->TemplateName.ToString(),
							       *S->Class->GetName());
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

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
			[
				SNew(STextBlock).Text(FText::FromString("Property:"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
			[
				SAssignNew(S->CompPropCombo, SSearchableComboBox)
				                                                 .OptionsSource(&S->CompPropOpts)
 // TArray<TSharedPtr<FString>>
				                                                 .OnGenerateWidget_Lambda([](TSharedPtr<FString> It)
				                                                 {
					                                                 return SNew(STextBlock).Text(
							                                                 FText::FromString(
								                                                 It.IsValid() ? *It : TEXT("None")));
				                                                 })
				                                                 .OnSelectionChanged_Lambda(
					                                                 [S](TSharedPtr<FString> NewSel,
					                                                     ESelectInfo::Type Info)
					                                                 {
						                                                 // If Enter was pressed with no explicit row highlighted, pick first filtered option
						                                                 if (!NewSel.IsValid() && S->CompPropOpts.Num()
							                                                 > 0)
						                                                 {
							                                                 NewSel = S->CompPropOpts[0];
							                                                 if (S->CompPropCombo.IsValid())
							                                                 {
								                                                 S->CompPropCombo->SetSelectedItem(
									                                                 NewSel);
							                                                 }
						                                                 }

						                                                 S->CompPropSel = NewSel;

						                                                 // Close the dropdown when selection comes from keyboard (Enter/Navigation)
						                                                 if (Info == ESelectInfo::OnKeyPress || Info ==
							                                                 ESelectInfo::OnNavigation)
						                                                 {
							                                                 if (S->CompPropCombo.IsValid())
							                                                 {
								                                                 S->CompPropCombo->SetIsOpen(false);
							                                                 }
						                                                 }
					                                                 })
				                                                 .InitiallySelectedItem(S->CompPropSel)
				[
					SNew(STextBlock)
					.Text_Lambda([S]()
					{
						return S->CompPropSel.IsValid()
							       ? FText::FromString(*S->CompPropSel)
							       : FText::FromString(TEXT("None"));
					})
				]
			]
		]

		// Group
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 10, 12, 8)
		[
			SNew(STextBlock).Text(FText::FromString("Group Name (`|` for subcategories):"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 0, 12, 12)
		[
			SNew(SEditableTextBox)
			.Text_Lambda([S]() { return FText::FromString(S->GroupStr); })
			.OnTextChanged_Lambda([S](const FText& T) { S->GroupStr = T.ToString(); })
		]


		// Buttons
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 0, 12, 12)
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
			.Padding(8, 0, 0, 0)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.IsEnabled_Lambda([S]() { return S->ExistingGroupSel.IsValid() && !S->ExistingGroupSel->IsEmpty(); })
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
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
								const FName PrettyVar = S->CompSel->Label;
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
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
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
					case FState::ESourceType::LocalBPVar: if (S->LocalVarSel.IsValid())
							VarName = FName(
								**S->LocalVarSel);
						break;
					case FState::ESourceType::LocalCppVar: if (S->NativePropSel.IsValid())
							VarName = FName(
								**S->NativePropSel);
						break;
					case FState::ESourceType::ComponentVar:
						{
							if (S->CompSel.IsValid() && S->CompPropSel.IsValid())
							{
								UActorComponent* Tmpl =
									S->CompSel->Template.IsValid()
										? S->CompSel->Template.Get()
										: Cast<UActorComponent>(
											FindComponentTemplate(S->Class, S->CompSel->TemplateName));

								const FName TemplateKey = Tmpl ? Tmpl->GetFName() : S->CompSel->TemplateName;
								const FName PrettyVar = S->CompSel->Label;
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

					if (VarName.IsNone())
					{
						UE_LOG(LogTemp, Warning, TEXT("PinVar: Add aborted — no variable selected."));
						return FReply::Handled();
					}

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
				.OnClicked_Lambda([Dialog]()
				{
					Dialog->RequestDestroyWindow();
					return FReply::Handled();
				})
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

	const bool bIsBlueprintClass = (Class->ClassGeneratedBy != nullptr);

	for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* P = *It;
		if (!IsEditableProperty(P)) continue;

		UStruct* OwnerStruct = P->GetOwnerStruct();
		UClass* OwnerClass = Cast<UClass>(OwnerStruct);

		const bool bDeclaredOnNative = (OwnerClass && OwnerClass->ClassGeneratedBy == nullptr);
		const bool bDeclaredOnThisClass = (OwnerClass == Class);

		if (bDeclaredOnNative && (!bIsBlueprintClass || !bDeclaredOnThisClass))
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
