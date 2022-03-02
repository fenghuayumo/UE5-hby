// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/RingBuffer.h"
#include "Engine/DataAsset.h"
#include "Modules/ModuleManager.h"
#include "Misc/EnumClassFlags.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimMetaData.h"
#include "Animation/AnimNodeMessages.h"
#include "Animation/AnimationAsset.h"
#include "Animation/MotionTrajectoryTypes.h"
#include "AlphaBlend.h"
#include "BoneIndices.h"
#include "GameplayTagContainer.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"

#include "PoseSearch.generated.h"

class UAnimSequence;
struct FCompactPose;
struct FPoseContext;
struct FReferenceSkeleton;

DECLARE_LOG_CATEGORY_EXTERN(LogPoseSearch, Log, All);

namespace UE { namespace PoseSearch {

class FPoseHistory;

}}

UENUM()
enum class EPoseSearchFeatureType : int32
{
	Position,	
	Rotation,
	LinearVelocity,
	AngularVelocity,
	ForwardVector,

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};

UENUM()
enum class EPoseSearchFeatureDomain : int32
{
	Time,
	Distance,

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};

UENUM()
enum class EPoseSearchBooleanRequest : int32
{
	FalseValue,
	TrueValue,
	Indifferent, // if this is used, there will be no cost difference between true and false results

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};


/** Describes each feature of a vector, including data type, sampling options, and buffer offset. */
USTRUCT()
struct POSESEARCH_API FPoseSearchFeatureDesc
{
	GENERATED_BODY()

	static constexpr int32 TrajectoryBoneIndex = -1; 

	UPROPERTY()
	int32 SchemaBoneIdx = 0;

	UPROPERTY()
	int32 SubsampleIdx = 0;

	UPROPERTY()
	EPoseSearchFeatureType Type = EPoseSearchFeatureType::Invalid;

	UPROPERTY()
	EPoseSearchFeatureDomain Domain = EPoseSearchFeatureDomain::Invalid;

	UPROPERTY()
	int8 ChannelIdx = 0;

	// Set via FPoseSearchFeatureLayout::Init() and ignored by operator==
	UPROPERTY()
	int32 ValueOffset = 0;

	bool operator==(const FPoseSearchFeatureDesc& Other) const;

	bool IsValid() const { return Type != EPoseSearchFeatureType::Invalid; }
};


/**
* Explicit description of a pose feature vector.
* Determined by options set in a UPoseSearchSchema and owned by the schema.
* See UPoseSearchSchema::GenerateLayout().
*/
USTRUCT()
struct POSESEARCH_API FPoseSearchFeatureVectorLayout
{
	GENERATED_BODY()

	void Init();
	void Reset();

	UPROPERTY()
	TArray<FPoseSearchFeatureDesc> Features;

	UPROPERTY()
	int32 NumFloats = 0;

	UPROPERTY()
	int32 NumChannels = 0;

	bool IsValid(int32 MaxNumBones) const;

	bool EnumerateBy(int32 ChannelIdx, EPoseSearchFeatureType Type, int32& InOutFeatureIdx) const;
};

UENUM()
enum class EPoseSearchDataPreprocessor : int32
{
	None,
	Automatic,
	Normalize,
	Sphere UMETA(Hidden),

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};

USTRUCT()
struct POSESEARCH_API FPoseSearchBone
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Config)
	FBoneReference Reference;
	
	UPROPERTY(EditAnywhere, Category = Config)
	bool bUseVelocity = false;
	
	UPROPERTY(EditAnywhere, Category = Config)
	bool bUsePosition = false;
	
	UPROPERTY(EditAnywhere, Category = Config)
	bool bUseRotation = false;

	// this function will return a mask out of EPoseSearchFeatureType based on which features were selected for
	// the bone.
	uint32 GetTypeMask() const;
};

/**
* Specifies the format of a pose search index. At runtime, queries are built according to the schema for searching.
*/
UCLASS(BlueprintType, Category = "Animation|Pose Search", Experimental)
class POSESEARCH_API UPoseSearchSchema : public UDataAsset, public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

