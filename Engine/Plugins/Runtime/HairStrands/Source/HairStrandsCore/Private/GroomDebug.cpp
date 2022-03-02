// Copyright Epic Games, Inc. All Rights Reserved. 

#include "GeometryCacheComponent.h"
#include "GroomInstance.h"
#include "GroomManager.h"
#include "GPUSkinCache.h"
#include "HairStrandsMeshProjection.h"
#include "HairStrandsInterface.h"
#include "CommonRenderResources.h"
#include "GroomGeometryCache.h"
#include "Components/SkeletalMeshComponent.h"
#include "UnrealEngine.h"
#include "SystemTextures.h"
#include "CanvasTypes.h"
#include "ShaderCompilerCore.h"

///////////////////////////////////////////////////////////////////////////////////////////////////

static int32 GHairDebugMeshProjection_SkinCacheMesh = 0;
static int32 GHairDebugMeshProjection_SkinCacheMeshInUVsSpace = 0;
static int32 GHairDebugMeshProjection_Sim_HairRestTriangles = 0;
static int32 GHairDebugMeshProjection_Sim_HairRestFrames = 0;
static int32 GHairDebugMeshProjection_Sim_HairDeformedTriangles = 0;
static int32 GHairDebugMeshProjection_Sim_HairDeformedFrames = 0;

static int32 GHairDebugMeshProjection_Render_HairRestTriangles = 0;
static int32 GHairDebugMeshProjection_Render_HairRestFrames = 0;
static int32 GHairDebugMeshProjection_Render_HairDeformedTriangles = 0;
static int32 GHairDebugMeshProjection_Render_HairDeformedFrames = 0;


