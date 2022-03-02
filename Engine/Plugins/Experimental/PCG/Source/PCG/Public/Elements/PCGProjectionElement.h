// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGElement.h"
#include "PCGSettings.h"

#include "PCGProjectionElement.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGProjectionSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("ProjectionNode")); }
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient, BlueprintReadWrite, EditAnywhere, Category = Debug)
	bool bKeepZeroDensityPoints = false;
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface
};

class FPCGProjectionElement : public FSimpleTypedPCGElement<UPCGProjectionSettings>
{
protected:
	virtual bool ExecuteInternal(FPCGContextPtr Context) const;
};