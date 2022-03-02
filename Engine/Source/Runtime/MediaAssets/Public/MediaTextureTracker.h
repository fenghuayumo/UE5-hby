// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class UMediaTexture;

/** Holds info on a single object. */
struct FMediaTextureTrackerObject
{
	/** Actor that is using our img sequence. */
	TWeakObjectPtr<class AActor> Object;
	/** LOD bias for the mipmap level. */
	float MipMapLODBias;
};

/**
 * Tracks which media textures are used by which objects.
 */
class FMediaTextureTracker
{
public:
	/**
	 * Access engine from here.
	 *
	 * @return Engine.
	 */
	MEDIAASSETS_API static FMediaTextureTracker& Get();

	virtual ~FMediaTextureTracker() {}

	/**
	 * Each object should call this for each media texture that the object has.
	 *
	 * @param InInfo Object that has the texture.
	 * @param InTexture Texture to register.
	 */
	MEDIAASSETS_API void RegisterTexture(TSharedPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe>& InInfo, TObjectPtr<UMediaTexture> InTexture);

	/**
	 * Each object should call this for each media texture that the object has.
	 *
	 * @param InInfo Object that has the texture.
	 * @param InTexture Texture to unregister.
	 */
	MEDIAASSETS_API void UnregisterTexture(TSharedPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe>& InInfo, TObjectPtr<UMediaTexture> InTexture);

	/**
	 * Get which objects are usnig a specific media texture.
	 *
	 * @param InTexture Media texture to check.
	 * @return Nullptr if nothing found, or a pointer to an array of objects.
	 */
	MEDIAASSETS_API const TArray<TWeakPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe>>* GetObjects(TObjectPtr<UMediaTexture> InTexture) const;

	/**
	 * Get a list of media textures we know about.
	 *
	 * @ return List of textures.
	 */
	MEDIAASSETS_API const TArray<TWeakObjectPtr<UMediaTexture>>& GetTextures() { return MediaTextures; }

protected:
	/** Maps a media texture to an array of playback components. */
	TMap<TWeakObjectPtr<UMediaTexture>, TArray<TWeakPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe>>> MapTextureToObject;
	/** Array of media textures that we know about. */
	TArray<TWeakObjectPtr<UMediaTexture>> MediaTextures;
};