public:

	static constexpr int32 DefaultSampleRate = 10;

	UPROPERTY(EditAnywhere, Category = "Schema")
	TObjectPtr<USkeleton> Skeleton = nullptr;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "1", ClampMax = "60"), Category = "Schema")
	int32 SampleRate = DefaultSampleRate;

	UPROPERTY()
	bool bUseBoneVelocities_DEPRECATED = true;

	UPROPERTY()
	bool bUseBonePositions_DEPRECATED = true;

	UPROPERTY(EditAnywhere, Category = "Schema")
	bool bUseTrajectoryVelocities = true;

	UPROPERTY(EditAnywhere, Category = "Schema")
	bool bUseTrajectoryPositions = true;

	UPROPERTY(EditAnywhere, Category = "Schema", DisplayName="Use Facing Directions")
	bool bUseTrajectoryForwardVectors = false;
	
	UPROPERTY(EditAnywhere, Category = "Schema")
	TArray<FPoseSearchBone> SampledBones;
	
	UPROPERTY()
	TArray<FBoneReference> Bones_DEPRECATED;

	UPROPERTY(EditAnywhere, Category = "Schema")
	TArray<float> PoseSampleTimes;

	UPROPERTY(EditAnywhere, Category = "Schema")
	TArray<float> TrajectorySampleTimes;

	UPROPERTY(EditAnywhere, Category = "Schema")
	TArray<float> TrajectorySampleDistances;

	// If set, this schema will support mirroring pose search databases
	UPROPERTY(EditAnywhere, Category = "Schema")
	TObjectPtr<UMirrorDataTable> MirrorDataTable;

	UPROPERTY(EditAnywhere, Category = "Schema")
	EPoseSearchDataPreprocessor DataPreprocessor = EPoseSearchDataPreprocessor::Automatic;

	UPROPERTY()
	EPoseSearchDataPreprocessor EffectiveDataPreprocessor = EPoseSearchDataPreprocessor::Invalid;

	UPROPERTY()
	float SamplingInterval = 1.0f / DefaultSampleRate;

	UPROPERTY()
	FPoseSearchFeatureVectorLayout Layout;

	UPROPERTY(Transient)
	TArray<uint16> BoneIndices;

	UPROPERTY(Transient)
	TArray<uint16> BoneIndicesWithParents;


	bool IsValid () const;

	int32 GetNumBones () const { return BoneIndices.Num(); }

	// Returns farthest future sample time >= 0.0f.
	// Returns a negative value when there are no future sample times.
	float GetTrajectoryFutureTimeHorizon () const;

	// Returns farthest past sample time <= 0.0f.
	// Returns a positive value when there are no past sample times.
	float GetTrajectoryPastTimeHorizon () const;

	// Returns farthest future sample distance >= 0.0f.
	// Returns a negative value when there are no future sample distances.
	float GetTrajectoryFutureDistanceHorizon () const;

	// Returns farthest path sample distance <= 0.0f.
	// Returns a positive value when there are no past sample distances.
	float GetTrajectoryPastDistanceHorizon () const;

	TArrayView<const float> GetChannelSampleOffsets (int32 ChannelIdx) const;

public: // UObject
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	virtual void PostLoad() override;

public: // IBoneReferenceSkeletonProvider
	class USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override { bInvalidSkeletonIsError = false; return Skeleton; }

private:
	void GenerateLayout();
	void ResolveBoneReferences();
};

USTRUCT()
struct POSESEARCH_API FPoseSearchIndexPreprocessInfo
{
	GENERATED_BODY()

	UPROPERTY()
	int32 NumDimensions = 0;

	UPROPERTY()
	TArray<float> TransformationMatrix;

	UPROPERTY()
	TArray<float> InverseTransformationMatrix;

	UPROPERTY()
	TArray<float> SampleMean;

	void Reset()
	{
		NumDimensions = 0;
		TransformationMatrix.Reset();
		InverseTransformationMatrix.Reset();
		SampleMean.Reset();
	}
};

UENUM()
enum class EPoseSearchPoseFlags : uint32
{
	None = 0,

	// Don't return this pose as a search result
	BlockTransition = 1 << 0,
};
ENUM_CLASS_FLAGS(EPoseSearchPoseFlags);

// This is kept for each pose in the search index along side the feature vector values and is used to influence the
// search.
USTRUCT()
struct POSESEARCH_API FPoseSearchPoseMetadata
{
	GENERATED_BODY()

	UPROPERTY()
	EPoseSearchPoseFlags Flags = EPoseSearchPoseFlags::None;

	UPROPERTY()
	float CostAddend = 0.0f;
};