static FAutoConsoleVariableRef CVarHairDebugMeshProjection_SkinCacheMeshInUVsSpace(TEXT("r.HairStrands.MeshProjection.DebugInUVsSpace"), GHairDebugMeshProjection_SkinCacheMeshInUVsSpace, TEXT("Render debug mes projection in UVs space"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_SkinCacheMesh(TEXT("r.HairStrands.MeshProjection.DebugSkinCache"), GHairDebugMeshProjection_SkinCacheMesh, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Render_HairRestTriangles(TEXT("r.HairStrands.MeshProjection.Render.Rest.Triangles"), GHairDebugMeshProjection_Render_HairRestTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Render_HairRestFrames(TEXT("r.HairStrands.MeshProjection.Render.Rest.Frames"), GHairDebugMeshProjection_Render_HairRestFrames, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Render_HairDeformedTriangles(TEXT("r.HairStrands.MeshProjection.Render.Deformed.Triangles"), GHairDebugMeshProjection_Render_HairDeformedTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Render_HairDeformedFrames(TEXT("r.HairStrands.MeshProjection.Render.Deformed.Frames"), GHairDebugMeshProjection_Render_HairDeformedFrames, TEXT("Render debug mes projection"));

static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Sim_HairRestTriangles(TEXT("r.HairStrands.MeshProjection.Sim.Rest.Triangles"), GHairDebugMeshProjection_Sim_HairRestTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Sim_HairRestFrames(TEXT("r.HairStrands.MeshProjection.Sim.Rest.Frames"), GHairDebugMeshProjection_Sim_HairRestFrames, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Sim_HairDeformedTriangles(TEXT("r.HairStrands.MeshProjection.Sim.Deformed.Triangles"), GHairDebugMeshProjection_Sim_HairDeformedTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Sim_HairDeformedFrames(TEXT("r.HairStrands.MeshProjection.Sim.Deformed.Frames"), GHairDebugMeshProjection_Sim_HairDeformedFrames, TEXT("Render debug mes projection"));

static int32 GHairCardsAtlasDebug = 0;
static FAutoConsoleVariableRef CVarHairCardsAtlasDebug(TEXT("r.HairStrands.Cards.DebugAtlas"), GHairCardsAtlasDebug, TEXT("Draw debug hair cards atlas."));

static int32 GHairCardsVoxelDebug = 0;
static FAutoConsoleVariableRef CVarHairCardsVoxelDebug(TEXT("r.HairStrands.Cards.DebugVoxel"), GHairCardsVoxelDebug, TEXT("Draw debug hair cards voxel datas."));

static int32 GHairCardsGuidesDebug_Ren = 0;
static int32 GHairCardsGuidesDebug_Sim = 0;
static FAutoConsoleVariableRef CVarHairCardsGuidesDebug_Ren(TEXT("r.HairStrands.Cards.DebugGuides.Render"), GHairCardsGuidesDebug_Ren, TEXT("Draw debug hair cards guides (1: Rest, 2: Deformed)."));
static FAutoConsoleVariableRef CVarHairCardsGuidesDebug_Sim(TEXT("r.HairStrands.Cards.DebugGuides.Sim"), GHairCardsGuidesDebug_Sim, TEXT("Draw debug hair sim guides (1: Rest, 2: Deformed)."));

static int32 GHairStrandsControlPointDebug = 0;
static FAutoConsoleVariableRef CVarHairStrandsControlPointDebug(TEXT("r.HairStrands.Strands.DebugControlPoint"), GHairStrandsControlPointDebug, TEXT("Draw debug hair strands control points)."));

///////////////////////////////////////////////////////////////////////////////////////////////////

bool IsHairStrandsSkinCacheEnable();

static void GetGroomInterpolationData(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const FHairStrandsInstances& Instances,
	const EHairStrandsProjectionMeshType MeshType,
	const FGPUSkinCache* SkinCache,
	FHairStrandsProjectionMeshData::LOD& OutGeometries)
{
	for (FHairStrandsInstance* AbstractInstance : Instances)
	{
		FHairGroupInstance* Instance = static_cast<FHairGroupInstance*>(AbstractInstance);

		if (!Instance || !Instance->Debug.MeshComponent)
			continue;

		FCachedGeometry CachedGeometry;
		if (Instance->Debug.GroomBindingType == EGroomBindingMeshType::SkeletalMesh)
		{
			if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(Instance->Debug.MeshComponent))
			{
				if (SkinCache)
				{
					CachedGeometry = SkinCache->GetCachedGeometry(SkeletalMeshComponent->ComponentId.PrimIDValue, EGPUSkinCacheEntryMode::Raster);
				}

				if (IsHairStrandsSkinCacheEnable() && CachedGeometry.Sections.Num() == 0)
				{
					BuildCacheGeometry(GraphBuilder, ShaderMap, SkeletalMeshComponent, CachedGeometry);
				}
			}
		}
		else if (UGeometryCacheComponent* GeometryCacheComponent = Cast<UGeometryCacheComponent>(Instance->Debug.MeshComponent))
		{
			BuildCacheGeometry(GraphBuilder, ShaderMap, GeometryCacheComponent, CachedGeometry);
		}
		if (CachedGeometry.Sections.Num() == 0)
			continue;

		if (MeshType == EHairStrandsProjectionMeshType::DeformedMesh || MeshType == EHairStrandsProjectionMeshType::RestMesh)
		{
			for (const FCachedGeometry::Section& Section : CachedGeometry.Sections)
			{
				FHairStrandsProjectionMeshData::Section OutSection = ConvertMeshSection(Section, CachedGeometry.LocalToWorld);
				if (MeshType == EHairStrandsProjectionMeshType::RestMesh)
				{					
					// If the mesh has some mesh-tranferred data, we display that otherwise we use the rest data
					const bool bHasTransferData = Section.LODIndex < Instance->Debug.TransferredPositions.Num();
					if (bHasTransferData)
					{
						OutSection.PositionBuffer = Instance->Debug.TransferredPositions[Section.LODIndex].SRV;
					}
					else if (Instance->Debug.TargetMeshData.LODs.Num() > 0)
					{
						OutGeometries = Instance->Debug.TargetMeshData.LODs[0];
					}
				}
				OutGeometries.Sections.Add(OutSection);
			}
		}

		if (MeshType == EHairStrandsProjectionMeshType::TargetMesh && Instance->Debug.TargetMeshData.LODs.Num() > 0)
		{
			OutGeometries = Instance->Debug.TargetMeshData.LODs[0];
		}

		if (MeshType == EHairStrandsProjectionMeshType::SourceMesh && Instance->Debug.SourceMeshData.LODs.Num() > 0)
		{
			OutGeometries = Instance->Debug.SourceMeshData.LODs[0];
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SHADER_PARAMETER_STRUCT(FHairProjectionMeshDebugParameters, )
	SHADER_PARAMETER(FMatrix44f, LocalToWorld)
	SHADER_PARAMETER(uint32, VertexOffset)
	SHADER_PARAMETER(uint32, IndexOffset)
	SHADER_PARAMETER(uint32, MaxIndexCount)
	SHADER_PARAMETER(uint32, MaxVertexCount)
	SHADER_PARAMETER(uint32, MeshUVsChannelOffset)
	SHADER_PARAMETER(uint32, MeshUVsChannelCount)
	SHADER_PARAMETER(uint32, bOutputInUVsSpace)
	SHADER_PARAMETER(uint32, MeshType)
	SHADER_PARAMETER(uint32, SectionIndex)
	SHADER_PARAMETER(FVector2f, OutputResolution)
	SHADER_PARAMETER_SRV(StructuredBuffer, InputIndexBuffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, InputVertexPositionBuffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, InputVertexUVsBuffer)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FHairProjectionMeshDebug : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform);
	}


	FHairProjectionMeshDebug() = default;
	FHairProjectionMeshDebug(const CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer) {}
};


class FHairProjectionMeshDebugVS : public FHairProjectionMeshDebug
{
public:
	class FInputType : SHADER_PERMUTATION_INT("PERMUTATION_INPUT_TYPE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FInputType>;

	DECLARE_GLOBAL_SHADER(FHairProjectionMeshDebugVS);
	SHADER_USE_PARAMETER_STRUCT(FHairProjectionMeshDebugVS, FHairProjectionMeshDebug);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairProjectionMeshDebugParameters, Pass)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform);
	}
};

class FHairProjectionMeshDebugPS : public FHairProjectionMeshDebug
{
	DECLARE_GLOBAL_SHADER(FHairProjectionMeshDebugPS);
	SHADER_USE_PARAMETER_STRUCT(FHairProjectionMeshDebugPS, FHairProjectionMeshDebug);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairProjectionMeshDebugParameters, Pass)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairProjectionMeshDebugVS, "/Engine/Private/HairStrands/HairStrandsMeshProjectionMeshDebug.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FHairProjectionMeshDebugPS, "/Engine/Private/HairStrands/HairStrandsMeshProjectionMeshDebug.usf", "MainPS", SF_Pixel);

static void AddDebugProjectionMeshPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const FIntRect Viewport,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
	const EHairStrandsProjectionMeshType MeshType,
	const bool bClearDepth,
	FHairStrandsProjectionMeshData::Section& MeshSectionData,
	FRDGTextureRef& ColorTexture,
	FRDGTextureRef& DepthTexture)
{
	const EPrimitiveType PrimitiveType = PT_TriangleList;
	const bool bHasIndexBuffer = MeshSectionData.IndexBuffer != nullptr;
	const uint32 PrimitiveCount = MeshSectionData.NumPrimitives;

	if (!MeshSectionData.PositionBuffer || PrimitiveCount == 0)
		return;

	const FIntPoint Resolution(Viewport.Width(), Viewport.Height());

	FHairProjectionMeshDebugParameters* Parameters = GraphBuilder.AllocParameters<FHairProjectionMeshDebugParameters>();
	Parameters->LocalToWorld = FMatrix44f(MeshSectionData.LocalToWorld.ToMatrixWithScale());		// LWC_TODO: Precision loss
	Parameters->OutputResolution = Resolution;
	Parameters->MeshType = uint32(MeshType);
	Parameters->bOutputInUVsSpace = GHairDebugMeshProjection_SkinCacheMeshInUVsSpace ? 1 : 0;
	Parameters->VertexOffset = MeshSectionData.VertexBaseIndex;
	Parameters->IndexOffset = MeshSectionData.IndexBaseIndex;
	Parameters->MaxIndexCount = MeshSectionData.TotalIndexCount;
	Parameters->MaxVertexCount = MeshSectionData.TotalVertexCount;
	Parameters->MeshUVsChannelOffset = MeshSectionData.UVsChannelOffset;
	Parameters->MeshUVsChannelCount = MeshSectionData.UVsChannelCount;
	Parameters->InputIndexBuffer = MeshSectionData.IndexBuffer;
	Parameters->InputVertexPositionBuffer = MeshSectionData.PositionBuffer;
	Parameters->InputVertexUVsBuffer = MeshSectionData.UVsBuffer;
	Parameters->SectionIndex = MeshSectionData.SectionIndex;
	Parameters->ViewUniformBuffer = ViewUniformBuffer;
	Parameters->RenderTargets[0] = FRenderTargetBinding(ColorTexture, ERenderTargetLoadAction::ELoad, 0);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(DepthTexture, bClearDepth ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);

	FHairProjectionMeshDebugVS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairProjectionMeshDebugVS::FInputType>(bHasIndexBuffer ? 1 : 0);

	TShaderMapRef<FHairProjectionMeshDebugVS> VertexShader(ShaderMap, PermutationVector);
	TShaderMapRef<FHairProjectionMeshDebugPS> PixelShader(ShaderMap);

	FHairProjectionMeshDebugVS::FParameters VSParameters;
	VSParameters.Pass = *Parameters;
	FHairProjectionMeshDebugPS::FParameters PSParameters;
	PSParameters.Pass = *Parameters;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrands::MeshProjectionMeshDebug"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VSParameters, PSParameters, VertexShader, PixelShader, Viewport, Resolution, PrimitiveCount, PrimitiveType](FRHICommandList& RHICmdList)
		{

			RHICmdList.SetViewport(
				Viewport.Min.X,
				Viewport.Min.Y,
				0.0f,
				Viewport.Max.X,
				Viewport.Max.Y,
				1.0f);

			// Apply additive blending pipeline state.
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Wireframe>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PrimitiveType;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VSParameters);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PSParameters);

			// Emit an instanced quad draw call on the order of the number of pixels on the screen.	
			RHICmdList.DrawPrimitive(0, PrimitiveCount, 1);
		});
}

///////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SHADER_PARAMETER_STRUCT(FHairProjectionHairDebugParameters, )
	SHADER_PARAMETER(FVector2f, OutputResolution)
	SHADER_PARAMETER(uint32, MaxRootCount)
	SHADER_PARAMETER(uint32, DeformedFrameEnable)
	SHADER_PARAMETER(FMatrix44f, RootLocalToWorld)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, RestPosition0Buffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, RestPosition1Buffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, RestPosition2Buffer)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, DeformedPosition0Buffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, DeformedPosition1Buffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, DeformedPosition2Buffer)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, RootBarycentricBuffer)

	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FHairProjectionHairDebug : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform);
	}

	FHairProjectionHairDebug() = default;
	FHairProjectionHairDebug(const CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer) {}
};


