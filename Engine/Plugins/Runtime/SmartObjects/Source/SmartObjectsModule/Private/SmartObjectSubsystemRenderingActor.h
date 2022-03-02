﻿// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SmartObjectDebugRenderingComponent.h"
#include "GameFramework/Actor.h"
#include "SmartObjectSubsystemRenderingActor.generated.h"

/** Rendering component for SmartObjectRendering actor. */
UCLASS(ClassGroup = Debug, NotBlueprintable, NotPlaceable)
class SMARTOBJECTSMODULE_API USmartObjectSubsystemRenderingComponent : public USmartObjectDebugRenderingComponent
{
	GENERATED_BODY()

public:
	explicit USmartObjectSubsystemRenderingComponent(const FObjectInitializer& ObjectInitializer)
		: USmartObjectDebugRenderingComponent(ObjectInitializer)
	{
#if UE_ENABLE_DEBUG_DRAWING
		ViewFlagName = TEXT("Game");
#endif
	}

protected:
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

#if UE_ENABLE_DEBUG_DRAWING
	virtual void DebugDraw(FDebugRenderSceneProxy* DebugProxy) override;
	virtual void DebugDrawCanvas(UCanvas* Canvas, APlayerController*) override;
#endif
};

UCLASS(Transient, NotBlueprintable, NotPlaceable)
class SMARTOBJECTSMODULE_API ASmartObjectSubsystemRenderingActor : public AActor
{
	GENERATED_BODY()

public:
	ASmartObjectSubsystemRenderingActor();

private:
	UPROPERTY()
	USmartObjectSubsystemRenderingComponent* RenderingComponent;
};

