// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImportTestFunctions/InterchangeResultImportTestFunctions.h"
#include "InterchangeTestFunction.h"
#include "InterchangeResultsContainer.h"


UClass* UInterchangeResultImportTestFunctions::GetAssociatedAssetType() const
{
	return UInterchangeResultsContainer::StaticClass();
}
