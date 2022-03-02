// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGPolyLineData.h"
#include "PCGProjectionData.h"
#include "PCGSplineData.generated.h"

class USplineComponent;
class UPCGSurfaceData;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGSplineData : public UPCGPolyLineData
{
	GENERATED_BODY()

public:
	void Initialize(USplineComponent* InSpline);

	//~Begin UPCGPolyLineData interface
	virtual int GetNumSegments() const override;
	virtual float GetSegmentLength(int SegmentIndex) const override;
	virtual FVector GetLocationAtDistance(int SegmentIndex, float Distance) const override;
	//~End UPCGPolyLineData interface

	//~Begin UPCGSpatialDataWithPointCache interface
	virtual const UPCGPointData* CreatePointData() const override;
	//~End UPCGSpatialDataWithPointCache interface

	//~Begin UPCGSpatialData interface
	virtual FBox GetBounds() const override;
	virtual float GetDensityAtPosition(const FVector& InPosition) const override;
	virtual UPCGProjectionData* ProjectOn(const UPCGSpatialData* InOther) const override;
	//~End 

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = SourceData)
	TSoftObjectPtr<USplineComponent> Spline;

protected:
	UPROPERTY()
	FBox CachedBounds = FBox(EForceInit::ForceInit);
};

UCLASS(BlueprintType, ClassGroup=(Procedural))
class UPCGSplineProjectionData : public UPCGProjectionData
{
	GENERATED_BODY()
public:
	void Initialize(const UPCGSplineData* InSourceSpline, const UPCGSpatialData* InTargetSurface);

	//~Begin UPCGSpatialData interface
	virtual float GetDensityAtPosition(const FVector& InPosition) const override;
	//~End UPCGSpatialData interface

	const UPCGSplineData* GetSpline() const;
	const UPCGSpatialData* GetSurface() const;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = SpatialData)
	FInterpCurveVector2D ProjectedPosition;

protected:
	FVector2D Project(const FVector& InVector) const;
};