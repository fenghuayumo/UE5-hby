// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Mesh/InterchangeSkeletalMeshFactory.h"

#include "Async/ParallelFor.h"
#include "Components.h"
#include "Engine/SkeletalMesh.h"
#include "GPUSkinPublicDefs.h"
#include "InterchangeAssetImportData.h"
#include "InterchangeImportCommon.h"
#include "InterchangeImportLog.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangeSceneNode.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
#include "Math/GenericOctree.h"
#include "Mesh/InterchangeSkeletalMeshPayload.h"
#include "Mesh/InterchangeSkeletalMeshPayloadInterface.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "SkeletalMeshAttributes.h"
#include "SkeletalMeshOperations.h"

#if WITH_EDITORONLY_DATA

#include "EditorFramework/AssetImportData.h"

#endif //WITH_EDITORONLY_DATA

#if WITH_EDITOR
namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			//Get the mesh node context for each MeshUids
			struct FMeshNodeContext
			{
				const UInterchangeMeshNode* MeshNode = nullptr;
				const UInterchangeSceneNode* SceneNode = nullptr;
				TOptional<FTransform> SceneGlobalTransform;
				FString TranslatorPayloadKey;
			};

			struct FJointInfo
			{
				FString Name;
				int32 ParentIndex;  // 0 if this is the root bone.  
				FTransform	LocalTransform; // local transform
			};

			void RecursiveAddBones(const UInterchangeBaseNodeContainer* NodeContainer, const FString& JointNodeId, TArray <FJointInfo>& JointInfos, int32 ParentIndex, TArray<SkeletalMeshImportData::FBone>& RefBonesBinary, const bool bUseTimeZeroAsBindPose, bool & bOutDiffPose)
			{
				const UInterchangeSceneNode* JointNode = Cast<UInterchangeSceneNode>(NodeContainer->GetNode(JointNodeId));
				if (!JointNode)
				{
					UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid Skeleton Joint"));
					return;
				}

				int32 JointInfoIndex = JointInfos.Num();
				FJointInfo& Info = JointInfos.AddZeroed_GetRef();
				Info.Name = JointNode->GetDisplayLabel();

				FTransform LocalTransform;
				ensure(JointNode->GetCustomLocalTransform(LocalTransform));

				FTransform TimeZeroLocalTransform;
				const bool bHasTimeZeroTransform = JointNode->GetCustomTimeZeroLocalTransform(TimeZeroLocalTransform);
				FTransform BindPoseLocalTransform;
				const bool bHasBindPoseTransform = JointNode->GetCustomBindPoseLocalTransform(BindPoseLocalTransform);

				Info.LocalTransform = bHasBindPoseTransform ? BindPoseLocalTransform : LocalTransform;
				//If user want to bind the mesh at time zero try to get the time zero transform
				if (bUseTimeZeroAsBindPose && bHasTimeZeroTransform)
				{
					if (bHasBindPoseTransform)
					{
						if(!TimeZeroLocalTransform.Equals(Info.LocalTransform))
						{
							bOutDiffPose = true;
						}
					}
					Info.LocalTransform = TimeZeroLocalTransform;
				}
				else if (bHasBindPoseTransform)
				{
					Info.LocalTransform = BindPoseLocalTransform;
				}

				Info.ParentIndex = ParentIndex;

				SkeletalMeshImportData::FBone& Bone = RefBonesBinary.AddZeroed_GetRef();
				Bone.Name = Info.Name;
				Bone.BonePos.Transform = FTransform3f(Info.LocalTransform);
				Bone.ParentIndex = ParentIndex;
				//Fill the scrap we do not need
				Bone.BonePos.Length = 0.0f;
				Bone.BonePos.XSize = 1.0f;
				Bone.BonePos.YSize = 1.0f;
				Bone.BonePos.ZSize = 1.0f;
				
				const TArray<FString> ChildrenIds = NodeContainer->GetNodeChildrenUids(JointNodeId);
				Bone.NumChildren = ChildrenIds.Num();
				for (int32 ChildIndex = 0; ChildIndex < ChildrenIds.Num(); ++ChildIndex)
				{
					RecursiveAddBones(NodeContainer, ChildrenIds[ChildIndex], JointInfos, JointInfoIndex, RefBonesBinary, bUseTimeZeroAsBindPose, bOutDiffPose);
				}
			}

			bool ProcessImportMeshSkeleton(const USkeleton* SkeletonAsset, FReferenceSkeleton& RefSkeleton, int32& SkeletalDepth, const UInterchangeBaseNodeContainer* NodeContainer, const FString& RootJointNodeId, TArray<SkeletalMeshImportData::FBone>& RefBonesBinary, const bool bUseTimeZeroAsBindPose, bool& bOutDiffPose)
			{
				auto FixupBoneName = [](FString BoneName)
				{
					BoneName.TrimStartAndEndInline();
					BoneName.ReplaceInline(TEXT(" "), TEXT("-"), ESearchCase::IgnoreCase);
					return BoneName;
				};

				RefBonesBinary.Empty();
				// Setup skeletal hierarchy + names structure.
				RefSkeleton.Empty();

				FReferenceSkeletonModifier RefSkelModifier(RefSkeleton, SkeletonAsset);
				TArray <FJointInfo> JointInfos;
				RecursiveAddBones(NodeContainer, RootJointNodeId, JointInfos, INDEX_NONE, RefBonesBinary, bUseTimeZeroAsBindPose, bOutDiffPose);
				if (bOutDiffPose)
				{
					//bOutDiffPose can only be true if the user ask to bind with time zero transform.
					ensure(bUseTimeZeroAsBindPose);
				}
				// Digest bones to the serializable format.
				for (int32 b = 0; b < JointInfos.Num(); b++)
				{
					const FJointInfo& BinaryBone = JointInfos[b];

					const FString BoneName = FixupBoneName(BinaryBone.Name);
					const FMeshBoneInfo BoneInfo(FName(*BoneName, FNAME_Add), BinaryBone.Name, BinaryBone.ParentIndex);
					const FTransform BoneTransform(BinaryBone.LocalTransform);
					if (RefSkeleton.FindRawBoneIndex(BoneInfo.Name) != INDEX_NONE)
					{
						UE_LOG(LogInterchangeImport, Error, TEXT("Invalid Skeleton because of non-unique bone names [%s]"), *BoneInfo.Name.ToString());
						return false;
					}
					RefSkelModifier.Add(BoneInfo, BoneTransform);
				}

				// Add hierarchy index to each bone and detect max depth.
				SkeletalDepth = 0;

				TArray<int32> SkeletalDepths;
				SkeletalDepths.Empty(JointInfos.Num());
				SkeletalDepths.AddZeroed(JointInfos.Num());
				for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetRawBoneNum(); ++BoneIndex)
				{
					int32 Parent = RefSkeleton.GetRawParentIndex(BoneIndex);
					int32 Depth = 1.0f;

					SkeletalDepths[BoneIndex] = 1.0f;
					if (Parent != INDEX_NONE)
					{
						Depth += SkeletalDepths[Parent];
					}
					if (SkeletalDepth < Depth)
					{
						SkeletalDepth = Depth;
					}
					SkeletalDepths[BoneIndex] = Depth;
				}

				return true;
			}

			void FillBlendShapeMeshDescriptionsPerBlendShapeName(const FMeshNodeContext& MeshNodeContext
																 , TMap<FString, TOptional<UE::Interchange::FSkeletalMeshBlendShapePayloadData>>& BlendShapeMeshDescriptionsPerBlendShapeName
																 , const IInterchangeSkeletalMeshPayloadInterface* SkeletalMeshTranslatorPayloadInterface
																 , const int32 VertexOffset
																 , const UInterchangeBaseNodeContainer* NodeContainer
																 , FString AssetName)
			{
				TArray<FString> BlendShapeUids;
				MeshNodeContext.MeshNode->GetShapeDependencies(BlendShapeUids);
				TMap<FString, TFuture<TOptional<UE::Interchange::FSkeletalMeshBlendShapePayloadData>>> TempBlendShapeMeshDescriptionsPerBlendShapeName;
				TempBlendShapeMeshDescriptionsPerBlendShapeName.Reserve(BlendShapeUids.Num());
				for (const FString& BlendShapeUid : BlendShapeUids)
				{
					if (const UInterchangeMeshNode* BlendShapeMeshNode = Cast<UInterchangeMeshNode>(NodeContainer->GetNode(BlendShapeUid)))
					{
						TOptional<FString> BlendShapePayloadKey = BlendShapeMeshNode->GetPayLoadKey();
						if (!BlendShapePayloadKey.IsSet())
						{
							UE_LOG(LogInterchangeImport, Warning, TEXT("Empty LOD morph target mesh reference payload when importing SkeletalMesh asset %s"), *AssetName);
							continue;
						}
						const FString PayloadKey = BlendShapePayloadKey.GetValue();
						//Add the map entry key, the translator will be call after to bulk get all the needed payload
						TempBlendShapeMeshDescriptionsPerBlendShapeName.Add(PayloadKey, SkeletalMeshTranslatorPayloadInterface->GetSkeletalMeshBlendShapePayloadData(PayloadKey));
					}
				}

				for (const FString& BlendShapeUid : BlendShapeUids)
				{
					if (const UInterchangeMeshNode* BlendShapeMeshNode = Cast<UInterchangeMeshNode>(NodeContainer->GetNode(BlendShapeUid)))
					{
						
						TOptional<FString> BlendShapePayloadKey = BlendShapeMeshNode->GetPayLoadKey();
						if (!BlendShapePayloadKey.IsSet())
						{
							continue;
						}
						const FString& BlendShapePayloadKeyString = BlendShapePayloadKey.GetValue();
						if (!ensure(TempBlendShapeMeshDescriptionsPerBlendShapeName.Contains(BlendShapePayloadKeyString)))
						{
							continue;
						}

						TOptional<UE::Interchange::FSkeletalMeshBlendShapePayloadData> BlendShapeMeshPayload = TempBlendShapeMeshDescriptionsPerBlendShapeName.FindChecked(BlendShapePayloadKeyString).Get();
						if (!BlendShapeMeshPayload.IsSet())
						{
							UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid Skeletal mesh morph target payload key [%s] SkeletalMesh asset %s"), *BlendShapePayloadKeyString, *AssetName);
							continue;
						}
						BlendShapeMeshPayload->VertexOffset = VertexOffset;
						//Use the Mesh node parent bake transform
						if (MeshNodeContext.SceneGlobalTransform.IsSet())
						{
							BlendShapeMeshPayload->GlobalTransform = MeshNodeContext.SceneGlobalTransform;
						}
						else
						{
							BlendShapeMeshPayload->GlobalTransform.Reset();
						}

						if (!BlendShapeMeshNode->GetBlendShapeName(BlendShapeMeshPayload->BlendShapeName))
						{
							BlendShapeMeshPayload->BlendShapeName = BlendShapePayloadKeyString;
						}
						//Add the Blend shape to the blend shape map
						BlendShapeMeshDescriptionsPerBlendShapeName.Add(BlendShapePayloadKeyString, BlendShapeMeshPayload);
					}
				}
			}

			void CopyBlendShapesMeshDescriptionToSkeletalMeshImportData(const TMap<FString, TOptional<UE::Interchange::FSkeletalMeshBlendShapePayloadData>>& LodBlendShapeMeshDescriptions, FSkeletalMeshImportData& DestinationSkeletalMeshImportData)
			{
				const int32 OriginalMorphTargetCount = LodBlendShapeMeshDescriptions.Num();
				TArray<FString> Keys;
				int32 MorphTargetCount = 0;
				for (const TPair<FString, TOptional<UE::Interchange::FSkeletalMeshBlendShapePayloadData>>& Pair : LodBlendShapeMeshDescriptions)
				{
					const FString BlendShapeName(Pair.Key);
					const TOptional<UE::Interchange::FSkeletalMeshBlendShapePayloadData>& BlendShapePayloadData = Pair.Value;
					if (!BlendShapePayloadData.IsSet())
					{
						UE_LOG(LogInterchangeImport, Error, TEXT("Empty blend shape optional payload data [%s]"), *BlendShapeName);
						continue;
					}

					const FMeshDescription& SourceMeshDescription = BlendShapePayloadData.GetValue().LodMeshDescription;
					const int32 VertexOffset = BlendShapePayloadData->VertexOffset;
					const int32 SourceMeshVertexCount = SourceMeshDescription.Vertices().Num();
					const int32 DestinationVertexIndexMax = VertexOffset + SourceMeshVertexCount;
					if (!DestinationSkeletalMeshImportData.Points.IsValidIndex(DestinationVertexIndexMax-1))
					{
						UE_LOG(LogInterchangeImport, Error, TEXT("Corrupted blend shape optional payload data [%s]"), *BlendShapeName);
						continue;
					}
					Keys.Add(Pair.Key);
					MorphTargetCount++;
				}

				//No morph target to import
				if (MorphTargetCount == 0)
				{
					return;
				}

				ensure(Keys.Num() == MorphTargetCount);
				//Allocate the data
				DestinationSkeletalMeshImportData.MorphTargetNames.AddDefaulted(MorphTargetCount);
				DestinationSkeletalMeshImportData.MorphTargetModifiedPoints.AddDefaulted(MorphTargetCount);
				DestinationSkeletalMeshImportData.MorphTargets.AddDefaulted(MorphTargetCount);

				int32 NumMorphGroup = FMath::Min(FPlatformMisc::NumberOfWorkerThreadsToSpawn(), MorphTargetCount);
				const int32 MorphTargetGroupSize = FMath::Max(FMath::CeilToInt(static_cast<float>(MorphTargetCount) / static_cast<float>(NumMorphGroup)), 1);
				//Re-Adjust the group Number in case we have a reminder error (exemple MorphTargetGroupSize = 4.8 -> 5 so the number of group can be lower if there is a large amount of Group)
				NumMorphGroup = FMath::CeilToInt(static_cast<float>(MorphTargetCount) / static_cast<float>(MorphTargetGroupSize));

				ParallelFor(NumMorphGroup, [MorphTargetGroupSize,
							MorphTargetCount,
							NumMorphGroup,
							&LodBlendShapeMeshDescriptions,
							&Keys,
							&DestinationSkeletalMeshImportData](const int32 MorphTargetGroupIndex)
				{
					const int32 MorphTargetIndexOffset = MorphTargetGroupIndex * MorphTargetGroupSize;
					const int32 MorphTargetEndLoopCount = MorphTargetIndexOffset + MorphTargetGroupSize;
					for (int32 MorphTargetIndex = MorphTargetIndexOffset; MorphTargetIndex < MorphTargetEndLoopCount; ++MorphTargetIndex)
					{
						if (!Keys.IsValidIndex(MorphTargetIndex))
						{
							ensure(MorphTargetGroupIndex + 1 == NumMorphGroup);
							//Executing the last morph target group, in case we do not have a full last group.
							break;
						}
						const FString BlendShapeKey(Keys[MorphTargetIndex]);
						const TOptional<UE::Interchange::FSkeletalMeshBlendShapePayloadData>& BlendShapePayloadData = LodBlendShapeMeshDescriptions.FindChecked(BlendShapeKey);
						if (!ensure(BlendShapePayloadData.IsSet()))
						{
							//This error was suppose to be catch in the pre parallel for loop
							break;
						}

						const FMeshDescription& SourceMeshDescription = BlendShapePayloadData.GetValue().LodMeshDescription;
						const FTransform GlobalTransform = BlendShapePayloadData->GlobalTransform.IsSet() ? BlendShapePayloadData->GlobalTransform.GetValue() : FTransform::Identity;
						const int32 VertexOffset = BlendShapePayloadData->VertexOffset;
						const int32 SourceMeshVertexCount = SourceMeshDescription.Vertices().Num();
						const int32 DestinationVertexIndexMax = VertexOffset + SourceMeshVertexCount;
						if (!ensure(DestinationSkeletalMeshImportData.Points.IsValidIndex(DestinationVertexIndexMax-1)))
						{
							//This error was suppose to be catch in the pre parallel for loop
							break;
						}
						TArray<FVector3f> CompressPoints;
						CompressPoints.Reserve(SourceMeshVertexCount);
						FStaticMeshConstAttributes Attributes(SourceMeshDescription);
						TVertexAttributesConstRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();

						//Create the morph target source data
						FString& MorphTargetName = DestinationSkeletalMeshImportData.MorphTargetNames[MorphTargetIndex];
						MorphTargetName = BlendShapePayloadData->BlendShapeName;
						TSet<uint32>& ModifiedPoints = DestinationSkeletalMeshImportData.MorphTargetModifiedPoints[MorphTargetIndex];
						FSkeletalMeshImportData& MorphTargetData = DestinationSkeletalMeshImportData.MorphTargets[MorphTargetIndex];

						//Reserve the point and influences
						MorphTargetData.Points.AddZeroed(SourceMeshVertexCount);

						for (FVertexID VertexID : SourceMeshDescription.Vertices().GetElementIDs())
						{
							//We can use GetValue because the Meshdescription was compacted before the copy
							MorphTargetData.Points[VertexID.GetValue()] = (FVector3f)GlobalTransform.TransformPosition((FVector)VertexPositions[VertexID]);
						}

						for (int32 PointIdx = VertexOffset; PointIdx < DestinationVertexIndexMax; ++PointIdx)
						{
							int32 OriginalPointIdx = DestinationSkeletalMeshImportData.PointToRawMap[PointIdx] - VertexOffset;
							//Rebuild the data with only the modified point
							if ((MorphTargetData.Points[OriginalPointIdx] - DestinationSkeletalMeshImportData.Points[PointIdx]).SizeSquared() > FMath::Square(THRESH_POINTS_ARE_SAME))
							{
								ModifiedPoints.Add(PointIdx);
								CompressPoints.Add(MorphTargetData.Points[OriginalPointIdx]);
							}
						}
						MorphTargetData.Points = CompressPoints;
					}
				}
				, EParallelForFlags::BackgroundPriority);
				return;
			}

			const UInterchangeSceneNode* RecursiveFindJointByName(const UInterchangeBaseNodeContainer* NodeContainer, const FString& ParentJointNodeId, const FString& JointName)
			{
				if (const UInterchangeSceneNode* JointNode = Cast<UInterchangeSceneNode>(NodeContainer->GetNode(ParentJointNodeId)))
				{
					if (JointNode->GetDisplayLabel().Equals(JointName))
					{
						return JointNode;
					}
				}
				TArray<FString> NodeChildrenUids = NodeContainer->GetNodeChildrenUids(ParentJointNodeId);
				for (int32 ChildIndex = 0; ChildIndex < NodeChildrenUids.Num(); ++ChildIndex)
				{
					if (const UInterchangeSceneNode* JointNode = RecursiveFindJointByName(NodeContainer, NodeChildrenUids[ChildIndex], JointName))
					{
						return JointNode;
					}
				}

				return nullptr;
			}

			void SkinVertexPositionToTimeZero(UE::Interchange::FSkeletalMeshLodPayloadData& LodMeshPayload
				, const UInterchangeBaseNodeContainer* NodeContainer
				, const FString& RootJointNodeId
				, const FTransform& MeshGlobalTransform)
			{
				FMeshDescription& MeshDescription = LodMeshPayload.LodMeshDescription;
				const int32 VertexCount = MeshDescription.Vertices().Num();
				const TArray<FString>& JointNames = LodMeshPayload.JointNames;
				// Create a copy of the vertex array to receive vertex deformations.
				TArray<FVector> DestinationVertexPositions;
				DestinationVertexPositions.AddZeroed(VertexCount);

				FSkeletalMeshAttributes Attributes(MeshDescription);
				TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
				FSkinWeightsVertexAttributesRef VertexSkinWeights = Attributes.GetVertexSkinWeights();

				for (FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
				{
					//We can use GetValue because the Meshdescription was compacted before the copy
					DestinationVertexPositions[VertexID.GetValue()] = VertexPositions[VertexID];
				}

				// Deform the vertex array with the links contained in the mesh.
				TArray<FMatrix> SkinDeformations;
				SkinDeformations.AddZeroed(VertexCount);

				TArray<double> SkinWeights;
				SkinWeights.AddZeroed(VertexCount);

				//We assume normalize weight method in this bind pose conversion

				const FTransform MeshGlobalTransformInverse = MeshGlobalTransform.Inverse();
				const int32 JointCount = JointNames.Num();
				for (int32 JointIndex = 0; JointIndex < JointCount; ++JointIndex)
				{
					const FString& JointName = JointNames[JointIndex];

					const UInterchangeSceneNode* JointNode = RecursiveFindJointByName(NodeContainer, RootJointNodeId, JointName);
					if (!ensure(JointNode))
					{
						continue;
					}
					
					FTransform JointBindPoseGlobalTransform;
					if (!JointNode->GetCustomBindPoseGlobalTransform(NodeContainer, JointBindPoseGlobalTransform))
					{
						//If there is no bind pose we will fall back on the CustomGlobalTransform of the link.
						//We ensure here because any scenenode should have a valid CustomGlobalTransform.
						if (!ensure(JointNode->GetCustomGlobalTransform(NodeContainer, JointBindPoseGlobalTransform)))
						{
							//No value to convert from, skip this joint.
							continue;
						}
					}

					FTransform JointTimeZeroGlobalTransform;
					if (!JointNode->GetCustomTimeZeroGlobalTransform(NodeContainer, JointTimeZeroGlobalTransform))
					{
						//If there is no time zero global transform we cannot set the bind pose to time zero.
						//We must skip this joint.
						continue;
					}

					//Get the mesh transform in local relative to the bind pose transform
 					const FTransform MeshTransformRelativeToBindPoseTransform = MeshGlobalTransform * JointBindPoseGlobalTransform.Inverse();
					//Get the time zero pose transform in local relative to the mesh transform
 					const FTransform TimeZeroTransformRelativeToMeshTransform = JointTimeZeroGlobalTransform * MeshGlobalTransformInverse;
					//Multiply both transform to get a matrix that will transform the mesh vertices from the bind pose skinning to the time zero skinning
 					const FMatrix VertexTransformMatrix = (MeshTransformRelativeToBindPoseTransform * TimeZeroTransformRelativeToMeshTransform).ToMatrixWithScale();

					//Iterate all bone vertices
					for (FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
					{
						const int32 VertexIndex = VertexID.GetValue();
						const FVertexBoneWeights BoneWeights = VertexSkinWeights.Get(VertexID);
						const int32 InfluenceCount = BoneWeights.Num();
						float Weight = 0.0f;
						for (int32 InfluenceIndex = 0; InfluenceIndex < InfluenceCount; ++InfluenceIndex)
						{
							FBoneIndexType BoneIndex = BoneWeights[InfluenceIndex].GetBoneIndex();
							if (JointIndex == BoneIndex)
							{
								Weight = BoneWeights[InfluenceIndex].GetWeight();
								break;
							}
						}
						if (FMath::IsNearlyZero(Weight))
						{
							continue;
						}

						//The weight multiply the vertex transform matrix so we can have multiple joint affecting this vertex.
						const FMatrix Influence = VertexTransformMatrix * Weight;
						//Add the weighted result
						SkinDeformations[VertexIndex] += Influence;
						//Add the total weight so we can normalize the result in case the accumulated weight is different then 1
						SkinWeights[VertexIndex] += Weight;
					}
				}

				for (FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
				{
					const int32 VertexIndex = VertexID.GetValue();
					const FVector3f lSrcVertex = DestinationVertexPositions[VertexIndex];
					FVector& lDstVertex = DestinationVertexPositions[VertexIndex];
					double Weight = SkinWeights[VertexIndex];

					// Deform the vertex if there was at least a link with an influence on the vertex,
					if (!FMath::IsNearlyZero(Weight))
					{
						//Apply skinning of all joints
						lDstVertex = SkinDeformations[VertexIndex].TransformPosition(lSrcVertex);
						//Normalized, in case the weight is different then 1
						lDstVertex /= Weight;
						//Set the new vertex position in the mesh description
						VertexPositions[VertexID] = lDstVertex;
					}
				}
			}

			void RetrieveAllSkeletalMeshPayloadsAndFillImportData(const UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode
																  , FSkeletalMeshImportData& DestinationImportData
																  , TArray<FMeshNodeContext>& MeshReferences
																  , TArray<SkeletalMeshImportData::FBone>& RefBonesBinary
																  , const UInterchangeSkeletalMeshFactory::FCreateAssetParams& Arguments
																  , const IInterchangeSkeletalMeshPayloadInterface* SkeletalMeshTranslatorPayloadInterface
																  , const bool bSkinControlPointToTimeZero
																  , const UInterchangeBaseNodeContainer* NodeContainer
																  , const FString& RootJointNodeId)
			{
				if (!SkeletalMeshTranslatorPayloadInterface)
				{
					return;
				}
				FMeshDescription LodMeshDescription;
				FSkeletalMeshAttributes SkeletalMeshAttributes(LodMeshDescription);
				SkeletalMeshAttributes.Register();
				FStaticMeshOperations::FAppendSettings AppendSettings;
				for (int32 ChannelIdx = 0; ChannelIdx < FStaticMeshOperations::FAppendSettings::MAX_NUM_UV_CHANNELS; ++ChannelIdx)
				{
					AppendSettings.bMergeUVChannels[ChannelIdx] = true;
				}

				bool bImportMorphTarget = true;
				SkeletalMeshFactoryNode->GetCustomImportMorphTarget(bImportMorphTarget);

				TMap<FString, TFuture<TOptional<UE::Interchange::FSkeletalMeshLodPayloadData>>> LodMeshPayloadPerTranslatorPayloadKey;
				LodMeshPayloadPerTranslatorPayloadKey.Reserve(MeshReferences.Num());

				TMap<FString, TOptional<UE::Interchange::FSkeletalMeshBlendShapePayloadData>> BlendShapeMeshDescriptionsPerBlendShapeName;
				int32 BlendShapeCount = 0;

				for (const FMeshNodeContext& MeshNodeContext : MeshReferences)
				{
					//Add the payload entry key, the payload data will be fill later in bulk by the translator
					LodMeshPayloadPerTranslatorPayloadKey.Add(MeshNodeContext.TranslatorPayloadKey, SkeletalMeshTranslatorPayloadInterface->GetSkeletalMeshLodPayloadData(MeshNodeContext.TranslatorPayloadKey));
					//Count the blend shape dependencies so we can reserve the right amount
					BlendShapeCount += (bImportMorphTarget && MeshNodeContext.MeshNode) ? MeshNodeContext.MeshNode->GetShapeDependeciesCount() : 0;
				}
				BlendShapeMeshDescriptionsPerBlendShapeName.Reserve(BlendShapeCount);

				//Fill the lod mesh description using all combined mesh part
				for (const FMeshNodeContext& MeshNodeContext : MeshReferences)
				{
					TOptional<UE::Interchange::FSkeletalMeshLodPayloadData> LodMeshPayload = LodMeshPayloadPerTranslatorPayloadKey.FindChecked(MeshNodeContext.TranslatorPayloadKey).Get();
					if (!LodMeshPayload.IsSet())
					{
						UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid Skeletal mesh payload key [%s] SkeletalMesh asset %s"), *MeshNodeContext.TranslatorPayloadKey, *Arguments.AssetName);
						continue;
					}
					const int32 VertexOffset = LodMeshDescription.Vertices().Num();

					FSkeletalMeshOperations::FSkeletalMeshAppendSettings SkeletalMeshAppendSettings;
					SkeletalMeshAppendSettings.SourceVertexIDOffset = VertexOffset;
					FElementIDRemappings ElementIDRemappings;
					LodMeshPayload->LodMeshDescription.Compact(ElementIDRemappings);

					if (bSkinControlPointToTimeZero)
					{
						//We need to rebind the mesh at time 0. Skeleton joint have the time zero transform, so we need to apply the skinning to the mesh
						//With the skeleton transform at time zero
						FTransform MeshGlobalTransform;
						MeshGlobalTransform.SetIdentity();
						if (MeshNodeContext.SceneGlobalTransform.IsSet())
						{
							MeshGlobalTransform = MeshNodeContext.SceneGlobalTransform.GetValue();
						}
						SkinVertexPositionToTimeZero(LodMeshPayload.GetValue(), NodeContainer, RootJointNodeId, MeshGlobalTransform);
					}

					const int32 RefBoneCount = RefBonesBinary.Num();
					//Remap the influence vertex index to point on the correct index
					if (LodMeshPayload->JointNames.Num() > 0)
					{
						const int32 LocalJointCount = LodMeshPayload->JointNames.Num();
						
						SkeletalMeshAppendSettings.SourceRemapBoneIndex.AddZeroed(LocalJointCount);
						for (int32 LocalJointIndex = 0; LocalJointIndex < LocalJointCount; ++LocalJointIndex)
						{
							SkeletalMeshAppendSettings.SourceRemapBoneIndex[LocalJointIndex] = LocalJointIndex;
							const FString& LocalJointName = LodMeshPayload->JointNames[LocalJointIndex];
							for (int32 RefBoneIndex = 0; RefBoneIndex < RefBoneCount; ++RefBoneIndex)
							{
								const SkeletalMeshImportData::FBone& Bone = RefBonesBinary[RefBoneIndex];
								if (Bone.Name.Equals(LocalJointName))
								{
									SkeletalMeshAppendSettings.SourceRemapBoneIndex[LocalJointIndex] = RefBoneIndex;
									break;
								}
							}
						}
					}
					//Bake the payload, with the provide transform
					if (MeshNodeContext.SceneGlobalTransform.IsSet())
					{
						AppendSettings.MeshTransform = MeshNodeContext.SceneGlobalTransform;
					}
					else
					{
						AppendSettings.MeshTransform.Reset();
					}
					FStaticMeshOperations::AppendMeshDescription(LodMeshPayload->LodMeshDescription, LodMeshDescription, AppendSettings);
					if (MeshNodeContext.MeshNode->IsSkinnedMesh())
					{
						FSkeletalMeshOperations::AppendSkinWeight(LodMeshPayload->LodMeshDescription, LodMeshDescription, SkeletalMeshAppendSettings);
					}
					if (bImportMorphTarget)
					{
						FillBlendShapeMeshDescriptionsPerBlendShapeName(MeshNodeContext
																		, BlendShapeMeshDescriptionsPerBlendShapeName
																		, SkeletalMeshTranslatorPayloadInterface
																		, VertexOffset
																		, Arguments.NodeContainer
																		, Arguments.AssetName);
					}
				}

				DestinationImportData = FSkeletalMeshImportData::CreateFromMeshDescription(LodMeshDescription);
				DestinationImportData.RefBonesBinary = RefBonesBinary;

				//Copy all the lod blend shapes data to the DestinationImportData.
				CopyBlendShapesMeshDescriptionToSkeletalMeshImportData(BlendShapeMeshDescriptionsPerBlendShapeName, DestinationImportData);
			}

			/**
			 * Fill the Materials array using the raw skeletalmesh geometry data (using material imported name)
			 * Find the material from the dependencies of the skeletalmesh before searching in all package.
			 */
			//TODO: the pipeline should search for existing material and hook those before the factory is called
			void ProcessImportMeshMaterials(TArray<FSkeletalMaterial>& Materials, FSkeletalMeshImportData& ImportData, TMap<FString, UMaterialInterface*>& AvailableMaterials)
			{
				TArray <SkeletalMeshImportData::FMaterial>& ImportedMaterials = ImportData.Materials;
				// If direct linkup of materials is requested, try to find them here - to get a texture name from a 
				// material name, cut off anything in front of the dot (beyond are special flags).
				int32 SkinOffset = INDEX_NONE;
				for (int32 MatIndex = 0; MatIndex < ImportedMaterials.Num(); ++MatIndex)
				{
					const SkeletalMeshImportData::FMaterial& ImportedMaterial = ImportedMaterials[MatIndex];

					UMaterialInterface* Material = nullptr;

					const FName SearchMaterialSlotName(*ImportedMaterial.MaterialImportName);
					int32 MaterialIndex = 0;
					FSkeletalMaterial* SkeletalMeshMaterialFind = Materials.FindByPredicate([&SearchMaterialSlotName, &MaterialIndex](const FSkeletalMaterial& ItemMaterial)
					{
						//Imported material slot name is available only WITH_EDITOR
						FName ImportedMaterialSlot = NAME_None;
						ImportedMaterialSlot = ItemMaterial.ImportedMaterialSlotName;
						if (ImportedMaterialSlot != SearchMaterialSlotName)
						{
							MaterialIndex++;
							return false;
						}
						return true;
					});

					if (SkeletalMeshMaterialFind != nullptr)
					{
						Material = SkeletalMeshMaterialFind->MaterialInterface;
					}

					if(!Material)
					{
						//Try to find the material in the skeletal mesh node dependencies (Materials are import before skeletal mesh when there is a dependency)
						if (AvailableMaterials.Contains(ImportedMaterial.MaterialImportName))
						{
							Material = AvailableMaterials.FindChecked(ImportedMaterial.MaterialImportName);
						}
						else
						{
							//We did not found any material in the dependencies so try to find material everywhere
							Material = FindObject<UMaterialInterface>(ANY_PACKAGE, *ImportedMaterial.MaterialImportName);
						}

						const bool bEnableShadowCasting = true;
						const bool bInRecomputeTangent = false;
						Materials.Add(FSkeletalMaterial(Material, bEnableShadowCasting, bInRecomputeTangent, Material != nullptr ? Material->GetFName() : FName(*ImportedMaterial.MaterialImportName), FName(*(ImportedMaterial.MaterialImportName))));
					}
				}

				int32 NumMaterialsToAdd = FMath::Max<int32>(ImportedMaterials.Num(), ImportData.MaxMaterialIndex + 1);

				// Pad the material pointers
				while (NumMaterialsToAdd > Materials.Num())
				{
					UMaterialInterface* NullMaterialInterface = nullptr;
					Materials.Add(FSkeletalMaterial(NullMaterialInterface));
				}
			}

			void ProcessImportMeshInfluences(const int32 WedgeCount, TArray<SkeletalMeshImportData::FRawBoneInfluence>& Influences)
			{

				// Sort influences by vertex index.
				struct FCompareVertexIndex
				{
					bool operator()(const SkeletalMeshImportData::FRawBoneInfluence& A, const SkeletalMeshImportData::FRawBoneInfluence& B) const
					{
						if (A.VertexIndex > B.VertexIndex) return false;
						else if (A.VertexIndex < B.VertexIndex) return true;
						else if (A.Weight < B.Weight) return false;
						else if (A.Weight > B.Weight) return true;
						else if (A.BoneIndex > B.BoneIndex) return false;
						else if (A.BoneIndex < B.BoneIndex) return true;
						else									  return  false;
					}
				};
				Influences.Sort(FCompareVertexIndex());

				TArray <SkeletalMeshImportData::FRawBoneInfluence> NewInfluences;
				int32	LastNewInfluenceIndex = 0;
				int32	LastVertexIndex = INDEX_NONE;
				int32	InfluenceCount = 0;

				float TotalWeight = 0.f;
				const float MINWEIGHT = 0.01f;

				int MaxVertexInfluence = 0;
				float MaxIgnoredWeight = 0.0f;

				//We have to normalize the data before filtering influences
				//Because influence filtering is base on the normalize value.
				//Some DCC like Daz studio don't have normalized weight
				for (int32 i = 0; i < Influences.Num(); i++)
				{
					// if less than min weight, or it's more than 8, then we clear it to use weight
					InfluenceCount++;
					TotalWeight += Influences[i].Weight;
					// we have all influence for the same vertex, normalize it now
					if (i + 1 >= Influences.Num() || Influences[i].VertexIndex != Influences[i + 1].VertexIndex)
					{
						// Normalize the last set of influences.
						if (InfluenceCount && (TotalWeight != 1.0f))
						{
							float OneOverTotalWeight = 1.f / TotalWeight;
							for (int r = 0; r < InfluenceCount; r++)
							{
								Influences[i - r].Weight *= OneOverTotalWeight;
							}
						}

						if (MaxVertexInfluence < InfluenceCount)
						{
							MaxVertexInfluence = InfluenceCount;
						}

						// clear to count next one
						InfluenceCount = 0;
						TotalWeight = 0.f;
					}

					if (InfluenceCount > MAX_TOTAL_INFLUENCES && Influences[i].Weight > MaxIgnoredWeight)
					{
						MaxIgnoredWeight = Influences[i].Weight;
					}
				}

				// warn about too many influences
				if (MaxVertexInfluence > MAX_TOTAL_INFLUENCES)
				{
					//TODO log a display message to the user
					//UE_LOG(LogLODUtilities, Display, TEXT("Skeletal mesh (%s) influence count of %d exceeds max count of %d. Influence truncation will occur. Maximum Ignored Weight %f"), *MeshName, MaxVertexInfluence, MAX_TOTAL_INFLUENCES, MaxIgnoredWeight);
				}

				for (int32 i = 0; i < Influences.Num(); i++)
				{
					// we found next verts, normalize it now
					if (LastVertexIndex != Influences[i].VertexIndex)
					{
						// Normalize the last set of influences.
						if (InfluenceCount && (TotalWeight != 1.0f))
						{
							float OneOverTotalWeight = 1.f / TotalWeight;
							for (int r = 0; r < InfluenceCount; r++)
							{
								NewInfluences[LastNewInfluenceIndex - r].Weight *= OneOverTotalWeight;
							}
						}

						// now we insert missing verts
						if (LastVertexIndex != INDEX_NONE)
						{
							int32 CurrentVertexIndex = Influences[i].VertexIndex;
							for (int32 j = LastVertexIndex + 1; j < CurrentVertexIndex; j++)
							{
								// Add a 0-bone weight if none other present (known to happen with certain MAX skeletal setups).
								LastNewInfluenceIndex = NewInfluences.AddUninitialized();
								NewInfluences[LastNewInfluenceIndex].VertexIndex = j;
								NewInfluences[LastNewInfluenceIndex].BoneIndex = 0;
								NewInfluences[LastNewInfluenceIndex].Weight = 1.f;
							}
						}

						// clear to count next one
						InfluenceCount = 0;
						TotalWeight = 0.f;
						LastVertexIndex = Influences[i].VertexIndex;
					}

					// if less than min weight, or it's more than 8, then we clear it to use weight
					if (Influences[i].Weight > MINWEIGHT && InfluenceCount < MAX_TOTAL_INFLUENCES)
					{
						LastNewInfluenceIndex = NewInfluences.Add(Influences[i]);
						InfluenceCount++;
						TotalWeight += Influences[i].Weight;
					}
				}

				Influences = NewInfluences;

				// Ensure that each vertex has at least one influence as e.g. CreateSkinningStream relies on it.
				// The below code relies on influences being sorted by vertex index.
				if (Influences.Num() == 0)
				{
					// warn about no influences
					//TODO add a user log
					//UE_LOG(LogLODUtilities, Warning, TEXT("Warning skeletal mesh (%s) has no vertex influences"), *MeshName);
					// add one for each wedge entry
					Influences.AddUninitialized(WedgeCount);
					for (int32 WedgeIdx = 0; WedgeIdx < WedgeCount; WedgeIdx++)
					{
						Influences[WedgeIdx].VertexIndex = WedgeIdx;
						Influences[WedgeIdx].BoneIndex = 0;
						Influences[WedgeIdx].Weight = 1.0f;
					}
					for (int32 i = 0; i < Influences.Num(); i++)
					{
						int32 CurrentVertexIndex = Influences[i].VertexIndex;

						if (LastVertexIndex != CurrentVertexIndex)
						{
							for (int32 j = LastVertexIndex + 1; j < CurrentVertexIndex; j++)
							{
								// Add a 0-bone weight if none other present (known to happen with certain MAX skeletal setups).
								Influences.InsertUninitialized(i, 1);
								Influences[i].VertexIndex = j;
								Influences[i].BoneIndex = 0;
								Influences[i].Weight = 1.f;
							}
							LastVertexIndex = CurrentVertexIndex;
						}
					}
				}
			}

			/** Helper struct for the mesh component vert position octree */
			struct FSkeletalMeshVertPosOctreeSemantics
			{
				enum { MaxElementsPerLeaf = 16 };
				enum { MinInclusiveElementsPerNode = 7 };
				enum { MaxNodeDepth = 12 };

				typedef TInlineAllocator<MaxElementsPerLeaf> ElementAllocator;

				/**
				 * Get the bounding box of the provided octree element. In this case, the box
				 * is merely the point specified by the element.
				 *
				 * @param	Element	Octree element to get the bounding box for
				 *
				 * @return	Bounding box of the provided octree element
				 */
				FORCEINLINE static FBoxCenterAndExtent GetBoundingBox(const FSoftSkinVertex& Element)
				{
					return FBoxCenterAndExtent(Element.Position, FVector::ZeroVector);
				}

				/**
				 * Determine if two octree elements are equal
				 *
				 * @param	A	First octree element to check
				 * @param	B	Second octree element to check
				 *
				 * @return	true if both octree elements are equal, false if they are not
				 */
				FORCEINLINE static bool AreElementsEqual(const FSoftSkinVertex& A, const FSoftSkinVertex& B)
				{
					return (A.Position == B.Position && A.UVs[0] == B.UVs[0]);
				}

				/** Ignored for this implementation */
				FORCEINLINE static void SetElementId(const FSoftSkinVertex& Element, FOctreeElementId2 Id)
				{
				}
			};
			typedef TOctree2<FSoftSkinVertex, FSkeletalMeshVertPosOctreeSemantics> TSKCVertPosOctree;

			void RemapSkeletalMeshVertexColorToImportData(const USkeletalMesh* SkeletalMesh, const int32 LODIndex, FSkeletalMeshImportData* SkelMeshImportData)
			{
				//Make sure we have all the source data we need to do the remap
				if (!SkeletalMesh->GetImportedModel() || !SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(LODIndex) || !SkeletalMesh->GetHasVertexColors())
				{
					return;
				}

				// Find the extents formed by the cached vertex positions in order to optimize the octree used later
				FBox Bounds(ForceInitToZero);
				SkelMeshImportData->bHasVertexColors = true;

				int32 WedgeNumber = SkelMeshImportData->Wedges.Num();
				for (int32 WedgeIndex = 0; WedgeIndex < WedgeNumber; ++WedgeIndex)
				{
					SkeletalMeshImportData::FVertex& Wedge = SkelMeshImportData->Wedges[WedgeIndex];
					const FVector& Position = SkelMeshImportData->Points[Wedge.VertexIndex];
					Bounds += Position;
				}

				TArray<FSoftSkinVertex> Vertices;
				SkeletalMesh->GetImportedModel()->LODModels[LODIndex].GetVertices(Vertices);
				for (int32 SkinVertexIndex = 0; SkinVertexIndex < Vertices.Num(); ++SkinVertexIndex)
				{
					const FSoftSkinVertex& SkinVertex = Vertices[SkinVertexIndex];
					Bounds += SkinVertex.Position;
				}

				TSKCVertPosOctree VertPosOctree(Bounds.GetCenter(), Bounds.GetExtent().GetMax());

				// Add each old vertex to the octree
				for (int32 SkinVertexIndex = 0; SkinVertexIndex < Vertices.Num(); ++SkinVertexIndex)
				{
					const FSoftSkinVertex& SkinVertex = Vertices[SkinVertexIndex];
					VertPosOctree.AddElement(SkinVertex);
				}

				TMap<int32, FVector3f> WedgeIndexToNormal;
				WedgeIndexToNormal.Reserve(WedgeNumber);
				for (int32 FaceIndex = 0; FaceIndex < SkelMeshImportData->Faces.Num(); ++FaceIndex)
				{
					const SkeletalMeshImportData::FTriangle& Triangle = SkelMeshImportData->Faces[FaceIndex];
					for (int32 Corner = 0; Corner < 3; ++Corner)
					{
						WedgeIndexToNormal.Add(Triangle.WedgeIndex[Corner], Triangle.TangentZ[Corner]);
					}
				}

				// Iterate over each new vertex position, attempting to find the old vertex it is closest to, applying
				// the color of the old vertex to the new position if possible.
				for (int32 WedgeIndex = 0; WedgeIndex < WedgeNumber; ++WedgeIndex)
				{
					SkeletalMeshImportData::FVertex& Wedge = SkelMeshImportData->Wedges[WedgeIndex];
					const FVector& Position = SkelMeshImportData->Points[Wedge.VertexIndex];
					const FVector2f UV = Wedge.UVs[0];
					const FVector3f& Normal = WedgeIndexToNormal.FindChecked(WedgeIndex);

					TArray<FSoftSkinVertex> PointsToConsider;
					VertPosOctree.FindNearbyElements(Position, [&PointsToConsider](const FSoftSkinVertex& Vertex)
						{
							PointsToConsider.Add(Vertex);
						});

					if (PointsToConsider.Num() > 0)
					{
						//Get the closest position
						float MaxNormalDot = -MAX_FLT;
						float MinUVDistance = MAX_FLT;
						int32 MatchIndex = INDEX_NONE;
						for (int32 ConsiderationIndex = 0; ConsiderationIndex < PointsToConsider.Num(); ++ConsiderationIndex)
						{
							const FSoftSkinVertex& SkinVertex = PointsToConsider[ConsiderationIndex];
							const FVector2f& SkinVertexUV = SkinVertex.UVs[0];
							const float UVDistanceSqr = FVector2f::DistSquared(UV, SkinVertexUV);
							if (UVDistanceSqr < MinUVDistance)
							{
								MinUVDistance = FMath::Min(MinUVDistance, UVDistanceSqr);
								MatchIndex = ConsiderationIndex;
								MaxNormalDot = Normal | SkinVertex.TangentZ;
							}
							else if (FMath::IsNearlyEqual(UVDistanceSqr, MinUVDistance, KINDA_SMALL_NUMBER))
							{
								//This case is useful when we have hard edge that shared vertice, somtime not all the shared wedge have the same paint color
								//Think about a cube where each face have different vertex color.
								float NormalDot = Normal | SkinVertex.TangentZ;
								if (NormalDot > MaxNormalDot)
								{
									MaxNormalDot = NormalDot;
									MatchIndex = ConsiderationIndex;
								}
							}
						}
						if (PointsToConsider.IsValidIndex(MatchIndex))
						{
							Wedge.Color = PointsToConsider[MatchIndex].Color;
						}
					}
				}
			}

		} //Namespace Private
	} //namespace Interchange
} //namespace UE