/**
* Information about a source animation asset used by a search index.
* Some source animation entries may generate multiple FPoseSearchIndexAsset entries.
**/
USTRUCT()
struct POSESEARCH_API FPoseSearchIndexAsset
{
	GENERATED_BODY()
public:
	FPoseSearchIndexAsset()
	{}

	FPoseSearchIndexAsset(
		int32 InSourceGroupIdx, 
		int32 InSourceAssetIdx, 
		bool bInMirrored, 
		const FFloatInterval& InSamplingInterval)
		: SourceGroupIdx(InSourceGroupIdx)
		, SourceAssetIdx(InSourceAssetIdx)
		, bMirrored(bInMirrored)
		, SamplingInterval(InSamplingInterval)
	{}

	UPROPERTY()
	int32 SourceGroupIdx = INDEX_NONE;

	// Index of the source asset in search index's container (i.e. UPoseSearchDatabase)
	UPROPERTY()
	int32 SourceAssetIdx = INDEX_NONE;

	UPROPERTY()
	bool bMirrored = false;

	UPROPERTY()
	FFloatInterval SamplingInterval;

	UPROPERTY()
	int32 FirstPoseIdx = INDEX_NONE;

	UPROPERTY()
	int32 NumPoses = 0;

	bool IsPoseInRange(int32 PoseIdx) const
	{
		return (PoseIdx >= FirstPoseIdx) && (PoseIdx < FirstPoseIdx + NumPoses);
	}
};

/**
* A search index for animation poses. The structure of the search index is determined by its UPoseSearchSchema.
* May represent a single animation (see UPoseSearchSequenceMetaData) or a collection (see UPoseSearchDatabase).
*/
USTRUCT()
struct POSESEARCH_API FPoseSearchIndex
{
	GENERATED_BODY()

	UPROPERTY()
	int32 NumPoses = 0;

	UPROPERTY()
	TArray<float> Values;

	UPROPERTY()
	TArray<FPoseSearchPoseMetadata> PoseMetadata;

	UPROPERTY()
	TObjectPtr<const UPoseSearchSchema> Schema = nullptr;

	UPROPERTY()
	FPoseSearchIndexPreprocessInfo PreprocessInfo;

	UPROPERTY()
	TArray<FPoseSearchIndexAsset> Assets;

	bool IsValid() const;

	TArrayView<const float> GetPoseValues(int32 PoseIdx) const;

	int32 FindAssetIndex(const FPoseSearchIndexAsset* Asset) const;
	const FPoseSearchIndexAsset* FindAssetForPose(int32 PoseIdx) const;
	float GetTimeOffset(int32 PoseIdx, const FPoseSearchIndexAsset* Asset) const;

	void Reset();

	void Normalize (TArrayView<float> PoseVector) const;
	void InverseNormalize (TArrayView<float> PoseVector) const;
};

USTRUCT()
struct FPoseSearchExtrapolationParameters
{
	GENERATED_BODY()

public:
	// If the angular root motion speed in degrees is below this value, it will be treated as zero.
	UPROPERTY(EditAnywhere, Category = "Settings")
	float AngularSpeedThreshold = 1.0f;
	
	// If the root motion linear speed is below this value, it will be treated as zero.
	UPROPERTY(EditAnywhere, Category = "Settings")
	float LinearSpeedThreshold = 1.0f;

	// Time from sequence start/end used to extrapolate the trajectory.
	UPROPERTY(EditAnywhere, Category = "Settings")
	float SampleTime = 0.05f;
};

USTRUCT()
struct FPoseSearchBlockTransitionParameters
{
	GENERATED_BODY()

	// Excluding the beginning of sequences can help ensure an exact past trajectory is used when building the features
	UPROPERTY(EditAnywhere, Category = "Settings")
	float SequenceStartInterval = 0.0f;

	// Excluding the end of sequences help ensure an exact future trajectory, and also prevents the selection of
	// a sequence which will end too soon to be worth selecting.
	UPROPERTY(EditAnywhere, Category = "Settings")
	float SequenceEndInterval = 0.2f;

};

/** Animation metadata object for indexing a single animation. */
UCLASS(BlueprintType, Category = "Animation|Pose Search", Experimental)
class POSESEARCH_API UPoseSearchSequenceMetaData : public UAnimMetaData
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category="Settings")
	TObjectPtr<const UPoseSearchSchema> Schema = nullptr;

	UPROPERTY(EditAnywhere, Category="Settings")
	FFloatInterval SamplingRange = FFloatInterval(0.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category = "Settings")
	FPoseSearchExtrapolationParameters ExtrapolationParameters;

	UPROPERTY()
	FPoseSearchIndex SearchIndex;

	bool IsValidForIndexing() const;
	bool IsValidForSearch() const;

