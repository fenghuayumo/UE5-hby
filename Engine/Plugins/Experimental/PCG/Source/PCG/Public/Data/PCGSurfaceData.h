// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSpatialData.h"

#include "PCGSurfaceData.generated.h"

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural))
class UPCGSurfaceData : public UPCGSpatialDataWithPointCache
{
	GENERATED_BODY()

public:
	//~Begin UPCGSpatialData interface
	virtual int GetDimension() const override { return 2; }
	virtual FVector TransformPosition(const FVector& InPosition) const override;
	virtual FPCGPoint TransformPoint(const FPCGPoint& InPoint) const override;
	virtual bool HasNonTrivialTransform() const override { return true; }
	//~End UPCGSpatialData interface

protected:
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = SpatialData)
	FTransform Transform;
};