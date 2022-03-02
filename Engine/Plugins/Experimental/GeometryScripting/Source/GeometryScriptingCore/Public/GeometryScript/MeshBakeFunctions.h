// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "Sampling/MeshMapBaker.h"
#include "MeshBakeFunctions.generated.h"

class UDynamicMesh;

UENUM(BlueprintType)
enum class EGeometryScriptBakeResolution : uint8
{
	Resolution16 UMETA(DisplayName = "16 x 16"),
	Resolution32 UMETA(DisplayName = "32 x 32"),
	Resolution64 UMETA(DisplayName = "64 x 64"),
	Resolution128 UMETA(DisplayName = "128 x 128"),
	Resolution256 UMETA(DisplayName = "256 x 256"),
	Resolution512 UMETA(DisplayName = "512 x 512"),
	Resolution1024 UMETA(DisplayName = "1024 x 1024"),
	Resolution2048 UMETA(DisplayName = "2048 x 2048"),
	Resolution4096 UMETA(DisplayName = "4096 x 4096"),
	Resolution8192 UMETA(DisplayName = "8192 x 8192")
};

UENUM(BlueprintType)
enum class EGeometryScriptBakeBitDepth : uint8
{
	ChannelBits8 UMETA(DisplayName = "8 bits/channel"),
	ChannelBits16 UMETA(DisplayName = "16 bits/channel")
};

UENUM(BlueprintType)
enum class EGeometryScriptBakeSamplesPerPixel : uint8
{
	Sample1 UMETA(DisplayName = "1"),
	Sample4 UMETA(DisplayName = "4"),
	Sample16 UMETA(DisplayName = "16"),
	Sample64 UMETA(DisplayName = "64"),
	Samples256 UMETA(DisplayName = "256")
};

UENUM(BlueprintType)
enum class EGeometryScriptBakeTypes : uint8
{
	/* Normals in tangent space */
	TangentSpaceNormal     UMETA(DisplayName = "Tangent Normal"),
	/* Interpolated normals in object space */
	ObjectSpaceNormal      UMETA(DisplayName = "Object Normal"),
	/* Geometric face normals in object space */
	FaceNormal             ,
	/* Normals skewed towards the least occluded direction */
	BentNormal             ,
	/* Positions in object space */
	Position               ,
	/* Local curvature of the mesh surface */
	Curvature              ,
	/* Ambient occlusion sampled across the hemisphere */
	AmbientOcclusion       ,
	/* Transfer a given texture */
	Texture                ,
	/* Transfer a texture per material ID */
	MultiTexture           ,
	/* Interpolated vertex colors */
	VertexColor            ,
	/* Material IDs as unique colors */
	MaterialID             UMETA(DisplayName = "Material ID"),
};