public: // UObject
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
};

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchChannelHorizonParams
{
	GENERATED_BODY()

	// Total score contribution of all samples within this horizon, normalized with other horizons
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	// Whether to interpolate samples within this horizon
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Advanced")
	bool bInterpolate = false;

	// Horizon sample weights will be interpolated from InitialValue to 1.0 - InitialValue and then normalized
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Advanced", meta = (EditCondition = "bInterpolate", ClampMin="0.0", ClampMax="1.0"))
	float InitialValue = 0.1f;

	// Curve type for horizon interpolation 
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Advanced", meta = (EditCondition = "bInterpolate"))
	EAlphaBlendOption InterpolationMethod = EAlphaBlendOption::Linear;
};

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchChannelWeightParams
{
	GENERATED_BODY()

	// Contribution of this score component. Normalized with other channels.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (ClampMin = "0.0"))
	float ChannelWeight = 1.0f;

	// History horizon params (for sample offsets <= 0)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FPoseSearchChannelHorizonParams HistoryParams;

	// Prediction horizon params (for sample offsets > 0)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FPoseSearchChannelHorizonParams PredictionParams;

	// Contribution of each type within this channel
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	TMap<EPoseSearchFeatureType, float> TypeWeights;

	FPoseSearchChannelWeightParams();
};

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchChannelDynamicWeightParams
{
	GENERATED_BODY()

	// Multiplier for the contribution of this score component. Final weight will be normalized with other channels after scaling.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (ClampMin = "0.0"))
	float ChannelWeightScale = 1.0f;

	// Multiplier for history score contribution. Normalized with prediction weight after scaling.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (ClampMin = "0.0"))
	float HistoryWeightScale = 1.0f;

	// Multiplier for prediction score contribution. Normalized with history weight after scaling.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (ClampMin = "0.0"))
	float PredictionWeightScale = 1.0f;

	bool operator==(const FPoseSearchChannelDynamicWeightParams& Rhs) const
	{
		return (ChannelWeightScale == Rhs.ChannelWeightScale)
			&& (HistoryWeightScale == Rhs.HistoryWeightScale)
			&& (PredictionWeightScale == Rhs.PredictionWeightScale);
	}
	bool operator!=(const FPoseSearchChannelDynamicWeightParams& Rhs) const { return !(*this == Rhs); }
};

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchWeightParams
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FPoseSearchChannelWeightParams PoseWeight;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FPoseSearchChannelWeightParams TrajectoryWeight;

	FPoseSearchWeightParams();
};

USTRUCT(BlueprintType, Category="Animation|Pose Search")
struct POSESEARCH_API FPoseSearchDynamicWeightParams
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FPoseSearchChannelDynamicWeightParams PoseDynamicWeights;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FPoseSearchChannelDynamicWeightParams TrajectoryDynamicWeights;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	bool bDebugDisableWeights = false;

	bool operator==(const FPoseSearchDynamicWeightParams& Rhs) const
	{
		return (PoseDynamicWeights == Rhs.PoseDynamicWeights)
			&& (TrajectoryDynamicWeights == Rhs.TrajectoryDynamicWeights)
			&& (bDebugDisableWeights == Rhs.bDebugDisableWeights);
	}
	bool operator!=(const FPoseSearchDynamicWeightParams& Rhs) const { return !(*this == Rhs); }
};

USTRUCT()
struct POSESEARCH_API FPoseSearchWeights
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<float> Weights;

	bool IsInitialized() const { return !Weights.IsEmpty(); }
	void Init(const FPoseSearchWeightParams& WeightParams, const UPoseSearchSchema* Schema, const FPoseSearchDynamicWeightParams& RuntimeParams);
};

USTRUCT()
struct POSESEARCH_API FPoseSearchWeightsContext
{
	GENERATED_BODY()

public:
	// Check if the database or runtime weight parameters have changed and then computes and caches new group weights
	void Update(const FPoseSearchDynamicWeightParams& DynamicWeights, const UPoseSearchDatabase * Database);

	const FPoseSearchWeights* GetGroupWeights (int32 WeightsGroupIdx) const;
	
private:
	UPROPERTY(Transient)
	TWeakObjectPtr<const UPoseSearchDatabase> Database = nullptr;

