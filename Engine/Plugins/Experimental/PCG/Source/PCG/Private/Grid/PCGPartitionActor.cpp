// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGPartitionActor.h"
#include "PCGComponent.h"
#include "PCGHelpers.h"
#include "PCGWorldActor.h"

#include "Landscape.h"

void APCGPartitionActor::BeginPlay()
{
	Super::BeginPlay();

	// Register cell to the PCG grid
	// TODO
}

void APCGPartitionActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unregister each cell to the PCG grid
	// TODO

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
uint32 APCGPartitionActor::GetDefaultGridSize(UWorld* InWorld) const
{
	// TODO: promote to world settings
	return 25600;
}
#endif

FBox APCGPartitionActor::GetFixedBounds() const
{
	const FVector Center = GetActorLocation();
	// TODO: keep this in the actor instead of hardcoding.
	const uint32 HalfGridSize = 25600 / 2;
	return FBox(Center - HalfGridSize, Center + HalfGridSize);
}

void APCGPartitionActor::GetActorBounds(bool bOnlyCollidingComponents, FVector& Origin, FVector& BoxExtent, bool bIncludeFromChildActors) const
{
	Super::GetActorBounds(bOnlyCollidingComponents, Origin, BoxExtent, bIncludeFromChildActors);

	// To keep consistency with the other GetBounds functions, transform our result into an origin / extent formatting
	FBox Bounds(Origin - BoxExtent, Origin + BoxExtent);
	Bounds += GetFixedBounds();
	Bounds.GetCenterAndExtents(Origin, BoxExtent);
}

UPCGComponent* APCGPartitionActor::GetLocalComponent(const UPCGComponent* OriginalComponent) const
{
	const TObjectPtr<UPCGComponent>* LocalComponent = OriginalToLocalMap.Find(OriginalComponent);
	return LocalComponent ? *LocalComponent : nullptr;
}

UPCGComponent* APCGPartitionActor::GetOriginalComponent(const UPCGComponent* LocalComponent) const
{
	const TObjectPtr<UPCGComponent>* OriginalComponent = LocalToOriginalMap.Find(LocalComponent);
	return OriginalComponent ? (*OriginalComponent).Get() : nullptr;
}

#if WITH_EDITOR
FBox APCGPartitionActor::GetStreamingBounds() const
{
	return Super::GetStreamingBounds() + GetFixedBounds();
}

AActor* APCGPartitionActor::GetSceneOutlinerParent() const
{
	if (APCGWorldActor* PCGActor = PCGHelpers::GetPCGWorldActor(GetWorld()))
	{
		return PCGActor;
	}
	else
	{
		return Super::GetSceneOutlinerParent();
	}	
}

void APCGPartitionActor::AddGraphInstance(UPCGComponent* OriginalComponent)
{
	if (!OriginalComponent)
	{
		return;
	}

	// Make sure we don't have that graph twice;
	// Here we'll check if there has been some changes worth propagating or not
	UPCGComponent* LocalComponent = GetLocalComponent(OriginalComponent);

	if (LocalComponent)
	{
		// Update properties as needed and early out
		LocalComponent->SetPropertiesFromOriginal(OriginalComponent);
		return;
	}

	Modify();

	// Create a new local component
	LocalComponent = NewObject<UPCGComponent>(this);
	LocalComponent->SetPropertiesFromOriginal(OriginalComponent);

	LocalComponent->RegisterComponent();
	// TODO: check if we should use a non-instanced component?
	AddInstanceComponent(LocalComponent);

	OriginalToLocalMap.Add(OriginalComponent, LocalComponent);
	LocalToOriginalMap.Add(LocalComponent, OriginalComponent);
}

bool APCGPartitionActor::RemoveGraphInstance(UPCGComponent* OriginalComponent)
{
	UPCGComponent* LocalComponent = GetLocalComponent(OriginalComponent);

	if (!LocalComponent)
	{
		return false;
	}

	Modify();

	OriginalToLocalMap.Remove(OriginalComponent);
	LocalToOriginalMap.Remove(LocalComponent);

	// TODO Add option to not cleanup?
	LocalComponent->Cleanup(/*bRemoveComponents=*/true);
	LocalComponent->DestroyComponent();

	return OriginalToLocalMap.IsEmpty();
}

bool APCGPartitionActor::CleanupDeadGraphInstances()
{
	TSet<TObjectPtr<UPCGComponent>> DeadLocalInstances;

	// Note: since we might end up with multiple nulls in the original to local map
	// it might not be very stable to use it; we'll use the 
	for (const auto& LocalToOriginalItem : LocalToOriginalMap)
	{
		if (!LocalToOriginalItem.Value)
		{
			DeadLocalInstances.Add(LocalToOriginalItem.Key);
		}
	}

	if (DeadLocalInstances.Num() == 0)
	{
		return OriginalToLocalMap.IsEmpty();
	}

	Modify();

	for (const TObjectPtr<UPCGComponent>& DeadInstance : DeadLocalInstances)
	{
		LocalToOriginalMap.Remove(DeadInstance);

		if (DeadInstance)
		{
			DeadInstance->Cleanup(/*bRemoveComponents=*/true);
			DeadInstance->DestroyComponent();
		}
	}

	// Remove all dead entries
	OriginalToLocalMap.Remove(nullptr);

	return OriginalToLocalMap.IsEmpty();
}
#endif // WITH_EDITOR