class FHairProjectionHairDebugVS : public FHairProjectionHairDebug
{
public:
	class FInputType : SHADER_PERMUTATION_INT("PERMUTATION_INPUT_TYPE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FInputType>;

	DECLARE_GLOBAL_SHADER(FHairProjectionHairDebugVS);
	SHADER_USE_PARAMETER_STRUCT(FHairProjectionHairDebugVS, FHairProjectionHairDebug);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairProjectionHairDebugParameters, Pass)
		END_SHADER_PARAMETER_STRUCT()
};

class FHairProjectionHairDebugPS : public FHairProjectionHairDebug
{
	DECLARE_GLOBAL_SHADER(FHairProjectionHairDebugPS);
	SHADER_USE_PARAMETER_STRUCT(FHairProjectionHairDebugPS, FHairProjectionHairDebug);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairProjectionHairDebugParameters, Pass)
		END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FHairProjectionHairDebugVS, "/Engine/Private/HairStrands/HairStrandsMeshProjectionHairDebug.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FHairProjectionHairDebugPS, "/Engine/Private/HairStrands/HairStrandsMeshProjectionHairDebug.usf", "MainPS", SF_Pixel);

enum class EDebugProjectionHairType
{
	HairFrame,
	HairTriangle,
};

static void AddDebugProjectionHairPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	FIntRect Viewport,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
	const bool bClearDepth,
	const EDebugProjectionHairType GeometryType,
	const HairStrandsTriangleType PoseType,
	const int32 MeshLODIndex,
	const FHairStrandsRestRootResource* RestRootResources,
	const FHairStrandsDeformedRootResource* DeformedRootResources,
	const FTransform& LocalToWorld,
	FRDGTextureRef ColorTarget,
	FRDGTextureRef DepthTexture)
{
	const EPrimitiveType PrimitiveType = GeometryType == EDebugProjectionHairType::HairFrame ? PT_LineList : PT_TriangleList;
	const uint32 RootCount = RestRootResources->BulkData.RootCount;
	const uint32 PrimitiveCount = RootCount;

	if (PrimitiveCount == 0 || MeshLODIndex < 0 || MeshLODIndex >= RestRootResources->LODs.Num() || MeshLODIndex >= DeformedRootResources->LODs.Num())
		return;

	if (EDebugProjectionHairType::HairFrame == GeometryType &&
		!RestRootResources->LODs[MeshLODIndex].RootTriangleBarycentricBuffer.Buffer)
		return;

	const FHairStrandsRestRootResource::FLOD& RestLODDatas = RestRootResources->LODs[MeshLODIndex];
	const FHairStrandsDeformedRootResource::FLOD& DeformedLODDatas = DeformedRootResources->LODs[MeshLODIndex];

	if (!RestLODDatas.RestRootTrianglePosition0Buffer.Buffer ||
		!RestLODDatas.RestRootTrianglePosition1Buffer.Buffer ||
		!RestLODDatas.RestRootTrianglePosition2Buffer.Buffer ||
		!DeformedLODDatas.DeformedRootTrianglePosition0Buffer.Buffer ||
		!DeformedLODDatas.DeformedRootTrianglePosition1Buffer.Buffer ||
		!DeformedLODDatas.DeformedRootTrianglePosition2Buffer.Buffer)
		return;

	const FIntPoint Resolution(Viewport.Width(), Viewport.Height());

	FHairProjectionHairDebugParameters* Parameters = GraphBuilder.AllocParameters<FHairProjectionHairDebugParameters>();
	Parameters->OutputResolution = Resolution;
	Parameters->MaxRootCount = RootCount;
	Parameters->RootLocalToWorld = FMatrix44f(LocalToWorld.ToMatrixWithScale());	// LWC_TODO: Precision loss
	Parameters->DeformedFrameEnable = PoseType == HairStrandsTriangleType::DeformedPose;

	if (EDebugProjectionHairType::HairFrame == GeometryType)
	{
		Parameters->RootBarycentricBuffer	= RegisterAsSRV(GraphBuilder, RestLODDatas.RootTriangleBarycentricBuffer);
	}

	Parameters->RestPosition0Buffer = RegisterAsSRV(GraphBuilder, RestLODDatas.RestRootTrianglePosition0Buffer);
	Parameters->RestPosition1Buffer = RegisterAsSRV(GraphBuilder, RestLODDatas.RestRootTrianglePosition1Buffer);
	Parameters->RestPosition2Buffer = RegisterAsSRV(GraphBuilder, RestLODDatas.RestRootTrianglePosition2Buffer);

	Parameters->DeformedPosition0Buffer = RegisterAsSRV(GraphBuilder, DeformedLODDatas.DeformedRootTrianglePosition0Buffer);
	Parameters->DeformedPosition1Buffer = RegisterAsSRV(GraphBuilder, DeformedLODDatas.DeformedRootTrianglePosition1Buffer);
	Parameters->DeformedPosition2Buffer = RegisterAsSRV(GraphBuilder, DeformedLODDatas.DeformedRootTrianglePosition2Buffer);

	Parameters->ViewUniformBuffer = ViewUniformBuffer;
	Parameters->RenderTargets[0] = FRenderTargetBinding(ColorTarget, ERenderTargetLoadAction::ELoad, 0);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(DepthTexture, bClearDepth ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);

	FHairProjectionHairDebugVS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairProjectionHairDebugVS::FInputType>(PrimitiveType == PT_LineList ? 0 : 1);

	TShaderMapRef<FHairProjectionHairDebugVS> VertexShader(ShaderMap, PermutationVector);
	TShaderMapRef<FHairProjectionHairDebugPS> PixelShader(ShaderMap);

	FHairProjectionHairDebugVS::FParameters VSParameters;
	VSParameters.Pass = *Parameters;
	FHairProjectionHairDebugPS::FParameters PSParameters;
	PSParameters.Pass = *Parameters;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrands::MeshProjectionHairDebug"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VSParameters, PSParameters, VertexShader, PixelShader, Viewport, Resolution, PrimitiveCount, PrimitiveType](FRHICommandList& RHICmdList)
		{

			RHICmdList.SetViewport(
				Viewport.Min.X,
				Viewport.Min.Y,
				0.0f,
				Viewport.Max.X,
				Viewport.Max.Y,
				1.0f);

			// Apply additive blending pipeline state.
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PrimitiveType;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VSParameters);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PSParameters);

			// Emit an instanced quad draw call on the order of the number of pixels on the screen.	
			RHICmdList.DrawPrimitive(0, PrimitiveCount, 1);
		});
}	