#endif //#if WITH_EDITOR


UClass* UInterchangeSkeletalMeshFactory::GetFactoryClass() const
{
	return USkeletalMesh::StaticClass();
}

UObject* UInterchangeSkeletalMeshFactory::CreateEmptyAsset(const FCreateAssetParams& Arguments)
{
#if !WITH_EDITOR || !WITH_EDITORONLY_DATA

	UE_LOG(LogInterchangeImport, Error, TEXT("Cannot import skeletalMesh asset in runtime, this is an editor only feature."));
	return nullptr;

#else
	USkeletalMesh* SkeletalMesh = nullptr;
	if (!Arguments.AssetNode || !Arguments.AssetNode->GetObjectClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	const UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = Cast<UInterchangeSkeletalMeshFactoryNode>(Arguments.AssetNode);
	if (SkeletalMeshFactoryNode == nullptr)
	{
		return nullptr;
	}

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	// create a new material or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		SkeletalMesh = NewObject<USkeletalMesh>(Arguments.Parent, *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else if (ExistingAsset->GetClass()->IsChildOf(USkeletalMesh::StaticClass()))
	{
		//This is a reimport, we are just re-updating the source data
		SkeletalMesh = Cast<USkeletalMesh>(ExistingAsset);
	}
	
	if (!SkeletalMesh)
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("Could not create SkeletalMesh asset %s"), *Arguments.AssetName);
		return nullptr;
	}
	
	SkeletalMesh->PreEditChange(nullptr);
	//Allocate the LODImport data in the main thread
	SkeletalMesh->ReserveLODImportData(SkeletalMeshFactoryNode->GetLodDataCount());
	
	return SkeletalMesh;
