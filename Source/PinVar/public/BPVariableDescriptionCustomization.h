#pragma once

#include "IPropertyTypeCustomization.h"
#include "Templates/SharedPointer.h"

class IPropertyHandle;
class UBlueprint;

class FBPVariableDescriptionCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	// IPropertyTypeCustomization
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
								 class FDetailWidgetRow& HeaderRow,
								 IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle,
								   class IDetailChildrenBuilder& ChildBuilder,
								   IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:
	// Helpers
	void ReadPinnedGroups(FString& OutGroups) const;
	void WritePinnedGroups(const FString& Groups);

private:
	// Root handle to the FBPVariableDescription
	TSharedPtr<IPropertyHandle> RootHandle;
	// Handle to FBPVariableDescription::VarName
	TSharedPtr<IPropertyHandle> VarNameHandle;
	// Owning blueprint (the variables live here)
	TWeakObjectPtr<UBlueprint> OwningBP;
};
