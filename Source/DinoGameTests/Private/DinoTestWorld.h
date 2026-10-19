#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "OnlineSubsystemTypes.h"
#include "GameFramework/OnlineReplStructs.h"

/**
 * A bare game world for tests that need real actors - a PlayerState's components, a
 * PlayerController's mute list. Standalone and with no net driver, so actors have authority and
 * RPCs run locally. Destroyed with the scope.
 *
 * No game mode, game state or players: tests that need the match flow belong in the PIE test
 * tier (Notion: "PIE flow tests"), not here.
 */
class FDinoTestWorld
{
public:
	FDinoTestWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("DinoTestWorld"));
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
	}

	~FDinoTestWorld()
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	template <class T>
	T* Spawn()
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<T>(T::StaticClass(), FTransform::Identity, Params);
	}

	UWorld* Get() const { return World; }

	/** A net ID that cannot collide with a real player's, unique per call. */
	static FUniqueNetIdRepl MakeNetId(const TCHAR* Name)
	{
		static int32 Counter = 0;
		const FUniqueNetIdRef Id = FUniqueNetIdString::Create(FString::Printf(TEXT("DinoTest-%s-%d"), Name, ++Counter), FName(TEXT("NULL")));
		return FUniqueNetIdRepl(Id);
	}

private:
	UWorld* World = nullptr;
};
