// Copyright Epic Games, Inc. All Rights Reserved. 

#include "InterchangeGenericTexturePipeline.h"

#include "Engine/Texture.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureLightProfile.h"
#include "InterchangePipelineLog.h"
#include "InterchangeTexture2DArrayFactoryNode.h"
#include "InterchangeTexture2DArrayFactoryNode.h"
#include "InterchangeTexture2DArrayNode.h"
#include "InterchangeTexture2DArrayNode.h"
#include "InterchangeTexture2DFactoryNode.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeTextureCubeFactoryNode.h"
#include "InterchangeTextureCubeFactoryNode.h"
#include "InterchangeTextureCubeNode.h"
#include "InterchangeTextureCubeNode.h"
#include "InterchangeTextureFactoryNode.h"
#include "InterchangeTextureLightProfileFactoryNode.h"
#include "InterchangeTextureLightProfileFactoryNode.h"
#include "InterchangeTextureLightProfileNode.h"
#include "InterchangeTextureLightProfileNode.h"
#include "InterchangeTextureNode.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "NormalMapIdentification.h"
#include "TextureCompiler.h"
#include "UDIMUtilities.h"
#endif //WITH_EDITOR

namespace UE::Interchange::Private
{
	UClass* GetDefaultFactoryClassFromTextureNodeClass(UClass* NodeClass)
	{
		if (UInterchangeTexture2DNode::StaticClass() == NodeClass)
		{
			return UInterchangeTexture2DFactoryNode::StaticClass();
		}

		if (UInterchangeTextureCubeNode::StaticClass() == NodeClass)
		{
			return UInterchangeTextureCubeFactoryNode::StaticClass();
		}

		if (UInterchangeTexture2DArrayNode::StaticClass() == NodeClass)
		{
			return UInterchangeTexture2DArrayFactoryNode::StaticClass();
		}

		if (UInterchangeTextureLightProfileNode::StaticClass() == NodeClass)
		{
			return UInterchangeTextureLightProfileFactoryNode::StaticClass();
		}

		return nullptr;
	}

#if WITH_EDITOR
	void AdjustTextureForNormalMap(UTexture* Texture, bool bFlipNormalMapGreenChannel)
	{
		if (Texture)
		{
			Texture->PreEditChange(nullptr);
			if (UE::NormalMapIdentification::HandleAssetPostImport(Texture))
			{
				if (bFlipNormalMapGreenChannel)
				{
					Texture->bFlipGreenChannel = true;
				}
			}
			Texture->PostEditChange();
		}
	}
#endif
}

void UInterchangeGenericTexturePipeline::ExecutePreImportPipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas)
{
	if (!InBaseNodeContainer)
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("UInterchangeGenericTexturePipeline: Cannot execute pre-import pipeline because InBaseNodeContrainer is null"));
		return;
	}

	BaseNodeContainer = InBaseNodeContainer;
	SourceDatas.Empty(InSourceDatas.Num());
	for (const UInterchangeSourceData* SourceData : InSourceDatas)
	{
		SourceDatas.Add(SourceData);
	}
	
	//Find all translated node we need for this pipeline
	BaseNodeContainer->IterateNodes([this](const FString& NodeUid, UInterchangeBaseNode* Node)
	{
		switch(Node->GetNodeContainerType())
		{
			case EInterchangeNodeContainerType::TranslatedAsset:
			{
				if (UInterchangeTextureNode* TextureNode = Cast<UInterchangeTextureNode>(Node))
				{
					TextureNodes.Add(TextureNode);
				}
			}
			break;
		}
	});

	if (bImportTextures)
	{
		for (const UInterchangeTextureNode* TextureNode : TextureNodes)
		{
			HandleCreationOfTextureFactoryNode(TextureNode);
		}
	}
}

void UInterchangeGenericTexturePipeline::ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* InBaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport)
{
	//We do not use the provided base container since ExecutePreImportPipeline cache it
	//We just make sure the same one is pass in parameter
	if (!InBaseNodeContainer || !ensure(BaseNodeContainer == InBaseNodeContainer) || !CreatedAsset)
	{
		return;
	}

	UInterchangeBaseNode* Node = BaseNodeContainer->GetNode(NodeKey);
	if (!Node)
	{
		return;
	}

	PostImportTextureAssetImport(CreatedAsset, bIsAReimport);
}

