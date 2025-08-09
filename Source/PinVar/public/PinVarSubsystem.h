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

    FPinnedVariable() {}
    FPinnedVariable(FName InVarName, FName InGroupName)
        : VariableName(InVarName), GroupName(InGroupName) {}
};

UCLASS()
class UPinVarSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()

public:

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    
    TMap<FName, TArray<FPinnedVariable>> PinnedGroups;

    TMap<FName, TArray<FPinnedVariable>> StagedPinnedGroups;
    
    void PinVariable(FName ClassName, FName VariableName, FName GroupName);
    void StagePinVariable(FName ClassName, FName VariableName, FName GroupName);
    bool UnstagePinVariable(FName ClassName, FName VariableName, FName GroupName);
    void MergeStagedIntoPinned();

    const TArray<FPinnedVariable>* GetPinnedVariables(FName ClassName) const;
    void GetAllStaged(TArray<TTuple<FName,FName,FName>>& OutTriples) const;

    bool SaveToDisk() const;

    bool LoadFromDisk();
    private:
    FString GetPinsFilePath() const;
};
