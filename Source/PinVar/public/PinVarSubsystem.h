#pragma once
#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "PinVarSubsystem.generated.h"

USTRUCT()
struct FPinnedVariable
{
    GENERATED_BODY()

    UPROPERTY() FName VariableName;
    UPROPERTY() FName GroupName;

    UPROPERTY() FName ComponentTemplateName = NAME_None;

    FPinnedVariable() {}
    FPinnedVariable(FName InVarName, FName InGroupName, FName InComp = NAME_None)
        : VariableName(InVarName), GroupName(InGroupName), ComponentTemplateName(InComp) {}
};

UCLASS()
class UPinVarSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()
public:
    // Class -> array of pinned entries
    TMap<FName, TArray<FPinnedVariable>> PinnedGroups;
    TMap<FName, TArray<FPinnedVariable>> StagedPinnedGroups;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    
    // Overloads with optional component template (defaults keep old callsites working)
    void PinVariable(FName ClassName, FName VariableName, FName GroupName, FName ComponentTemplateName = NAME_None);
    void StagePinVariable(FName ClassName, FName VariableName, FName GroupName, FName ComponentTemplateName = NAME_None);
    bool UnstagePinVariable(FName ClassName, FName VariableName, FName GroupName, FName ComponentTemplateName = NAME_None);
    void MergeStagedIntoPinned();

    const TArray<FPinnedVariable>* GetPinnedVariables(FName ClassName) const;
    void GetAllStaged(TArray<TTuple<FName,FName,FName>>& OutTriples) const; // keep for compatibility
    void GetAllStagedWithComp(TArray<TTuple<FName,FName,FName,FName>>& OutQuads) const;

    bool SaveToDisk() const;
    bool LoadFromDisk();
private:
    FString GetPinsFilePath() const;
};