UInterchangeTextureFactoryNode* UInterchangeGenericTexturePipeline::HandleCreationOfTextureFactoryNode(const UInterchangeTextureNode* TextureNode)
{
	UClass* FactoryClass = UE::Interchange::Private::GetDefaultFactoryClassFromTextureNodeClass(TextureNode->GetClass());

	TOptional<FString> SourceFile = TextureNode->GetPayLoadKey();
#if WITH_EDITORONLY_DATA
	if (FactoryClass == UInterchangeTexture2DFactoryNode::StaticClass())
	{ 
		if (SourceFile)
		{
			const FString Extension = FPaths::GetExtension(SourceFile.GetValue()).ToLower();
			if (FileExtensionsToImportAsLongLatCubemap.Contains(Extension))
			{
				FactoryClass = UInterchangeTextureCubeFactoryNode::StaticClass();
			}
		}
	}
#endif

	UInterchangeTextureFactoryNode* InterchangeTextureFactoryNode =  CreateTextureFactoryNode(TextureNode, FactoryClass);

	if (FactoryClass == UInterchangeTexture2DFactoryNode::StaticClass() && InterchangeTextureFactoryNode)
	{
		// Forward the UDIM from the translator to the factory node
		TMap<int32, FString> SourceBlocks;
		UInterchangeTexture2DFactoryNode* Texture2DFactoryNode = static_cast<UInterchangeTexture2DFactoryNode*>(InterchangeTextureFactoryNode);
		if (const UInterchangeTexture2DNode* Texture2DNode = Cast<UInterchangeTexture2DNode>(TextureNode))
		{
			SourceBlocks = Texture2DNode->GetSourceBlocks();
		}

#if WITH_EDITOR
		if (SourceBlocks.IsEmpty() && bImportUDIMs && SourceFile)
		{
			FString PrettyAssetName;
			SourceBlocks = UE::TextureUtilitiesCommon::GetUDIMBlocksFromSourceFile(SourceFile.GetValue(), UE::TextureUtilitiesCommon::DefaultUdimRegexPattern, &PrettyAssetName);
			if (!PrettyAssetName.IsEmpty())
			{
				InterchangeTextureFactoryNode->SetAssetName(PrettyAssetName);
			}
		}
#endif

		if (!SourceBlocks.IsEmpty())
		{
			Texture2DFactoryNode->SetSourceBlocks(MoveTemp(SourceBlocks));
		}
	}

	return InterchangeTextureFactoryNode;
}

UInterchangeTextureFactoryNode* UInterchangeGenericTexturePipeline::CreateTextureFactoryNode(const UInterchangeTextureNode* TextureNode, const TSubclassOf<UInterchangeTextureFactoryNode>& FactorySubclass)
{
	FString DisplayLabel = TextureNode->GetDisplayLabel();
	FString NodeUid = UInterchangeTextureFactoryNode::GetTextureFactoryNodeUidFromTextureNodeUid(TextureNode->GetUniqueID());
	UInterchangeTextureFactoryNode* TextureFactoryNode = nullptr;
	if (BaseNodeContainer->IsNodeUidValid(NodeUid))
	{
		TextureFactoryNode = Cast<UInterchangeTextureFactoryNode>(BaseNodeContainer->GetNode(NodeUid));
		if (!ensure(TextureFactoryNode))
		{
			//Log an error
		}
	}
	else
	{
		UClass* FactoryClass = FactorySubclass.Get();
		if (!ensure(FactoryClass))
		{
			// Log an error
			return nullptr;
		}

		TextureFactoryNode = NewObject<UInterchangeTextureFactoryNode>(BaseNodeContainer, FactoryClass);
		if (!ensure(TextureFactoryNode))
		{
			return nullptr;
		}
		//Creating a Texture
		TextureFactoryNode->InitializeTextureNode(NodeUid, DisplayLabel, TextureNode->GetDisplayLabel());
		TextureFactoryNode->SetCustomTranslatedTextureNodeUid(TextureNode->GetUniqueID());
		BaseNodeContainer->AddNode(TextureFactoryNode);
		TextureFactoryNodes.Add(TextureFactoryNode);

		TextureFactoryNode->AddTargetNodeUid(TextureNode->GetUniqueID());
		TextureNode->AddTargetNodeUid(TextureFactoryNode->GetUniqueID());
	}
	return TextureFactoryNode;
}

void UInterchangeGenericTexturePipeline::PostImportTextureAssetImport(UObject* CreatedAsset, bool bIsAReimport)
{
#if WITH_EDITOR
	if (!bIsAReimport && bDetectNormalMapTexture)
	{
		// Verify if the texture is a normal map
		if (UTexture* Texture = Cast<UTexture>(CreatedAsset))
		{
			if (!Texture->IsNormalMap())
			{
				// This can create 2 build of the texture (we should revisit this at some point)
				if (FTextureCompilingManager::Get().IsCompilingTexture(Texture))
				{
					TWeakObjectPtr<UTexture> WeakTexturePtr = Texture;
					TSharedRef<FDelegateHandle> HandlePtr = MakeShared<FDelegateHandle>();
					HandlePtr.Get() = FTextureCompilingManager::Get().OnTexturePostCompileEvent().AddLambda([this, WeakTexturePtr, HandlePtr](const TArrayView<UTexture* const>&)
						{
							if (UTexture* TextureToTest = WeakTexturePtr.Get())
							{
								if (FTextureCompilingManager::Get().IsCompilingTexture(TextureToTest))
								{
									return;
								}

								UE::Interchange::Private::AdjustTextureForNormalMap(TextureToTest, bFlipNormalMapGreenChannel);
							}

							FTextureCompilingManager::Get().OnTexturePostCompileEvent().Remove(HandlePtr.Get());
						});
				}
				else
				{
					UE::Interchange::Private::AdjustTextureForNormalMap(Texture, bFlipNormalMapGreenChannel);
				}
			}
		}
	}
#endif //WITH_EDITOR
}