	UPROPERTY(Transient)
	FPoseSearchDynamicWeightParams DynamicWeights;

	UPROPERTY(Transient)
	FPoseSearchWeights ComputedDefaultGroupWeights;

	UPROPERTY(Transient)
	TArray<FPoseSearchWeights> ComputedGroupWeights;
};

UENUM()
enum class EPoseSearchMirrorOption : int32
{
	UnmirroredOnly UMETA(DisplayName="Original Only"),
	MirroredOnly UMETA(DisplayName="Mirrored Only"),
	UnmirroredAndMirrored UMETA(DisplayName="Original and Mirrored"),
	
	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};

/** An entry in a UPoseSearchDatabase. */
USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchDatabaseSequence
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Sequence")
	TObjectPtr<UAnimSequence> Sequence = nullptr;

	UPROPERTY(EditAnywhere, Category="Sequence")
	FFloatInterval SamplingRange = FFloatInterval(0.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category="Sequence")
	bool bLoopAnimation = false;

	UPROPERTY(EditAnywhere, Category = "Sequence")
	EPoseSearchMirrorOption MirrorOption = EPoseSearchMirrorOption::UnmirroredOnly;

	// Used for sampling past pose information at the beginning of the main sequence.
	// This setting is intended for transitions between cycles. It is optional and only used
	// for one shot anims with past sampling. When past sampling is used without a lead in sequence,
	// the sampling range of the main sequence will be clamped if necessary.
	UPROPERTY(EditAnywhere, Category="Sequence")
	TObjectPtr<UAnimSequence> LeadInSequence = nullptr;

	UPROPERTY(EditAnywhere, Category="Sequence")
	bool bLoopLeadInAnimation = false;

	// Used for sampling future pose information at the end of the main sequence.
	// This setting is intended for transitions between cycles. It is optional and only used
	// for one shot anims with future sampling. When future sampling is used without a follow up sequence,
	// the sampling range of the main sequence will be clamped if necessary.
	UPROPERTY(EditAnywhere, Category="Sequence")
	TObjectPtr<UAnimSequence> FollowUpSequence = nullptr;

	UPROPERTY(EditAnywhere, Category="Sequence")
	bool bLoopFollowUpAnimation = false;

	UPROPERTY(EditAnywhere, Category = "Group")
	FGameplayTagContainer GroupTags;

	FFloatInterval GetEffectiveSamplingRange() const;
};

USTRUCT()
struct POSESEARCH_API FPoseSearchDatabaseGroup
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bUseGroupWeights = false;

	UPROPERTY(EditAnywhere, Category = "Settings", meta=(EditCondition="bUseGroupWeights", EditConditionHides))
	FPoseSearchWeightParams Weights;
};

/** A data asset for indexing a collection of animation sequences. */
UCLASS(BlueprintType, Category = "Animation|Pose Search", Experimental)
class POSESEARCH_API UPoseSearchDatabase : public UDataAsset
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Database")
	const UPoseSearchSchema* Schema;

	UPROPERTY(EditAnywhere, Category = "Database")
	FPoseSearchWeightParams DefaultWeights;

	// If there's a mirroring mismatch between the currently playing sequence and a search candidate, this cost will be 
	// added to the candidate, making it less likely to be selected
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Database")
	float MirroringMismatchCost = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Database")
	FPoseSearchExtrapolationParameters ExtrapolationParameters;

	UPROPERTY(EditAnywhere, Category = "Database")
	FPoseSearchBlockTransitionParameters BlockTransitionParameters;

	UPROPERTY(EditAnywhere, Category = "Database")
	TArray<FPoseSearchDatabaseGroup> Groups;

	// Drag and drop animations here to add them in bulk to Sequences
	UPROPERTY(EditAnywhere, Category = "Database", DisplayName="Drag And Drop Anims Here")
	TArray<TObjectPtr<UAnimSequence>> SimpleSequences;

	UPROPERTY(EditAnywhere, Category="Database")
	TArray<FPoseSearchDatabaseSequence> Sequences;

	UPROPERTY()
	FPoseSearchIndex SearchIndex;

	int32 FindSequenceForPose(int32 PoseIdx) const;
	float GetSequenceLength(int32 DbSequenceIdx) const;
	bool DoesSequenceLoop(int32 DbSequenceIdx) const;

	bool IsValidForIndexing() const;
	bool IsValidForSearch() const;

	int32 GetPoseIndexFromAssetTime(float AssetTime, const FPoseSearchIndexAsset* SearchIndexAsset) const;
	float GetTimeOffset(int32 PoseIdx, const FPoseSearchIndexAsset* SearchIndexAsset = nullptr) const;
	const FPoseSearchDatabaseSequence& GetSourceAsset(const FPoseSearchIndexAsset* SearchIndexAsset) const;

