// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_ResourceAccessorBase.h"

#include "OptimusCoreModule.h"
#include "OptimusResourceDescription.h"
#include "DataInterfaces/DataInterfaceRawBuffer.h"


void UOptimusNode_ResourceAccessorBase::SetResourceDescription(UOptimusResourceDescription* InResourceDesc)
{
	if (!ensure(InResourceDesc))
	{
		return;
	}

	if (!EnumHasAnyFlags(InResourceDesc->DataType->UsageFlags, EOptimusDataTypeUsageFlags::Resource))
	{
		UE_LOG(LogOptimusCore, Error, TEXT("Data type '%s' is not usable in a resource"),
		    *InResourceDesc->DataType->TypeName.ToString());
		return;
	}
	ResourceDesc = InResourceDesc;
}


UOptimusResourceDescription* UOptimusNode_ResourceAccessorBase::GetResourceDescription() const
{
	return ResourceDesc.Get();
}


UOptimusComputeDataInterface* UOptimusNode_ResourceAccessorBase::GetDataInterface(
	UObject* InOuter
	) const
{
	UOptimusResourceDescription* Description = ResourceDesc.Get();
	if (!Description)
	{
		// FIXME: This should be handled as a nullptr and dealt with on the calling side.
		UPersistentBufferDataInterface* DummyInterface = NewObject<UPersistentBufferDataInterface>(InOuter);
		DummyInterface->ValueType = FShaderValueType::Get(EShaderFundamentalType::Float);
		DummyInterface->DataDomain = Optimus::DomainName::Vertex;
		return DummyInterface;
	}
	
	if (!Description->DataInterface)
	{
		Description->DataInterface = NewObject<UPersistentBufferDataInterface>(InOuter);
		Description->DataInterface->ResourceName = Description->ResourceName;
		Description->DataInterface->ValueType = Description->DataType->ShaderValueType;
		Description->DataInterface->DataDomain = Description->DataDomain;
	}
	
	return Description->DataInterface;
}