///////////////////////////////////////////////////////////////////////////////////////////////////
class FVoxelPlainRaymarchingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelPlainRaymarchingCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelPlainRaymarchingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
		SHADER_PARAMETER(FVector2f, OutputResolution)		
		SHADER_PARAMETER(FIntVector, Voxel_Resolution)
		SHADER_PARAMETER(float, Voxel_VoxelSize)
		SHADER_PARAMETER(FVector3f, Voxel_MinBound)
		SHADER_PARAMETER(FVector3f, Voxel_MaxBound)
		SHADER_PARAMETER_SRV(Buffer, Voxel_TangentBuffer)
		SHADER_PARAMETER_SRV(Buffer, Voxel_NormalBuffer)
		SHADER_PARAMETER_SRV(Buffer, Voxel_DensityBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, Voxel_ProcessedDensityBuffer)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputTexture)
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_PLAIN"), 1);
	}	
};

IMPLEMENT_GLOBAL_SHADER(FVoxelPlainRaymarchingCS, "/Engine/Private/HairStrands/HairCardsVoxel.usf", "MainCS", SF_Compute);

static void AddVoxelPlainRaymarchingPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FGlobalShaderMap* ShaderMap,
	const FHairGroupInstance* Instance,
	const FShaderPrintData* ShaderPrintData,
	FRDGTextureRef OutputTexture)
{
#if 0 // #hair_todo: renable if needed
	const FHairStrandClusterData::FHairGroup& HairGroupClusters = HairClusterData.HairGroups[DataIndex];

	FViewInfo& View = Views[ViewIndex];
	if (ShaderPrint::IsEnabled(View))
	{
		if (Instance->HairGroupPublicData->VFInput.GeometryType != EHairGeometryType::Cards)
			return;

		FSceneTextureParameters SceneTextures;
		SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

		const FIntPoint OutputResolution(OutputTexture->Desc.Extent);
		const FHairCardsVoxel& CardsVoxel = Instance->HairGroupPublicData->VFInput.Cards.Voxel;

		FRDGBufferRef VoxelDensityBuffer2 = nullptr;
		AddVoxelProcessPass(GraphBuilder, View, CardsVoxel, VoxelDensityBuffer2);

		FVoxelPlainRaymarchingCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVoxelPlainRaymarchingCS::FParameters>();
		Parameters->ViewUniformBuffer	= View.ViewUniformBuffer;
		Parameters->OutputResolution	= OutputResolution;
		Parameters->Voxel_Resolution	= CardsVoxel.Resolution;
		Parameters->Voxel_VoxelSize		= CardsVoxel.VoxelSize;
		Parameters->Voxel_MinBound		= CardsVoxel.MinBound;
		Parameters->Voxel_MaxBound		= CardsVoxel.MaxBound;
		Parameters->Voxel_TangentBuffer	= CardsVoxel.TangentBuffer.SRV;
		Parameters->Voxel_NormalBuffer	= CardsVoxel.NormalBuffer.SRV;
		Parameters->Voxel_DensityBuffer = CardsVoxel.DensityBuffer.SRV;
		Parameters->Voxel_ProcessedDensityBuffer = GraphBuilder.CreateSRV(VoxelDensityBuffer2, PF_R32_UINT);

		ShaderPrint::SetParameters(GraphBuilder, ShaderPrintData, Parameters->ShaderPrintParameters);
		//ShaderPrint::SetParameters(View, Parameters->ShaderPrintParameters);
		Parameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);

		TShaderMapRef<FVoxelPlainRaymarchingCS> ComputeShader(ShaderMap);
		const FIntVector DispatchCount = DispatchCount.DivideAndRoundUp(FIntVector(OutputTexture->Desc.Extent.X, OutputTexture->Desc.Extent.Y, 1), FIntVector(8, 8, 1));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrands::VoxelPlainRaymarching"), ComputeShader, Parameters, DispatchCount);
	}
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FDrawDebugCardAtlasCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDrawDebugCardAtlasCS);
	SHADER_USE_PARAMETER_STRUCT(FDrawDebugCardAtlasCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_TEXTURE(Texture2D, AtlasTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputTexture)
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(FIntPoint, AtlasResolution)
		SHADER_PARAMETER(int32, DebugMode)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_ATLAS"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDrawDebugCardAtlasCS, "/Engine/Private/HairStrands/HairCardsDebug.usf", "MainCS", SF_Compute);

static void AddDrawDebugCardsAtlasPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FGlobalShaderMap* ShaderMap,
	const FHairGroupInstance* Instance,
	const FShaderPrintData* ShaderPrintData,
	FRDGTextureRef SceneColorTexture)
{
	if (Instance->HairGroupPublicData->VFInput.GeometryType != EHairGeometryType::Cards || ShaderPrintData == nullptr)
	{
		return;
	}

	const int32 LODIndex = Instance->HairGroupPublicData->GetIntLODIndex();
	if (!Instance->Cards.IsValid(LODIndex))
	{
		return;
	}

	FTextureReferenceRHIRef AtlasTexture = nullptr;

	const int32 DebugMode = FMath::Clamp(GHairCardsAtlasDebug, 1, 6);
	switch (DebugMode)
	{
	case 1: AtlasTexture = Instance->Cards.LODs[LODIndex].RestResource->DepthTexture; break;
	case 2: AtlasTexture = Instance->Cards.LODs[LODIndex].RestResource->CoverageTexture; break;
	case 3: AtlasTexture = Instance->Cards.LODs[LODIndex].RestResource->TangentTexture; break;
	case 4:
	case 5:
	case 6: AtlasTexture = Instance->Cards.LODs[LODIndex].RestResource->AttributeTexture; break;
	}

	if (AtlasTexture != nullptr)
	{
		TShaderMapRef<FDrawDebugCardAtlasCS> ComputeShader(ShaderMap);

		FDrawDebugCardAtlasCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDrawDebugCardAtlasCS::FParameters>();
		Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
		Parameters->OutputResolution = SceneColorTexture->Desc.Extent;
		Parameters->AtlasResolution = FIntPoint(AtlasTexture->GetSizeXYZ().X, AtlasTexture->GetSizeXYZ().Y);
		Parameters->AtlasTexture = AtlasTexture;
		Parameters->DebugMode = DebugMode;
		Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->OutputTexture = GraphBuilder.CreateUAV(SceneColorTexture);

		ShaderPrint::SetParameters(GraphBuilder, *ShaderPrintData, Parameters->ShaderPrintParameters);

		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrands::DrawDebugCardsAtlas"), ComputeShader, Parameters,
		FIntVector::DivideAndRoundUp(FIntVector(Parameters->OutputResolution.X, Parameters->OutputResolution.Y, 1), FIntVector(8, 8, 1)));

	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FDrawDebugStrandsCVsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDrawDebugStrandsCVsCS);
	SHADER_USE_PARAMETER_STRUCT(FDrawDebugStrandsCVsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(uint32, MaxVertexCount)
		SHADER_PARAMETER(FMatrix44f, LocalToWorld)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ColorTexture)
		SHADER_PARAMETER_STRUCT_REF(FHairStrandsVertexFactoryUniformShaderParameters, HairStrandsVF)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_CVS"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDrawDebugStrandsCVsCS, "/Engine/Private/HairStrands/HairStrandsDebug.usf", "MainCS", SF_Compute);

static void AddDrawDebugStrandsCVsPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FGlobalShaderMap* ShaderMap,
	const FHairGroupInstance* Instance,
	const FShaderPrintData* ShaderPrintData,
	FRDGTextureRef ColorTexture,
	FRDGTextureRef DepthTexture)
{
	if (Instance->HairGroupPublicData->VFInput.GeometryType != EHairGeometryType::Strands || ShaderPrintData == nullptr)
	{
		return;
	}

	if (!Instance->Strands.IsValid())
	{
		return;
	}

	TShaderMapRef<FDrawDebugStrandsCVsCS> ComputeShader(ShaderMap);
	FDrawDebugStrandsCVsCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDrawDebugStrandsCVsCS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->HairStrandsVF = Instance->Strands.UniformBuffer;
	Parameters->LocalToWorld = FMatrix44f(Instance->LocalToWorld.ToMatrixWithScale());		// LWC_TODO: Precision loss
	Parameters->MaxVertexCount = Instance->Strands.Data->PointCount;
	Parameters->ColorTexture = GraphBuilder.CreateUAV(ColorTexture);
	Parameters->DepthTexture = DepthTexture;
	Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	const uint32 VertexCount = Instance->HairGroupPublicData->VFInput.Strands.VertexCount;
	FComputeShaderUtils::AddPass(
		GraphBuilder, 
		RDG_EVENT_NAME("HairStrands::DrawCVs"), 
		ComputeShader, 
		Parameters,
		FIntVector::DivideAndRoundUp(FIntVector(VertexCount, 1, 1), FIntVector(256, 1, 1)));
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FDrawDebugCardGuidesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDrawDebugCardGuidesCS);
	SHADER_USE_PARAMETER_STRUCT(FDrawDebugCardGuidesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(uint32,  DebugMode)
		SHADER_PARAMETER(FMatrix44f, LocalToWorld)
		
		SHADER_PARAMETER(uint32,  RenVertexCount)
		SHADER_PARAMETER(FVector3f, RenRestOffset)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RenDeformedOffset)
		
		SHADER_PARAMETER(uint32,  SimVertexCount)
		SHADER_PARAMETER(FVector3f, SimRestOffset)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, SimDeformedOffset)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RenRestPosition)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RenDeformedPosition)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, SimRestPosition)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, SimDeformedPosition)

		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_GUIDE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDrawDebugCardGuidesCS, "/Engine/Private/HairStrands/HairCardsDebug.usf", "MainCS", SF_Compute);

static void AddDrawDebugCardsGuidesPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FGlobalShaderMap* ShaderMap,
	const FHairGroupInstance* Instance,
	const FShaderPrintData* ShaderPrintData,
	const bool bDeformed, 
	const bool bRen)
{
	if (!ShaderPrint::IsSupported(View.GetShaderPlatform()))
	{
		return;
	}

	// Force shader debug to be enabled
	if (!ShaderPrint::IsEnabled())
	{
		ShaderPrint::SetEnabled(true);
	}

	const uint32 MaxCount = 128000;
	ShaderPrint::RequestSpaceForLines(MaxCount);

	if (Instance->HairGroupPublicData->VFInput.GeometryType != EHairGeometryType::Cards || ShaderPrintData == nullptr)
	{
		return;
	}

	const int32 HairLODIndex = Instance->HairGroupPublicData->GetIntLODIndex();
	if (!Instance->Cards.IsValid(HairLODIndex))
	{
		return;
	}

	const FHairGroupInstance::FCards::FLOD& LOD = Instance->Cards.LODs[HairLODIndex];
	
	if (!LOD.Guides.Data)
	{
		return;
	}
	TShaderMapRef<FDrawDebugCardGuidesCS> ComputeShader(ShaderMap);
	const bool bGuideValid			= Instance->Guides.RestResource != nullptr;
	const bool bGuideDeformValid	= Instance->Guides.DeformedResource != nullptr;
	const bool bRenderValid			= LOD.Guides.RestResource != nullptr;
	const bool bRenderDeformValid	= LOD.Guides.DeformedResource != nullptr;
	if (bRen  && !bRenderValid)						{ return; }
	if (bRen  && bDeformed && !bRenderDeformValid)	{ return; }
	if (!bRen && !bGuideValid)						{ return; }
	if (!bRen && bDeformed && !bGuideDeformValid)	{ return; }

	FRDGBufferSRVRef DefaultBuffer = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultBuffer(GraphBuilder, 8, 0u), PF_R16G16B16A16_UINT);

	FDrawDebugCardGuidesCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDrawDebugCardGuidesCS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;

	Parameters->RenVertexCount = 0;
	Parameters->RenRestOffset = FVector3f::ZeroVector;
	Parameters->RenRestPosition = DefaultBuffer;
	Parameters->RenDeformedOffset = DefaultBuffer;
	Parameters->RenDeformedPosition = DefaultBuffer;

	Parameters->SimVertexCount = 0;
	Parameters->SimRestOffset = FVector3f::ZeroVector;
	Parameters->SimRestPosition = DefaultBuffer;
	Parameters->SimDeformedOffset = DefaultBuffer;
	Parameters->SimDeformedPosition = DefaultBuffer;

	if (bRen)
	{
		Parameters->RenVertexCount = LOD.Guides.RestResource->GetVertexCount();
		Parameters->RenRestOffset = (FVector3f)LOD.Guides.RestResource->GetPositionOffset();
		Parameters->RenRestPosition = RegisterAsSRV(GraphBuilder, LOD.Guides.RestResource->PositionBuffer);
		if (bDeformed)
		{
			Parameters->RenDeformedOffset = RegisterAsSRV(GraphBuilder, LOD.Guides.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::Current));
			Parameters->RenDeformedPosition = RegisterAsSRV(GraphBuilder, LOD.Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current));
		}
	}

	if (!bRen)
	{
		Parameters->SimVertexCount = Instance->Guides.RestResource->GetVertexCount();
		Parameters->SimRestOffset = (FVector3f)Instance->Guides.RestResource->GetPositionOffset();
		Parameters->SimRestPosition = RegisterAsSRV(GraphBuilder, Instance->Guides.RestResource->PositionBuffer);
		if (bDeformed)
		{
			Parameters->SimDeformedOffset = RegisterAsSRV(GraphBuilder, Instance->Guides.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::Current));
			Parameters->SimDeformedPosition = RegisterAsSRV(GraphBuilder, Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current));
		}
	}

	Parameters->LocalToWorld = FMatrix44f(Instance->LocalToWorld.ToMatrixWithScale());		// LWC_TODO: Precision loss

	const TCHAR* DebugName = TEXT("Unknown");
	if (!bDeformed &&  bRen) { Parameters->DebugMode = 1; DebugName = TEXT("Ren, Rest"); } 
	if ( bDeformed &&  bRen) { Parameters->DebugMode = 2; DebugName = TEXT("Ren, Deformed"); } 
	if (!bDeformed && !bRen) { Parameters->DebugMode = 3; DebugName = TEXT("Sim, Rest"); } 
	if ( bDeformed && !bRen) { Parameters->DebugMode = 4; DebugName = TEXT("Sim, Deformed"); } 

	ShaderPrint::SetParameters(GraphBuilder, *ShaderPrintData, Parameters->ShaderPrintParameters);

	const uint32 VertexCount = Parameters->DebugMode <= 2 ? Parameters->RenVertexCount : Parameters->SimVertexCount;
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrands::DebugCards(%s)", DebugName), ComputeShader, Parameters,
	FIntVector::DivideAndRoundUp(FIntVector(VertexCount, 1, 1), FIntVector(32, 1, 1)));
}

