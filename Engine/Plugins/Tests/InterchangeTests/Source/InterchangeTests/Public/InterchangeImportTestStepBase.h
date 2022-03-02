// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "InterchangeImportTestData.h"
#include "InterchangeManager.h"
#include "InterchangeImportTestStepBase.generated.h"

class FAutomationTestExecutionInfo;
struct FInterchangeTestFunction;


struct FTestStepResults
{
	bool bTestStepSuccess = true;
	bool bTriggerGC = false;
};


UCLASS(BlueprintType, EditInlineNew, Abstract, autoExpandCategories = (General, Test))
class INTERCHANGETESTS_API UInterchangeImportTestStepBase : public UObject
{
	GENERATED_BODY()

public:
	/** An array of results to check against */
	UPROPERTY(EditAnywhere, Category = General)
	TArray<FInterchangeTestFunction> Tests;

public:
	virtual UE::Interchange::FAssetImportResultPtr StartStep(const FInterchangeImportTestData& Data) PURE_VIRTUAL(UInterchangeImportTestStepBase::StartStep, return {}; );
	virtual FTestStepResults FinishStep(FInterchangeImportTestData& Data, FAutomationTestExecutionInfo& ExecutionInfo) PURE_VIRTUAL(UInterchangeImportTestStepBase::FinishStep, return {}; );
	virtual FString GetContextString() const PURE_VIRTUAL(UInterchangeImportTestStepBase::GetContextString, return {}; );

	bool PerformTests(FInterchangeImportTestData& Data, FAutomationTestExecutionInfo& ExecutionInfo);
};