UENUM(BlueprintType)
enum class EGeometryScriptBakeNormalSpace : uint8
{
	/* Tangent space */
	Tangent,
	/* Object space */
	Object
};

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptBakeTypes
{
	GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptBakeType_Occlusion : public FGeometryScriptBakeTypes
{
	GENERATED_BODY()
public:
	/** Number of occlusion rays per sample */
	UPROPERTY(BlueprintReadWrite, Category = OcclusionOutput, meta = (UIMin = "1", UIMax = "1024", ClampMin = "1", ClampMax = "65536"))
	int32 OcclusionRays = 16;

	/** Maximum distance for occlusion rays to test for intersections; a value of 0 means infinity */
	UPROPERTY(BlueprintReadWrite, Category = OcclusionOutput, meta = (UIMin = "0.0", UIMax = "1000.0", ClampMin = "0.0", ClampMax = "99999999.0"))
	float MaxDistance = 0.0f;

	/** Maximum spread angle in degrees for occlusion rays; for example, 180 degrees will cover the entire hemisphere whereas 90 degrees will only cover the center of the hemisphere down to 45 degrees from the horizon. */
	UPROPERTY(BlueprintReadWrite, Category = OcclusionOutput, meta = (UIMin = "0", UIMax = "180.0", ClampMin = "0", ClampMax = "180.0"))
	float SpreadAngle = 180.0f;

	/** Angle in degrees from the horizon for occlusion rays for which the contribution is attenuated to reduce faceting artifacts. */
	UPROPERTY(BlueprintReadWrite, Category = OcclusionOutput, meta = (UIMin = "0", UIMax = "45.0", ClampMin = "0", ClampMax = "89.9"))
	float BiasAngle = 15.0f;
};

UENUM(BlueprintType)
enum class EGeometryScriptBakeCurvatureTypeMode : uint8
{
	/** Average of the minimum and maximum principal curvatures */
	Mean,
	/** Maximum principal curvature */
	Max,
	/** Minimum principal curvature */
	Min,
	/** Product of the minimum and maximum principal curvatures */
	Gaussian
};


UENUM(BlueprintType)
enum class EGeometryScriptBakeCurvatureColorMode : uint8
{
	/** Map curvature values to grayscale such that black is negative, grey is zero, and white is positive */
	Grayscale,
	/** Map curvature values to red and blue such that red is negative, black is zero, and blue is positive */
	RedBlue,
	/** Map curvature values to red, green, blue such that red is negative, green is zero, and blue is positive */
	RedGreenBlue
};


UENUM(BlueprintType)
enum class EGeometryScriptBakeCurvatureClampMode : uint8
{
	/** Include both negative and positive curvatures */
	None,
	/** Clamp negative curvatures to zero */
	OnlyPositive,
	/** Clamp positive curvatures to zero */
	OnlyNegative
};

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptBakeType_Curvature : public FGeometryScriptBakeTypes
{
	GENERATED_BODY()
public:
	/** Type of curvature */
	UPROPERTY(BlueprintReadWrite, Category = CurvatureOutput)
	EGeometryScriptBakeCurvatureTypeMode CurvatureType = EGeometryScriptBakeCurvatureTypeMode::Mean;

	/** How to map calculated curvature values to colors */
	UPROPERTY(BlueprintReadWrite, Category = CurvatureOutput)
	EGeometryScriptBakeCurvatureColorMode ColorMapping = EGeometryScriptBakeCurvatureColorMode::Grayscale;

	/** Multiplier for how the curvature values fill the available range in the selected Color Mapping; a larger value means that higher curvature is required to achieve the maximum color value. */
	UPROPERTY(BlueprintReadWrite, Category = CurvatureOutput, meta = (UIMin = "0.1", UIMax = "2.0", ClampMin = "0.001", ClampMax = "100.0"))
	float ColorRangeMultiplier = 1.0f;

	/** Minimum for the curvature values to not be clamped to zero relative to the curvature for the maximum color value; a larger value means that higher curvature is required to not be considered as no curvature. */
	UPROPERTY(BlueprintReadWrite, Category = CurvatureOutput, AdvancedDisplay, meta = (DisplayName = "Color Range Minimum", UIMin = "0.0", UIMax = "1.0"))
	float MinRangeMultiplier = 0.0f;

	/** Clamping applied to curvature values before color mapping */
	UPROPERTY(BlueprintReadWrite, Category = CurvatureOutput)
	EGeometryScriptBakeCurvatureClampMode Clamping = EGeometryScriptBakeCurvatureClampMode::None;
};

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptBakeType_Texture : public FGeometryScriptBakeTypes
{
	GENERATED_BODY()
public:
	/** Source mesh texture that is to be resampled into a new texture */
	UPROPERTY(BlueprintReadWrite, Category = TextureOutput)
	TObjectPtr<UTexture2D> SourceTexture = nullptr;

	/** UV channel to use for the source mesh texture */
	UPROPERTY(BlueprintReadWrite, Category = TextureOutput, meta = (DisplayName = "Source Texture UV Channel", ClampMin="0"))
	int SourceUVLayer = 0;
};

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptBakeType_MultiTexture : public FGeometryScriptBakeTypes
{
	GENERATED_BODY()
public:
	/** For each material ID, the source texture that will be resampled in that material's region*/
	UPROPERTY(BlueprintReadWrite, Category = MultiTextureOutput, meta = (DisplayName = "Source Textures by Material IDs"))
	TArray<TObjectPtr<UTexture2D>> MaterialIDSourceTextures;

	/** UV channel to use for the source mesh texture */
	UPROPERTY(BlueprintReadWrite, Category = MultiTextureOutput, meta = (DisplayName = "Source Texture UV Channel", ClampMin="0"))
	int SourceUVLayer = 0;
};

/**
 * Opaque struct for storing bake type options.
 */
USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptBakeTypeOptions
{
	GENERATED_BODY()

	/** The bake output type to generate */
	UPROPERTY(BlueprintReadOnly, Category = Type)
	EGeometryScriptBakeTypes BakeType = EGeometryScriptBakeTypes::TangentSpaceNormal;

	TSharedPtr<FGeometryScriptBakeTypes> Options;
};

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptBakeTextureOptions
{
	GENERATED_BODY()

	/** The pixel resolution of the generated textures */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	EGeometryScriptBakeResolution Resolution = EGeometryScriptBakeResolution::Resolution256;

	/** The bit depth for each channel of the generated textures */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	EGeometryScriptBakeBitDepth BitDepth = EGeometryScriptBakeBitDepth::ChannelBits8;

	/** Number of samples per pixel */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	EGeometryScriptBakeSamplesPerPixel SamplesPerPixel = EGeometryScriptBakeSamplesPerPixel::Sample1;

	/** Maximum allowed distance for the projection from target mesh to source mesh for the sample to be considered valid.
	 * This is only relevant if a separate source mesh is provided. */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	float ProjectionDistance = 3.0f;

	/** If true, uses the world space positions for the projection from target mesh to source mesh, otherwise it uses their object space positions.
	 * This is only relevant if a separate source mesh is provided. */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bProjectionInWorldSpace = false;
};

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptBakeTargetMeshOptions
{
	GENERATED_BODY();

	UPROPERTY(BlueprintReadWrite, Category = Options, meta = (DisplayName="Target UV Channel"))
	int TargetUVLayer = 0;
};

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptBakeSourceMeshOptions
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = Options)
	TObjectPtr<UTexture2D> SourceNormalMap = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = Options, meta = (DisplayName="Source Normal UV Channel"))
	int SourceNormalUVLayer = 0;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	EGeometryScriptBakeNormalSpace SourceNormalSpace = EGeometryScriptBakeNormalSpace::Tangent;
};

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptBakeTextureAsyncResult
{
	GENERATED_BODY();

	FGeometryScriptBakeTextureOptions BakeOptions;
	TSharedPtr<UE::Geometry::FMeshMapBaker, ESPMode::ThreadSafe> BakeResult;
};