public: // UObject
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void CollectSimpleSequences();

public:
	// Populates the FPoseSearchIndex::Assets array by evaluating the data in the Sequences array
	bool TryInitSearchIndexAssets();
};

/** 
* Helper object for writing features into a float buffer according to a feature vector layout.
* Keeps track of which features are present, allowing the feature vector to be built up piecemeal.
* FFeatureVectorBuilder is used to build search queries at runtime and for adding samples during search index construction.
*/

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchFeatureVectorBuilder
{
	GENERATED_BODY()
public:
	void Init(const UPoseSearchSchema* Schema);
	void Reset();
	void ResetFeatures();

	const UPoseSearchSchema* GetSchema() const { return Schema.Get(); }

	TArrayView<const float> GetValues() const { return Values; }
	TArrayView<const float> GetNormalizedValues() const { return ValuesNormalized; }

	void SetTransform(FPoseSearchFeatureDesc Feature, const FTransform& Transform);
	void SetTransformVelocity(FPoseSearchFeatureDesc Feature, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime);
	void SetPosition(FPoseSearchFeatureDesc Feature, const FVector& Translation);
	void SetRotation(FPoseSearchFeatureDesc Feature, const FQuat& Rotation);
	void SetLinearVelocity(FPoseSearchFeatureDesc Feature, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime);
	void SetAngularVelocity(FPoseSearchFeatureDesc Feature, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime);
	void SetVector(FPoseSearchFeatureDesc Feature, const FVector& Vector);
	void BuildFromTrajectory(const FTrajectorySampleRange& Trajectory);
	bool TrySetPoseFeatures(UE::PoseSearch::FPoseHistory* History, const FBoneContainer& BoneContainer);

	void CopyFromSearchIndex(const FPoseSearchIndex& SearchIndex, int32 PoseIdx);
	void CopyFeature(const FPoseSearchFeatureVectorBuilder& OtherBuilder, int32 FeatureIdx);

	void MergeReplace(const FPoseSearchFeatureVectorBuilder& OtherBuilder);

	bool IsInitialized() const;
	bool IsInitializedForSchema(const UPoseSearchSchema* Schema) const;
	bool IsComplete() const;
	bool IsCompatible(const FPoseSearchFeatureVectorBuilder& OtherBuilder) const;

	void Normalize(const FPoseSearchIndex& ForSearchIndex);

private:
	void BuildFromTrajectoryTimeBased(const FTrajectorySampleRange& Trajectory);
	void BuildFromTrajectoryDistanceBased(const FTrajectorySampleRange& Trajectory);

	UPROPERTY(Transient)
	TWeakObjectPtr<const UPoseSearchSchema> Schema = nullptr;

	TArray<float> Values;
	TArray<float> ValuesNormalized;
	TBitArray<> FeaturesAdded;
	int32 NumFeaturesAdded = 0;
};


namespace UE { namespace PoseSearch {

/** Records poses over time in a ring buffer. FFeatureVectorBuilder uses this to sample from the present or past poses according to the search schema. */
class POSESEARCH_API FPoseHistory
{
public:
	void Init(int32 InNumPoses, float InTimeHorizon);
	void Init(const FPoseHistory& History);
	bool TrySamplePose(float SecondsAgo, const FReferenceSkeleton& RefSkeleton, const TArray<FBoneIndexType>& RequiredBones);

	void Update(float SecondsElapsed, const FPoseContext& PoseContext);
	float GetSampleTimeInterval() const;
	TArrayView<const FTransform> GetLocalPoseSample() const { return SampledLocalPose; }
	TArrayView<const FTransform> GetComponentPoseSample() const { return SampledComponentPose; }
	TArrayView<const FTransform> GetPrevLocalPoseSample() const { return SampledPrevLocalPose; }
	TArrayView<const FTransform> GetPrevComponentPoseSample() const { return SampledPrevComponentPose; }
	float GetTimeHorizon() const { return TimeHorizon; }
	FPoseSearchFeatureVectorBuilder& GetQueryBuilder() { return QueryBuilder; }

private:
	bool TrySampleLocalPose(float Time, const TArray<FBoneIndexType>& RequiredBones, TArray<FTransform>& LocalPose);

