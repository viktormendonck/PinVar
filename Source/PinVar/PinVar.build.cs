using UnrealBuildTool;

public class PinVar : ModuleRules
{
    public PinVar(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] { "AppFramework", "Json", "JsonUtilities","Projects" });
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "EditorSubsystem",
            "UnrealEd",
            "LevelEditor",
            "PropertyEditor",
            "AssetRegistry",
            "ToolMenus",
            "InputCore" 
        });
    }
}