DECLARE_DYNAMIC_DELEGATE_TwoParams(FBakeTextureDelegate, int, BakeId, FGeometryScriptBakeTextureAsyncResult, ResultOut);

UCLASS(meta = (ScriptName = "GeometryScript_Bake"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshBakeFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Bake")
	static UPARAM(DisplayName="Bake Type Out") FGeometryScriptBakeTypeOptions MakeBakeTypeTangentNormal();

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Bake")
	static UPARAM(DisplayName="Bake Type Out") FGeometryScriptBakeTypeOptions MakeBakeTypeObjectNormal();
	
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Bake")
	static UPARAM(DisplayName="Bake Type Out") FGeometryScriptBakeTypeOptions MakeBakeTypeFaceNormal();

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Bake")
	static UPARAM(DisplayName="Bake Type Out") FGeometryScriptBakeTypeOptions MakeBakeTypeBentNormal(
		int OcclusionRays = 16,
		float MaxDistance = 0.0f,
		float SpreadAngle = 180.0f);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Bake")
	static UPARAM(DisplayName="Bake Type Out") FGeometryScriptBakeTypeOptions MakeBakeTypePosition();

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Bake")
	static UPARAM(DisplayName="Bake Type Out") FGeometryScriptBakeTypeOptions MakeBakeTypeCurvature(
		EGeometryScriptBakeCurvatureTypeMode CurvatureType = EGeometryScriptBakeCurvatureTypeMode::Mean,
		EGeometryScriptBakeCurvatureColorMode ColorMapping = EGeometryScriptBakeCurvatureColorMode::Grayscale,
		float ColorRangeMultiplier = 1.0f,
		float MinRangeMultiplier = 0.0f,
		EGeometryScriptBakeCurvatureClampMode Clamping = EGeometryScriptBakeCurvatureClampMode::None);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Bake")
	static UPARAM(DisplayName="Bake Type Out") FGeometryScriptBakeTypeOptions MakeBakeTypeAmbientOcclusion(
		int OcclusionRays = 16,
		float MaxDistance = 0.0f,
		float SpreadAngle = 180.0f,
		float BiasAngle = 15.0f);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Bake")
	static UPARAM(DisplayName="Bake Type Out") FGeometryScriptBakeTypeOptions MakeBakeTypeTexture(
		UTexture2D* SourceTexture = nullptr,
		int SourceUVLayer = 0);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Bake")
	static UPARAM(DisplayName="Bake Type Out") FGeometryScriptBakeTypeOptions MakeBakeTypeMultiTexture(
		const TArray<UTexture2D*>& MaterialIDSourceTextures,
		UPARAM(DisplayName="Source UV Channel") int SourceUVLayer = 0);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Bake")
	static UPARAM(DisplayName="Bake Type Out") FGeometryScriptBakeTypeOptions MakeBakeTypeVertexColor();

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Bake")
	static UPARAM(DisplayName="Bake Type Out") FGeometryScriptBakeTypeOptions MakeBakeTypeMaterialID();
	
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Bake")
	static UPARAM(DisplayName="Textures Out") TArray<UTexture2D*> BakeTexture(
		UDynamicMesh* TargetMesh,
		FTransform TargetTransform,
		FGeometryScriptBakeTargetMeshOptions TargetOptions,
		UDynamicMesh* SourceMesh,
		FTransform SourceTransform,
		FGeometryScriptBakeSourceMeshOptions SourceOptions,
		const TArray<FGeometryScriptBakeTypeOptions>& BakeTypes,
		FGeometryScriptBakeTextureOptions BakeOptions,
		UGeometryScriptDebug* Debug = nullptr);

	/** 
	 * BakeTextureAsyncBegin() is the entry point for an asynchronous variant of BakeTexture.
	 * Usage of this method is as follows:
	 *
	 * 1. BakeTextureAsyncBegin() kicks off an async compute.
	 * 2. When the async compute is complete, it invokes the provided delegate back on the game thread.
	 * 3. The delegate output is consumed by BakeTextureAsyncEnd() which converts the bake results into UTexture2D.
	 * 4. An optional BakeId can be provided to BakeTextureAsyncBegin() that will be associated with the async compute.
	 *    The BakeId can be used to distinguish multiple async computes on the same delegate.
	 *
	 * @param Completed the delegate to invoke on completion. Delegate signature: (int, FGeometryScriptBakeTextureAsyncResult)
	 * @param BakeId optional integer identifier to aid in distinguishing multiple async computes on the same delegate.
	 * @param TargetMesh the target mesh to bake to.
	 * @param TargetTransform the transform of the target mesh. Only applicable when projecting in world space.
	 * @param TargetOptions target mesh options such as which UV layer to bake with.
	 * @param SourceMesh the source mesh to bake from.
	 * @param SourceTransform the transform of the source mesh. Only applicable when projecting in world space.
	 * @param SourceOptions source mesh options such source normal map.
	 * @param BakeTypes the array of bake types & type specific parameters to bake
	 * @param BakeOptions bake options (ex. resolution, number of samples, projection distance).
	 * @param Debug debug structure to capture debug messages.
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Bake")
	static void BakeTextureAsyncBegin(
		const FBakeTextureDelegate& Completed,
		int BakeId,
		UDynamicMesh* TargetMesh,
		FTransform TargetTransform,
		FGeometryScriptBakeTargetMeshOptions TargetOptions,
		UDynamicMesh* SourceMesh,
		FTransform SourceTransform,
		FGeometryScriptBakeSourceMeshOptions SourceOptions,
		const TArray<FGeometryScriptBakeTypeOptions>& BakeTypes,
		FGeometryScriptBakeTextureOptions BakeOptions,
		UGeometryScriptDebug* Debug = nullptr);

	/**
	 * Converts the bake results of BakeTextureAsyncBegin() into UTexture2D.
	 * This function is intended to be invoked by the delegate passed to
	 * BakeTextureAsyncBegin()
	 *
	 * @param Result the result of a BakeTextureAsyncBegin() via delegate
	 * @return array of UTexture2D corresponding to the requested bake types in BakeTextureAsyncBegin().
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Bake")
	static UPARAM(DisplayName="Textures Out") TArray<UTexture2D*> BakeTextureAsyncEnd(
		const FGeometryScriptBakeTextureAsyncResult& Result);
};