///////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SHADER_PARAMETER_STRUCT(FHairDebugCanvasParameter, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

static const TCHAR* ToString(EHairGeometryType In)
{
	switch (In)
	{
	case EHairGeometryType::NoneGeometry:	return TEXT("None");
	case EHairGeometryType::Strands:		return TEXT("Strands");
	case EHairGeometryType::Cards:			return TEXT("Cards");
	case EHairGeometryType::Meshes:			return TEXT("Meshes");
	}
	return TEXT("None");
}

static const TCHAR* ToString(EHairBindingType In)
{
	switch (In)
	{
	case EHairBindingType::NoneBinding: return TEXT("None");
	case EHairBindingType::Rigid:		return TEXT("Rigid");
	case EHairBindingType::Skinning:	return TEXT("Skinning");
	}
	return TEXT("None");
}

static const TCHAR* ToString(EHairLODSelectionType In)
{
	switch (In)
	{
	case EHairLODSelectionType::Immediate:	return TEXT("Immed");
	case EHairLODSelectionType::Predicted:	return TEXT("Predic");
	case EHairLODSelectionType::Forced:		return TEXT("Forced");
	}
	return TEXT("None");
}

FCachedGeometry GetCacheGeometryForHair(
	FRDGBuilder& GraphBuilder,
	FHairGroupInstance* Instance,
	const FGPUSkinCache* SkinCache,
	FGlobalShaderMap* ShaderMap);


