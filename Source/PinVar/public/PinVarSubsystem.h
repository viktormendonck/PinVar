// PinVarSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UObject/WeakObjectPtr.h"
#include "PinVarSubsystem.generated.h"

struct FPinnedVariable
{
    FPinnedVariable() = default;

    FPinnedVariable(FName InVar, FName InGroup, FName InComp = NAME_None, FName InCompVarPretty = NAME_None, FSoftObjectPath Path = FSoftObjectPath())
        : VariableName(InVar)


        , GroupName(InGroup)
        , ComponentTemplateName(InComp)
        , ComponentVariablePrettyName(InCompVarPretty)
        , AssetPath(Path)
    {
    }

    bool operator==(const FPinnedVariable& Other) const
    {
        return VariableName == Other.VariableName
               && GroupName == Other.GroupName
               && ComponentTemplateName == Other.ComponentTemplateName;
    }

    // persisted
    FName VariableName{ NAME_None };
    FName GroupName{ NAME_None };
    FName ComponentTemplateName{ NAME_None }; // template subobject name on CDO (e.g. AmbrosiaHealth_GEN_VARIABLE)
    FName ComponentVariablePrettyName{ NAME_None };
    // SCS variable name (e.g. AmbrosiaHealth) — optional but helps resolution
    FSoftObjectPath AssetPath;
    // session-only (not persisted)
    TWeakObjectPtr<UObject> ResolvedTemplate; // resolved component template for this session
};

UCLASS()
class UPinVarSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    TMap<FName, TArray<FPinnedVariable>> PinnedGroups;

    void PinBlueprintVariable(FName ClassName, FName VariableName, FName GroupName);
    void PinDataAssetVariable(FName ClassName, FName VariableName, FName GroupName, UObject* DataAssetInstance);
    void PinComponentVariable(FName ClassName, FName VariableName, FName GroupName, FName ComponentTemplateName, UObject* TemplatePtr, FName ComponentVariablePrettyName = NAME_None);

    bool UnstagePinVariable(FName ClassName, FName VariableName, FName GroupName, FName ComponentTemplateName = NAME_None);

    // Persistence
    bool SaveToDisk() const;
    bool LoadFromDisk();
    static FString GetPinsFilePath();
    void RepopulateSessionCacheAll();
};
