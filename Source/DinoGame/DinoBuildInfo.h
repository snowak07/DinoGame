#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DinoBuildInfo.generated.h"

/**
 * Identifies which build a player is actually running.
 *
 * During a playtest the useful question is never "what did I publish" but "what is the
 * person reporting this bug running" — an auto-update that silently did not apply looks
 * exactly like a bug that was never fixed. The version is compiled in at package time so
 * it cannot drift from the binary it describes.
 */
UCLASS()
class DINOGAME_API UDinoBuildInfo : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Git short SHA of the tree this build was packaged from, suffixed "-dirty" when it was
	 * built with uncommitted changes. "unknown" for editor and local builds, which are not
	 * stamped.
	 */
	UFUNCTION(BlueprintPure, Category = "Dino|Build")
	static FString GetBuildVersion();

	/** False for editor/local builds, where the version says nothing useful. */
	UFUNCTION(BlueprintPure, Category = "Dino|Build")
	static bool IsStampedBuild();
};
