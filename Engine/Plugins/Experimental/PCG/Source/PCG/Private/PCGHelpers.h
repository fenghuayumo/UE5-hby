// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class APCGWorldActor;
class ALandscape;
class ALandscapeProxy;
class UWorld;

namespace PCGHelpers
{
	/** Tag that will be added on every component generated through the PCG system */
	const FName DefaultPCGTag = TEXT("PCG Generated Component");
	const FName DefaultPCGDebugTag = TEXT("PCG Generated Debug Component");
	const FName DefaultPCGActorTag = TEXT("PCG Generated Actor");

	int ComputeSeed(int A);
	int ComputeSeed(int A, int B);
	int ComputeSeed(int A, int B, int C);

	FBox GetActorBounds(AActor* InActor);
	FBox GetLandscapeBounds(ALandscapeProxy* InLandscape);

	ALandscape* GetLandscape(UWorld* InWorld, const FBox& InActorBounds);

#if WITH_EDITOR
	APCGWorldActor* GetPCGWorldActor(UWorld* InWorld);
#endif
};