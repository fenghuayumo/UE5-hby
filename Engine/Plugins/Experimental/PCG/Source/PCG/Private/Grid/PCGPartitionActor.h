// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorPartition/PartitionActor.h"

#include "PCGPartitionActor.generated.h"

class UPCGComponent;

/** 
* The APCGPartitionActor actor is used to store grid cell data
* and its size will be a multiple of the grid size.
*/
UCLASS(MinimalAPI, NotBlueprintable, NotPlaceable)
class APCGPartitionActor : public APartitionActor
{
	GENERATED_BODY()

public:
	//~Begin AActor Interface
	virtual void BeginPlay();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetActorBounds(bool bOnlyCollidingComponents, FVector& Origin, FVector& BoxExtent, bool bIncludeFromChildActors) const override;
#if WITH_EDITOR
	virtual FBox GetStreamingBounds() const override;
	virtual AActor* GetSceneOutlinerParent() const override;
#endif
	//~End AActor Interface

#if WITH_EDITOR
	//~Begin APartitionActor Interface
	virtual uint32 GetDefaultGridSize(UWorld* InWorld) const override;
	virtual FGuid GetGridGuid() const override { return PCGGuid; }
	//~End APartitionActor Interface
#endif

	FBox GetFixedBounds() const;
#if WITH_EDITOR
	void AddGraphInstance(UPCGComponent* OriginalComponent);
	bool RemoveGraphInstance(UPCGComponent* OriginalComponent);
	bool CleanupDeadGraphInstances();
#endif

	// TODO: Make this in-editor only; during runtime, we should keep a map of component to bounds/volume only
	// and preferably precompute the intersection, so this would make it easier/possible to not have the original actor in game version.
	UFUNCTION(BlueprintCallable, Category = "PCG|PartitionActor")
	UPCGComponent* GetLocalComponent(const UPCGComponent* OriginalComponent) const;

	UFUNCTION(BlueprintCallable, Category = "PCG|PartitionActor")
	UPCGComponent* GetOriginalComponent(const UPCGComponent* LocalComponent) const;

	UPROPERTY()
	FGuid PCGGuid;

private:
	// TODO: Make these properties editor only (see comment before).
	UPROPERTY()
	TMap<TObjectPtr<UPCGComponent>, TObjectPtr<UPCGComponent>> OriginalToLocalMap;

	UPROPERTY()
	TMap<TObjectPtr<UPCGComponent>, TObjectPtr<UPCGComponent>> LocalToOriginalMap;
};
