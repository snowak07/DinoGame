using UnrealBuildTool;

/**
 * Unit tests for DinoGame, on Unreal's automation framework. Editor-only: never cooked or
 * shipped. Run with Tools/run_tests.py (headless) or the editor's Session Frontend -> Automation,
 * under "DinoGame".
 */
public class DinoGameTests : ModuleRules
{
	public DinoGameTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"CoreOnline",
			"OnlineSubsystem",
			// The engine's voice sound (UVoipListenerSynthComponent), for the playback-check test.
			"OnlineSubsystemUtils",
			"AudioMixer",
			// IVoiceCapture, which the engine's voice-engine header needs, for the talking-quirk test.
			"Voice",
			"DinoGame"
		});
	}
}