	struct FPose
	{
		TArray<FTransform> LocalTransforms;
	};

	TRingBuffer<FPose> Poses;
	TRingBuffer<float> Knots;
	TArray<FTransform> SampledLocalPose;
	TArray<FTransform> SampledComponentPose;
	TArray<FTransform> SampledPrevLocalPose;
	TArray<FTransform> SampledPrevComponentPose;
	FPoseSearchFeatureVectorBuilder QueryBuilder;

	float TimeHorizon = 0.0f;
};

class IPoseHistoryProvider : public UE::Anim::IGraphMessage
{
	DECLARE_ANIMGRAPH_MESSAGE(IPoseHistoryProvider);

public:

	virtual const FPoseHistory& GetPoseHistory() const = 0;
	virtual FPoseHistory& GetPoseHistory() = 0;
};


/** Helper object for extracting features from a float buffer according to the feature vector layout. */
class POSESEARCH_API FFeatureVectorReader
{
public:
	void Init(const FPoseSearchFeatureVectorLayout* Layout);
	void SetValues(TArrayView<const float> Values);
	bool IsValid() const;

	bool GetTransform(FPoseSearchFeatureDesc Feature, FTransform* OutTransform) const;
	bool GetPosition(FPoseSearchFeatureDesc Feature, FVector* OutPosition) const;
	bool GetRotation(FPoseSearchFeatureDesc Feature, FQuat* OutRotation) const;
	bool GetForwardVector(FPoseSearchFeatureDesc Feature, FVector* OutForwardVector) const;
	bool GetLinearVelocity(FPoseSearchFeatureDesc Feature, FVector* OutLinearVelocity) const;
	bool GetAngularVelocity(FPoseSearchFeatureDesc Feature, FVector* OutAngularVelocity) const;
	bool GetVector(FPoseSearchFeatureDesc Feature, FVector* OutVector) const;

	const FPoseSearchFeatureVectorLayout* GetLayout() const { return Layout; }

private:
	const FPoseSearchFeatureVectorLayout* Layout = nullptr;
	TArrayView<const float> Values;
};


//////////////////////////////////////////////////////////////////////////
// Main PoseSearch API

enum class EDebugDrawFlags : uint32
{
	None			    = 0,

	// Draw the entire search index as a point cloud
	DrawSearchIndex     = 1 << 0,

	// Draw pose features for each pose vector
	IncludePose         = 1 << 1,

	// Draw trajectory features for each pose vector
	IncludeTrajectory   = 1 << 2,

	// Draw all pose vector features
	IncludeAllFeatures  = IncludePose | IncludeTrajectory,

	/**
	 * Keep rendered data until the next call to FlushPersistentDebugLines().
	 * Combine with DrawSearchIndex to draw the search index only once.
	 */
	Persistent = 1 << 3,
	
	// Label samples with their indices
	DrawSampleLabels = 1 << 4,

	// Fade colors
	DrawSamplesWithColorGradient = 1 << 5,
};
ENUM_CLASS_FLAGS(EDebugDrawFlags);

struct POSESEARCH_API FDebugDrawParams
{
	const UWorld* World = nullptr;
	const UPoseSearchDatabase* Database = nullptr;
	const UPoseSearchSequenceMetaData* SequenceMetaData = nullptr;
	EDebugDrawFlags Flags = EDebugDrawFlags::IncludeAllFeatures;

	float DefaultLifeTime = 5.0f;
	float PointSize = 1.0f;

	FTransform RootTransform = FTransform::Identity;

	// If set, draw the corresponding pose from the search index
	int32 PoseIdx = INDEX_NONE;

	// If set, draw using this uniform color instead of feature-based coloring
	const FLinearColor* Color = nullptr;

	// If set, interpret the buffer as a pose vector and draw it
	TArrayView<const float> PoseVector;

	// Optional prefix for sample labels
	FStringView LabelPrefix;

