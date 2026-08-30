#include "DinoBuildInfo.h"

// Defined by DinoGame.Build.cs from Build/DinoBuildVersion.txt. The fallback keeps the
// module compiling for anyone building without that file present.
#ifndef DINO_BUILD_VERSION
#define DINO_BUILD_VERSION "unknown"
#endif

FString UDinoBuildInfo::GetBuildVersion()
{
	// Deliberately not TEXT(DINO_BUILD_VERSION): TEXT() pastes with ##, which suppresses
	// macro expansion of its argument and would yield the literal token name.
	return FString(ANSI_TO_TCHAR(DINO_BUILD_VERSION));
}

bool UDinoBuildInfo::IsStampedBuild()
{
	return GetBuildVersion() != TEXT("unknown");
}
