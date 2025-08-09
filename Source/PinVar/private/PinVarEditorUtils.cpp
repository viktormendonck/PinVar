#include "PinVarEditorUtils.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

static UStruct* GetLocalVarScope(UBlueprint* BP)
{
	return BP ? BP->SkeletonGeneratedClass : nullptr;
}

static bool ReadGroups(UBlueprint* BP, FName VarName, FString& Out)
{
	if (!BP || VarName.IsNone()) return false;
	UStruct* Scope = GetLocalVarScope(BP);
	if (!Scope) return false;
	return FBlueprintEditorUtils::GetBlueprintVariableMetaData(BP, VarName, Scope, FName("PinnedGroup"), Out);
}

static void WriteGroups(UBlueprint* BP, FName VarName, const FString& In)
{
	UStruct* Scope = GetLocalVarScope(BP);
	FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, VarName, Scope, FName("PinnedGroup"), In);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP);
}

bool PinVarEditorUtils::SetBPVarPinnedGroup(UBlueprint* BP, FName VarName, const FString& GroupStr)
{
	if (!BP || VarName.IsNone()) return false;
	WriteGroups(BP, VarName, GroupStr);
	FString Verify;
	const bool bHas = ReadGroups(BP, VarName, Verify);
	return bHas && Verify == GroupStr;
}

bool PinVarEditorUtils::AddBPVarPinnedGroupToken(UBlueprint* BP, FName VarName, const FString& GroupTokenOrPipeList)
{
	if (!BP || VarName.IsNone()) return false;

	// Current value
	FString Current;
	ReadGroups(BP, VarName, Current);

	// Build a de-duped set from current + new tokens
	TSet<FString> Set;
	auto AddTokens = [&Set](const FString& Source)
	{
		TArray<FString> Toks;
		Source.Replace(TEXT(","), TEXT("|")).ParseIntoArray(Toks, TEXT("|"), true);
		for (FString& T : Toks)
		{
			T = T.TrimStartAndEnd();
			if (!T.IsEmpty()) Set.Add(T);
		}
	};

	AddTokens(Current);
	AddTokens(GroupTokenOrPipeList);

	// Join back
	FString NewCombined;
	for (const FString& T : Set)
	{
		if (!NewCombined.IsEmpty()) NewCombined += TEXT("|");
		NewCombined += T;
	}

	WriteGroups(BP, VarName, NewCombined);

	FString Verify;
	const bool bHas = ReadGroups(BP, VarName, Verify);
	return bHas && Verify == NewCombined;
}
