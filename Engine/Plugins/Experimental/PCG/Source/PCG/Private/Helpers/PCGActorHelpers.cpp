// Copyright Epic Games, Inc. All Rights Reserved.

#include "Helpers/PCGActorHelpers.h"

#include "PCGHelpers.h"

#include "PCGComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartition.h"

#if WITH_EDITOR
#include "Editor.h"
#include "SourceControlHelpers.h"
#include "PackageSourceControlHelper.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#endif

UInstancedStaticMeshComponent* UPCGActorHelpers::GetOrCreateISMC(AActor* InTargetActor, const UPCGComponent* InSourceComponent, UStaticMesh* InMesh, const TArray<UMaterialInterface*>& InMaterials)
{
	check(InTargetActor != nullptr && InMesh != nullptr);

	TArray<UInstancedStaticMeshComponent*> ISMCs;
	InTargetActor->GetComponents<UInstancedStaticMeshComponent>(ISMCs);

	for (UInstancedStaticMeshComponent* ISMC : ISMCs)
	{
		if (ISMC->GetStaticMesh() == InMesh &&
			(!InSourceComponent || ISMC->ComponentTags.Contains(InSourceComponent->GetFName())))
		{
			// If materials are provided, we'll make sure they match to the already set materials.
			// If not provided, we'll make sure that the current materials aren't overriden
			bool bMaterialsMatched = true;

			for (int32 MaterialIndex = 0; MaterialIndex < ISMC->GetNumMaterials() && bMaterialsMatched; ++MaterialIndex)
			{
				if (MaterialIndex < InMaterials.Num() && InMaterials[MaterialIndex])
				{
					if (InMaterials[MaterialIndex] != ISMC->GetMaterial(MaterialIndex))
					{
						bMaterialsMatched = false;
					}
				}
				else
				{
					// Material is currently overriden
					if (ISMC->OverrideMaterials.IsValidIndex(MaterialIndex) && ISMC->OverrideMaterials[MaterialIndex])
					{
						bMaterialsMatched = false;
					}
				}
			}

			if (bMaterialsMatched)
			{
				return ISMC;
			}
		}
	}

	InTargetActor->Modify();

	// Otherwise, create a new component
	// TODO: use static mesh component if there's only one instance
	// TODO: add hism/ism switch or better yet, use a template component
	UInstancedStaticMeshComponent* ISMC = NewObject<UHierarchicalInstancedStaticMeshComponent>(InTargetActor);
	ISMC->SetStaticMesh(InMesh);

	// TODO: improve material override mechanisms
	const int32 NumMaterials = ISMC->GetNumMaterials();
	for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
	{
		ISMC->SetMaterial(MaterialIndex, MaterialIndex < InMaterials.Num() ? InMaterials[MaterialIndex] : nullptr);
	}

	ISMC->RegisterComponent();
	InTargetActor->AddInstanceComponent(ISMC);
	ISMC->SetMobility(EComponentMobility::Static);
	// TODO: add option for collision, or use a template
	ISMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ISMC->AttachToComponent(InTargetActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);

	if (InSourceComponent)
	{
		ISMC->ComponentTags.Add(InSourceComponent->GetFName());
	}
	
	ISMC->ComponentTags.Add(PCGHelpers::DefaultPCGTag);

	return ISMC;
}

