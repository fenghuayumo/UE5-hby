// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GLTFAsset.h"
#include "InterchangeTranslatorBase.h"
#include "Mesh/InterchangeStaticMeshPayload.h"
#include "Mesh/InterchangeStaticMeshPayloadInterface.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Texture/InterchangeTexturePayloadInterface.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeGltfTranslator.generated.h"

class UInterchangeShaderGraphNode;
class UInterchangeShaderNode;

/* Gltf translator class support import of texture, material, static mesh, skeletal mesh, */

UCLASS(BlueprintType, Experimental)
class UInterchangeGltfTranslator : public UInterchangeTranslatorBase,
	public IInterchangeStaticMeshPayloadInterface, public IInterchangeTexturePayloadInterface
{
	GENERATED_BODY()

public:
	/** Begin UInterchangeTranslatorBase API*/
	virtual EInterchangeTranslatorType GetTranslatorType() const override;
	virtual TArray<FString> GetSupportedFormats() const override;
	virtual bool Translate( UInterchangeBaseNodeContainer& BaseNodeContainer ) const override;
	/** End UInterchangeTranslatorBase API*/

	/* IInterchangeStaticMeshPayloadInterface Begin */

	virtual TFuture< TOptional< UE::Interchange::FStaticMeshPayloadData > > GetStaticMeshPayloadData( const FString& PayLoadKey ) const override;

	/* IInterchangeStaticMeshPayloadInterface End */

	/* IInterchangeTexturePayloadInterface Begin */

	virtual TOptional< UE::Interchange::FImportImage > GetTexturePayloadData( const UInterchangeSourceData* InSourceData, const FString& PayLoadKey ) const override;

	/* IInterchangeTexturePayloadInterface End */

protected:
	void HandleGltfNode( UInterchangeBaseNodeContainer& NodeContainer, const GLTF::FNode& GltfNode, const FString& ParentNodeUid, const int32 NodeIndex ) const;
	void HandleGltfMaterial( UInterchangeBaseNodeContainer& NodeContainer, const GLTF::FMaterial& GltfMaterial, UInterchangeShaderGraphNode& ShaderGraphNode ) const;
	void HandleGltfMaterialParameter( UInterchangeBaseNodeContainer& NodeContainer, const GLTF::FTextureMap& TextureMap, UInterchangeShaderNode& ShaderNode,
		const FString& MapName, const TVariant< FLinearColor, float >& MapFactor, const FString& OutputChannel, const bool bInverse = false ) const;

	/** Support for KHR_materials_clearcoat */
	void HandleGltfClearCoat( UInterchangeBaseNodeContainer& NodeContainer, const GLTF::FMaterial& GltfMaterial, UInterchangeShaderGraphNode& ShaderGraphNode ) const;
	/** Support for KHR_materials_transmission */
	void HandleGltfTransmission( UInterchangeBaseNodeContainer& NodeContainer, const GLTF::FMaterial& GltfMaterial, UInterchangeShaderGraphNode& ShaderGraphNode ) const;

private:
	GLTF::FAsset GltfAsset;
};


