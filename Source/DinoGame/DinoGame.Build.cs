using System;
using System.IO;
using UnrealBuildTool;

public class DinoGame : ModuleRules
{
	public DinoGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			// NavigationSystem is separate from AIModule: perception comes from AIModule,
			// but UNavigationInvokerComponent and the navmesh types live here.
			"NavigationSystem",
			// Native gameplay tags, used to drive StateTree transitions by event.
			"GameplayTags"
		});

		PublicIncludePaths.AddRange(new string[] {
			"DinoGame"
		});

		// --- Build version stamp -------------------------------------------------------
		// package.bat writes the git short SHA into Build/DinoBuildVersion.txt before
		// building, and it is compiled straight into the binary. Build/ is gitignored, so
		// stamping a build never dirties the working tree - which matters, because the
		// version string itself records whether the tree was dirty.
		//
		// ExternalDependencies is what makes this reliable: without it UBT reuses a cached
		// makefile, never re-evaluates this file, and every package would ship whatever
		// version happened to be compiled in first.
		string VersionFile = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "Build", "DinoBuildVersion.txt"));
		ExternalDependencies.Add(VersionFile);

		string BuildVersion = "unknown";
		if (File.Exists(VersionFile))
		{
			string Contents = File.ReadAllText(VersionFile).Trim();
			if (!string.IsNullOrEmpty(Contents))
			{
				// The define ends up inside a C string literal, so anything that could close
				// the quote or escape out of it is dropped rather than trusted.
				char[] Allowed = Contents.ToCharArray();
				string Sanitised = "";
				foreach (char c in Allowed)
				{
					if (char.IsLetterOrDigit(c) || c == '-' || c == '_' || c == '.')
					{
						Sanitised += c;
					}
				}

				if (Sanitised.Length > 0)
				{
					BuildVersion = Sanitised;
				}
			}
		}

		PrivateDefinitions.Add("DINO_BUILD_VERSION=\"" + BuildVersion + "\"");
	}
}
