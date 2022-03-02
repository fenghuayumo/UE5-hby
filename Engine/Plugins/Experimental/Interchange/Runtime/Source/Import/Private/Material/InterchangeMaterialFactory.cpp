// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Material/InterchangeMaterialFactory.h"

#include "InterchangeImportCommon.h"
#include "InterchangeImportLog.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeTextureNode.h"
#include "InterchangeTextureFactoryNode.h"
#include "InterchangeShaderGraphNode.h"
#include "InterchangeSourceData.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionClearCoatNormalCustomOutput.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionThinTranslucentMaterialOutput.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"


#if WITH_EDITORONLY_DATA
#include "EditorFramework/AssetImportData.h"
#endif //WITH_EDITORONLY_DATA

#if WITH_EDITOR
#include "MaterialEditingLibrary.h"
#endif //WITH_EDITOR

#define LOCTEXT_NAMESPACE "InterchangeMaterialFactory"

namespace UE
{
	namespace Interchange
	{
		namespace MaterialFactory
		{
			namespace Internal
			{
#if WITH_EDITOR
				/**
				 * Finds a UMaterialExpression class by name.
				 * @param ClassName		The name of the class to look for (ie:UClass*->GetName()).
				 * @return				A sub class of UMaterialExpression or nullptr.
				 */
				TSubclassOf<UMaterialExpression> FindExpressionClass(const TCHAR* ClassName)
				{
					check(ClassName);

					UObject* const ClassPackage = ANY_PACKAGE;
					UClass* MaterialExpressionClass = FindObject<UClass>(ClassPackage, ClassName);

					if (!MaterialExpressionClass)
					{
						if (UObjectRedirector* RenamedClassRedirector = FindObject<UObjectRedirector>(ClassPackage, ClassName))
						{
							MaterialExpressionClass = CastChecked<UClass>(RenamedClassRedirector->DestinationObject);
						}
					}

					if (MaterialExpressionClass && MaterialExpressionClass->IsChildOf<UMaterialExpression>())
					{
						return MaterialExpressionClass;
					}
					else
					{
						return nullptr;
					}
				}

				int32 GetInputIndex(UMaterialExpression& MaterialExpression, const FString& InputName)
				{
					int32 ExpressionInputIndex = 0;

					for (const FExpressionInput* ExpressionInput : MaterialExpression.GetInputs())
					{
						// MaterialFuncCall appends the type to the input name when calling GetInputName
						// and the InputName in FExpressionInput is optional so we'll check both here to be safe
						if (MaterialExpression.GetInputName(ExpressionInputIndex) == *InputName ||
							(ExpressionInput && ExpressionInput->InputName == *InputName))
						{
							return ExpressionInputIndex;
						}

						++ExpressionInputIndex;
					}

					return INDEX_NONE;
				}

				int32 GetOutputIndex(UMaterialExpression& MaterialExpression, const FString& OutputName)
				{
					int32 ExpressionOutputIndex = 0;

					for (const FExpressionOutput& ExpressionOutput : MaterialExpression.GetOutputs())
					{
						if (ExpressionOutput.OutputName == *OutputName)
						{
							return ExpressionOutputIndex;
						}

						++ExpressionOutputIndex;
					}

					return 0; // Consider 0 as the default output to connect to since most expressions have a single output
				}

				void SetupFunctionCallExpression(UMaterial* Material, const UInterchangeFactoryBase::FCreateAssetParams& Arguments, UMaterialExpressionMaterialFunctionCall* FunctionCallExpression)
				{
					FunctionCallExpression->UpdateFromFunctionResource();
				}

				void SetupTextureExpression(const UInterchangeFactoryBase::FCreateAssetParams& Arguments, const UInterchangeMaterialExpressionFactoryNode* ExpressionNode, UMaterialExpressionTextureBase* TextureExpression)
				{
					using namespace UE::Interchange::Materials::Standard::Nodes::TextureSample;

					FString TextureFactoryNodeUid;
					ExpressionNode->GetStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Inputs::Texture.ToString()), TextureFactoryNodeUid);

