// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSpatialData.h"

#include "PCGVolumeData.generated.h"

class AVolume;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGVolumeData : public UPCGSpatialDataWithPointCache
{
	GENERATED_BODY()

public:
	void Initialize(AVolume* InVolume, AActor* InTargetActor = nullptr);
	void Initialize(const FBox& InBounds, AActor* InTargetActor);

	// ~Begin UPGCSpatialData interface
	virtual int GetDimension() const override { return 3; }
	virtual FBox GetBounds() const override;
	virtual FBox GetStrictBounds() const override;
	virtual float GetDensityAtPosition(const FVector& InPosition) const override;
	// ~End UPGCSpatialData interface

	// ~Begin UPCGSpatialDataWithPointCache interface
	virtual const UPCGPointData* CreatePointData() const override;
	// ~End UPCGSpatialDataWithPointCache interface

protected:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = SourceData)
	TObjectPtr<AVolume> Volume = nullptr;

	UPROPERTY()
	FBox Bounds = FBox(EForceInit::ForceInit);

	UPROPERTY()
	FBox StrictBounds = FBox(EForceInit::ForceInit);
};