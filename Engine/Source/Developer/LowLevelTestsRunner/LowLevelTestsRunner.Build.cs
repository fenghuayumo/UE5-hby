// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
public class LowLevelTestsRunner : ModuleRules
{
	public LowLevelTestsRunner(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		PrecompileForTargets = PrecompileTargetsType.None;
		bUseUnity = false;
		bRequiresImplementModule = false;

		PublicIncludePaths.AddRange(
			new string[]
			{
				"Runtime/Launch/Public",
				Path.Combine(Target.UEThirdPartySourceDirectory, "Catch2")
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				"Runtime/Launch/Private"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Projects"
			}
		);

		if (Target.bCompileAgainstApplicationCore)
		{
			PrivateDependencyModuleNames.Add("ApplicationCore");
		}

		if (Target.bCompileAgainstCoreUObject)
		{
			PrivateDependencyModuleNames.Add("CoreUObject");
		}
	}
}