// Copyright Epic Games, Inc. All Rights Reserved.

#include "VulkanRayTracing.h"

#if VULKAN_RHI_RAYTRACING

#include "VulkanContext.h"
#include "VulkanDescriptorSets.h"
#include "BuiltInRayTracingShaders.h"
#include "Experimental/Containers/SherwoodHashTable.h"
#include "Async/ParallelFor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

TAutoConsoleVariable<int32> GVulkanRayTracingCVar(
	TEXT("r.Vulkan.RayTracing"),
	0,
	TEXT("0: Do not enable Vulkan ray tracing extensions (default)\n")
	TEXT("1: Enable experimental ray tracing support (for development and testing purposes)"),
	ECVF_ReadOnly
);

// Define ray tracing entry points
#define DEFINE_VK_ENTRYPOINTS(Type,Func) VULKANRHI_API Type VulkanDynamicAPI::Func = NULL;
ENUM_VK_ENTRYPOINTS_RAYTRACING(DEFINE_VK_ENTRYPOINTS)

void FVulkanRayTracingPlatform::GetDeviceExtensions(EGpuVendorId VendorId, TArray<const ANSICHAR*>& OutExtensions)
{
	if (!GVulkanRayTracingCVar.GetValueOnAnyThread() || FParse::Param(FCommandLine::Get(), TEXT("noraytracing")))
	{
		return;
	}

	// Primary extensions
	OutExtensions.Add(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	OutExtensions.Add(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
	OutExtensions.Add(VK_KHR_RAY_QUERY_EXTENSION_NAME);

	// VK_KHR_acceleration_structure dependencies 
	OutExtensions.Add(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME); // Note: Promoted to Vulkan 1.2
	OutExtensions.Add(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME); // Note: Promoted to Vulkan 1.2
	OutExtensions.Add(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	
	// VK_KHR_ray_tracing_pipeline dependency
	OutExtensions.Add(VK_KHR_SPIRV_1_4_EXTENSION_NAME); // Note: Promoted to Vulkan 1.2

	// VK_KHR_spirv_1_4 dependency
	OutExtensions.Add(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME); // Note: Promoted to Vulkan 1.2
}

void FVulkanRayTracingPlatform::EnablePhysicalDeviceFeatureExtensions(VkDeviceCreateInfo& DeviceInfo, FVulkanDevice& Device)
{
	if (Device.GetOptionalExtensions().HasRaytracingExtensions())
	{
		FOptionalVulkanDeviceFeatures& Features = Device.GetOptionalFeatures();

		ZeroVulkanStruct(Features.BufferDeviceAddressFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);
		Features.BufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
		Features.BufferDeviceAddressFeatures.pNext = const_cast<void*>(DeviceInfo.pNext);

		ZeroVulkanStruct(Features.AccelerationStructureFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR);;
		Features.AccelerationStructureFeatures.accelerationStructure = VK_TRUE;
		Features.AccelerationStructureFeatures.descriptorBindingAccelerationStructureUpdateAfterBind = VK_TRUE;
		Features.AccelerationStructureFeatures.pNext = &Features.BufferDeviceAddressFeatures;
		
		ZeroVulkanStruct(Features.RayTracingPipelineFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR);
		Features.RayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
		Features.RayTracingPipelineFeatures.rayTraversalPrimitiveCulling = VK_TRUE;
		Features.RayTracingPipelineFeatures.pNext = &Features.AccelerationStructureFeatures;

		ZeroVulkanStruct(Features.RayQueryFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR);
		Features.RayQueryFeatures.pNext = &Features.RayTracingPipelineFeatures;
		Features.RayQueryFeatures.rayQuery = VK_TRUE;

		ZeroVulkanStruct(Features.DescriptorIndexingFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT);
		Features.DescriptorIndexingFeatures.pNext = &Features.RayQueryFeatures;

		DeviceInfo.pNext = &Features.DescriptorIndexingFeatures;
	}
}

#if PLATFORM_WINDOWS
#pragma warning(push)
#pragma warning(disable : 4191) // warning C4191: 'type cast': unsafe conversion
#endif // PLATFORM_WINDOWS
bool FVulkanRayTracingPlatform::LoadVulkanInstanceFunctions(VkInstance inInstance)
{
	bool bFoundAllEntryPoints = true;
#define CHECK_VK_ENTRYPOINTS(Type,Func) if (VulkanDynamicAPI::Func == NULL) { bFoundAllEntryPoints = false; UE_LOG(LogRHI, Warning, TEXT("Failed to find entry point for %s"), TEXT(#Func)); }
#define GETINSTANCE_VK_ENTRYPOINTS(Type, Func) VulkanDynamicAPI::Func = (Type)VulkanDynamicAPI::vkGetInstanceProcAddr(inInstance, #Func);
	ENUM_VK_ENTRYPOINTS_RAYTRACING(GETINSTANCE_VK_ENTRYPOINTS);
#if UE_BUILD_DEBUG
	ENUM_VK_ENTRYPOINTS_RAYTRACING(CHECK_VK_ENTRYPOINTS);
#endif
	return bFoundAllEntryPoints;
}
#if PLATFORM_WINDOWS
#pragma warning(pop) // restore 4191
#endif

static VkDeviceAddress GetDeviceAddress(VkDevice Device, VkBuffer Buffer)
{
	VkBufferDeviceAddressInfoKHR DeviceAddressInfo;
	ZeroVulkanStruct(DeviceAddressInfo, VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
	DeviceAddressInfo.buffer = Buffer;
	return vkGetBufferDeviceAddressKHR(Device, &DeviceAddressInfo);
}

VkDeviceAddress FVulkanResourceMultiBuffer::GetDeviceAddress() const
{
	return ::GetDeviceAddress(Device->GetInstanceHandle(), GetHandle()) + GetOffset();
}

// Temporary brute force allocation helper, this should be handled by the memory sub-allocator
static uint32 FindMemoryType(VkPhysicalDevice Gpu, uint32 Filter, VkMemoryPropertyFlags RequestedProperties)
{
	VkPhysicalDeviceMemoryProperties Properties = {};
	vkGetPhysicalDeviceMemoryProperties(Gpu, &Properties);

	uint32 Result = UINT32_MAX;
	for (uint32 i = 0; i < Properties.memoryTypeCount; ++i)
	{
		const bool bTypeFilter = Filter & (1 << i);
		const bool bPropFilter = (Properties.memoryTypes[i].propertyFlags & RequestedProperties) == RequestedProperties;
		if (bTypeFilter && bPropFilter)
		{
			Result = i;
			break;
		}
	}

	check(Result < UINT32_MAX);
	return Result;
}

// Temporary brute force allocation
void FVulkanRayTracingAllocator::Allocate(FVulkanDevice* Device, VkDeviceSize Size, VkBufferUsageFlags UsageFlags, VkMemoryPropertyFlags MemoryFlags, FVkRtAllocation& Result)
{
	VkMemoryRequirements MemoryRequirements;
	Result.Buffer = VulkanRHI::CreateBuffer(Device, Size, UsageFlags, MemoryRequirements);

	VkDevice DeviceHandle = Device->GetInstanceHandle();
	VkPhysicalDevice Gpu = Device->GetPhysicalHandle();

	VkMemoryAllocateFlagsInfo MemoryAllocateFlagsInfo;
	ZeroVulkanStruct(MemoryAllocateFlagsInfo, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO);
	MemoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

	VkMemoryAllocateInfo MemoryAllocateInfo;
	ZeroVulkanStruct(MemoryAllocateInfo, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
	MemoryAllocateInfo.pNext = &MemoryAllocateFlagsInfo;
	MemoryAllocateInfo.allocationSize = MemoryRequirements.size;
	MemoryAllocateInfo.memoryTypeIndex = FindMemoryType(Gpu, MemoryRequirements.memoryTypeBits, MemoryFlags);
	VERIFYVULKANRESULT(vkAllocateMemory(DeviceHandle, &MemoryAllocateInfo, VULKAN_CPU_ALLOCATOR, &Result.Memory));
	VERIFYVULKANRESULT(vkBindBufferMemory(DeviceHandle, Result.Buffer, Result.Memory, 0));

	Result.Device = DeviceHandle;
}

// Temporary brute force deallocation
void FVulkanRayTracingAllocator::Free(FVkRtAllocation& Allocation)
{
	if (Allocation.Buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(Allocation.Device, Allocation.Buffer, VULKAN_CPU_ALLOCATOR);
		Allocation.Buffer = VK_NULL_HANDLE;
	}
	if (Allocation.Memory != VK_NULL_HANDLE)
	{
		vkFreeMemory(Allocation.Device, Allocation.Memory, VULKAN_CPU_ALLOCATOR);
		Allocation.Memory = VK_NULL_HANDLE;
	}
}

static void GetBLASBuildData(
	const VkDevice Device,
	const TArrayView<const FRayTracingGeometrySegment> Segments,
	const FBufferRHIRef IndexBufferRHI,
	const uint32 IndexBufferOffset,
	const bool bFastBuild,
	const bool bAllowUpdate,
	const uint32 IndexStrideInBytes,
	const EAccelerationStructureBuildMode BuildMode,
	FVkRtBLASBuildData& BuildData)
{
	static constexpr uint32 IndicesPerPrimitive = 3; // Only triangle meshes are supported

	FVulkanResourceMultiBuffer* const IndexBuffer = ResourceCast(IndexBufferRHI.GetReference());
	VkDeviceOrHostAddressConstKHR IndexBufferDeviceAddress = {};
	IndexBufferDeviceAddress.deviceAddress = IndexBufferRHI ? IndexBuffer->GetDeviceAddress() + IndexBufferOffset : 0;

	TArray<uint32, TInlineAllocator<1>> PrimitiveCounts;

	for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
	{
		const FRayTracingGeometrySegment& Segment = Segments[SegmentIndex];

		FVulkanResourceMultiBuffer* const VertexBuffer = ResourceCast(Segment.VertexBuffer.GetReference());

		VkDeviceOrHostAddressConstKHR VertexBufferDeviceAddress = {};
		VertexBufferDeviceAddress.deviceAddress = VertexBuffer->GetDeviceAddress() + Segment.VertexBufferOffset;

		VkAccelerationStructureGeometryKHR SegmentGeometry;
		ZeroVulkanStruct(SegmentGeometry, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);

		if (Segment.bForceOpaque)
		{
			SegmentGeometry.flags |= VK_GEOMETRY_OPAQUE_BIT_KHR;
		}

		if (!Segment.bAllowDuplicateAnyHitShaderInvocation)
		{
			// Allow only a single any-hit shader invocation per primitive
			SegmentGeometry.flags |= VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
		}

		// Only support triangles
		SegmentGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;

		SegmentGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		SegmentGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		SegmentGeometry.geometry.triangles.vertexData = VertexBufferDeviceAddress;
		SegmentGeometry.geometry.triangles.maxVertex = Segment.MaxVertices;
		SegmentGeometry.geometry.triangles.vertexStride = Segment.VertexBufferStride;
		SegmentGeometry.geometry.triangles.indexData = IndexBufferDeviceAddress;

		switch (Segment.VertexBufferElementType)
		{
		case VET_Float3:
		case VET_Float4:
			SegmentGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
			break;
		default:
			checkNoEntry();
			break;
		}

		// No support for segment transform
		SegmentGeometry.geometry.triangles.transformData.deviceAddress = 0;
		SegmentGeometry.geometry.triangles.transformData.hostAddress = nullptr;

		uint32 PrimitiveOffset = 0;

		if (IndexBufferRHI)
		{
			SegmentGeometry.geometry.triangles.indexType = (IndexStrideInBytes == 2) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
			// offset in bytes into the index buffer where primitive data for the current segment is defined
			PrimitiveOffset = Segment.FirstPrimitive * IndicesPerPrimitive * IndexStrideInBytes;
		}
		else
		{
			SegmentGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
			// for non-indexed geometry, primitiveOffset is applied when reading from vertex buffer
			PrimitiveOffset = Segment.FirstPrimitive * IndicesPerPrimitive * Segment.VertexBufferStride;
		}

		BuildData.Segments.Add(SegmentGeometry);

		VkAccelerationStructureBuildRangeInfoKHR RangeInfo = {};
		RangeInfo.firstVertex = 0;

		// Disabled segments use an empty range. We still build them to keep the sbt valid.
		RangeInfo.primitiveCount = (Segment.bEnabled) ? Segment.NumPrimitives : 0;
		RangeInfo.primitiveOffset = PrimitiveOffset;
		RangeInfo.transformOffset = 0;

		BuildData.Ranges.Add(RangeInfo);

		PrimitiveCounts.Add(Segment.NumPrimitives);
	}

	BuildData.GeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	BuildData.GeometryInfo.flags = (bFastBuild) ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	if (bAllowUpdate)
	{
		BuildData.GeometryInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
	}
	BuildData.GeometryInfo.mode = (BuildMode == EAccelerationStructureBuildMode::Build) ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
	BuildData.GeometryInfo.geometryCount = BuildData.Segments.Num();
	BuildData.GeometryInfo.pGeometries = BuildData.Segments.GetData();

	vkGetAccelerationStructureBuildSizesKHR(
		Device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&BuildData.GeometryInfo,
		PrimitiveCounts.GetData(),
		&BuildData.SizesInfo);
}

FVulkanRayTracingGeometry::FVulkanRayTracingGeometry(ENoInit)
{}

FVulkanRayTracingGeometry::FVulkanRayTracingGeometry(const FRayTracingGeometryInitializer& InInitializer, FVulkanDevice* InDevice)
	: FRHIRayTracingGeometry(InInitializer), Device(InDevice)
{
	// Only supporting triangles initially
	check(Initializer.GeometryType == ERayTracingGeometryType::RTGT_Triangles);

	uint32 IndexBufferStride = 0;
	if (Initializer.IndexBuffer)
	{
		// In case index buffer in initializer is not yet in valid state during streaming we assume the geometry is using UINT32 format.
		IndexBufferStride = Initializer.IndexBuffer->GetSize() > 0
			? Initializer.IndexBuffer->GetStride()
			: 4;
	}

	checkf(!Initializer.IndexBuffer || (IndexBufferStride == 2 || IndexBufferStride == 4), TEXT("Index buffer must be 16 or 32 bit if in use."));

	FVkRtBLASBuildData BuildData;
	GetBLASBuildData(
		Device->GetInstanceHandle(), 
		MakeArrayView(Initializer.Segments),
		Initializer.IndexBuffer,
		Initializer.IndexBufferOffset,
		Initializer.bFastBuild,
		Initializer.bAllowUpdate,
		IndexBufferStride,
		EAccelerationStructureBuildMode::Build,
		BuildData);

	FString DebugNameString = Initializer.DebugName.ToString();
	FRHIResourceCreateInfo BlasBufferCreateInfo(*DebugNameString);
	AccelerationStructureBuffer = ResourceCast(RHICreateBuffer(BuildData.SizesInfo.accelerationStructureSize, BUF_AccelerationStructure, 0, ERHIAccess::BVHWrite, BlasBufferCreateInfo).GetReference());

	FRHIResourceCreateInfo ScratchBufferCreateInfo(TEXT("BuildScratchBLAS"));
	ScratchBuffer = ResourceCast(RHICreateBuffer(BuildData.SizesInfo.buildScratchSize, BUF_StructuredBuffer | BUF_RayTracingScratch, 0, ERHIAccess::UAVCompute, ScratchBufferCreateInfo).GetReference());

	VkDevice NativeDevice = Device->GetInstanceHandle();

	VkAccelerationStructureCreateInfoKHR CreateInfo;
	ZeroVulkanStruct(CreateInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
	CreateInfo.buffer = AccelerationStructureBuffer->GetHandle();
	CreateInfo.offset = AccelerationStructureBuffer->GetOffset();
	CreateInfo.size = BuildData.SizesInfo.accelerationStructureSize;
	CreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	VERIFYVULKANRESULT(vkCreateAccelerationStructureKHR(NativeDevice, &CreateInfo, VULKAN_CPU_ALLOCATOR, &Handle));

	
	SizeInfo.ResultSize = BuildData.SizesInfo.accelerationStructureSize;
	SizeInfo.BuildScratchSize = BuildData.SizesInfo.buildScratchSize;
	SizeInfo.UpdateScratchSize = BuildData.SizesInfo.updateScratchSize;


	VkAccelerationStructureDeviceAddressInfoKHR DeviceAddressInfo;
	ZeroVulkanStruct(DeviceAddressInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
	DeviceAddressInfo.accelerationStructure = Handle;
	Address = vkGetAccelerationStructureDeviceAddressKHR(NativeDevice, &DeviceAddressInfo);
}

FVulkanRayTracingGeometry::~FVulkanRayTracingGeometry()
{
	if (Handle != VK_NULL_HANDLE)
	{
		Device->GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::AccelerationStructure, Handle);
	}
}

void FVulkanRayTracingGeometry::SetInitializer(const FRayTracingGeometryInitializer& InInitializer)
{
	checkf(InitializedType == ERayTracingGeometryInitializerType::StreamingDestination, TEXT("Only FVulkanRayTracingGeometry that was created as StreamingDestination can update their initializer."));
	Initializer = InInitializer;

	// TODO: Update HitGroup Parameters
}

void FVulkanRayTracingGeometry::Swap(FVulkanRayTracingGeometry& Other)
{
	::Swap(Handle, Other.Handle);
	::Swap(Address, Other.Address);

	AccelerationStructureBuffer = Other.AccelerationStructureBuffer;
	ScratchBuffer = Other.ScratchBuffer;

	// The rest of the members should be updated using SetInitializer()
}

void FVulkanRayTracingGeometry::BuildAccelerationStructure(FVulkanCommandListContext& CommandContext, EAccelerationStructureBuildMode BuildMode)
{
	FVkRtBLASBuildData BuildData;
	GetBLASBuildData(
		Device->GetInstanceHandle(),
		MakeArrayView(Initializer.Segments),
		Initializer.IndexBuffer,
		Initializer.IndexBufferOffset,
		Initializer.bFastBuild,
		Initializer.bAllowUpdate,
		Initializer.IndexBuffer ? Initializer.IndexBuffer->GetStride() : 0,
		BuildMode,
		BuildData);

	check(BuildData.SizesInfo.accelerationStructureSize <= AccelerationStructureBuffer->GetSize());

	BuildData.GeometryInfo.dstAccelerationStructure = Handle;
	BuildData.GeometryInfo.scratchData.deviceAddress = ScratchBuffer->GetDeviceAddress();

	VkAccelerationStructureBuildRangeInfoKHR* const pBuildRanges = BuildData.Ranges.GetData();

	FVulkanCommandBufferManager& CommandBufferManager = *CommandContext.GetCommandBufferManager();
	FVulkanCmdBuffer* const CmdBuffer = CommandBufferManager.GetActiveCmdBuffer();
	vkCmdBuildAccelerationStructuresKHR(CmdBuffer->GetHandle(), 1, &BuildData.GeometryInfo, &pBuildRanges);

	CommandBufferManager.SubmitActiveCmdBuffer();
	CommandBufferManager.PrepareForNewActiveCommandBuffer();

	// No longer need scratch memory for a static build
	if (!Initializer.bAllowUpdate)
	{
		ScratchBuffer = nullptr;
	}
}

static void GetTLASBuildData(
	const VkDevice Device,
	const uint32 NumInstances,
	const VkDeviceAddress InstanceBufferAddress,
	FVkRtTLASBuildData& BuildData)
{
	VkDeviceOrHostAddressConstKHR InstanceBufferDeviceAddress = {};
	InstanceBufferDeviceAddress.deviceAddress = InstanceBufferAddress;

	BuildData.Geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	BuildData.Geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	BuildData.Geometry.geometry.instances.arrayOfPointers = VK_FALSE;
	BuildData.Geometry.geometry.instances.data = InstanceBufferDeviceAddress;

	BuildData.GeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	BuildData.GeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	BuildData.GeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	BuildData.GeometryInfo.geometryCount = 1;
	BuildData.GeometryInfo.pGeometries = &BuildData.Geometry;

	vkGetAccelerationStructureBuildSizesKHR(
		Device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&BuildData.GeometryInfo,
		&NumInstances,
		&BuildData.SizesInfo);
}

static VkGeometryInstanceFlagsKHR TranslateRayTracingInstanceFlags(ERayTracingInstanceFlags InFlags)
{
	VkGeometryInstanceFlagsKHR Result = 0;

	if (EnumHasAnyFlags(InFlags, ERayTracingInstanceFlags::TriangleCullDisable))
	{
		Result |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
	}

	if (!EnumHasAnyFlags(InFlags, ERayTracingInstanceFlags::TriangleCullReverse))
	{
		// Counterclockwise is the default for UE.
		Result |= VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR;
	}

	if (EnumHasAnyFlags(InFlags, ERayTracingInstanceFlags::ForceOpaque))
	{
		Result |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
	}

	if (EnumHasAnyFlags(InFlags, ERayTracingInstanceFlags::ForceNonOpaque))
	{
		Result |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
	}

	return Result;
}

// This structure is analogous to FHitGroupSystemParameters in D3D12 RHI.
// However, it only contains generic parameters that do not require a full shader binding table (i.e. no per-hit-group user data).
// It is designed to be used to access vertex and index buffers during inline ray tracing.
struct FVulkanRayTracingGeometryParameters
{
	union
	{
		struct
		{
			uint32 IndexStride : 8; // Can be just 1 bit to indicate 16 or 32 bit indices
			uint32 VertexStride : 8; // Can be just 2 bits to indicate float3, float2 or half2 format
			uint32 Unused : 16;
		} Config;
		uint32 ConfigBits = 0;
	};
	uint32 IndexBufferOffsetInBytes = 0;
	uint64 IndexBuffer = 0;
	uint64 VertexBuffer = 0;
};

FVulkanRayTracingScene::FVulkanRayTracingScene(FRayTracingSceneInitializer2 InInitializer, FVulkanDevice* InDevice)
	: Device(InDevice), Initializer(MoveTemp(InInitializer))
{
	const ERayTracingAccelerationStructureFlags BuildFlags = ERayTracingAccelerationStructureFlags::FastTrace; // #yuriy_todo: pass this in
	SizeInfo = RHICalcRayTracingSceneSize(Initializer.NumNativeInstances, BuildFlags);

	const uint32 ParameterBufferSize = FMath::Max<uint32>(1, Initializer.NumTotalSegments) * sizeof(FVulkanRayTracingGeometryParameters);
	FRHIResourceCreateInfo ParameterBufferCreateInfo(TEXT("RayTracingSceneMetadata"));
	PerInstanceGeometryParameterBuffer = ResourceCast(RHICreateBuffer(
		ParameterBufferSize, BUF_StructuredBuffer | BUF_ShaderResource, sizeof(FVulkanRayTracingGeometryParameters),
		ERHIAccess::SRVCompute, ParameterBufferCreateInfo).GetReference());

	PerInstanceGeometryParameterSRV = new FVulkanShaderResourceView(Device, PerInstanceGeometryParameterBuffer, 0);
}

FVulkanRayTracingScene::~FVulkanRayTracingScene()
{
}

void FVulkanRayTracingScene::BindBuffer(FRHIBuffer* InBuffer, uint32 InBufferOffset)
{
	checkf(AccelerationStructureView == nullptr, TEXT("Binding multiple buffers is not currently supported."));

	check(IsInRHIThread() || !IsRunningRHIInSeparateThread());

	check(SizeInfo.ResultSize + InBufferOffset <= InBuffer->GetSize());
	check(InBufferOffset % 256 == 0); // Spec requires offset to be a multiple of 256
	AccelerationStructureBuffer = ResourceCast(InBuffer);

	FShaderResourceViewInitializer ViewInitializer(InBuffer, InBufferOffset, 0);
	AccelerationStructureView = new FVulkanShaderResourceView(Device, AccelerationStructureBuffer, InBufferOffset);
}

void FVulkanRayTracingScene::BuildAccelerationStructure(
	FVulkanCommandListContext& CommandContext,
	FVulkanResourceMultiBuffer* InScratchBuffer, uint32 InScratchOffset,
	FVulkanResourceMultiBuffer* InInstanceBuffer, uint32 InInstanceOffset)
{
	check(AccelerationStructureBuffer.IsValid());
	check(InInstanceBuffer != nullptr);
	const bool bExternalScratchBuffer = InScratchBuffer != nullptr;

	VkDeviceAddress InstanceBufferAddress = InInstanceBuffer->GetDeviceAddress() + InInstanceOffset;

	// Build a metadata buffer	that contains VulkanRHI-specific per-geometry parameters that allow us to access
	// vertex and index buffers from shaders that use inline ray tracing.
	BuildPerInstanceGeometryParameterBuffer();

	FVkRtTLASBuildData BuildData;
	GetTLASBuildData(Device->GetInstanceHandle(), Initializer.NumNativeInstances, InstanceBufferAddress, BuildData);

	TRefCountPtr<FVulkanResourceMultiBuffer> ScratchBuffer;

	if (!bExternalScratchBuffer)
	{
		FRHIResourceCreateInfo ScratchBufferCreateInfo(TEXT("BuildScratchTLAS"));
		ScratchBuffer = ResourceCast(RHICreateBuffer(BuildData.SizesInfo.buildScratchSize, BUF_UnorderedAccess | BUF_StructuredBuffer, 0, ERHIAccess::UAVCompute, ScratchBufferCreateInfo).GetReference());
		InScratchBuffer = ScratchBuffer.GetReference();
	}

	checkf(AccelerationStructureView, TEXT("A buffer must be bound to the ray tracing scene before it can be built."));
	BuildData.GeometryInfo.dstAccelerationStructure = AccelerationStructureView->AccelerationStructureHandle;

	BuildData.GeometryInfo.scratchData.deviceAddress = InScratchBuffer->GetDeviceAddress();
	if (bExternalScratchBuffer)
	{
		BuildData.GeometryInfo.scratchData.deviceAddress += InScratchOffset;
	}

	VkAccelerationStructureBuildRangeInfoKHR TLASBuildRangeInfo;
	TLASBuildRangeInfo.primitiveCount = Initializer.NumNativeInstances;
	TLASBuildRangeInfo.primitiveOffset = 0;
	TLASBuildRangeInfo.transformOffset = 0;
	TLASBuildRangeInfo.firstVertex = 0;

	VkAccelerationStructureBuildRangeInfoKHR* const pBuildRanges = &TLASBuildRangeInfo;

	FVulkanCommandBufferManager& CommandBufferManager = *CommandContext.GetCommandBufferManager();
	FVulkanCmdBuffer* const CmdBuffer = CommandBufferManager.GetActiveCmdBuffer();
	vkCmdBuildAccelerationStructuresKHR(CmdBuffer->GetHandle(), 1, &BuildData.GeometryInfo, &pBuildRanges);

	CommandBufferManager.SubmitActiveCmdBuffer();
	CommandBufferManager.PrepareForNewActiveCommandBuffer();
}

void FVulkanRayTracingScene::BuildPerInstanceGeometryParameterBuffer()
{
	// TODO: we could cache parameters in the geometry object to avoid some of the pointer chasing (if this is measured to be a performance issue)

	const uint32 ParameterBufferSize = FMath::Max<uint32>(1, Initializer.NumTotalSegments) * sizeof(FVulkanRayTracingGeometryParameters);
	check(PerInstanceGeometryParameterBuffer->GetSize() >= ParameterBufferSize);

	check(IsInRHIThread() || !IsRunningRHIInSeparateThread());
	const bool bTopOfPipe = false; // running on RHI timeline

	void* MappedBuffer = PerInstanceGeometryParameterBuffer->Lock(bTopOfPipe, RLM_WriteOnly, ParameterBufferSize, 0);
	FVulkanRayTracingGeometryParameters* MappedParameters = reinterpret_cast<FVulkanRayTracingGeometryParameters*>(MappedBuffer);
	uint32 ParameterIndex = 0;

	for (FRHIRayTracingGeometry* GeometryRHI : Initializer.PerInstanceGeometries)
	{
		const FVulkanRayTracingGeometry* Geometry = ResourceCast(GeometryRHI);
		const FRayTracingGeometryInitializer& GeometryInitializer = Geometry->GetInitializer();

		const FVulkanResourceMultiBuffer* IndexBuffer = ResourceCast(GeometryInitializer.IndexBuffer.GetReference());

		const uint32 IndexStride = IndexBuffer ? IndexBuffer->GetStride() : 0;
		const uint32 IndexOffsetInBytes = GeometryInitializer.IndexBufferOffset;
		const VkDeviceAddress IndexBufferAddress = IndexBuffer ? IndexBuffer->GetDeviceAddress() : VkDeviceAddress(0);

		for (const FRayTracingGeometrySegment& Segment : GeometryInitializer.Segments)
		{
			const FVulkanResourceMultiBuffer* VertexBuffer = ResourceCast(Segment.VertexBuffer.GetReference());
			checkf(VertexBuffer, TEXT("All ray tracing geometry segments must have a valid vertex buffer"));
			const VkDeviceAddress VertexBufferAddress = VertexBuffer->GetDeviceAddress();

			FVulkanRayTracingGeometryParameters SegmentParameters;
			SegmentParameters.Config.IndexStride = IndexStride;
			SegmentParameters.Config.VertexStride = Segment.VertexBufferStride;

			if (IndexStride)
			{
				SegmentParameters.IndexBufferOffsetInBytes = IndexOffsetInBytes + IndexStride * Segment.FirstPrimitive * 3;
				SegmentParameters.IndexBuffer = static_cast<uint64>(IndexBufferAddress);
			}
			else
			{
				SegmentParameters.IndexBuffer = 0;
			}

			SegmentParameters.VertexBuffer = static_cast<uint64>(VertexBufferAddress) + Segment.VertexBufferOffset;

			check(ParameterIndex < Initializer.NumTotalSegments);
			MappedParameters[ParameterIndex] = SegmentParameters;
			ParameterIndex++;
		}
	}

	check(ParameterIndex == Initializer.NumTotalSegments);

	PerInstanceGeometryParameterBuffer->Unlock(bTopOfPipe);
}

void FVulkanDynamicRHI::RHITransferRayTracingGeometryUnderlyingResource(FRHIRayTracingGeometry* DestGeometry, FRHIRayTracingGeometry* SrcGeometry)
{
	check(DestGeometry);
	FVulkanRayTracingGeometry* Dest = ResourceCast(DestGeometry);
	if (!SrcGeometry)
	{		
		TRefCountPtr<FVulkanRayTracingGeometry> DeletionProxy = new FVulkanRayTracingGeometry(NoInit);
		Dest->Swap(*DeletionProxy);
	}
	else
	{
		FVulkanRayTracingGeometry* Src = ResourceCast(SrcGeometry);
		Dest->Swap(*Src);
	}
}

FRayTracingAccelerationStructureSize FVulkanDynamicRHI::RHICalcRayTracingSceneSize(uint32 MaxInstances, ERayTracingAccelerationStructureFlags Flags)
{
	FVkRtTLASBuildData BuildData;
	const VkDeviceAddress InstanceBufferAddress = 0; // No device address available when only querying TLAS size
	GetTLASBuildData(Device->GetInstanceHandle(), MaxInstances, InstanceBufferAddress, BuildData);

	FRayTracingAccelerationStructureSize Result;
	Result.ResultSize = BuildData.SizesInfo.accelerationStructureSize;
	Result.BuildScratchSize = BuildData.SizesInfo.buildScratchSize;
	Result.UpdateScratchSize = BuildData.SizesInfo.updateScratchSize;

	return Result;
}

FRayTracingAccelerationStructureSize FVulkanDynamicRHI::RHICalcRayTracingGeometrySize(const FRayTracingGeometryInitializer& Initializer)
{	
	const uint32 IndexStrideInBytes = Initializer.IndexBuffer ? Initializer.IndexBuffer->GetStride() : 0;

	FVkRtBLASBuildData BuildData;
	GetBLASBuildData(
		Device->GetInstanceHandle(),
		MakeArrayView(Initializer.Segments),
		Initializer.IndexBuffer,
		Initializer.IndexBufferOffset,
		Initializer.bFastBuild,
		Initializer.bAllowUpdate,
		IndexStrideInBytes,
		EAccelerationStructureBuildMode::Build,
		BuildData);

	FRayTracingAccelerationStructureSize Result;
	Result.ResultSize = BuildData.SizesInfo.accelerationStructureSize;
	Result.BuildScratchSize = BuildData.SizesInfo.buildScratchSize;
	Result.UpdateScratchSize = BuildData.SizesInfo.updateScratchSize;
	
	return Result;
}

FRayTracingSceneRHIRef FVulkanDynamicRHI::RHICreateRayTracingScene(FRayTracingSceneInitializer2 Initializer)
{
	return new FVulkanRayTracingScene(MoveTemp(Initializer), GetDevice());
}

FRayTracingGeometryRHIRef FVulkanDynamicRHI::RHICreateRayTracingGeometry(const FRayTracingGeometryInitializer& Initializer)
{
	return new FVulkanRayTracingGeometry(Initializer, GetDevice());
}

void FVulkanCommandListContext::RHIClearRayTracingBindings(FRHIRayTracingScene* Scene)
{
	 // TODO
}

void FVulkanCommandListContext::RHIBindAccelerationStructureMemory(FRHIRayTracingScene* Scene, FRHIBuffer* Buffer, uint32 BufferOffset)
{
	ResourceCast(Scene)->BindBuffer(Buffer, BufferOffset);
}

// Todo: High level rhi call should have transitioned and verified vb and ib to read for each segment
void FVulkanCommandListContext::RHIBuildAccelerationStructures(const TArrayView<const FRayTracingGeometryBuildParams> Params, const FRHIBufferRange& ScratchBufferRange)
{
	for (const FRayTracingGeometryBuildParams& P : Params)
	{
		FVulkanRayTracingGeometry* const Geometry = ResourceCast(P.Geometry.GetReference());

		// Todo: Update geometry from params for each segment
		// Todo: Can we do this only for an update?
		// Todo: Use provided ScratchBuffer instead of allocating.

		// Build as for each segment
		Geometry->BuildAccelerationStructure(*this, P.BuildMode);
	}
}

void FVulkanCommandListContext::RHIBuildAccelerationStructure(const FRayTracingSceneBuildParams& SceneBuildParams)
{
	FVulkanRayTracingScene* const Scene = ResourceCast(SceneBuildParams.Scene);
	FVulkanResourceMultiBuffer* const ScratchBuffer = ResourceCast(SceneBuildParams.ScratchBuffer);
	FVulkanResourceMultiBuffer* const InstanceBuffer = ResourceCast(SceneBuildParams.InstanceBuffer);
	Scene->BuildAccelerationStructure(
		*this, 
		ScratchBuffer, SceneBuildParams.ScratchBufferOffset, 
		InstanceBuffer, SceneBuildParams.InstanceBufferOffset);
}

void FVulkanCommandListContext::RHIRayTraceOcclusion(FRHIRayTracingScene* Scene, FRHIShaderResourceView* Rays, FRHIUnorderedAccessView* Output, uint32 NumRays)
{
	// todo
	return;
}

template<typename ShaderType>
static FRHIRayTracingShader* GetBuiltInRayTracingShader()
{
	const FGlobalShaderMap* const ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	auto Shader = ShaderMap->GetShader<ShaderType>();
	return static_cast<FRHIRayTracingShader*>(Shader.GetRayTracingShader());
}

void FVulkanDevice::InitializeRayTracing()
{
	check(BasicRayTracingPipeline == nullptr);
	// the pipeline should be initialized on the first use due to the ability to disable RT in the game settings
	//BasicRayTracingPipeline = new FVulkanBasicRaytracingPipeline(this);
}

void FVulkanDevice::CleanUpRayTracing()
{
	if (BasicRayTracingPipeline != nullptr)
	{
		delete BasicRayTracingPipeline;
		BasicRayTracingPipeline = nullptr;
	}
}

static uint32 GetAlignedSize(uint32 Value, uint32 Alignment)
{
	return (Value + Alignment - 1) & ~(Alignment - 1);
}

FVulkanRayTracingPipelineState::FVulkanRayTracingPipelineState(FVulkanDevice* const InDevice, const FRayTracingPipelineStateInitializer& Initializer)
{	
	check(Layout == nullptr);

	TArrayView<FRHIRayTracingShader*> InitializerRayGenShaders = Initializer.GetRayGenTable();
	TArrayView<FRHIRayTracingShader*> InitializerMissShaders = Initializer.GetMissTable();
	TArrayView<FRHIRayTracingShader*> InitializerHitGroupShaders = Initializer.GetHitGroupTable();
	// vkrt todo: Callable shader support

	FVulkanDescriptorSetsLayoutInfo DescriptorSetLayoutInfo;
	FUniformBufferGatherInfo UBGatherInfo;
	
	for (FRHIRayTracingShader* RayGenShader : InitializerRayGenShaders)
	{
		const FVulkanShaderHeader& Header = static_cast<FVulkanRayGenShader*>(RayGenShader)->GetCodeHeader();
		DescriptorSetLayoutInfo.ProcessBindingsForStage(VK_SHADER_STAGE_RAYGEN_BIT_KHR, ShaderStage::RayGen, Header, UBGatherInfo);
	}

	for (FRHIRayTracingShader* MissShader : InitializerMissShaders)
	{
		const FVulkanShaderHeader& Header = static_cast<FVulkanRayMissShader*>(MissShader)->GetCodeHeader();
		DescriptorSetLayoutInfo.ProcessBindingsForStage(VK_SHADER_STAGE_MISS_BIT_KHR, ShaderStage::RayMiss, Header, UBGatherInfo);
	}

	for (FRHIRayTracingShader* HitGroupShader : InitializerHitGroupShaders)
	{
		const FVulkanShaderHeader& Header = static_cast<FVulkanRayHitGroupShader*>(HitGroupShader)->GetCodeHeader();
		DescriptorSetLayoutInfo.ProcessBindingsForStage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, ShaderStage::RayHitGroup, Header, UBGatherInfo);
		// vkrt todo: How to handle any hit for hit group?
	}

	DescriptorSetLayoutInfo.FinalizeBindings<false>(*InDevice, UBGatherInfo, TArrayView<FRHISamplerState*>());

	Layout = new FVulkanRayTracingLayout(InDevice);
	Layout->DescriptorSetLayout.CopyFrom(DescriptorSetLayoutInfo);
	FVulkanDescriptorSetLayoutMap DSetLayoutMap;
	Layout->Compile(DSetLayoutMap);

	TArray<VkPipelineShaderStageCreateInfo> ShaderStages;
	TArray<VkRayTracingShaderGroupCreateInfoKHR> ShaderGroups;
	TArray<ANSICHAR*> EntryPointNames;
	const uint32 EntryPointNameMaxLength = 24;

	for (FRHIRayTracingShader* const RayGenShaderRHI : InitializerRayGenShaders)
	{
		VkPipelineShaderStageCreateInfo ShaderStage;
		ZeroVulkanStruct(ShaderStage, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
		ShaderStage.module = static_cast<FVulkanRayGenShader*>(RayGenShaderRHI)->GetOrCreateHandle(Layout, Layout->GetDescriptorSetLayoutHash());
		ShaderStage.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
			
		ANSICHAR* const EntryPoint = new ANSICHAR[EntryPointNameMaxLength];
		static_cast<FVulkanRayGenShader*>(RayGenShaderRHI)->GetEntryPoint(EntryPoint, EntryPointNameMaxLength);
		EntryPointNames.Add(EntryPoint);
		ShaderStage.pName = EntryPoint;
		ShaderStages.Add(ShaderStage);

		VkRayTracingShaderGroupCreateInfoKHR ShaderGroup;
		ZeroVulkanStruct(ShaderGroup, VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR);
		ShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		ShaderGroup.generalShader = ShaderStages.Num() - 1;
		ShaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
		ShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		ShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		ShaderGroups.Add(ShaderGroup);
	}

	for (FRHIRayTracingShader* const MissShaderRHI : InitializerMissShaders)
	{
		VkPipelineShaderStageCreateInfo ShaderStage;
		ZeroVulkanStruct(ShaderStage, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
		ShaderStage.module = static_cast<FVulkanRayMissShader*>(MissShaderRHI)->GetOrCreateHandle(Layout, Layout->GetDescriptorSetLayoutHash());
		ShaderStage.stage = VK_SHADER_STAGE_MISS_BIT_KHR;

		ANSICHAR* const EntryPoint = new char[EntryPointNameMaxLength];
		static_cast<FVulkanRayGenShader*>(MissShaderRHI)->GetEntryPoint(EntryPoint, EntryPointNameMaxLength);
		EntryPointNames.Add(EntryPoint);
		ShaderStage.pName = EntryPoint;
		ShaderStages.Add(ShaderStage);

		VkRayTracingShaderGroupCreateInfoKHR ShaderGroup;
		ZeroVulkanStruct(ShaderGroup, VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR);
		ShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		ShaderGroup.generalShader = ShaderStages.Num() - 1;
		ShaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
		ShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		ShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		ShaderGroups.Add(ShaderGroup);
	}

	for (FRHIRayTracingShader* const HitGroupShaderRHI : InitializerHitGroupShaders)
	{
		VkPipelineShaderStageCreateInfo ShaderStage;
		ZeroVulkanStruct(ShaderStage, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
		ShaderStage.module = static_cast<FVulkanRayHitGroupShader*>(HitGroupShaderRHI)->GetOrCreateHandle(Layout, Layout->GetDescriptorSetLayoutHash());
		ShaderStage.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

		ANSICHAR* const EntryPoint = new char[EntryPointNameMaxLength];
		static_cast<FVulkanRayHitGroupShader*>(HitGroupShaderRHI)->GetEntryPoint(EntryPoint, EntryPointNameMaxLength);
		EntryPointNames.Add(EntryPoint);
		ShaderStage.pName = EntryPoint;
		ShaderStages.Add(ShaderStage);

		VkRayTracingShaderGroupCreateInfoKHR ShaderGroup;
		ZeroVulkanStruct(ShaderGroup, VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR);
		ShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
		ShaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
		ShaderGroup.closestHitShader = ShaderStages.Num() - 1;
		ShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR; // vkrt: todo
		ShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		ShaderGroups.Add(ShaderGroup);
	}

	VkRayTracingPipelineCreateInfoKHR RayTracingPipelineCreateInfo;
	ZeroVulkanStruct(RayTracingPipelineCreateInfo, VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR);
	RayTracingPipelineCreateInfo.stageCount = ShaderStages.Num();
	RayTracingPipelineCreateInfo.pStages = ShaderStages.GetData();
	RayTracingPipelineCreateInfo.groupCount = ShaderGroups.Num();
	RayTracingPipelineCreateInfo.pGroups = ShaderGroups.GetData();
	RayTracingPipelineCreateInfo.maxPipelineRayRecursionDepth = 1;
	RayTracingPipelineCreateInfo.layout = Layout->GetPipelineLayout();
	
	VERIFYVULKANRESULT(vkCreateRayTracingPipelinesKHR(
		InDevice->GetInstanceHandle(), 
		VK_NULL_HANDLE, // Deferred Operation 
		VK_NULL_HANDLE, // Pipeline Cache 
		1, 
		&RayTracingPipelineCreateInfo, 
		VULKAN_CPU_ALLOCATOR, 
		&Pipeline));

	for (ANSICHAR* const EntryPoint : EntryPointNames)
	{
		delete[] EntryPoint;
	}

	const FRayTracingProperties& Props = InDevice->GetRayTracingProperties();
	const uint32 HandleSize = Props.RayTracingPipeline.shaderGroupHandleSize;
	const uint32 HandleSizeAligned = GetAlignedSize(HandleSize, Props.RayTracingPipeline.shaderGroupHandleAlignment);
	const uint32 GroupCount = ShaderGroups.Num();
	const uint32 SBTSize = GroupCount * HandleSizeAligned;

	TArray<uint8> ShaderHandleStorage;
	ShaderHandleStorage.AddUninitialized(SBTSize);
	VERIFYVULKANRESULT(vkGetRayTracingShaderGroupHandlesKHR(InDevice->GetInstanceHandle(), Pipeline, 0, GroupCount, SBTSize, ShaderHandleStorage.GetData()));

	auto CopyHandlesToSBT = [InDevice, HandleSize, ShaderHandleStorage](FVkRtAllocation& Allocation, uint32 Offset)
	{
		FVulkanRayTracingAllocator::Allocate(
			InDevice,
			HandleSize,
			VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			Allocation);

		void* pMappedBufferMemory = nullptr;
		VERIFYVULKANRESULT(vkMapMemory(InDevice->GetInstanceHandle(), Allocation.Memory, 0, VK_WHOLE_SIZE, 0, &pMappedBufferMemory));
		{
			FMemory::Memcpy(pMappedBufferMemory, ShaderHandleStorage.GetData() + Offset, HandleSize);
		}
		vkUnmapMemory(InDevice->GetInstanceHandle(), Allocation.Memory);
	};

	CopyHandlesToSBT(RayGenShaderBindingTable, 0);
	CopyHandlesToSBT(MissShaderBindingTable, HandleSizeAligned);
	CopyHandlesToSBT(HitShaderBindingTable, HandleSizeAligned * 2);
}

FVulkanRayTracingPipelineState::~FVulkanRayTracingPipelineState()
{
	FVulkanRayTracingAllocator::Free(RayGenShaderBindingTable);
	FVulkanRayTracingAllocator::Free(MissShaderBindingTable);
	FVulkanRayTracingAllocator::Free(HitShaderBindingTable);

	if (Layout != nullptr)
	{
		delete Layout;
		Layout = nullptr;
	}
}

FVulkanBasicRaytracingPipeline::FVulkanBasicRaytracingPipeline(FVulkanDevice* const InDevice)
{
	check(Occlusion == nullptr);

	// Occlusion pipeline
	{
		FRayTracingPipelineStateInitializer OcclusionInitializer;

		FRHIRayTracingShader* OcclusionRGSTable[] = { GetBuiltInRayTracingShader<FOcclusionMainRG>() };
		OcclusionInitializer.SetRayGenShaderTable(OcclusionRGSTable);

		FRHIRayTracingShader* OcclusionMSTable[] = { GetBuiltInRayTracingShader<FDefaultPayloadMS>() };
		OcclusionInitializer.SetMissShaderTable(OcclusionMSTable);

		FRHIRayTracingShader* OcclusionCHSTable[] = { GetBuiltInRayTracingShader<FDefaultMainCHS>() };
		OcclusionInitializer.SetHitGroupTable(OcclusionCHSTable);

		OcclusionInitializer.bAllowHitGroupIndexing = false;

		Occlusion = new FVulkanRayTracingPipelineState(InDevice, OcclusionInitializer);
	}
}

FVulkanBasicRaytracingPipeline::~FVulkanBasicRaytracingPipeline()
{
	if (Occlusion != nullptr)
	{
		delete Occlusion;
		Occlusion = nullptr;
	}
}
#endif // VULKAN_RHI_RAYTRACING
