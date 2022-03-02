// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

class UPCGGraph;

#include "PCGSubgraph.generated.h"

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPCGStructuralSettingsChanged, UPCGSettings*);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPCGNodeStructuralSettingsChanged, UPCGNode*);
#endif

UCLASS(Abstract)
class UPCGBaseSubgraphSettings : public UPCGSettings
{
	GENERATED_BODY()
public:
	virtual UPCGGraph* GetSubgraph() const { return nullptr; }

protected:
	//~Begin UObject interface implementation
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	//~End UObject interface implementation

	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual void GetTrackedActorTags(FPCGTagToSettingsMap& OutTagToSettings) const override;
#endif
	//~End UPCGSettings interface

	void OnSubgraphChanged(UPCGGraph* InGraph, bool bIsStructural);

	virtual bool IsStructuralProperty(const FName& InPropertyName) const { return false; }
#endif

public:
#if WITH_EDITOR
	FOnPCGStructuralSettingsChanged OnStructuralSettingsChangedDelegate;
#endif
};

UCLASS(BlueprintType, ClassGroup=(Procedural))
class UPCGSubgraphSettings : public UPCGBaseSubgraphSettings
{
	GENERATED_BODY()

public:
	//~UPCGSettings interface implementation
	virtual UPCGNode* CreateNode() const override;

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("SubgraphNode")); }
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface implementation

	//~Begin UPCGBaseSubgraphSettings interface
public:
	virtual UPCGGraph* GetSubgraph() const override { return Subgraph; }
protected:
#if WITH_EDITOR
	virtual bool IsStructuralProperty(const FName& InPropertyName) const override;
#endif
	//~End UPCGBaseSubgraphSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TObjectPtr<UPCGGraph> Subgraph;
};

UCLASS(Abstract)
class UPCGBaseSubgraphNode : public UPCGNode
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bDynamicGraph = false;

	virtual TObjectPtr<UPCGGraph> GetSubgraph() const { return nullptr; }
};

UCLASS(ClassGroup = (Procedural))
class UPCGSubgraphNode : public UPCGBaseSubgraphNode
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	FOnPCGNodeStructuralSettingsChanged OnNodeStructuralSettingsChangedDelegate;
#endif

	/** ~Begin UPCGBaseSubgraphNode interface */
	TObjectPtr<UPCGGraph> GetSubgraph() const override;
	/** ~End UPCGBaseSubgraphNode interface */

protected:	
	/** ~Begin UObject interface */
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	/** ~End UObject interface */

#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	void OnStructuralSettingsChanged(UPCGSettings* InSettings);
#endif
};

struct FPCGSubgraphContext : public FPCGContext
{
	FPCGTaskId SubgraphTaskId = InvalidTaskId;
	bool bScheduledSubgraph = false;
};

class FPCGSubgraphElement : public IPCGElement
{
public:
	virtual FPCGContextPtr Initialize(const FPCGDataCollection& InputData, UPCGComponent* SourceComponent) override;

protected:
	virtual bool ExecuteInternal(FPCGContextPtr Context) const override;
};

class FPCGInputForwardingElement : public FSimplePCGElement
{
public:
	FPCGInputForwardingElement(const FPCGDataCollection& InputToForward);

protected:
	virtual bool ExecuteInternal(FPCGContextPtr Context) const override;
	FPCGDataCollection Input;
};