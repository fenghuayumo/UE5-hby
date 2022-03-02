// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FLayoutExtender;
class FExtensibilityManager;
class ADisplayClusterRootActor;

DECLARE_EVENT_OneParam(IDisplayClusterOperator, FOnRegisterLayoutExtensions, FLayoutExtender&);
DECLARE_EVENT_OneParam(IDisplayClusterOperator, FOnActiveRootActorChanged, ADisplayClusterRootActor*);
DECLARE_EVENT_OneParam(IDisplayClusterOperator, FOnDetailObjectsChanged, const TArray<UObject*>&);

/**
 * Display Cluster Operator module interface
 */
class IDisplayClusterOperator : public IModuleInterface
{
public:
	static constexpr const TCHAR* ModuleName = TEXT("DisplayClusterOperator");

public:
	virtual ~IDisplayClusterOperator() = default;

	/**
	* Singleton-like access to this module's interface.  This is just for convenience!
	* Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	*
	* @return Returns singleton instance, loading the module on demand if needed
	*/
	static inline IDisplayClusterOperator& Get()
	{
		return FModuleManager::GetModuleChecked<IDisplayClusterOperator>(ModuleName);
	}

	/**
	* Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	*
	* @return True if the module is loaded and ready to use
	*/
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(ModuleName);
	}

	/** Gets the event handler that is raised when the operator panel processes extensions to its layout */
	virtual FOnRegisterLayoutExtensions& OnRegisterLayoutExtensions() = 0;

	/** Gets the event handler that is raised when the operator panel changes the root actor being operated on */
	virtual FOnActiveRootActorChanged& OnActiveRootActorChanged() = 0;

	/** Gets the event handler that is raised when the objects being displayed in the operator's details panel are changed */
	virtual FOnDetailObjectsChanged& OnDetailObjectsChanged() = 0;

	/** Gets the extension ID that can be used to add tabs to the operator panel */
	virtual FName GetOperatorExtensionId() = 0;

	/** Gets the extensibility manager for the operator panel's toolbar */
	virtual TSharedPtr<FExtensibilityManager> GetOperatorToolBarExtensibilityManager() = 0;

	/** Gets a list of all nDisplay root actor instances that are on the currently loaded level */
	virtual void GetRootActorLevelInstances(TArray<ADisplayClusterRootActor*>& OutRootActorInstances) = 0;

	/** Displays the properties of the specified object in the operator's details panel */
	virtual void ShowDetailsForObject(UObject* Object) = 0;

	/** Displays the properties of the specified object in the operator's details panel */
	virtual void ShowDetailsForObjects(const TArray<UObject*>& Objects) = 0;
};