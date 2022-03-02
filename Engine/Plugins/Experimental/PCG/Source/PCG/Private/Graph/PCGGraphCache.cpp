// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGGraphCache.h"
#include "PCGSettings.h"

#include "Misc/ScopeRWLock.h"
#include "Algo/AnyOf.h"
#include "GameFramework/Actor.h"

FPCGGraphCacheEntry::FPCGGraphCacheEntry(const FPCGDataCollection& InInput, const UPCGSettings* InSettings, const FPCGDataCollection& InOutput, TWeakObjectPtr<UObject> InOwner, TSet<UObject*>& OutRootedObjects)
	: Input(InInput)
	, Output(InOutput)
{
	Settings = InSettings ? Cast<UPCGSettings>(StaticDuplicateObject(InSettings, InOwner.Get())) : nullptr;

	Input.RootUnrootedData(OutRootedObjects);
	Output.RootUnrootedData(OutRootedObjects);

	if (Settings && !Settings->IsRooted())
	{
		Settings->AddToRoot();
		OutRootedObjects.Add(Settings);
	}
}

bool FPCGGraphCacheEntry::Matches(const FPCGDataCollection& InInput, const UPCGSettings* InSettings) const
{
	const bool bHasSameSettings = ((InSettings == nullptr && Settings == nullptr) || (InSettings && Settings && *InSettings == *Settings));
	return bHasSameSettings && (Input == InInput);
}

FPCGGraphCache::FPCGGraphCache(TWeakObjectPtr<UObject> InOwner)
	: Owner(InOwner)
{
}

FPCGGraphCache::~FPCGGraphCache()
{
	ClearCache();
}

bool FPCGGraphCache::GetFromCache(const IPCGElement* InElement, const FPCGDataCollection& InInput, const UPCGSettings* InSettings, FPCGDataCollection& OutOutput) const
{
	if (!Owner.IsValid())
	{
		return false;
	}

	FReadScopeLock ScopedReadLock(CacheLock);

	if (const FPCGGraphCacheEntries* Entries = CacheData.Find(InElement))
	{
		for (const FPCGGraphCacheEntry& Entry : *Entries)
		{
			if (Entry.Matches(InInput, InSettings))
			{
				OutOutput = Entry.Output;
				return true;
			}
		}

		return false;
	}

	return false;
}

void FPCGGraphCache::StoreInCache(const IPCGElement* InElement, const FPCGDataCollection& InInput, const UPCGSettings* InSettings, const FPCGDataCollection& InOutput)
{
	if (!Owner.IsValid())
	{
		return;
	}

	FWriteScopeLock ScopedWriteLock(CacheLock);

	FPCGGraphCacheEntries* Entries = CacheData.Find(InElement);
	if(!Entries)
	{
		Entries = &(CacheData.Add(InElement));
	}

	Entries->Emplace(InInput, InSettings, InOutput, Owner, RootedData);
}

void FPCGGraphCache::ClearCache()
{
	FWriteScopeLock ScopedWriteLock(CacheLock);

	// Remove all entries
	CacheData.Reset();

	// Unroot all previously rooted data
	for (UObject* Data : RootedData)
	{
		Data->RemoveFromRoot();
	}
	RootedData.Reset();
}

#if WITH_EDITOR
void FPCGGraphCache::CleanFromCache(const IPCGElement* InElement)
{
	if (!InElement)
	{
		return;
	}

	FWriteScopeLock ScopeWriteLock(CacheLock);
	CacheData.Remove(InElement);
}
#endif // WITH_EDITOR