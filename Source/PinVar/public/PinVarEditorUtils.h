#pragma once
#include "CoreMinimal.h"

class UBlueprint;

namespace PinVarEditorUtils
{
	/** Replace the full PinnedGroup string (pipe-separated) on a BP-local variable. */
	bool SetBPVarPinnedGroup(UBlueprint* BP, FName VarName, const FString& GroupStr);

	/** Append a token (or tokens like "Core|Debug") to PinnedGroup, avoiding duplicates. */
	bool AddBPVarPinnedGroupToken(UBlueprint* BP, FName VarName, const FString& GroupTokenOrPipeList);
}