					if (const UInterchangeTextureFactoryNode* TextureFactoryNode = Cast<UInterchangeTextureFactoryNode>(Arguments.NodeContainer->GetNode(TextureFactoryNodeUid)))
					{
						if (UTexture* Texture = Cast<UTexture>(TextureFactoryNode->ReferenceObject.TryLoad()))
						{
							TextureExpression->Texture = Texture;
						}
					}

					TextureExpression->AutoSetSampleType();
				}

				UMaterialExpression* CreateMaterialExpression(UMaterial* Material, const TSubclassOf<UMaterialExpression>& ExpressionClass)
				{
					UMaterialFunction* const MaterialFunction = nullptr;
					UObject* const SelectedAsset = nullptr;
					const int32 NodePosX = 0;
					const int32 NodePosY = 0;
					const bool bAllowMarkingPackageDirty = false;

					return UMaterialEditingLibrary::CreateMaterialExpressionEx(Material, MaterialFunction, ExpressionClass, SelectedAsset, NodePosX, NodePosY, bAllowMarkingPackageDirty);
				}
#endif // #if WITH_EDITOR
			}
		}
	}
}

UClass* UInterchangeMaterialFactory::GetFactoryClass() const
{
	return UMaterialInterface::StaticClass();
}

UObject* UInterchangeMaterialFactory::CreateEmptyAsset(const FCreateAssetParams& Arguments)
{
	UObject* Material = nullptr;

	if (!Arguments.AssetNode || !Arguments.AssetNode->GetObjectClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	const UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode = Cast<UInterchangeBaseMaterialFactoryNode>(Arguments.AssetNode);
	if (MaterialFactoryNode == nullptr)
	{
		return nullptr;
	}

	const UClass* MaterialClass = MaterialFactoryNode->GetObjectClass();
	if (!ensure(MaterialClass && MaterialClass->IsChildOf(GetFactoryClass())))
	{
		return nullptr;
	}

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	// create a new material or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		if (MaterialClass->IsChildOf<UMaterialInstanceDynamic>())
		{
			if (const UInterchangeMaterialInstanceFactoryNode* MaterialInstanceFactoryNode = Cast<UInterchangeMaterialInstanceFactoryNode>(MaterialFactoryNode))
			{
				FString ParentPath;
				if (MaterialInstanceFactoryNode->GetCustomParent(ParentPath))
				{
					FSoftObjectPath ParentMaterial(ParentPath);
					Material = UMaterialInstanceDynamic::Create(Cast<UMaterialInterface>(ParentMaterial.TryLoad()), Arguments.Parent);
				}
			}
		}
		else
		{
			Material = NewObject<UObject>(Arguments.Parent, MaterialClass, *Arguments.AssetName, RF_Public | RF_Standalone);
		}
	}
	else if (ExistingAsset->GetClass()->IsChildOf(MaterialClass))
	{
		//This is a reimport, we are just re-updating the source data
		Material = ExistingAsset;
	}

	if (!Material)
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("Could not create Material asset %s"), *Arguments.AssetName);
		return nullptr;
	}

#if WITH_EDITOR
	Material->PreEditChange(nullptr);

	if (UMaterialInstanceConstant* MaterialInstanceConstant = Cast<UMaterialInstanceConstant>(Material))
	{
		if (const UInterchangeMaterialInstanceFactoryNode* MaterialInstanceFactoryNode = Cast<UInterchangeMaterialInstanceFactoryNode>(MaterialFactoryNode))
		{
			FString ParentPath;
			if (MaterialInstanceFactoryNode->GetCustomParent(ParentPath))
			{
				FSoftObjectPath ParentMaterial(ParentPath);
				MaterialInstanceConstant->SetParentEditorOnly(Cast<UMaterialInterface>(ParentMaterial.TryLoad()));
			}
		}
	}
#endif //WITH_EDITOR

	return Material;
}

