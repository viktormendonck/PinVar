#include "BPVariableDescriptionCustomization.h"

#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"

#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "PinVarEditorUtils.h" // our helper from Step 1

#define LOCTEXT_NAMESPACE "PinVar.BPVarCustomization"

TSharedRef<IPropertyTypeCustomization> FBPVariableDescriptionCustomization::MakeInstance()
{
    return MakeShared<FBPVariableDescriptionCustomization>();
}

void FBPVariableDescriptionCustomization::CustomizeHeader(
    TSharedRef<IPropertyHandle> StructPropertyHandle,
    FDetailWidgetRow& HeaderRow,
    IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
    RootHandle = StructPropertyHandle;

    // Prefer getting outer objects from the property handle (contains the UBlueprint)
    TArray<UObject*> Outers;
    RootHandle->GetOuterObjects(Outers);
    for (UObject* Obj : Outers)
    {
        if (UBlueprint* BP = Cast<UBlueprint>(Obj))
        {
            OwningBP = BP;
            break;
        }
    }

    // Cache the VarName handle (member of FBPVariableDescription)
    VarNameHandle = RootHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBPVariableDescription, VarName));

    // Keep the default header behavior
    HeaderRow
        .NameContent()[ RootHandle->CreatePropertyNameWidget() ]
        .ValueContent()[ RootHandle->CreatePropertyValueWidget() ];
}

void FBPVariableDescriptionCustomization::CustomizeChildren(
    TSharedRef<IPropertyHandle> StructPropertyHandle,
    IDetailChildrenBuilder& ChildBuilder,
    IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
    FString CurrentGroups;
    ReadPinnedGroups(CurrentGroups);

    ChildBuilder.AddCustomRow(LOCTEXT("PinnedGroupFilter", "Pinned Group"))
    .NameContent()
    [
        SNew(STextBlock)
        .Text(LOCTEXT("PinnedGroupsLabel", "Pinned Groups"))
        .ToolTipText(LOCTEXT("PinnedGroupsTip", "Pipe-separated list of groups, e.g. Core|Debug"))
        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
    ]
    .ValueContent()
    .MinDesiredWidth(260.f)
    [
        SNew(SEditableTextBox)
        .Text(FText::FromString(CurrentGroups))
        .HintText(LOCTEXT("PinnedGroupsHint", "e.g. Core or Core|Debug"))
        .OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
        {
            WritePinnedGroups(NewText.ToString());
        })
    ];
}

void FBPVariableDescriptionCustomization::ReadPinnedGroups(FString& OutGroups) const
{
    OutGroups.Reset();
    if (!OwningBP.IsValid() || !VarNameHandle.IsValid())
        return;

    FName VarName = NAME_None;
    if (VarNameHandle->GetValue(VarName) != FPropertyAccess::Success || VarName.IsNone())
        return;

    // Scope is the BP's SkeletonGeneratedClass for local vars
    if (UBlueprint* BP = OwningBP.Get())
    {
        UStruct* Scope = BP->SkeletonGeneratedClass;
        if (Scope)
        {
            FBlueprintEditorUtils::GetBlueprintVariableMetaData(
                BP, VarName, Scope, FName("PinnedGroup"), OutGroups);
        }
    }
}

void FBPVariableDescriptionCustomization::WritePinnedGroups(const FString& Groups)
{
    if (!OwningBP.IsValid() || !VarNameHandle.IsValid())
        return;

    FName VarName = NAME_None;
    if (VarNameHandle->GetValue(VarName) != FPropertyAccess::Success || VarName.IsNone())
        return;

    if (UBlueprint* BP = OwningBP.Get())
    {
        // Use our helper so it marks + compiles
        PinVarEditorUtils::SetBPVarPinnedGroup(BP, VarName, Groups);
    }
}

#undef LOCTEXT_NAMESPACE