#endif //else !WITH_EDITOR || !WITH_EDITORONLY_DATA
}

UObject* UInterchangeSkeletalMeshFactory::CreateAsset(const FCreateAssetParams& Arguments)
{
#if !WITH_EDITOR || !WITH_EDITORONLY_DATA

	UE_LOG(LogInterchangeImport, Error, TEXT("Cannot import skeletalMesh asset in runtime, this is an editor only feature."));
	return nullptr;

#else

	if (!Arguments.AssetNode || !Arguments.AssetNode->GetObjectClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = Cast<UInterchangeSkeletalMeshFactoryNode>(Arguments.AssetNode);
	if (SkeletalMeshFactoryNode == nullptr)
	{
		return nullptr;
	}

	const IInterchangeSkeletalMeshPayloadInterface* SkeletalMeshTranslatorPayloadInterface = Cast<IInterchangeSkeletalMeshPayloadInterface>(Arguments.Translator);
	if (!SkeletalMeshTranslatorPayloadInterface)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Cannot import skeletalMesh, the translator do not implement the IInterchangeSkeletalMeshPayloadInterface."));
		return nullptr;
	}

	const UClass* SkeletalMeshClass = SkeletalMeshFactoryNode->GetObjectClass();
	check(SkeletalMeshClass && SkeletalMeshClass->IsChildOf(GetFactoryClass()));

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	UObject* SkeletalMeshObject = nullptr;
	// create a new material or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		//NewObject is not thread safe, the asset registry directory watcher tick on the main thread can trig before we finish initializing the UObject and will crash
		//The UObject should have been create by calling CreateEmptyAsset on the main thread.
		check(IsInGameThread());
		SkeletalMeshObject = NewObject<UObject>(Arguments.Parent, SkeletalMeshClass, *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else if(ExistingAsset->GetClass()->IsChildOf(SkeletalMeshClass))
	{
		//This is a reimport, we are just re-updating the source data
		SkeletalMeshObject = ExistingAsset;
	}

	if (!SkeletalMeshObject)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Could not create SkeletalMesh asset %s"), *Arguments.AssetName);
		return nullptr;
	}

	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(SkeletalMeshObject);

	const bool bIsReImport = (Arguments.ReimportObject != nullptr);

	if (!ensure(SkeletalMesh))
	{
		if (!bIsReImport)
		{
			UE_LOG(LogInterchangeImport, Error, TEXT("Could not create skeletalMesh asset %s"), *Arguments.AssetName);
		}
		else
		{
			UE_LOG(LogInterchangeImport, Error, TEXT("Could not find reimported skeletalMesh asset %s"), *Arguments.AssetName);
		}
		return nullptr;
	}

	//Dirty the DDC Key for any imported Skeletal Mesh
	SkeletalMesh->InvalidateDeriveDataCacheGUID();
	USkeleton* SkeletonReference = nullptr;
		
	FSkeletalMeshModel* ImportedResource = SkeletalMesh->GetImportedModel();
	if (!bIsReImport)
	{
		if (!ensure(ImportedResource->LODModels.Num() == 0))
		{
			ImportedResource->LODModels.Empty();
		}
	}
	else
	{
		SkeletalMesh->GetImportedBounds().BoxExtent.Set(0.0, 0.0, 0.0);

		//When we re-import, we force the current skeletalmesh skeleton, to be specified and to be the reference
		FSoftObjectPath SpecifiedSkeleton = SkeletalMesh->GetSkeleton();
		SkeletalMeshFactoryNode->SetCustomSkeletonSoftObjectPath(SpecifiedSkeleton);
	}
			
	int32 LodCount = SkeletalMeshFactoryNode->GetLodDataCount();
	TArray<FString> LodDataUniqueIds;
	SkeletalMeshFactoryNode->GetLodDataUniqueIds(LodDataUniqueIds);
	ensure(LodDataUniqueIds.Num() == LodCount);
	int32 CurrentLodIndex = 0;

	EInterchangeSkeletalMeshContentType ImportContent = EInterchangeSkeletalMeshContentType::All;
	SkeletalMeshFactoryNode->GetCustomImportContentType(ImportContent);
	const bool bApplyGeometry = !bIsReImport || (ImportContent == EInterchangeSkeletalMeshContentType::All || ImportContent == EInterchangeSkeletalMeshContentType::Geometry);
	const bool bApplySkinning = !bIsReImport || (ImportContent == EInterchangeSkeletalMeshContentType::All || ImportContent == EInterchangeSkeletalMeshContentType::SkinningWeights);
	const bool bApplyPartialContent = bIsReImport && ImportContent != EInterchangeSkeletalMeshContentType::All;
	const bool bApplyGeometryOnly = bApplyPartialContent && bApplyGeometry;
	const bool bApplySkinningOnly = bApplyPartialContent && bApplySkinning;
	
	if (bApplySkinningOnly)
	{
		//Ignore vertex color when we import only the skinning
		constexpr bool bForceIgnoreVertexColor = true;
		SkeletalMeshFactoryNode->SetCustomVertexColorIgnore(bForceIgnoreVertexColor);
		constexpr bool bFalseSetting = false;
		SkeletalMeshFactoryNode->SetCustomVertexColorReplace(bFalseSetting);
	}

	for (int32 LodIndex = 0; LodIndex < LodCount; ++LodIndex)
	{
		ESkeletalMeshGeoImportVersions GeoImportVersion = ESkeletalMeshGeoImportVersions::LatestVersion;
		ESkeletalMeshSkinningImportVersions SkinningImportVersion = ESkeletalMeshSkinningImportVersions::LatestVersion;
		if (bIsReImport && SkeletalMesh->GetImportedModel() && SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(CurrentLodIndex))
		{
			SkeletalMesh->GetLODImportedDataVersions(CurrentLodIndex, GeoImportVersion, SkinningImportVersion);
		}

		FString LodUniqueId = LodDataUniqueIds[LodIndex];
		const UInterchangeSkeletalMeshLodDataNode* LodDataNode = Cast<UInterchangeSkeletalMeshLodDataNode>(Arguments.NodeContainer->GetNode(LodUniqueId));
		if (!LodDataNode)
		{
			UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid LOD when importing SkeletalMesh asset %s"), *Arguments.AssetName);
			continue;
		}

		FString SkeletonNodeUid;
		if (!LodDataNode->GetCustomSkeletonUid(SkeletonNodeUid))
		{
			UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid Skeleton LOD when importing SkeletalMesh asset %s"), *Arguments.AssetName);
			continue;
		}
		const UInterchangeSkeletonFactoryNode* SkeletonNode = Cast<UInterchangeSkeletonFactoryNode>(Arguments.NodeContainer->GetNode(SkeletonNodeUid));
		if (!SkeletonNode)
		{
			UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid Skeleton LOD when importing SkeletalMesh asset %s"), *Arguments.AssetName);
			continue;
		}

		FSoftObjectPath SpecifiedSkeleton;
		SkeletalMeshFactoryNode->GetCustomSkeletonSoftObjectPath(SpecifiedSkeleton);
		bool bSpecifiedSkeleton = SpecifiedSkeleton.IsValid();
		if (SkeletonReference == nullptr)
		{
			UObject* SkeletonObject = nullptr;

			if (SpecifiedSkeleton.IsValid())
			{
				SkeletonObject = SpecifiedSkeleton.TryLoad();
			}
			else if (SkeletonNode->ReferenceObject.IsValid())
			{
				SkeletonObject = SkeletonNode->ReferenceObject.TryLoad();
			}

			if (SkeletonObject)
			{
				SkeletonReference = Cast<USkeleton>(SkeletonObject);

			}
				
			if (!ensure(SkeletonReference))
			{
				UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid Skeleton LOD when importing SkeletalMesh asset %s"), *Arguments.AssetName);
				break;
			}
		}

		FString RootJointNodeId;
		if (!SkeletonNode->GetCustomRootJointUid(RootJointNodeId))
		{
			UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid Skeleton LOD Root Joint when importing SkeletalMesh asset %s"), *Arguments.AssetName);
			continue;
		}

		int32 SkeletonDepth = 0;
		TArray<SkeletalMeshImportData::FBone> RefBonesBinary;
		bool bUseTimeZeroAsBindPose = false;
		SkeletonNode->GetCustomUseTimeZeroForBindPose(bUseTimeZeroAsBindPose);
		bool bDiffPose = false;
		UE::Interchange::Private::ProcessImportMeshSkeleton(SkeletonReference, SkeletalMesh->GetRefSkeleton(), SkeletonDepth, Arguments.NodeContainer, RootJointNodeId, RefBonesBinary, bUseTimeZeroAsBindPose, bDiffPose);
		if (bSpecifiedSkeleton && !SkeletonReference->IsCompatibleMesh(SkeletalMesh))
		{
			UE_LOG(LogInterchangeImport, Warning, TEXT("The skeleton %s is incompatible with the imported skeletalmesh asset %s"), *SkeletonReference->GetName(), *Arguments.AssetName);
		}
				
		TArray<UE::Interchange::Private::FMeshNodeContext> MeshReferences;
		//Scope to query the mesh node
		{
			TArray<FString> MeshUids;
			LodDataNode->GetMeshUids(MeshUids);
			MeshReferences.Reserve(MeshUids.Num());
			for (const FString& MeshUid : MeshUids)
			{
				UE::Interchange::Private::FMeshNodeContext MeshReference;
				MeshReference.MeshNode = Cast<UInterchangeMeshNode>(Arguments.NodeContainer->GetNode(MeshUid));
				if (!MeshReference.MeshNode)
				{
					//The reference is a scene node and we need to bake the geometry
					MeshReference.SceneNode = Cast<UInterchangeSceneNode>(Arguments.NodeContainer->GetNode(MeshUid));
					if (!ensure(MeshReference.SceneNode != nullptr))
					{
						UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid LOD mesh reference when importing SkeletalMesh asset %s"), *Arguments.AssetName);
						continue;
					}
					FString MeshDependencyUid;
					MeshReference.SceneNode->GetCustomAssetInstanceUid(MeshDependencyUid);
					MeshReference.MeshNode = Cast<UInterchangeMeshNode>(Arguments.NodeContainer->GetNode(MeshDependencyUid));
					//Cache the scene node global matrix, we will use this matrix to bake the vertices, add the node geometric mesh offset to this matrix to bake it properly
					FTransform SceneNodeGlobalTransform;
					if (!bUseTimeZeroAsBindPose || !MeshReference.SceneNode->GetCustomTimeZeroGlobalTransform(Arguments.NodeContainer, SceneNodeGlobalTransform))
					{
						ensure(MeshReference.SceneNode->GetCustomGlobalTransform(Arguments.NodeContainer, SceneNodeGlobalTransform));
					}
					FTransform SceneNodeGeometricTransform;
					if(MeshReference.SceneNode->GetCustomGeometricTransform(SceneNodeGeometricTransform))
					{
						SceneNodeGlobalTransform *= SceneNodeGeometricTransform;
					}
					MeshReference.SceneGlobalTransform = SceneNodeGlobalTransform;
				}
				if (!ensure(MeshReference.MeshNode != nullptr))
				{
					UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid LOD mesh reference when importing SkeletalMesh asset %s"), *Arguments.AssetName);
					continue;
				}
				TOptional<FString> MeshPayloadKey = MeshReference.MeshNode->GetPayLoadKey();
				if (MeshPayloadKey.IsSet())
				{
					MeshReference.TranslatorPayloadKey = MeshPayloadKey.GetValue();
				}
				else
				{
					UE_LOG(LogInterchangeImport, Warning, TEXT("Empty LOD mesh reference payload when importing SkeletalMesh asset %s"), *Arguments.AssetName);
					continue;
				}
				MeshReferences.Add(MeshReference);
			}
		}

		//Add the lod mesh data to the skeletalmesh
		FSkeletalMeshImportData SkeletalMeshImportData;
		const bool bSkinControlPointToTimeZero = bUseTimeZeroAsBindPose && bDiffPose;
		//Get all meshes and blend shapes payload and fill the SkeletalMeshImportData structure
		UE::Interchange::Private::RetrieveAllSkeletalMeshPayloadsAndFillImportData(SkeletalMeshFactoryNode
																					, SkeletalMeshImportData
																					, MeshReferences
																					, RefBonesBinary
																					, Arguments
																					, SkeletalMeshTranslatorPayloadInterface
																					, bSkinControlPointToTimeZero
																					, Arguments.NodeContainer
																					, RootJointNodeId);
		//////////////////////////////////////////////////////////////////////////
		//Manage vertex color, we want to use the translated source data
		//Replace -> do nothing
		//Ignore -> remove vertex color from import data (when we re-import, ignore have to put back the current mesh vertex color)
		//Override -> replace the vertex color by the override color
		{
			bool bReplaceVertexColor = false;
			SkeletalMeshFactoryNode->GetCustomVertexColorReplace(bReplaceVertexColor);
			if (!bReplaceVertexColor)
			{
				bool bIgnoreVertexColor = false;
				SkeletalMeshFactoryNode->GetCustomVertexColorIgnore(bIgnoreVertexColor);
				if (bIgnoreVertexColor)
				{
					if (bIsReImport)
					{
						//Get the vertex color we have in the current asset, 
						UE::Interchange::Private::RemapSkeletalMeshVertexColorToImportData(SkeletalMesh, LodIndex, &SkeletalMeshImportData);
					}
					else
					{
						//Flush the vertex color
						SkeletalMeshImportData.bHasVertexColors = false;
						for (SkeletalMeshImportData::FVertex& Wedge : SkeletalMeshImportData.Wedges)
						{
							Wedge.Color = FColor::White;
						}
					}
				}
				else
				{
					FColor OverrideVertexColor;
					if (SkeletalMeshFactoryNode->GetCustomVertexColorOverride(OverrideVertexColor))
					{
						SkeletalMeshImportData.bHasVertexColors = true;
						for(SkeletalMeshImportData::FVertex& Wedge : SkeletalMeshImportData.Wedges)
						{
							Wedge.Color = OverrideVertexColor;
						}
					}
				}
			}

			if (bApplyGeometry)
			{
				// Store whether or not this mesh has vertex colors
				SkeletalMesh->SetHasVertexColors(SkeletalMeshImportData.bHasVertexColors);
				SkeletalMesh->SetVertexColorGuid(SkeletalMesh->GetHasVertexColors() ? FGuid::NewGuid() : FGuid());
			}
		}

		if (bIsReImport)
		{
			while (ImportedResource->LODModels.Num() <= CurrentLodIndex)
			{
				ImportedResource->LODModels.Add(new FSkeletalMeshLODModel());
			}
		}
		else
		{
			ensure(ImportedResource->LODModels.Add(new FSkeletalMeshLODModel()) == CurrentLodIndex);
		}
		FSkeletalMeshLODModel& LODModel = ImportedResource->LODModels[CurrentLodIndex];

		TMap<FString, UMaterialInterface*> AvailableMaterials;
		TArray<FString> FactoryDependencies;
		SkeletalMeshFactoryNode->GetFactoryDependencies(FactoryDependencies);
		for (int32 DependencyIndex = 0; DependencyIndex < FactoryDependencies.Num(); ++DependencyIndex)
		{
			const UInterchangeMaterialFactoryNode* MaterialFactoryNode = Cast<UInterchangeMaterialFactoryNode>(Arguments.NodeContainer->GetNode(FactoryDependencies[DependencyIndex]));
			if (!MaterialFactoryNode || !MaterialFactoryNode->ReferenceObject.IsValid())
			{
				continue;
			}
			if (!MaterialFactoryNode->IsEnabled())
			{
				continue;
			}
			UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(MaterialFactoryNode->ReferenceObject.ResolveObject());
			if (!MaterialInterface)
			{
				continue;
			}
			AvailableMaterials.Add(MaterialFactoryNode->GetDisplayLabel(), MaterialInterface);
		}

		UE::Interchange::Private::ProcessImportMeshMaterials(SkeletalMesh->GetMaterials(), SkeletalMeshImportData, AvailableMaterials);
		UE::Interchange::Private::ProcessImportMeshInfluences(SkeletalMeshImportData.Wedges.Num(), SkeletalMeshImportData.Influences);

		if (bApplyGeometryOnly)
		{
			FSkeletalMeshImportData::ReplaceSkeletalMeshRigImportData(SkeletalMesh, &SkeletalMeshImportData, CurrentLodIndex);
		}
		else if(bApplySkinningOnly)
		{
			FSkeletalMeshImportData::ReplaceSkeletalMeshGeometryImportData(SkeletalMesh, &SkeletalMeshImportData, CurrentLodIndex);
		}

		//Store the original fbx import data the SkelMeshImportDataPtr should not be modified after this
		SkeletalMesh->SaveLODImportedData(CurrentLodIndex, SkeletalMeshImportData);

		if (bApplySkinningOnly)
		{
			SkeletalMesh->SetLODImportedDataVersions(CurrentLodIndex, GeoImportVersion, ESkeletalMeshSkinningImportVersions::LatestVersion);
		}
		else if (bApplyGeometryOnly)
		{
			SkeletalMesh->SetLODImportedDataVersions(CurrentLodIndex, ESkeletalMeshGeoImportVersions::LatestVersion, SkinningImportVersion);
		}
		else
		{
			//We reimport both
			SkeletalMesh->SetLODImportedDataVersions(CurrentLodIndex, ESkeletalMeshGeoImportVersions::LatestVersion, ESkeletalMeshSkinningImportVersions::LatestVersion);
		}

		auto AddLodInfo = [&SkeletalMesh]()
		{
			FSkeletalMeshLODInfo& NewLODInfo = SkeletalMesh->AddLODInfo();
			NewLODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
			NewLODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
			NewLODInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
			NewLODInfo.LODHysteresis = 0.02f;
			NewLODInfo.bImportWithBaseMesh = true;
		};

		if (bIsReImport)
		{
			while (SkeletalMesh->GetLODNum() <= CurrentLodIndex)
			{
				AddLodInfo();
			}
		}
		else
		{
			AddLodInfo();
		}

		TArray<FSkeletalMaterial>& Materials = SkeletalMesh->GetMaterials();
		const TArray<SkeletalMeshImportData::FMaterial>& ImportedMaterials = SkeletalMeshImportData.Materials;
		if (FSkeletalMeshLODInfo* LodInfo = SkeletalMesh->GetLODInfo(CurrentLodIndex))
		{
			LodInfo->LODMaterialMap.Empty();
			// Now set up the material mapping array.
			for (int32 ImportedMaterialIndex = 0; ImportedMaterialIndex < ImportedMaterials.Num(); ImportedMaterialIndex++)
			{
				FName ImportedMaterialName = *(ImportedMaterials[ImportedMaterialIndex].MaterialImportName);
				//Match by name
				int32 LODMatIndex = INDEX_NONE;
				for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
				{
					const FSkeletalMaterial& SkeletalMaterial = Materials[MaterialIndex];
					if (SkeletalMaterial.ImportedMaterialSlotName != NAME_None && SkeletalMaterial.ImportedMaterialSlotName == ImportedMaterialName)
					{
						LODMatIndex = MaterialIndex;
						break;
					}
				}
				// If we dont have a match, add a new entry to the material list.
				if (LODMatIndex == INDEX_NONE)
				{
					LODMatIndex = Materials.Add(FSkeletalMaterial(ImportedMaterials[ImportedMaterialIndex].Material.Get(), true, false, ImportedMaterialName, ImportedMaterialName));
				}
				LodInfo->LODMaterialMap.Add(LODMatIndex);
			}
		}

		//Add the bound to the skeletal mesh
		if (SkeletalMesh->GetImportedBounds().BoxExtent.IsNearlyZero())
		{
			FBox3f BoundingBox(SkeletalMeshImportData.Points.GetData(), SkeletalMeshImportData.Points.Num());
			const FVector3f BoundingBoxSize = BoundingBox.GetSize();

			if (SkeletalMeshImportData.Points.Num() > 2 && BoundingBoxSize.X < THRESH_POINTS_ARE_SAME && BoundingBoxSize.Y < THRESH_POINTS_ARE_SAME && BoundingBoxSize.Z < THRESH_POINTS_ARE_SAME)
			{
				//TODO log a user error
				//AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, FText::Format(LOCTEXT("FbxSkeletaLMeshimport_ErrorMeshTooSmall", "Cannot import this mesh, the bounding box of this mesh is smaller than the supported threshold[{0}]."), FText::FromString(FString::Printf(TEXT("%f"), THRESH_POINTS_ARE_SAME)))), FFbxErrors::SkeletalMesh_FillImportDataFailed);
			}
			SkeletalMesh->SetImportedBounds(FBoxSphereBounds((FBox)BoundingBox));
		}

		CurrentLodIndex++;
	}

	if(SkeletonReference)
	{
		SkeletonReference->MergeAllBonesToBoneTree(SkeletalMesh);
		if (SkeletalMesh->GetSkeleton() != SkeletonReference)
		{
			SkeletalMesh->SetSkeleton(SkeletonReference);
		}
	}
	else
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Interchange Import UInterchangeSkeletalMeshFactory::CreateAsset, USkeleton* SkeletonReference is nullptr."));
	}

	SkeletalMesh->CalculateInvRefMatrices();

	if (!bIsReImport)
	{
		/** Apply all SkeletalMeshFactoryNode custom attributes to the skeletal mesh asset */
		SkeletalMeshFactoryNode->ApplyAllCustomAttributeToObject(SkeletalMesh);

		bool bCreatePhysicsAsset = false;
		SkeletalMeshFactoryNode->GetCustomCreatePhysicsAsset(bCreatePhysicsAsset);

		if (!bCreatePhysicsAsset)
		{
			FSoftObjectPath SpecifiedPhysicAsset;
			SkeletalMeshFactoryNode->GetCustomPhysicAssetSoftObjectPath(SpecifiedPhysicAsset);
			if (SpecifiedPhysicAsset.IsValid())
			{
				UPhysicsAsset* PhysicsAsset = Cast<UPhysicsAsset>(SpecifiedPhysicAsset.TryLoad());
				SkeletalMesh->SetPhysicsAsset(PhysicsAsset);
			}
		}
	}
	else
	{
		//Apply the re import strategy 
		UInterchangeAssetImportData* InterchangeAssetImportData = Cast<UInterchangeAssetImportData>(SkeletalMesh->GetAssetImportData());
		UInterchangeBaseNode* PreviousNode = nullptr;
		if (InterchangeAssetImportData)
		{
			PreviousNode = InterchangeAssetImportData->NodeContainer->GetNode(InterchangeAssetImportData->NodeUniqueID);
		}
		UInterchangeBaseNode* CurrentNode = NewObject<UInterchangeBaseNode>(GetTransientPackage(), UInterchangeSkeletalMeshFactoryNode::StaticClass());
		UInterchangeBaseNode::CopyStorage(SkeletalMeshFactoryNode, CurrentNode);
		CurrentNode->FillAllCustomAttributeFromObject(SkeletalMesh);
		UE::Interchange::FFactoryCommon::ApplyReimportStrategyToAsset(SkeletalMesh, PreviousNode, CurrentNode, SkeletalMeshFactoryNode);
	}
		
	//Getting the file Hash will cache it into the source data
	Arguments.SourceData->GetFileContentHash();

	//The interchange completion task (call in the GameThread after the factories pass), will call PostEditChange which will trig another asynchronous system that will build all material in parallel
	return SkeletalMeshObject;