UObject* UInterchangeMaterialFactory::CreateAsset(const FCreateAssetParams& Arguments)
{
	if (!Arguments.AssetNode || !Arguments.AssetNode->GetObjectClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	const UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode = Cast<UInterchangeBaseMaterialFactoryNode>(Arguments.AssetNode);
	if (MaterialFactoryNode == nullptr)
	{
		return nullptr;
	}

	const UClass* MaterialClass = MaterialFactoryNode->GetObjectClass();
	check(MaterialClass && MaterialClass->IsChildOf(GetFactoryClass()));

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	UObject* MaterialObject = nullptr;
	// create a new material or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		//NewObject is not thread safe, the asset registry directory watcher tick on the main thread can trig before we finish initializing the UObject and will crash
		//The UObject should have been created by calling CreateEmptyAsset on the main thread.
		check(IsInGameThread());
		MaterialObject = NewObject<UObject>(Arguments.Parent, MaterialClass, *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else if(ExistingAsset->GetClass()->IsChildOf(MaterialClass))
	{
		//This is a reimport, we are just re-updating the source data
		MaterialObject = ExistingAsset;
	}

	if (!MaterialObject)
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("Could not create Material asset %s"), *Arguments.AssetName);
		return nullptr;
	}

	if (MaterialObject)
	{
		//Currently material re-import will not touch the material at all
		//TODO design a re-import process for the material (expressions and input connections)
		if(!Arguments.ReimportObject)
		{
			if (UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(MaterialObject))
			{
#if WITH_EDITOR
				if (UMaterial* Material = Cast<UMaterial>(MaterialObject))
				{
					SetupMaterial(Material, Arguments, MaterialFactoryNode);
				}
#endif // #if WITH_EDITOR

				MaterialFactoryNode->ApplyAllCustomAttributeToObject(MaterialObject);
			}
		}
		
		//Getting the file Hash will cache it into the source data
		Arguments.SourceData->GetFileContentHash();

		//The interchange completion task (call in the GameThread after the factories pass), will call PostEditChange which will trig another asynchronous system that will build all material in parallel
	}

	return MaterialObject;
}

/* This function is call in the completion task on the main thread, use it to call main thread post creation step for your assets*/
void UInterchangeMaterialFactory::PreImportPreCompletedCallback(const FImportPreCompletedCallbackParams& Arguments)
{
	check(IsInGameThread());
	Super::PreImportPreCompletedCallback(Arguments);

	//TODO make sure this work at runtime
#if WITH_EDITORONLY_DATA
	if (ensure(Arguments.ImportedObject && Arguments.SourceData))
	{
		//We must call the Update of the asset source file in the main thread because UAssetImportData::Update execute some delegate we do not control
		UMaterialInterface* ImportedMaterialInterface = CastChecked<UMaterialInterface>(Arguments.ImportedObject);

		//Update the samplers type in case the textures were changed during their PreImportPreCompletedCallback
		if (UMaterial* ImportedMaterial = Cast<UMaterial>(ImportedMaterialInterface))
		{
			for (UMaterialExpression* Expression : ImportedMaterial->Expressions)
			{
				if (UMaterialExpressionTextureBase* TextureSample = Cast<UMaterialExpressionTextureBase>(Expression))
				{
					TextureSample->AutoSetSampleType();
				}
			}
		}
		else if (UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(ImportedMaterialInterface))
		{
			// Material instances expect their parameters to only be updated from the game thread
			SetupMaterialInstance(MaterialInstance, Arguments.NodeContainer, Cast<UInterchangeBaseMaterialFactoryNode>(Arguments.FactoryNode));
		}

		UE::Interchange::FFactoryCommon::FUpdateImportAssetDataParameters UpdateImportAssetDataParameters(ImportedMaterialInterface
																										  , ImportedMaterialInterface->AssetImportData
																										  , Arguments.SourceData
																										  , Arguments.NodeUniqueID
																										  , Arguments.NodeContainer
																										  , Arguments.Pipelines);

		ImportedMaterialInterface->AssetImportData = UE::Interchange::FFactoryCommon::UpdateImportAssetData(UpdateImportAssetDataParameters);
	}
#endif
}

