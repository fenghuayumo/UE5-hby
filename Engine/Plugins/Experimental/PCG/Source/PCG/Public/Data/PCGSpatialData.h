// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGData.h"
#include "PCGPoint.h"

#include "PCGSpatialData.generated.h"

class UPCGPointData;
class UPCGIntersectionData;
class UPCGUnionData;
class UPCGDifferenceData;
class UPCGProjectionData;
class AActor;

/**
* "Concrete" data base class for PCG generation
* This will be the base class for data classes that actually represent
* concrete evidence of spatial data - points, surfaces, splines, etc.
* In opposition to settings/control type of data.
* 
* Conceptually, any concrete data can be decayed into points (potentially through transformations)
* which hold metadata and a transform, and this is the basic currency of the PCG framework.
*/
UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural))
class UPCGSpatialData : public UPCGData
{
	GENERATED_BODY()

public:
	/** Returns the dimension of the data type, which has nothing to do with the dimension of its points */
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	virtual int GetDimension() const PURE_VIRTUAL(UPCGSpatialData::GetDimension, return 0;);

	/** Returns the full bounds (including density fall-off) of the data */
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	virtual FBox GetBounds() const PURE_VIRTUAL(UPCGSpatialData::GetBounds, return FBox(EForceInit::ForceInit););

	/** Returns the bounds in which the density is always 1 */
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	virtual FBox GetStrictBounds() const { return FBox(EForceInit::ForceInit); }

	/** Returns the expected data normal (for surfaces) or eventual projection axis (for volumes) */
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	virtual FVector GetNormal() const { return FVector::UnitZ(); }

	/** Computes the density at a given location */
	UFUNCTION(BlueprintCallable, Category = Distribution)
	virtual float GetDensityAtPosition(const FVector& InPosition) const PURE_VIRTUAL(UPCGSpatialData::GetDensityAtPosition, return 0;);

	/** Discretizes the data into points */
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	virtual const UPCGPointData* ToPointData() const PURE_VIRTUAL(UPCGSpatialData::ToPointData, return nullptr;);

	/** Transform a world-space position to a world-space position in relation to the current data. (ex: projection on surface) */
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	virtual FVector TransformPosition(const FVector& InPosition) const { return InPosition; }

	/** Transform a full point similar to TransformPosition */
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	virtual FPCGPoint TransformPoint(const FPCGPoint& InPoint) const;

	/** Returns true if the data has a non-trivial transform */
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	virtual bool HasNonTrivialTransform() const { return false; }

	/** Returns a specialized data to intersect with another data */
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	virtual UPCGIntersectionData* IntersectWith(const UPCGSpatialData* InOther) const;

	/** Returns a specialized data to project this on another data of equal or higher dimension */
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	virtual UPCGProjectionData* ProjectOn(const UPCGSpatialData* InOther) const;

	/** Returns a specialized data to union this with another data */
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	virtual UPCGUnionData* UnionWith(const UPCGSpatialData* InOther) const;

	UFUNCTION(BlueprintCallable, Category = SpatialData)
	virtual UPCGDifferenceData* Subtract(const UPCGSpatialData* InOther) const;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data)
	AActor* TargetActor = nullptr;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient, BlueprintReadWrite, EditAnywhere, Category = Debug)
	bool bKeepZeroDensityPoints = false;
#endif
};

UCLASS(Abstract, ClassGroup = (Procedural))
class UPCGSpatialDataWithPointCache : public UPCGSpatialData
{
	GENERATED_BODY()

public:
	// ~UPCGSpatialData implementation
	virtual const UPCGPointData* ToPointData() const override;
	// ~End UPCGSpatialData implementation

protected:
	virtual const UPCGPointData* CreatePointData() const PURE_VIRTUAL(UPCGSpatialData::CreatePointData, return nullptr;);

private:
	UPROPERTY(Transient)
	mutable TObjectPtr<const UPCGPointData> CachedPointData;

	mutable FCriticalSection CacheLock;
};