using UnrealBuildTool;

public class PinVar : ModuleRules
{
	public PinVar(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"ToolWidgets"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AppFramework",
			"EditorSubsystem",
			"UnrealEd",
			"LevelEditor",
			"ToolMenus",
			"PropertyEditor",
			"ContentBrowser",
			"InputCore",
			"Kismet",
			"Json",
			"JsonUtilities",
			"Projects",
			"SourceControl",
			"SourceControlWindows",
			"AssetRegistry"
		});
	}
}