#if WITH_EDITOR
void UInterchangeMaterialFactory::SetupMaterial(UMaterial* Material, const FCreateAssetParams& Arguments, const UInterchangeBaseMaterialFactoryNode* BaseMaterialFactoryNode)
{
	using namespace UE::Interchange::MaterialFactory::Internal;

	const UInterchangeMaterialFactoryNode* MaterialFactoryNode = Cast<UInterchangeMaterialFactoryNode>(BaseMaterialFactoryNode);

	TMap<FString, UMaterialExpression*> Expressions;

	// Base Color
	{
		FString BaseColorUid;
		FString OutputName;

		if (MaterialFactoryNode->GetBaseColorConnection(BaseColorUid, OutputName))
		{
			const UInterchangeMaterialExpressionFactoryNode* BaseColor = Cast<UInterchangeMaterialExpressionFactoryNode>(Arguments.NodeContainer->GetNode(BaseColorUid));

			if (BaseColor)
			{
				if (UMaterialExpression* BaseColorExpression = CreateExpressionsForNode(Material, Arguments, BaseColor, Expressions))
				{
					if (FExpressionInput* BaseColorInput = Material->GetExpressionInputForProperty(MP_BaseColor))
					{
						BaseColorExpression->ConnectExpression(BaseColorInput, GetOutputIndex(*BaseColorExpression, OutputName));
					}
				}
			}
		}
	}
	
	// Metallic
	{
		FString MetallicUid;
		FString OutputName;

		if (MaterialFactoryNode->GetMetallicConnection(MetallicUid, OutputName))
		{
			const UInterchangeMaterialExpressionFactoryNode* MetallicNode = Cast<UInterchangeMaterialExpressionFactoryNode>(Arguments.NodeContainer->GetNode(MetallicUid));

			if (MetallicNode)
			{
				if (UMaterialExpression* MetallicExpression = CreateExpressionsForNode(Material, Arguments, MetallicNode, Expressions))
				{
					if (FExpressionInput* MetallicInput = Material->GetExpressionInputForProperty(MP_Metallic))
					{
						MetallicExpression->ConnectExpression(MetallicInput, GetOutputIndex(*MetallicExpression, OutputName));
					}
				}
			}
		}
	}

	// Specular
	{
		FString SpecularUid;
		FString OutputName;
		
		if (MaterialFactoryNode->GetSpecularConnection(SpecularUid, OutputName))
		{
			const UInterchangeMaterialExpressionFactoryNode* SpecularNode = Cast<UInterchangeMaterialExpressionFactoryNode>(Arguments.NodeContainer->GetNode(SpecularUid));

			if (SpecularNode)
			{
				if (UMaterialExpression* SpecularExpression = CreateExpressionsForNode(Material, Arguments, SpecularNode, Expressions))
				{
					if (FExpressionInput* SpecularInput = Material->GetExpressionInputForProperty(MP_Specular))
					{
						SpecularExpression->ConnectExpression(SpecularInput, GetOutputIndex(*SpecularExpression, OutputName));
					}
				}
			}
		}
	}

	// Roughness
	{
		FString RoughnessUid;
		FString OutputName;

		if (MaterialFactoryNode->GetRoughnessConnection(RoughnessUid, OutputName))
		{
			const UInterchangeMaterialExpressionFactoryNode* Roughness = Cast<UInterchangeMaterialExpressionFactoryNode>(Arguments.NodeContainer->GetNode(RoughnessUid));

			if (Roughness)
			{
				if (UMaterialExpression* RoughnessExpression = CreateExpressionsForNode(Material, Arguments, Roughness, Expressions))
				{
					if (FExpressionInput* RoughnessInput = Material->GetExpressionInputForProperty(MP_Roughness))
					{
						RoughnessExpression->ConnectExpression(RoughnessInput, GetOutputIndex(*RoughnessExpression, OutputName));
					}
				}
			}
		}
	}

	// Emissive
	{
		FString EmissiveColorUid;
		FString OutputName;
		
		if (MaterialFactoryNode->GetEmissiveColorConnection(EmissiveColorUid, OutputName))
		{
			const UInterchangeMaterialExpressionFactoryNode* EmissiveColor = Cast<UInterchangeMaterialExpressionFactoryNode>(Arguments.NodeContainer->GetNode(EmissiveColorUid));

			if (EmissiveColor)
			{
				if (UMaterialExpression* EmissiveExpression = CreateExpressionsForNode(Material, Arguments, EmissiveColor, Expressions))
				{
					if (FExpressionInput* EmissiveInput = Material->GetExpressionInputForProperty(MP_EmissiveColor))
					{
						EmissiveExpression->ConnectExpression(EmissiveInput, GetOutputIndex(*EmissiveExpression, OutputName));
					}
				}
			}
		}
	}
	
	// Normal
	{
		FString NormalUid;
		FString OutputName;
		
		if (MaterialFactoryNode->GetNormalConnection(NormalUid, OutputName))
		{
			const UInterchangeMaterialExpressionFactoryNode* NormalNode = Cast<UInterchangeMaterialExpressionFactoryNode>(Arguments.NodeContainer->GetNode(NormalUid));

			if (NormalNode)
			{
				if (UMaterialExpression* NormalExpression = CreateExpressionsForNode(Material, Arguments, NormalNode, Expressions))
				{
					if (FExpressionInput* NormalInput = Material->GetExpressionInputForProperty(MP_Normal))
					{
						NormalExpression->ConnectExpression(NormalInput, GetOutputIndex(*NormalExpression, OutputName));
					}
				}
			}
		}
	}

	// Opacity
	{
		FString OpacityUid;
		FString OutputName;

		if (MaterialFactoryNode->GetOpacityConnection(OpacityUid, OutputName))
		{
			const UInterchangeMaterialExpressionFactoryNode* OpacityNode = Cast<UInterchangeMaterialExpressionFactoryNode>(Arguments.NodeContainer->GetNode(OpacityUid));

			if (OpacityNode)
			{
				if (UMaterialExpression* OpacityExpression = CreateExpressionsForNode(Material, Arguments, OpacityNode, Expressions))
				{
					if (FExpressionInput* OpacityInput = Material->GetExpressionInputForProperty(MP_Opacity))
					{
						OpacityExpression->ConnectExpression(OpacityInput, GetOutputIndex(*OpacityExpression, OutputName));
					}
				}
			}
		}
	}

	// Occlusion
	{
		FString OcclusionUid;
		FString OutputName;

		if (MaterialFactoryNode->GetOcclusionConnection(OcclusionUid, OutputName))
		{
			const UInterchangeMaterialExpressionFactoryNode* OcclusionNode = Cast<UInterchangeMaterialExpressionFactoryNode>(Arguments.NodeContainer->GetNode(OcclusionUid));

			if (OcclusionNode)
			{
				if (UMaterialExpression* OcclusionExpression = CreateExpressionsForNode(Material, Arguments, OcclusionNode, Expressions))
				{
					if (FExpressionInput* OcclusionInput = Material->GetExpressionInputForProperty(MP_AmbientOcclusion))
					{
						OcclusionExpression->ConnectExpression(OcclusionInput, GetOutputIndex(*OcclusionExpression, OutputName));
					}
				}
			}
		}
	}

	// Refraction
	{
		FString RefractionUid;
		FString OutputName;

		if (MaterialFactoryNode->GetRefractionConnection(RefractionUid, OutputName))
		{
			const UInterchangeMaterialExpressionFactoryNode* RefractionNode = Cast<UInterchangeMaterialExpressionFactoryNode>(Arguments.NodeContainer->GetNode(RefractionUid));

			if (RefractionNode)
			{
				if (UMaterialExpression* RefractionExpression = CreateExpressionsForNode(Material, Arguments, RefractionNode, Expressions))
				{
					if (FExpressionInput* RefractionInput = Material->GetExpressionInputForProperty(MP_Refraction))
					{
						RefractionExpression->ConnectExpression(RefractionInput, GetOutputIndex(*RefractionExpression, OutputName));
					}
				}
			}
		}
	}

	// Clear Coat
	{
		FString ClearCoatUid;
		FString OutputName;

		if (MaterialFactoryNode->GetClearCoatConnection(ClearCoatUid, OutputName))
		{
			const UInterchangeMaterialExpressionFactoryNode* ClearCoatNode = Cast<UInterchangeMaterialExpressionFactoryNode>(Arguments.NodeContainer->GetNode(ClearCoatUid));

			if (ClearCoatNode)
			{
				if (UMaterialExpression* ClearCoatExpression = CreateExpressionsForNode(Material, Arguments, ClearCoatNode, Expressions))
				{
					if (FExpressionInput* ClearCoatInput = Material->GetExpressionInputForProperty(MP_CustomData0))
					{
						ClearCoatExpression->ConnectExpression(ClearCoatInput, GetOutputIndex(*ClearCoatExpression, OutputName));
					}
				}
			}
		}
	}

	// Clear Coat Roughness
	{
		FString ClearCoatRoughnessUid;
		FString OutputName;

		if (MaterialFactoryNode->GetClearCoatRoughnessConnection(ClearCoatRoughnessUid, OutputName))
		{
			const UInterchangeMaterialExpressionFactoryNode* ClearCoatRoughnessNode = Cast<UInterchangeMaterialExpressionFactoryNode>(Arguments.NodeContainer->GetNode(ClearCoatRoughnessUid));

			if (ClearCoatRoughnessNode)
			{
				if (UMaterialExpression* ClearCoatRoughnessExpression = CreateExpressionsForNode(Material, Arguments, ClearCoatRoughnessNode, Expressions))
				{
					if (FExpressionInput* ClearCoatRoughnessInput = Material->GetExpressionInputForProperty(MP_CustomData1))
					{
						ClearCoatRoughnessExpression->ConnectExpression(ClearCoatRoughnessInput, GetOutputIndex(*ClearCoatRoughnessExpression, OutputName));
					}
				}
			}
		}
	}

	// Clear Coat Normal
	{
		FString ClearCoatNormalUid;
		FString OutputName;

		if (MaterialFactoryNode->GetClearCoatNormalConnection(ClearCoatNormalUid, OutputName))
		{
			const UInterchangeMaterialExpressionFactoryNode* ClearCoatNormalNode = Cast<UInterchangeMaterialExpressionFactoryNode>(Arguments.NodeContainer->GetNode(ClearCoatNormalUid));

			if (ClearCoatNormalNode)
			{
				if (UMaterialExpression* ClearCoatNormalExpression = CreateExpressionsForNode(Material, Arguments, ClearCoatNormalNode, Expressions))
				{
					UMaterialExpression* ClearCoatNormalCustomOutput = CreateMaterialExpression(Material, UMaterialExpressionClearCoatNormalCustomOutput::StaticClass());
					
					ClearCoatNormalExpression->ConnectExpression(ClearCoatNormalCustomOutput->GetInput(0), GetOutputIndex(*ClearCoatNormalExpression, OutputName));
				}
			}
		}
	}

	// Thin Translucent
	{
		FString TransmissionColorUid;
		FString OutputName;

		if (MaterialFactoryNode->GetTransmissionColorConnection(TransmissionColorUid, OutputName))
		{
			const UInterchangeMaterialExpressionFactoryNode* TransmissionColorNode = Cast<UInterchangeMaterialExpressionFactoryNode>(Arguments.NodeContainer->GetNode(TransmissionColorUid));

			if (TransmissionColorNode)
			{
				if (UMaterialExpression* TransmissionColorExpression = CreateExpressionsForNode(Material, Arguments, TransmissionColorNode, Expressions))
				{
					UMaterialExpression* ThinTranslucentMaterialOutput = CreateMaterialExpression(Material, UMaterialExpressionThinTranslucentMaterialOutput::StaticClass());
					
					TransmissionColorExpression->ConnectExpression(ThinTranslucentMaterialOutput->GetInput(0), GetOutputIndex(*TransmissionColorExpression, OutputName));
				}
			}
		}
	}

	UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
}