#endif //else !WITH_EDITOR || !WITH_EDITORONLY_DATA
}

/* This function is call in the completion task on the main thread, use it to call main thread post creation step for your assets*/
void UInterchangeSkeletalMeshFactory::PreImportPreCompletedCallback(const FImportPreCompletedCallbackParams& Arguments)
{
	check(IsInGameThread());
	Super::PreImportPreCompletedCallback(Arguments);

	//TODO make sure this work at runtime
#if WITH_EDITORONLY_DATA
	if (ensure(Arguments.ImportedObject && Arguments.SourceData))
	{
		//We must call the Update of the asset source file in the main thread because UAssetImportData::Update execute some delegate we do not control
		USkeletalMesh* SkeletalMesh = CastChecked<USkeletalMesh>(Arguments.ImportedObject);

		UAssetImportData* ImportDataPtr = SkeletalMesh->GetAssetImportData();
		UE::Interchange::FFactoryCommon::FUpdateImportAssetDataParameters UpdateImportAssetDataParameters(SkeletalMesh
																										  , ImportDataPtr
																										  , Arguments.SourceData
																										  , Arguments.NodeUniqueID
																										  , Arguments.NodeContainer
																										  , Arguments.Pipelines);

		ImportDataPtr = UE::Interchange::FFactoryCommon::UpdateImportAssetData(UpdateImportAssetDataParameters, [&Arguments, SkeletalMesh](UInterchangeAssetImportData* AssetImportData)
			{
				auto GetSourceIndexFromContentType = [](const EInterchangeSkeletalMeshContentType& ImportContentType)->int32
				{
					return (ImportContentType == EInterchangeSkeletalMeshContentType::Geometry)
						? 1
						: (ImportContentType == EInterchangeSkeletalMeshContentType::SkinningWeights)
						? 2
						: 0;
				};

				auto GetSourceLabelFromSourceIndex = [](const int32 SourceIndex)->FString
				{
					return (SourceIndex == 1)
						? NSSkeletalMeshSourceFileLabels::GeometryText().ToString()
						: (SourceIndex == 2)
						? NSSkeletalMeshSourceFileLabels::SkinningText().ToString()
						: NSSkeletalMeshSourceFileLabels::GeoAndSkinningText().ToString();
				};

				if (UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = Cast<UInterchangeSkeletalMeshFactoryNode>(Arguments.NodeContainer->GetNode(Arguments.NodeUniqueID)))
				{
					EInterchangeSkeletalMeshContentType ImportContentType = EInterchangeSkeletalMeshContentType::All;
					SkeletalMeshFactoryNode->GetCustomImportContentType(ImportContentType);
					const FString& NewSourceFilename = Arguments.SourceData->GetFilename();
					const int32 NewSourceIndex = GetSourceIndexFromContentType(ImportContentType);
					//NewSourceIndex should be 0, 1 or 2 (All, Geo, Skinning)
					check(NewSourceIndex >= 0 && NewSourceIndex < 3);
					const FString DefaultFilename = AssetImportData->ScriptGetFirstFilename();
					const TArray<FString> OldFilenames = AssetImportData->ScriptExtractFilenames();
					for (int32 SourceIndex = 0; SourceIndex < 3; ++SourceIndex)
					{
						FString SourceLabel = GetSourceLabelFromSourceIndex(SourceIndex);
						if (SourceIndex == NewSourceIndex)
						{
							AssetImportData->ScriptedAddFilename(NewSourceFilename, SourceIndex, SourceLabel);
						}
						else
						{
							//Extract filename create a default path if the FSourceFile::RelativeFilename is empty
							//We want to fill the entry with the base source file (SourceIndex 0, All) in this case.
							const bool bValidOldFilename = AssetImportData->SourceData.SourceFiles.IsValidIndex(SourceIndex)
								&& !AssetImportData->SourceData.SourceFiles[SourceIndex].RelativeFilename.IsEmpty()
								&& OldFilenames.IsValidIndex(SourceIndex);
							const FString& OldFilename = bValidOldFilename ? OldFilenames[SourceIndex] : DefaultFilename;
							AssetImportData->ScriptedAddFilename(OldFilename, SourceIndex, SourceLabel);
						}
					}
				}
			});

		
		SkeletalMesh->SetAssetImportData(ImportDataPtr);
	}
#endif
}
