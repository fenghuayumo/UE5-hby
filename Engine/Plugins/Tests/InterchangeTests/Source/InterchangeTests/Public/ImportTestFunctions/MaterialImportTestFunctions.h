// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ImportTestFunctionsBase.h"
#include "MaterialImportTestFunctions.generated.h"

struct FInterchangeTestFunctionResult;


UCLASS()
class INTERCHANGETESTS_API UMaterialImportTestFunctions : public UImportTestFunctionsBase
{
	GENERATED_BODY()

public:

	// UImportTestFunctionsBase interface
	virtual UClass* GetAssociatedAssetType() const override;

	/** Check whether the expected number of materials are imported */
	UFUNCTION(Exec)
	static FInterchangeTestFunctionResult CheckImportedMaterialCount(const TArray<UMaterialInterface*>& Materials, int32 ExpectedNumberOfImportedMaterials);
};