UMaterialExpression* UInterchangeMaterialFactory::CreateExpression(UMaterial* Material, const UInterchangeFactoryBase::FCreateAssetParams& Arguments, const UInterchangeMaterialExpressionFactoryNode& ExpressionNode)
{
	using namespace UE::Interchange::MaterialFactory::Internal;

	FString ExpressionClassName;
	ExpressionNode.GetCustomExpressionClassName(ExpressionClassName);

	TSubclassOf<UMaterialExpression> ExpressionClass = FindExpressionClass(*ExpressionClassName);

	if (!ExpressionClass.Get())
	{
		UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
		Message->Text = FText::Format(LOCTEXT("ExpressionClassNotFound", "Invalid class {0} for material expression node {1}."),
			FText::FromString(ExpressionClassName),
			FText::FromString(ExpressionNode.GetDisplayLabel()));

		return nullptr;
	}

	UMaterialExpression* MaterialExpression = CreateMaterialExpression(Material, ExpressionClass);

	if (!MaterialExpression)
	{
		return nullptr;
	}

	// Set the parameter name if the material expression has one (some material expressions don't inherit from UMaterialExpressionParameter, ie: UMaterialExpressionTextureSampleParameter
	if (FNameProperty* Property = FindFProperty<FNameProperty>(MaterialExpression->GetClass(), GET_MEMBER_NAME_CHECKED(UMaterialExpressionParameter, ParameterName)))
	{
		*(Property->ContainerPtrToValuePtr<FName>(MaterialExpression)) = FName(*(ExpressionNode.GetDisplayLabel() + LexToString(Material->Expressions.Num())));
	}

	ExpressionNode.ApplyAllCustomAttributeToObject(MaterialExpression);

	if (UMaterialExpressionTextureBase* TextureExpression = Cast<UMaterialExpressionTextureBase>(MaterialExpression))
	{
		SetupTextureExpression(Arguments, &ExpressionNode, TextureExpression);	
	}
	else if (UMaterialExpressionMaterialFunctionCall* FunctionCallExpression = Cast<UMaterialExpressionMaterialFunctionCall>(MaterialExpression))
	{
		SetupFunctionCallExpression(Material, Arguments, FunctionCallExpression);
	}

	return MaterialExpression;
}

