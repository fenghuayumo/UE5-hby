// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSpatialData.h"

#include "PCGDifferenceData.generated.h"

class UPCGUnionData;

UENUM()
enum class EPCGDifferenceDensityFunction : uint8
{
	Minimum,
	ClampedSubstraction
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGDifferenceData : public UPCGSpatialDataWithPointCache
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	void Initialize(const UPCGSpatialData* InData);

	UFUNCTION(BlueprintCallable, Category = SpatialData)
	void AddDifference(const UPCGSpatialData* InDifference);

	UFUNCTION(BlueprintCallable, Category = Settings)
	void SetDensityFunction(EPCGDifferenceDensityFunction InDensityFunction);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	//~Begin UPCGSpatialData interface
	virtual int GetDimension() const override;
	virtual FBox GetBounds() const override;
	virtual FBox GetStrictBounds() const override;
	virtual float GetDensityAtPosition(const FVector& InPosition) const override;
	virtual FVector TransformPosition(const FVector& InPosition) const override;
	virtual FPCGPoint TransformPoint(const FPCGPoint& InPoint) const override;
	virtual bool HasNonTrivialTransform() const override;
	//~End UPCGSpatialData interface

	//~Begin UPCGSpatialDataWithPointCache interface
	virtual const UPCGPointData* CreatePointData() const override;
	//~End UPCGSpatialDataWithPointCache interface
protected:
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = SpatialData)
	TObjectPtr<const UPCGSpatialData> Source = nullptr;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = SpatialData)
	TObjectPtr<const UPCGSpatialData> Difference = nullptr;

	UPROPERTY()
	TObjectPtr<UPCGUnionData> DifferencesUnion = nullptr;

	UPROPERTY(BlueprintSetter = SetDensityFunction, EditAnywhere, Category = Settings)
	EPCGDifferenceDensityFunction DensityFunction = EPCGDifferenceDensityFunction::Minimum;
};