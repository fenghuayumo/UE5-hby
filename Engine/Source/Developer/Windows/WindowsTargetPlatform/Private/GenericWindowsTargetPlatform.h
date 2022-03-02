// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Common/TargetPlatformBase.h"
#include "Misc/ConfigCacheIni.h"
#if PLATFORM_WINDOWS
#include "LocalPcTargetDevice.h"
#endif
#include "Serialization/MemoryLayout.h"

#if WITH_ENGINE
	#include "Sound/SoundWave.h"
	#include "TextureResource.h"
	#include "Engine/VolumeTexture.h"
	#include "StaticMeshResources.h"
	#include "RHI.h"
	#include "AudioCompressionSettings.h"
#endif // WITH_ENGINE
#include "Windows/WindowsPlatformProperties.h"


#define LOCTEXT_NAMESPACE "TGenericWindowsTargetPlatform"

/**
 * Template for Windows target platforms
 */
#if PLATFORM_WINDOWS
template<typename TProperties, typename TTargetDevice = TLocalPcTargetDevice<PLATFORM_64BITS>>
#else
template<typename TProperties>
#endif

class TGenericWindowsTargetPlatform : public TTargetPlatformBase<TProperties>
{
public:
	typedef TTargetPlatformBase<TProperties> TSuper;

	/**
	 * Default constructor.
	 */
	TGenericWindowsTargetPlatform( )
	{
#if PLATFORM_WINDOWS
		// only add local device if actually running on Windows
		LocalDevice = MakeShareable(new TTargetDevice(*this));
#endif

	#if WITH_ENGINE
		TextureLODSettings = nullptr; // These are registered by the device profile system.
		StaticMeshLODSettings.Initialize(this);


		// Get the Target RHIs for this platform, we do not always want all those that are supported.
		TArray<FName> TargetedShaderFormats;
		TGenericWindowsTargetPlatform::GetAllTargetedShaderFormats(TargetedShaderFormats);

		static FName NAME_PCD3D_SM6(TEXT("PCD3D_SM6"));
		static FName NAME_PCD3D_SM5(TEXT("PCD3D_SM5"));
		static FName NAME_VULKAN_SM5(TEXT("SF_VULKAN_SM5"));
		static FName NAME_PCD3D_ES3_1(TEXT("PCD3D_ES31"));
		static FName NAME_SF_VULKAN_ES31(TEXT("SF_VULKAN_ES31"));
		static FName NAME_OPENGL_150_ES3_1(TEXT("GLSL_150_ES31"));

		bSupportDX11TextureFormats = true;
		bSupportCompressedVolumeTexture = true;

		for (FName TargetedShaderFormat : TargetedShaderFormats)
		{
			if (TargetedShaderFormat == NAME_PCD3D_SM6 ||
				TargetedShaderFormat == NAME_PCD3D_SM5 ||
				TargetedShaderFormat == NAME_VULKAN_SM5 ||
				TargetedShaderFormat == NAME_PCD3D_ES3_1)
			{
				// DX11 formats okay

				// technically PCD3D_ES3_1 might not support DX11 formats, but in UE5 we require them
				//   PCD3D_ES3_1 is used as a low-spec version of DX11
			}
			else
			{
				// some TargetedShaderFormat doesn't support DX11 formats
				// must turn it off altogether
				bSupportDX11TextureFormats = false;
				break;
			}
		}

		// If we are targeting ES3.1, we also must cook encoded HDR reflection captures
		bRequiresEncodedHDRReflectionCaptures =	TargetedShaderFormats.Contains(NAME_SF_VULKAN_ES31)
												|| TargetedShaderFormats.Contains(NAME_OPENGL_150_ES3_1)
												|| TargetedShaderFormats.Contains(NAME_PCD3D_ES3_1);
	#endif
	}

public:

	//~ Begin ITargetPlatform Interface

	virtual void EnableDeviceCheck(bool OnOff) override {}

	virtual void GetAllDevices( TArray<ITargetDevicePtr>& OutDevices ) const override
	{
		OutDevices.Reset();
		if (LocalDevice.IsValid())
		{
			OutDevices.Add(LocalDevice);
		}
	}

	virtual bool GenerateStreamingInstallManifest(const TMultiMap<FString, int32>& PakchunkMap, const TSet<int32>& PakchunkIndicesInUse) const override
	{
		return true;
	}

	virtual ITargetDevicePtr GetDefaultDevice( ) const override
	{
		if (LocalDevice.IsValid())
		{
			return LocalDevice;
		}

		return nullptr;
	}

	virtual ITargetDevicePtr GetDevice( const FTargetDeviceId& DeviceId )
	{
		if (LocalDevice.IsValid() && (DeviceId == LocalDevice->GetId()))
		{
			return LocalDevice;
		}

		return nullptr;
	}