UMaterialExpression* UInterchangeMaterialFactory::CreateExpressionsForNode(UMaterial* Material, const UInterchangeFactoryBase::FCreateAssetParams& Arguments,
	const UInterchangeMaterialExpressionFactoryNode* Expression, TMap<FString, UMaterialExpression*>& Expressions)
{
	using namespace UE::Interchange::MaterialFactory::Internal;

	UMaterialExpression* MaterialExpression = Expressions.FindRef(Expression->GetUniqueID());

	if (MaterialExpression)
	{
		return MaterialExpression;
	}

	MaterialExpression = CreateExpression(Material, Arguments, *Expression);

	if (!MaterialExpression)
	{
		return nullptr;
	}

	Expressions.Add(Expression->GetUniqueID()) = MaterialExpression;

	TArray<FString> Inputs;
	UInterchangeShaderPortsAPI::GatherInputs(Expression, Inputs);

	for (const FString& InputName : Inputs)
	{
		FString ConnectedExpressionUid;
		FString OutputName;
		if (UInterchangeShaderPortsAPI::GetInputConnection(Expression, InputName, ConnectedExpressionUid, OutputName))
		{
			const UInterchangeMaterialExpressionFactoryNode* ConnectedExpressionNode = Cast<UInterchangeMaterialExpressionFactoryNode>(Arguments.NodeContainer->GetNode(ConnectedExpressionUid));
			if (ConnectedExpressionNode)
			{
				UMaterialExpression* ConnectedExpression = Expressions.FindRef(ConnectedExpressionUid);
				if (!ConnectedExpression)
				{
					ConnectedExpression = CreateExpressionsForNode(Material, Arguments, ConnectedExpressionNode, Expressions);
				}

				const int32 InputIndex = GetInputIndex(*MaterialExpression, InputName);
				const int32 OutputIndex = GetOutputIndex(*ConnectedExpression, OutputName);

				if (InputIndex != INDEX_NONE)
				{
					FExpressionInput* ExpressionInput = MaterialExpression->GetInput(InputIndex);
					if (ExpressionInput)
					{
						ConnectedExpression->ConnectExpression(ExpressionInput, OutputIndex);
					}
				}
				else
				{
					UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
					Message->Text = FText::Format(LOCTEXT("InputNotFound", "Invalid input {0} for material expression node {1}."),
						FText::FromString(InputName),
						FText::FromString(Expression->GetDisplayLabel()));
				}
			}
		}
	}

	return MaterialExpression;
}
#endif // #if WITH_EDITOR

