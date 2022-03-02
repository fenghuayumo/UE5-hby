// Copyright Epic Games, Inc. All Rights Reserved.

#include "AllocationsAnalysis.h"
#include "Model/AllocationsProvider.h"
#include "ProfilingDebugging/MemoryTrace.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace TraceServices
{

#define INSIGHTS_MEM_TRACE_LEGACY_FORMAT 1 // backward compatibility with legacy memory trace format (5.0-EA)

////////////////////////////////////////////////////////////////////////////////////////////////////

FAllocationsAnalyzer::FAllocationsAnalyzer(IAnalysisSession& InSession, FAllocationsProvider& InAllocationsProvider)
	: Session(InSession)
	, AllocationsProvider(InAllocationsProvider)
	, BaseCycle(0)
	, MarkerPeriod(0)
	, LastMarkerCycle(0)
	, LastMarkerSeconds(0.0)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FAllocationsAnalyzer::~FAllocationsAnalyzer()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	FInterfaceBuilder& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_Init,                "Memory", "Init");
	Builder.RouteEvent(RouteId_Alloc,               "Memory", "Alloc");
	Builder.RouteEvent(RouteId_AllocSystem,         "Memory", "AllocSystem");
	Builder.RouteEvent(RouteId_AllocVideo,          "Memory", "AllocVideo");
	Builder.RouteEvent(RouteId_Free,                "Memory", "Free");
	Builder.RouteEvent(RouteId_FreeSystem,          "Memory", "FreeSystem");
	Builder.RouteEvent(RouteId_FreeVideo,           "Memory", "FreeVideo");
	Builder.RouteEvent(RouteId_ReallocAlloc,        "Memory", "ReallocAlloc");
	Builder.RouteEvent(RouteId_ReallocAllocSystem,  "Memory", "ReallocAllocSystem");
	Builder.RouteEvent(RouteId_ReallocFree,         "Memory", "ReallocFree");
	Builder.RouteEvent(RouteId_ReallocFreeSystem,   "Memory", "ReallocFreeSystem");
	Builder.RouteEvent(RouteId_Marker,              "Memory", "Marker");
	Builder.RouteEvent(RouteId_TagSpec,             "Memory", "TagSpec");
	Builder.RouteEvent(RouteId_HeapSpec,            "Memory", "HeapSpec");
	Builder.RouteEvent(RouteId_HeapMarkAlloc,       "Memory", "HeapMarkAlloc");
	Builder.RouteEvent(RouteId_HeapUnmarkAlloc,     "Memory", "HeapUnmarkAlloc");

	Builder.RouteLoggerEvents(RouteId_MemScope, "Memory", true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsAnalyzer::OnAnalysisEnd()
{
	double SessionTime;
	{
		FAnalysisSessionEditScope _(Session);
		SessionTime = Session.GetDurationSeconds();
		if (LastMarkerSeconds > SessionTime)
		{
			Session.UpdateDurationSeconds(LastMarkerSeconds);
		}
	}
	const double Time = FMath::Max(SessionTime, LastMarkerSeconds);

	FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
	AllocationsProvider.EditOnAnalysisCompleted(Time);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FAllocationsAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	const auto& EventData = Context.EventData;
	HeapId RootHeap = 0;

	switch (RouteId)
	{
		case RouteId_Init:
		{
#if !INSIGHTS_MEM_TRACE_LEGACY_FORMAT
			const uint8 Version = EventData.GetValue<uint8>("Version");

			constexpr uint8 MinSupportedVersion = 1; // UE 5.0
			constexpr uint8 MaxSupportedVersion = 1; // UE 5.0
			if (Version < MinSupportedVersion || Version > MaxSupportedVersion)
			{
				break;
			}
#endif // !INSIGHTS_MEM_TRACE_LEGACY_FORMAT

			const double Time = GetCurrentTime();
#if INSIGHTS_MEM_TRACE_LEGACY_FORMAT
			BaseCycle = EventData.GetValue<uint64>("BaseCycle", 0);
#endif // INSIGHTS_MEM_TRACE_LEGACY_FORMAT
			MarkerPeriod = EventData.GetValue<uint32>("MarkerPeriod");

			const uint8 MinAlignment = EventData.GetValue<uint8>("MinAlignment");
			SizeShift = EventData.GetValue<uint8>("SizeShift");

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.EditInit(Time, MinAlignment);
			break;
		}

		case RouteId_HeapSpec:
		{
			const HeapId Id = EventData.GetValue<uint16>("Id");
			const HeapId ParentId = EventData.GetValue<uint16>("ParentId");
			const EMemoryTraceHeapFlags Flags = EventData.GetValue<EMemoryTraceHeapFlags>("Flags");
			FStringView Name;
			EventData.GetString("Name", Name);

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.EditHeapSpec(Id, ParentId, Name, Flags);
			break;
		}

		case RouteId_AllocSystem:
		case RouteId_AllocVideo:
		case RouteId_ReallocAllocSystem:
		{
			RootHeap = RouteId == RouteId_AllocVideo ? EMemoryTraceRootHeap::VideoMemory : EMemoryTraceRootHeap::SystemMemory;
		}
		// intentional fallthrough
		case RouteId_Alloc:
		case RouteId_ReallocAlloc:
		{
			// TODO: Can we have a struct mapping over the EventData?
			// Something like:
			//     const auto& Ev = (const FAllocEvent&)EventData.Get(); // probably not aligned
			// Or something like:
			//     FAllocEvent Event; // aligned
			//     EventData.CopyData(&Event, sizeof(Event));

			const double Time = GetCurrentTime();

			// CallstackId is optional. If the field is not present CallstackId will be 0 (i.e. "no callstack").
			uint32 CallstackId = EventData.GetValue<uint32>("CallstackId", 0);
#if INSIGHTS_MEM_TRACE_LEGACY_FORMAT
			if (!CallstackId)
			{
				const ICallstacksProvider* CallstacksProvider = ReadCallstacksProvider(Session);
				if (CallstacksProvider)
				{
					// Legacy format of sending the hash value
					const uint64 CallstackHash = EventData.GetValue<uint64>("Owner");
					CallstackId = CallstacksProvider->GetCallstackIdForHash(CallstackHash);
				}
			}
#endif // INSIGHTS_MEM_TRACE_LEGACY_FORMAT

			uint64 Address = EventData.GetValue<uint64>("Address");

			RootHeap = EventData.GetValue<uint8>("RootHeap", RootHeap);

			uint64 SizeUpper = EventData.GetValue<uint32>("Size");
			const uint8 SizeLowerMask = ((1 << SizeShift) - 1);
			const uint8 AlignmentMask = ~SizeLowerMask;
			uint64 Size = 0;
			uint32 Alignment = 0;
#if INSIGHTS_MEM_TRACE_LEGACY_FORMAT
			const uint8 Alignment_SizeLower = EventData.GetValue<uint8>("Alignment_SizeLower");
			if (Alignment_SizeLower)
			{
				Size = SizeUpper << SizeShift | static_cast<uint64>(Alignment_SizeLower & SizeLowerMask);
				Alignment = Alignment_SizeLower & AlignmentMask;
			}
			else
#endif // INSIGHTS_MEM_TRACE_LEGACY_FORMAT
			{
				const uint8 AlignmentPow2_SizeLower = EventData.GetValue<uint8>("AlignmentPow2_SizeLower");
				Size = SizeUpper << SizeShift | static_cast<uint64>(AlignmentPow2_SizeLower & SizeLowerMask);
				Alignment = 1 << (AlignmentPow2_SizeLower >> SizeShift);
			}

			const uint32 TraceThreadId = Context.ThreadInfo.GetId();
			const uint32 SystemThreadId = Context.ThreadInfo.GetSystemId();

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.SetCurrentThreadId(TraceThreadId, SystemThreadId);
			AllocationsProvider.EditAlloc(Time, CallstackId, Address, Size, Alignment, RootHeap);
			if (RouteId == RouteId_ReallocAlloc || RouteId == RouteId_ReallocAllocSystem)
			{
				const uint8 Tracker = 0; // We only care about the default tracker for now.
				AllocationsProvider.EditPopTagFromPtr(SystemThreadId, Tracker);
			}
			break;
		}

		case RouteId_FreeSystem:
		case RouteId_FreeVideo:
		case RouteId_ReallocFreeSystem:
		{
			RootHeap = RouteId == RouteId_FreeVideo ? EMemoryTraceRootHeap::VideoMemory : EMemoryTraceRootHeap::SystemMemory;
		}
		// intentional fallthrough
		case RouteId_Free:
		case RouteId_ReallocFree:
		{
			const double Time = GetCurrentTime();

			// CallstackId is optional. If the field is not present CallstackId will be 0 (i.e. "no callstack").
			uint32 CallstackId = EventData.GetValue<uint32>("CallstackId", 0);

			uint64 Address = EventData.GetValue<uint64>("Address");
#if INSIGHTS_MEM_TRACE_LEGACY_FORMAT
			if (Address == 0)
			{
				// Legacy format of sending Address and RootHeap packed togather (after 5.0-EA).
				constexpr uint32 HeapShift = 60;
				constexpr uint64 RootHeapMask = uint64(0xF) << HeapShift;
				const uint64 Address_RootHeap = EventData.GetValue<uint64>("Address_RootHeap");
				Address = Address_RootHeap & ~RootHeapMask;
				RootHeap = (Address_RootHeap & RootHeapMask) >> HeapShift;
			}
#endif // INSIGHTS_MEM_TRACE_LEGACY_FORMAT

			RootHeap = EventData.GetValue<uint8>("RootHeap", RootHeap);

			const uint32 TraceThreadId = Context.ThreadInfo.GetId();
			const uint32 SystemThreadId = Context.ThreadInfo.GetSystemId();

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.SetCurrentThreadId(TraceThreadId, SystemThreadId);
			if (RouteId == RouteId_ReallocFree || RouteId == RouteId_ReallocFreeSystem)
			{
				const uint8 Tracker = 0; // We only care about the default tracker for now.
				AllocationsProvider.EditPushTagFromPtr(SystemThreadId, Tracker, Address);
			}
			AllocationsProvider.EditFree(Time, CallstackId, Address, RootHeap);
			break;
		}

		case RouteId_HeapMarkAlloc:
		{
			const double Time = GetCurrentTime();
			const uint64 Address = EventData.GetValue<uint64>("Address");
			const HeapId Heap = EventData.GetValue<uint16>("Heap", 0);
			const EMemoryTraceHeapAllocationFlags Flags = EventData.GetValue<EMemoryTraceHeapAllocationFlags>("Flags");

			const uint32 TraceThreadId = Context.ThreadInfo.GetId();
			const uint32 SystemThreadId = Context.ThreadInfo.GetSystemId();

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.SetCurrentThreadId(TraceThreadId, SystemThreadId);
			AllocationsProvider.EditMarkAllocationAsHeap(Time, Address, Heap, Flags);
			break;
		}

		case RouteId_HeapUnmarkAlloc:
		{
			const double Time = GetCurrentTime();
			const uint64 Address = EventData.GetValue<uint64>("Address");
			const HeapId Heap = EventData.GetValue<uint16>("Heap", 0);

			const uint32 TraceThreadId = Context.ThreadInfo.GetId();
			const uint32 SystemThreadId = Context.ThreadInfo.GetSystemId();

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.SetCurrentThreadId(TraceThreadId, SystemThreadId);
			AllocationsProvider.EditUnmarkAllocationAsHeap(Time, Address, Heap);
			break;
		}

		case RouteId_Marker:
		{
			// If BaseCycle is 0, then Cycle is a 64-bit absolute value, otherwise Cycle is a 32-bit value (relative to BaseCycle).
			const uint64 Cycle = (BaseCycle == 0) ? EventData.GetValue<uint64>("Cycle") : BaseCycle + EventData.GetValue<uint32>("Cycle");

			if (ensure(Cycle >= LastMarkerCycle))
			{
				const double Seconds = Context.EventTime.AsSeconds(Cycle);
				check(Seconds >= LastMarkerSeconds);
				if (ensure((Seconds - LastMarkerSeconds < 60.0) || LastMarkerSeconds == 0.0f))
				{
					LastMarkerCycle = Cycle;
					LastMarkerSeconds = Seconds;
					{
						FAnalysisSessionEditScope _(Session);
						double SessionTime = Session.GetDurationSeconds();
						if (LastMarkerSeconds > SessionTime)
						{
							Session.UpdateDurationSeconds(LastMarkerSeconds);
						}
					}
				}
			}
			break;
		}

		case RouteId_TagSpec:
		{
			const TagIdType Tag = Context.EventData.GetValue<TagIdType>("Tag");
			const TagIdType Parent = Context.EventData.GetValue<TagIdType>("Parent");

			FString Display;
			Context.EventData.GetString("Display", Display);
			const TCHAR* DisplayString = Session.StoreString(*Display);

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.EditAddTagSpec(Tag, Parent, DisplayString);
			break;
		}

		case RouteId_MemScope:
		{
			const uint32 ThreadId = Context.ThreadInfo.GetSystemId();
			const uint8 Tracker = 0; // We only care about the default tracker for now.

			if (Style == EStyle::EnterScope)
			{
				if (FCStringAnsi::Strlen(Context.EventData.GetTypeInfo().GetName()) == 11) // "MemoryScope"
				{
					const TagIdType Tag = Context.EventData.GetValue<TagIdType>("Tag");
					FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
					AllocationsProvider.EditPushTag(ThreadId, Tracker, Tag);
				}
				else // "MemoryScopePtr"
				{
					const uint64 Ptr = Context.EventData.GetValue<uint64>("Ptr");
					FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
					AllocationsProvider.EditPushTagFromPtr(ThreadId, Tracker, Ptr);
				}
			}
			else // EStyle::LeaveScope
			{
				FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
				if (AllocationsProvider.HasTagFromPtrScope(ThreadId, Tracker)) // Is TagFromPtr scope active?
				{
					AllocationsProvider.EditPopTagFromPtr(ThreadId, Tracker);
				}
				else
				{
					AllocationsProvider.EditPopTag(ThreadId, Tracker);
				}
			}
			break;
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double FAllocationsAnalyzer::GetCurrentTime() const
{
	return LastMarkerSeconds;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef INSIGHTS_MEM_TRACE_LEGACY_FORMAT

} // namespace TraceServices