	virtual bool IsRunningPlatform( ) const override
	{
		// Must be Windows platform as editor for this to be considered a running platform
		return PLATFORM_WINDOWS && !UE_SERVER && !UE_GAME && WITH_EDITOR && TProperties::HasEditorOnlyData();
	}

	virtual void GetShaderCompilerDependencies(TArray<FString>& OutDependencies) const override
	{		
		FTargetPlatformBase::AddDependencySCArrayHelper(OutDependencies, TEXT("Binaries/ThirdParty/Windows/DirectX/x64/d3dcompiler_47.dll"));
		FTargetPlatformBase::AddDependencySCArrayHelper(OutDependencies, TEXT("Binaries/ThirdParty/ShaderConductor/Win64/ShaderConductor.dll"));
		FTargetPlatformBase::AddDependencySCArrayHelper(OutDependencies, TEXT("Binaries/ThirdParty/ShaderConductor/Win64/dxcompiler.dll"));
		FTargetPlatformBase::AddDependencySCArrayHelper(OutDependencies, TEXT("Binaries/ThirdParty/ShaderConductor/Win64/dxil.dll"));
	}

	virtual bool SupportsFeature( ETargetPlatformFeatures Feature ) const override
	{
		// we currently do not have a build target for WindowsServer
		if (Feature == ETargetPlatformFeatures::Packaging)
		{
			return (TProperties::HasEditorOnlyData() || !TProperties::IsServerOnly());
		}

		if ( Feature == ETargetPlatformFeatures::ShouldSplitPaksIntoSmallerSizes )
		{
			return TProperties::IsClientOnly();
		}

		if (Feature == ETargetPlatformFeatures::MobileRendering)
		{
			static bool bCachedSupportsMobileRendering = false;
#if WITH_ENGINE
			static bool bHasCachedValue = false;
			if (!bHasCachedValue)
			{
				TArray<FName> TargetedShaderFormats;
				GetAllTargetedShaderFormats(TargetedShaderFormats);

				for (const FName& Format : TargetedShaderFormats)
				{
					if (IsMobilePlatform(ShaderFormatToLegacyShaderPlatform(Format)))
					{
						bCachedSupportsMobileRendering = true;
						break;
					}
				}
				bHasCachedValue = true;
			}
#endif

			return bCachedSupportsMobileRendering;
		}

		return TSuper::SupportsFeature(Feature);
	}

	virtual void GetBuildProjectSettingKeys(FString& OutSection, TArray<FString>& InBoolKeys, TArray<FString>& InIntKeys, TArray<FString>& InStringKeys) const override
	{
		OutSection = TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings");
		InStringKeys.Add(TEXT("MinimumOSVersion"));
	}

	virtual void GetAllPossibleShaderFormats( TArray<FName>& OutFormats ) const override
	{
		// no shaders needed for dedicated server target
		if (!TProperties::IsServerOnly())
		{
			static FName NAME_PCD3D_SM6(TEXT("PCD3D_SM6"));
			static FName NAME_PCD3D_SM5(TEXT("PCD3D_SM5"));
			static FName NAME_VULKAN_ES31(TEXT("SF_VULKAN_ES31"));
			static FName NAME_OPENGL_150_ES3_1(TEXT("GLSL_150_ES31"));
			static FName NAME_VULKAN_SM5(TEXT("SF_VULKAN_SM5"));
			static FName NAME_PCD3D_ES3_1(TEXT("PCD3D_ES31"));

			OutFormats.AddUnique(NAME_PCD3D_SM5);
			OutFormats.AddUnique(NAME_PCD3D_SM6);
			OutFormats.AddUnique(NAME_VULKAN_ES31);
			OutFormats.AddUnique(NAME_OPENGL_150_ES3_1);
			OutFormats.AddUnique(NAME_VULKAN_SM5);
			OutFormats.AddUnique(NAME_PCD3D_ES3_1);
		}
	}

	virtual void GetAllTargetedShaderFormats( TArray<FName>& OutFormats ) const override 
	{
		// Get the Target RHIs for this platform, we do not always want all those that are supported. (reload in case user changed in the editor)
		TArray<FString>TargetedShaderFormats;
		GConfig->GetArray(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("TargetedRHIs"), TargetedShaderFormats, GEngineIni);

		// Gather the list of Target RHIs and filter out any that may be invalid.
		TArray<FName> PossibleShaderFormats;
		GetAllPossibleShaderFormats(PossibleShaderFormats);

		for (int32 ShaderFormatIdx = TargetedShaderFormats.Num() - 1; ShaderFormatIdx >= 0; ShaderFormatIdx--)
		{
			FString ShaderFormat = TargetedShaderFormats[ShaderFormatIdx];
			if (PossibleShaderFormats.Contains(FName(*ShaderFormat)) == false)
			{
				TargetedShaderFormats.RemoveAt(ShaderFormatIdx);
			}
		}

		for(const FString& ShaderFormat : TargetedShaderFormats)
		{
			OutFormats.AddUnique(FName(*ShaderFormat));
		}
	}

#if WITH_ENGINE
	virtual void GetReflectionCaptureFormats(TArray<FName>& OutFormats) const override
	{
		if (bRequiresEncodedHDRReflectionCaptures)
		{
			OutFormats.Add(FName(TEXT("EncodedHDR")));
		}

		OutFormats.Add(FName(TEXT("FullHDR")));
	}

