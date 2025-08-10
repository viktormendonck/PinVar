// PinVarSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UObject/WeakObjectPtr.h"
#include "PinVarSubsystem.generated.h"

// Simple POD used in maps; JSON persistence is manual (no reflection needed)
struct FPinnedVariable
{
    FPinnedVariable() = default;
    FPinnedVariable(FName InVar, FName InGroup, FName InComp = NAME_None, FName InCompVarPretty = NAME_None)
        : VariableName(InVar)
        , GroupName(InGroup)
        , ComponentTemplateName(InComp)
        , ComponentVariablePrettyName(InCompVarPretty)
    {}

    // persisted
    FName VariableName{ NAME_None };
    FName GroupName{ NAME_None };
    FName ComponentTemplateName{ NAME_None };         // template subobject name on CDO (e.g. AmbrosiaHealth_GEN_VARIABLE)
    FName ComponentVariablePrettyName{ NAME_None };   // SCS variable name (e.g. AmbrosiaHealth) — optional but helps resolution

    // session-only (not persisted)
    TWeakObjectPtr<UObject> ResolvedTemplate;         // resolved component template for this session
};

UCLASS()
class UPinVarSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()

public:
    // UEditorSubsystem
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // In‑memory state for the panel (mirrors staged)
    TMap<FName, TArray<FPinnedVariable>> PinnedGroups;

    // Source of truth (what’s saved to disk)
    TMap<FName, TArray<FPinnedVariable>> StagedPinnedGroups;

    // Add/remove to the staged set (and persist)
    void StagePinVariable(FName ClassName, FName VariableName, FName GroupName, FName ComponentTemplateName = NAME_None);

    void StagePinVariableWithTemplate(FName ClassName, FName VariableName, FName GroupName,
                                      FName ComponentTemplateName, UObject* TemplatePtr,
                                      FName ComponentVariablePrettyName = NAME_None);

    bool UnstagePinVariable(FName ClassName, FName VariableName, FName GroupName, FName ComponentTemplateName = NAME_None);

    void GetAllStagedWithComp(TArray<TTuple<FName,FName,FName,FName>>& OutQuads) const;
    void MergeStagedIntoPinned();

    void RepopulateSessionCacheAll();

    // Persistence
    bool    SaveToDisk() const;
    bool    LoadFromDisk();
    FString GetPinsFilePath() const;
};

