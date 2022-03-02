// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameterMacros.h"
#include "ShaderParameterMetadata.h"

class COMPUTEFRAMEWORK_API FShaderParametersMetadataBuilder
{
public:
	template<typename T>
	void AddParam(
		const TCHAR* Name,
		EShaderPrecisionModifier::Type Precision = EShaderPrecisionModifier::Float
		)
	{
		using TParamTypeInfo = TShaderParameterTypeInfo<T>;

		NextMemberOffset = Align(NextMemberOffset, TParamTypeInfo::Alignment);

		new(Members) FShaderParametersMetadata::FMember(
			Name,
			TEXT(""),
			__LINE__,
			NextMemberOffset,
			TParamTypeInfo::BaseType,
			Precision,
			TParamTypeInfo::NumRows,
			TParamTypeInfo::NumColumns,
			TParamTypeInfo::NumElements,
			TParamTypeInfo::GetStructMetadata()
			);

		NextMemberOffset += sizeof(typename TParamTypeInfo::TAlignedType);
	}

	template<typename T>
	void AddNestedStruct(
		const TCHAR* Name,
		EShaderPrecisionModifier::Type Precision = EShaderPrecisionModifier::Float
		)
	{
		using TParamTypeInfo = TShaderParameterStructTypeInfo<T>;

		NextMemberOffset = Align(NextMemberOffset, TParamTypeInfo::Alignment);

		new(Members) FShaderParametersMetadata::FMember(
			Name,
			TEXT(""),
			__LINE__,
			NextMemberOffset,
			UBMT_NESTED_STRUCT,
			Precision,
			TParamTypeInfo::NumRows,
			TParamTypeInfo::NumColumns,
			TParamTypeInfo::NumElements,
			TParamTypeInfo::GetStructMetadata()
		);

		NextMemberOffset += sizeof(typename TParamTypeInfo::TAlignedType);
	}

	void AddNestedStruct(
		const TCHAR* Name,
		FShaderParametersMetadata* StructMetadata,
		EShaderPrecisionModifier::Type Precision = EShaderPrecisionModifier::Float
		);

	void AddBufferSRV(
		const TCHAR* Name,
		const TCHAR* ShaderType,
		EShaderPrecisionModifier::Type Precision = EShaderPrecisionModifier::Float
		);

	void AddBufferUAV(
		const TCHAR* Name,
		const TCHAR* ShaderType,
		EShaderPrecisionModifier::Type Precision = EShaderPrecisionModifier::Float
		);

	void AddRDGBufferSRV(
		const TCHAR* Name,
		const TCHAR* ShaderType,
		EShaderPrecisionModifier::Type Precision = EShaderPrecisionModifier::Float
		);

	void AddRDGBufferUAV(
		const TCHAR* Name,
		const TCHAR* ShaderType,
		EShaderPrecisionModifier::Type Precision = EShaderPrecisionModifier::Float
		);

	FShaderParametersMetadata* Build(
		FShaderParametersMetadata::EUseCase UseCase,
		const TCHAR* ShaderParameterName
		);

private:
	TArray<FShaderParametersMetadata::FMember> Members;
	uint32 NextMemberOffset = 0;
};