void UInterchangeMaterialFactory::SetupMaterialInstance(UMaterialInstance* MaterialInstance, const UInterchangeBaseNodeContainer* NodeContainer, const UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode)
{
	if (!MaterialFactoryNode || !NodeContainer)
	{
		return;
	}

	TArray<FString> Inputs;
	UInterchangeShaderPortsAPI::GatherInputs(MaterialFactoryNode, Inputs);

	for (const FString& InputName : Inputs)
	{
		const FName ParameterName = *InputName;

		switch(UInterchangeShaderPortsAPI::GetInputType(MaterialFactoryNode, InputName))
		{
		case UE::Interchange::EAttributeTypes::Float:
			{
				float InstanceValue;
				if (MaterialInstance->GetScalarParameterValue(ParameterName, InstanceValue))
				{
					float InputValue = 0.f;
					MaterialFactoryNode->GetFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputName), InputValue);

					if ( !FMath::IsNearlyEqual(InputValue, InstanceValue) )
					{
#if WITH_EDITOR
						if (UMaterialInstanceConstant* MaterialInstanceConstant = Cast<UMaterialInstanceConstant>(MaterialInstance))
						{
							MaterialInstanceConstant->SetScalarParameterValueEditorOnly(ParameterName, InputValue);
						}
						else
#endif // #if WITH_EDITOR
						if (UMaterialInstanceDynamic* MaterialInstanceDynamic = Cast<UMaterialInstanceDynamic>(MaterialInstance))
						{
							MaterialInstanceDynamic->SetScalarParameterValue(ParameterName, InputValue);
						}
					}
				}
			}
			break;
		case UE::Interchange::EAttributeTypes::LinearColor:
			{
				FLinearColor InstanceValue;
				if (MaterialInstance->GetVectorParameterValue(ParameterName, InstanceValue))
				{
					FLinearColor InputValue;
					if (MaterialFactoryNode->GetLinearColorAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputName), InputValue))
					{
						if ( !InputValue.Equals(InstanceValue) )
						{
#if WITH_EDITOR
							if (UMaterialInstanceConstant* MaterialInstanceConstant = Cast<UMaterialInstanceConstant>(MaterialInstance))
							{
								MaterialInstanceConstant->SetVectorParameterValueEditorOnly(ParameterName, InputValue);
							}
							else
#endif // #if WITH_EDITOR
							if (UMaterialInstanceDynamic* MaterialInstanceDynamic = Cast<UMaterialInstanceDynamic>(MaterialInstance))
							{
								MaterialInstanceDynamic->SetVectorParameterValue(ParameterName, InputValue);
							}
						}
					}
				}
			}
			break;
		case UE::Interchange::EAttributeTypes::String:
			{
				UTexture* InstanceValue;
				if (MaterialInstance->GetTextureParameterValue(ParameterName, InstanceValue))
				{
					FString InputValue;
					if (MaterialFactoryNode->GetStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputName), InputValue))
					{
						if (const UInterchangeTextureFactoryNode* TextureFactoryNode = Cast<UInterchangeTextureFactoryNode>(NodeContainer->GetNode(InputValue)))
						{
							if (UTexture* InputTexture = Cast<UTexture>(TextureFactoryNode->ReferenceObject.TryLoad()))
							{
								if ( InputTexture != InstanceValue )
								{
#if WITH_EDITOR
									if (UMaterialInstanceConstant* MaterialInstanceConstant = Cast<UMaterialInstanceConstant>(MaterialInstance))
									{
										MaterialInstanceConstant->SetTextureParameterValueEditorOnly(ParameterName, InputTexture);
									}
									else
#endif // #if WITH_EDITOR
									if (UMaterialInstanceDynamic* MaterialInstanceDynamic = Cast<UMaterialInstanceDynamic>(MaterialInstance))
									{
										MaterialInstanceDynamic->SetTextureParameterValue(ParameterName, InputTexture);
									}
								}
							}
						}
					}
				}
			}
			break;
		}
	}
}

#undef LOCTEXT_NAMESPACE