///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairDebugPrintInstanceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairDebugPrintInstanceCS);
	SHADER_USE_PARAMETER_STRUCT(FHairDebugPrintInstanceCS, FGlobalShader);

	class FOutputType : SHADER_PERMUTATION_INT("PERMUTATION_OUTPUT_TYPE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FOutputType>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, InstanceCount)
		SHADER_PARAMETER(uint32, NameInfoCount)
		SHADER_PARAMETER(uint32, NameCharacterCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, NameInfos)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint8>, Names)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, Infos)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<int>, InstanceAABB)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Tool, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		// Skip optimization for avoiding long compilation time due to large UAV writes
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_Debug);
		OutEnvironment.SetDefine(TEXT("SHADER_PRINT_INSTANCE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairDebugPrintInstanceCS, "/Engine/Private/HairStrands/HairStrandsDebugPrint.usf", "MainCS", SF_Compute);

struct FHairDebugNameInfo
{
	uint32 PrimitiveID;
	uint16 Offset;
	uint8  Length;
	uint8  Pad0;
};

static void AddHairDebugPrintInstancePass(
	FRDGBuilder& GraphBuilder, 
	FGlobalShaderMap* ShaderMap,
	const FShaderPrintData* ShaderPrintData,
	const FHairStrandsInstances& Instances)
{
	const uint32 InstanceCount = Instances.Num();

	// Request more drawing primitives & characters for printing if needed	
	ShaderPrint::SetEnabled(true);
	ShaderPrint::RequestSpaceForLines(InstanceCount * 16u);
	ShaderPrint::RequestSpaceForCharacters(InstanceCount * 256 + 512);

	if (!ShaderPrintData || !ShaderPrintData || InstanceCount == 0) { return; }

	const uint32 MaxPrimitiveNameCount = 128u;
	check(sizeof(FHairDebugNameInfo) == 8);

	TArray<FHairDebugNameInfo> NameInfos;
	TArray<uint8> Names;
	Names.Reserve(MaxPrimitiveNameCount * 30u);

	TArray<FUintVector4> Infos;
	Infos.Reserve(InstanceCount);
	for (uint32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
	{
		const FHairStrandsInstance* AbstractInstance = Instances[InstanceIndex];
		const FHairGroupInstance* Instance = static_cast<const FHairGroupInstance*>(AbstractInstance);

		// Collect Names
		if (InstanceIndex < MaxPrimitiveNameCount)
		{
			const FString Name = *Instance->Debug.GroomAssetName;
			const uint32 NameOffset = Names.Num();
			const uint32 NameLength = Name.Len();
			for (TCHAR C : Name)
			{
				Names.Add(uint8(C));
			}

			FHairDebugNameInfo& NameInfo = NameInfos.AddDefaulted_GetRef();
			NameInfo.PrimitiveID = InstanceIndex;
			NameInfo.Length = NameLength;
			NameInfo.Offset = NameOffset;
		}

		const float LODIndex = Instance->HairGroupPublicData->LODIndex;
		const uint32 IntLODIndex = Instance->HairGroupPublicData->LODIndex;
		const uint32 LODCount = Instance->HairGroupPublicData->GetLODScreenSizes().Num();

		const uint32 DataX =
			((Instance->Debug.GroupIndex & 0xFF)) |
			((Instance->Debug.GroupCount & 0xFF) << 8) |
			((LODCount & 0xFF) << 16) |
			((uint32(Instance->GeometryType) & 0x7)<<24) |
			((uint32(Instance->BindingType) & 0x7)<<27) |
			((Instance->Guides.bIsSimulationEnable ? 0x1 : 0x0) << 30) |
			((Instance->Guides.bHasGlobalInterpolation ? 0x1 : 0x0) << 31);

		const uint32 DataY =
			(FFloat16(LODIndex).Encoded) |
			(FFloat16(Instance->Strands.Modifier.HairLengthScale_Override ? Instance->Strands.Modifier.HairLengthScale : -1.f).Encoded << 16);

		uint32 DataZ = 0;
		uint32 DataW = 0;
		switch (Instance->GeometryType)
		{
		case EHairGeometryType::Strands:
			if (Instance->Strands.IsValid())
			{
				DataZ = Instance->Strands.Data->GetNumCurves(); // Change this later on for having dynamic value
				DataW = Instance->Strands.Data->GetNumPoints(); // Change this later on for having dynamic value
			}
			break;
		case EHairGeometryType::Cards:
			if (Instance->Cards.IsValid(IntLODIndex))
			{
				DataZ = Instance->Cards.LODs[IntLODIndex].Guides.IsValid() ? Instance->Cards.LODs[IntLODIndex].Guides.Data->GetNumCurves() : 0;
				DataW = Instance->Cards.LODs[IntLODIndex].Data->GetNumVertices();
			}
			break;
		case EHairGeometryType::Meshes:
			if (Instance->Meshes.IsValid(IntLODIndex))
			{
				DataZ = 0;
				DataW = Instance->Meshes.LODs[IntLODIndex].Data->GetNumVertices();
			}
			break;
		}

		Infos.Add(FUintVector4(DataX, DataY, DataZ, DataW));
	}

	if (NameInfos.IsEmpty())
	{
		FHairDebugNameInfo& NameInfo = NameInfos.AddDefaulted_GetRef();
		NameInfo.PrimitiveID = ~0;
		NameInfo.Length = 4;
		NameInfo.Offset = 0;
		Names.Add(uint8('N'));
		Names.Add(uint8('o'));
		Names.Add(uint8('n'));
		Names.Add(uint8('e'));
	}	

	const uint32 InfoInBytes = 16u;
	FRDGBufferRef NameBuffer = CreateVertexBuffer(GraphBuilder, TEXT("Hair.Debug.InstanceNames"), FRDGBufferDesc::CreateBufferDesc(1, Names.Num()), Names.GetData(), Names.Num());
	FRDGBufferRef NameInfoBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("Hair.Debug.InstanceNameInfos"), NameInfos);	
	FRDGBufferRef InfoBuffer = CreateVertexBuffer(GraphBuilder, TEXT("Hair.Debug.InstanceInfos"), FRDGBufferDesc::CreateBufferDesc(InfoInBytes, Infos.Num()), Infos.GetData(), InfoInBytes * Infos.Num());

	// Draw general information for all instances (one pass for all instances)
	{
		FHairDebugPrintInstanceCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairDebugPrintInstanceCS::FParameters>();
		Parameters->InstanceCount = InstanceCount;
		Parameters->NameInfoCount = NameInfos.Num();
		Parameters->NameCharacterCount = Names.Num();
		Parameters->Names = GraphBuilder.CreateSRV(NameBuffer, PF_R8_UINT);
		Parameters->NameInfos = GraphBuilder.CreateSRV(NameInfoBuffer);
		Parameters->Infos = GraphBuilder.CreateSRV(InfoBuffer, PF_R32G32B32A32_UINT);
		ShaderPrint::SetParameters(GraphBuilder, *ShaderPrintData, Parameters->ShaderPrintUniformBuffer);
		FHairDebugPrintInstanceCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FHairDebugPrintInstanceCS::FOutputType>(0);
		TShaderMapRef<FHairDebugPrintInstanceCS> ComputeShader(ShaderMap, PermutationVector);

		ClearUnusedGraphResources(ComputeShader, Parameters);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairStrands::DebugPrintInstance(Info,Instances:%d)", InstanceCount),
			ComputeShader,
			Parameters,
			FIntVector(1, 1, 1));
	}

	// Draw instances bound (one pass for each instance, due to separate AABB resources)
	FHairDebugPrintInstanceCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairDebugPrintInstanceCS::FOutputType>(1);
	TShaderMapRef<FHairDebugPrintInstanceCS> ComputeShader(ShaderMap, PermutationVector);
	for (uint32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
	{
		const FHairStrandsInstance* AbstractInstance = Instances[InstanceIndex];
		const FHairGroupInstance* Instance = static_cast<const FHairGroupInstance*>(AbstractInstance);

		if (Instance->GeometryType == EHairGeometryType::Strands)
		{
			FHairDebugPrintInstanceCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairDebugPrintInstanceCS::FParameters>();
			Parameters->InstanceAABB = Register(GraphBuilder, Instance->HairGroupPublicData->GroupAABBBuffer, ERDGImportedBufferFlags::CreateSRV).SRV;
			ShaderPrint::SetParameters(GraphBuilder, *ShaderPrintData, Parameters->ShaderPrintUniformBuffer);
			ClearUnusedGraphResources(ComputeShader, Parameters);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("HairStrands::DebugPrintInstance(Bound)", InstanceCount),
				ComputeShader,
				Parameters,
				FIntVector(1, 1, 1));
		}
	}
}

void RunHairStrandsDebug(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const FSceneView& View,
	const FHairStrandsInstances& Instances,
	const FGPUSkinCache* SkinCache,
	const FShaderPrintData* ShaderPrintData,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef SceneDepthTexture,
	FIntRect Viewport,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer)
{
	const EHairDebugMode HairDebugMode = GetHairStrandsDebugMode();

	if (HairDebugMode == EHairDebugMode::MacroGroups)
	{
		AddHairDebugPrintInstancePass(GraphBuilder, ShaderMap, ShaderPrintData, Instances);
	}

	if (HairDebugMode == EHairDebugMode::MeshProjection)
	{
		{
			bool bClearDepth = true;
			FRDGTextureRef DepthTexture;
			{
				const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(SceneColorTexture->Desc.Extent, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);
				DepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("Hair.InterpolationDepthTexture"));
			}

			if (GHairDebugMeshProjection_SkinCacheMesh > 0)
			{
				auto RenderMeshProjection = [&bClearDepth, ShaderMap, Viewport, &ViewUniformBuffer, Instances, SkinCache, &SceneColorTexture, &DepthTexture, &GraphBuilder](FRDGBuilder& LocalGraphBuilder, EHairStrandsProjectionMeshType MeshType)
				{
					FHairStrandsProjectionMeshData::LOD MeshProjectionLODData;
					GetGroomInterpolationData(GraphBuilder, ShaderMap, Instances, MeshType, SkinCache, MeshProjectionLODData);
					for (FHairStrandsProjectionMeshData::Section& Section : MeshProjectionLODData.Sections)
					{
						AddDebugProjectionMeshPass(LocalGraphBuilder, ShaderMap, Viewport, ViewUniformBuffer, MeshType, bClearDepth, Section, SceneColorTexture, DepthTexture);
						bClearDepth = false;
					}
				};

				RenderMeshProjection(GraphBuilder, EHairStrandsProjectionMeshType::DeformedMesh);
				RenderMeshProjection(GraphBuilder, EHairStrandsProjectionMeshType::RestMesh);
				RenderMeshProjection(GraphBuilder, EHairStrandsProjectionMeshType::SourceMesh);
				RenderMeshProjection(GraphBuilder, EHairStrandsProjectionMeshType::TargetMesh);
			}

			auto RenderProjectionData = [&GraphBuilder, ShaderMap, Viewport, &ViewUniformBuffer, Instances, SkinCache, &bClearDepth, SceneColorTexture, DepthTexture](EHairStrandsInterpolationType StrandType, bool bRestTriangle, bool bRestFrame, bool bDeformedTriangle, bool bDeformedFrame)
			{
				TArray<int32> HairLODIndices;
				for (FHairStrandsInstance* AbstractInstance : Instances)
				{
					FHairGroupInstance* Instance = static_cast<FHairGroupInstance*>(AbstractInstance);
					if (!Instance->HairGroupPublicData || Instance->BindingType != EHairBindingType::Skinning)
						continue;

					const bool bRenderStrands = StrandType == EHairStrandsInterpolationType::RenderStrands;
					FHairStrandsRestRootResource* RestRootResource = bRenderStrands ? Instance->Strands.RestRootResource : Instance->Guides.RestRootResource;
					FHairStrandsDeformedRootResource* DeformedRootResource = bRenderStrands ? Instance->Strands.DeformedRootResource : Instance->Guides.DeformedRootResource;
					if (RestRootResource == nullptr || DeformedRootResource == nullptr)
						continue;

					const int32 MeshLODIndex = Instance->Debug.MeshLODIndex;

					if (bRestTriangle)
					{
						AddDebugProjectionHairPass(
							GraphBuilder, 
							ShaderMap, 
							Viewport, 
							ViewUniformBuffer, 
							bClearDepth, 
							EDebugProjectionHairType::HairTriangle, 
							HairStrandsTriangleType::RestPose, 
							MeshLODIndex,
							RestRootResource, 
							DeformedRootResource, 
							Instance->HairGroupPublicData->VFInput.LocalToWorldTransform, 
							SceneColorTexture, 
							DepthTexture);
						bClearDepth = false;
					}
					if (bRestFrame)
					{
						AddDebugProjectionHairPass(
							GraphBuilder,
							ShaderMap,
							Viewport,
							ViewUniformBuffer,
							bClearDepth,
							EDebugProjectionHairType::HairFrame,
							HairStrandsTriangleType::RestPose,
							MeshLODIndex,
							RestRootResource,
							DeformedRootResource,
							Instance->HairGroupPublicData->VFInput.LocalToWorldTransform,
							SceneColorTexture,
							DepthTexture);
						bClearDepth = false;
					}
					if (bDeformedTriangle)
					{
						AddDebugProjectionHairPass(
							GraphBuilder,
							ShaderMap,
							Viewport,
							ViewUniformBuffer,
							bClearDepth,
							EDebugProjectionHairType::HairTriangle,
							HairStrandsTriangleType::DeformedPose,
							MeshLODIndex,
							RestRootResource,
							DeformedRootResource,
							Instance->HairGroupPublicData->VFInput.LocalToWorldTransform,
							SceneColorTexture,
							DepthTexture);
						bClearDepth = false;
					}
					if (bDeformedFrame)
					{
						AddDebugProjectionHairPass(
							GraphBuilder,
							ShaderMap,
							Viewport,
							ViewUniformBuffer,
							bClearDepth,
							EDebugProjectionHairType::HairFrame,
							HairStrandsTriangleType::DeformedPose,
							MeshLODIndex,
							RestRootResource,
							DeformedRootResource,
							Instance->HairGroupPublicData->VFInput.LocalToWorldTransform,
							SceneColorTexture,
							DepthTexture);
						bClearDepth = false;
					}
				}
			};

			if (GHairDebugMeshProjection_Render_HairRestTriangles > 0 ||
				GHairDebugMeshProjection_Render_HairRestFrames > 0 ||
				GHairDebugMeshProjection_Render_HairDeformedTriangles > 0 ||
				GHairDebugMeshProjection_Render_HairDeformedFrames > 0)
			{
				RenderProjectionData(
					EHairStrandsInterpolationType::RenderStrands,
					GHairDebugMeshProjection_Render_HairRestTriangles > 0,
					GHairDebugMeshProjection_Render_HairRestFrames > 0,
					GHairDebugMeshProjection_Render_HairDeformedTriangles > 0,
					GHairDebugMeshProjection_Render_HairDeformedFrames > 0);
			}

			if (GHairDebugMeshProjection_Sim_HairRestTriangles > 0 ||
				GHairDebugMeshProjection_Sim_HairRestFrames > 0 ||
				GHairDebugMeshProjection_Sim_HairDeformedTriangles > 0 ||
				GHairDebugMeshProjection_Sim_HairDeformedFrames > 0)
			{
				RenderProjectionData(
					EHairStrandsInterpolationType::SimulationStrands,
					GHairDebugMeshProjection_Sim_HairRestTriangles > 0,
					GHairDebugMeshProjection_Sim_HairRestFrames > 0,
					GHairDebugMeshProjection_Sim_HairDeformedTriangles > 0,
					GHairDebugMeshProjection_Sim_HairDeformedFrames > 0);
			}
		}
	}

	if (GHairCardsVoxelDebug > 0)
	{
		for (FHairStrandsInstance* AbstractInstance : Instances)
		{
			FHairGroupInstance* Instance = static_cast<FHairGroupInstance*>(AbstractInstance);

			AddVoxelPlainRaymarchingPass(GraphBuilder, View, ShaderMap, Instance, ShaderPrintData, SceneColorTexture);
		}
	}

	if (GHairCardsAtlasDebug > 0)
	{
		for (FHairStrandsInstance* AbstractInstance : Instances)
		{
			FHairGroupInstance* Instance = static_cast<FHairGroupInstance*>(AbstractInstance);

			AddDrawDebugCardsAtlasPass(GraphBuilder, View, ShaderMap, Instance, ShaderPrintData, SceneColorTexture);
		}
	}

	for (FHairStrandsInstance* AbstractInstance : Instances)
	{
		FHairGroupInstance* Instance = static_cast<FHairGroupInstance*>(AbstractInstance);

		if (GHairCardsGuidesDebug_Ren > 0 || Instance->Debug.bDrawCardsGuides)
		{
			AddDrawDebugCardsGuidesPass(GraphBuilder, View, ShaderMap, Instance, ShaderPrintData, Instance->Debug.bDrawCardsGuides ? false : GHairCardsGuidesDebug_Ren == 1, true);
		}

		if (GHairCardsGuidesDebug_Sim > 0)
		{
			AddDrawDebugCardsGuidesPass(GraphBuilder, View, ShaderMap, Instance, ShaderPrintData, GHairCardsGuidesDebug_Sim == 1, false);
		}

		if (GHairStrandsControlPointDebug || Instance->HairGroupPublicData->DebugMode == EHairStrandsDebugMode::RenderHairControlPoints)
		{
			AddDrawDebugStrandsCVsPass(GraphBuilder, View, ShaderMap, Instance, ShaderPrintData, SceneColorTexture, SceneDepthTexture);
		}
	}
}