bool UPCGActorHelpers::DeleteActors(UWorld* World, const TArray<TSoftObjectPtr<AActor>>& ActorsToDelete)
{
	check(World);

	if (ActorsToDelete.Num() == 0)
	{
		return true;
	}

#if WITH_EDITOR
	// Remove potential references to to-be deleted objects from the global selection sets.
	/*if (GIsEditor)
	{
		GEditor->ResetAllSelectionSets();
	}*/

	UWorldPartition* WorldPartition = World ? World->GetWorldPartition() : nullptr;

	if (WorldPartition)
	{
		TArray<FString> PackagesToDeleteFromSCC;
		TSet<UPackage*> PackagesToCleanup;

		for (const TSoftObjectPtr<AActor>& ManagedActor : ActorsToDelete)
		{
			// If actor is loaded, just remove from world and keep track of package to cleanup
			if (AActor* Actor = ManagedActor.Get())
			{
				if (UPackage* ActorPackage = Actor->GetExternalPackage())
				{
					PackagesToCleanup.Emplace(ActorPackage);
				}
				
				World->DestroyActor(Actor);
			}
			// Otherwise, get from World Partition.
			// Note that it is possible that some actors don't exist anymore, so a null here is not a critical condition
			else if (const FWorldPartitionActorDesc* ActorDesc = WorldPartition->GetActorDesc(ManagedActor.ToSoftObjectPath()))
			{
				PackagesToDeleteFromSCC.Emplace(ActorDesc->GetActorPackage().ToString());
				WorldPartition->RemoveActor(ActorDesc->GetGuid());
			}
		}

		// Save currently loaded packages so they get deleted
		if (PackagesToCleanup.Num() > 0)
		{
			ObjectTools::CleanupAfterSuccessfulDelete(PackagesToCleanup.Array(), /*bPerformReferenceCheck=*/true);
		}

		// Delete outstanding unloaded packages
		if (PackagesToDeleteFromSCC.Num() > 0)
		{
			FPackageSourceControlHelper PackageHelper;
			if (!PackageHelper.Delete(PackagesToDeleteFromSCC))
			{
				return false;
			}
		}
	}
	else
#endif
	{
		// Not in editor, really unlikely to happen but might be slow
		for (const TSoftObjectPtr<AActor>& ManagedActor : ActorsToDelete)
		{
			if (ManagedActor.Get())
			{
				World->DestroyActor(ManagedActor.Get());
			}
		}
	}

	return true;
}

// Note: this is copied verbatim from AIHelpers.h/.cpp
void UPCGActorHelpers::GetActorClassDefaultComponents(const TSubclassOf<AActor>& ActorClass, TArray<UActorComponent*>& OutComponents, const TSubclassOf<UActorComponent>& InComponentClass)
{
	if (!ensure(ActorClass.Get()))
	{
		return;
	}

	UClass* ClassPtr = InComponentClass.Get();
	TArray<UActorComponent*> ResultComponents;

	// Get the components defined on the native class.
	AActor* CDO = ActorClass->GetDefaultObject<AActor>();
	check(CDO);
	if (ClassPtr)
	{
		CDO->GetComponents(InComponentClass, ResultComponents);
	}
	else
	{
		CDO->GetComponents(ResultComponents);
	}

	// Try to get the components off the BP class.
	UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(*ActorClass);
	if (BPClass)
	{
		// A BlueprintGeneratedClass has a USimpleConstructionScript member. This member has an array of RootNodes
		// which contains the SCSNode for the root SceneComponent and non-SceneComponents. For the SceneComponent
		// hierarchy, each SCSNode knows its children SCSNodes. Each SCSNode stores the component template that will
		// be created when the Actor is spawned.
		//
		// WARNING: This may change in future engine versions!

		TArray<UActorComponent*> Unfiltered;
		// using this semantic to avoid duplicating following loops or adding a filtering check condition inside the loops
		TArray<UActorComponent*>& TmpComponents = ClassPtr ? Unfiltered : ResultComponents;

		// Check added components.
		USimpleConstructionScript* ConstructionScript = BPClass->SimpleConstructionScript;
		if (ConstructionScript)
		{
			for (const USCS_Node* Node : ConstructionScript->GetAllNodes())
			{
				TmpComponents.Add(Node->ComponentTemplate);
			}
		}
		// Check modified inherited components.
		UInheritableComponentHandler* InheritableComponentHandler = BPClass->InheritableComponentHandler;
		if (InheritableComponentHandler)
		{
			for (TArray<FComponentOverrideRecord>::TIterator It = InheritableComponentHandler->CreateRecordIterator(); It; ++It)
			{
				TmpComponents.Add(It->ComponentTemplate);
			}
		}

		// Filter to the ones matching the requested class.
		if (ClassPtr)
		{
			for (UActorComponent* TemplateComponent : Unfiltered)
			{
				if (TemplateComponent->IsA(ClassPtr))
				{
					ResultComponents.Add(TemplateComponent);
				}
			}
		}
	}

	OutComponents = MoveTemp(ResultComponents);
}