	bool CanDraw() const;
	const FPoseSearchIndex* GetSearchIndex() const;
	const UPoseSearchSchema* GetSchema() const;
};

struct FPoseCost
{
	float Dissimilarity = MAX_flt;
	float CostAddend = 0.0f;
	float TotalCost = MAX_flt;
	bool operator<(const FPoseCost& Other) const { return TotalCost < Other.TotalCost; }

};

struct FSearchResult
{
	FPoseCost PoseCost;
	int32 PoseIdx = INDEX_NONE;
	const FPoseSearchIndexAsset* SearchIndexAsset = nullptr;
	float TimeOffsetSeconds = 0.0f;

	bool IsValid() const { return PoseIdx >= 0; }
};

struct POSESEARCH_API FSearchContext
{
	TArrayView<const float> QueryValues;
	EPoseSearchBooleanRequest QueryMirrorRequest = EPoseSearchBooleanRequest::Indifferent;
	const FPoseSearchWeightsContext* WeightsContext = nullptr;
	const FGameplayTagQuery* DatabaseTagQuery = nullptr;
	FDebugDrawParams DebugDrawParams;

	void SetSource(const UPoseSearchDatabase* InSourceDatabase);
	void SetSource(const UAnimSequenceBase* InSourceSequence);
	const FPoseSearchIndex* GetSearchIndex() const;
	float GetMirrorMismatchCost() const;
	const UPoseSearchDatabase* GetSourceDatabase() { return SourceDatabase; }

private:
	const UPoseSearchDatabase* SourceDatabase = nullptr;
	const UAnimSequenceBase* SourceSequence = nullptr;
	const FPoseSearchIndex* SearchIndex = nullptr;
	float MirrorMismatchCost = 0.0f;
};

/**
* Visualize pose search debug information
*
* @param DrawParams		Visualization options
*/
POSESEARCH_API void Draw(const FDebugDrawParams& DrawParams);


/**
* Creates a pose search index for an animation sequence
* 
* @param Sequence			The input sequence create a search index for
* @param SequenceMetaData	The input sequence indexing info and output search index
* 
* @return Whether the index was built successfully
*/
POSESEARCH_API bool BuildIndex(const UAnimSequence* Sequence, UPoseSearchSequenceMetaData* SequenceMetaData);


/**
* Creates a pose search index for a collection of animations
* 
* @param Database	The input collection of animations and output search index
* 
* @return Whether the index was built successfully
*/
POSESEARCH_API bool BuildIndex(UPoseSearchDatabase* Database);


/**
* Performs a pose search on a UPoseSearchDatabase.
*
* @param SearchContext	Structure containing search parameters
* 
* @return The pose in the database that most closely matches the Query.
*/
POSESEARCH_API FSearchResult Search(FSearchContext& SearchContext);


/**
* Evaluate pose comparison metric between a pose in the search index and an input query
*
* @param PoseIdx			The index of the pose in the search index to compare to the query
* @param SearchContext		Structure containing search parameters
* @param GroupIdx			Indicated which weight to use when evaluating dissimilarity
*
* @return Dissimilarity between the two poses
*/

POSESEARCH_API FPoseCost ComparePoses(int32 PoseIdx, FSearchContext& SearchContext, int32 GroupIdx = INDEX_NONE);

/**
 * Cost details for pose analysis in the rewind debugger
 */
struct FPoseCostDetails
{
	FPoseCost PoseCost;

	// Contribution from ModifyCost anim notify
	float NotifyCostAddend = 0.0f;

	// Contribution from mirroring cost
	float MirrorMismatchCostAddend = 0.0f;

	// Cost breakdown per channel (e.g. pose cost, time-based trajectory cost, distance-based trajectory cost, etc.)
	TArray<float> ChannelCosts;

	// Difference vector computed as W*((P-Q)^2) without the cost modifier applied
	// Where P is the pose vector, Q is the query vector, W is the weights vector, and multiplication/exponentiation are element-wise operations
	TArray<float> CostVector;
};

/**
* Evaluate pose comparison metric between a pose in the search index and an input query with cost details
*
* @param SearchIndex		The search index containing the pose to compare to the query
* @param PoseIdx			The index of the pose in the search index to compare to the query
* @param SearchContext		Structure containing search parameters
* &param OutPoseCostDetails	Cost details for analysis in the debugger
*
* @return Dissimilarity between the two poses
*/
POSESEARCH_API FPoseCost ComparePoses(int32 PoseIdx, FSearchContext& SearchContext, FPoseCostDetails& OutPoseCostDetails);

}} // namespace UE::PoseSearch