	virtual void GetShaderFormatModuleHints(TArray<FName>& OutModuleNames) const override
	{
		OutModuleNames.Add(TEXT("ShaderFormatD3D"));
		OutModuleNames.Add(TEXT("ShaderFormatOpenGL"));
		OutModuleNames.Add(TEXT("VulkanShaderFormat"));
	}

	virtual const class FStaticMeshLODSettings& GetStaticMeshLODSettings( ) const override
	{
		return StaticMeshLODSettings;
	}

	virtual void GetTextureFormats( const UTexture* InTexture, TArray< TArray<FName> >& OutFormats) const override
	{
		if (!TProperties::IsServerOnly())
		{
			GetDefaultTextureFormatNamePerLayer(OutFormats.AddDefaulted_GetRef(), this, InTexture, bSupportDX11TextureFormats, bSupportCompressedVolumeTexture);
		}
	}

	virtual void GetAllTextureFormats(TArray<FName>& OutFormats) const override
	{
		if (!TProperties::IsServerOnly())
		{
			GetAllDefaultTextureFormats(this, OutFormats, bSupportDX11TextureFormats);
		}
	}

	virtual const UTextureLODSettings& GetTextureLODSettings() const override
	{
		return *TextureLODSettings;
	}

	virtual void RegisterTextureLODSettings(const UTextureLODSettings* InTextureLODSettings) override
	{
		TextureLODSettings = InTextureLODSettings;
	}

	virtual FName GetWaveFormat(const class USoundWave* Wave) const override
	{
		FName FormatName = Audio::ToName(Wave->GetSoundAssetCompressionType());

		if (FormatName == Audio::NAME_PLATFORM_SPECIFIC)
		{
#if !USE_VORBIS_FOR_STREAMING
			if (Wave->IsStreaming())
			{
				return Audio::NAME_OPUS;
			}
#endif

			return Audio::NAME_OGG;
		}
		else
		{
			return FormatName;
		}
	}

	virtual void GetAllWaveFormats(TArray<FName>& OutFormats) const override
	{
		OutFormats.Add(Audio::NAME_BINKA);
		OutFormats.Add(Audio::NAME_ADPCM);
		OutFormats.Add(Audio::NAME_PCM);
		OutFormats.Add(Audio::NAME_OGG);
		OutFormats.Add(Audio::NAME_OPUS);
	}

	virtual void GetWaveFormatModuleHints(TArray<FName>& OutModuleNames) const override
	{
		OutModuleNames.Add(TEXT("AudioFormatOPUS"));
		OutModuleNames.Add(TEXT("AudioFormatOGG"));
		OutModuleNames.Add(TEXT("AudioFormatADPCM"));
	}

#endif //WITH_ENGINE

	virtual bool SupportsVariants() const override
	{
		return true;
	}

	virtual float GetVariantPriority() const override
	{
		return TProperties::GetVariantPriority();
	}

	virtual bool UsesDistanceFields() const override
	{
		bool bEnableDistanceFields = false;
		GConfig->GetBool(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("bEnableDistanceFields"), bEnableDistanceFields, GEngineIni);

		return bEnableDistanceFields && TSuper::UsesDistanceFields();
	}

	virtual bool UsesRayTracing() const override
	{
		bool bEnableRayTracing = false;
		GConfig->GetBool(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("bEnableRayTracing"), bEnableRayTracing, GEngineIni);

		return bEnableRayTracing && TSuper::UsesRayTracing();
	}

	//~ End ITargetPlatform Interface

private:

	// Holds the local device.
	ITargetDevicePtr LocalDevice;

#if WITH_ENGINE
	// Holds the texture LOD settings.
	const UTextureLODSettings* TextureLODSettings;

	// Holds static mesh LOD settings.
	FStaticMeshLODSettings StaticMeshLODSettings;

	// True if the project supports non-DX11 texture formats.
	bool bSupportDX11TextureFormats;

	// True if the project requires encoded HDR reflection captures
	bool bRequiresEncodedHDRReflectionCaptures;

	// True if the project supports only compressed volume texture formats.
	bool bSupportCompressedVolumeTexture;
#endif // WITH_ENGINE

};

#undef LOCTEXT_NAMESPACE
