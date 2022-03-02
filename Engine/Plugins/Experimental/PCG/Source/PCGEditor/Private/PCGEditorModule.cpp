// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorModule.h"

#include "IAssetTools.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Toolkits/IToolkit.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "PCGComponentDetails.h"
#include "PCGGraphDetails.h"
#include "PCGVolumeFactory.h"
#include "AssetTypeActions/PCGGraphAssetTypeActions.h"
#include "AssetTypeActions/PCGSettingsAssetTypeActions.h"

#include "PCGSubsystem.h"

#define LOCTEXT_NAMESPACE "FPCGEditorModule"

EAssetTypeCategories::Type FPCGEditorModule::PCGAssetCategory;

void FPCGEditorModule::StartupModule()
{
	RegisterDetailsCustomizations();
	RegisterAssetTypeActions();
	RegisterMenuExtensions();

	if (GEditor)
	{
		GEditor->ActorFactories.Add(NewObject<UPCGVolumeFactory>());
	}
}

void FPCGEditorModule::ShutdownModule()
{
	UnregisterAssetTypeActions();
	UnregisterDetailsCustomizations();
	UnregisterMenuExtensions();

	if (GEditor)
	{
		GEditor->ActorFactories.RemoveAll([](const UActorFactory* ActorFactory) { return ActorFactory->IsA<UPCGVolumeFactory>(); });
	}
}

bool FPCGEditorModule::SupportsDynamicReloading()
{
	return true;
}

void FPCGEditorModule::RegisterDetailsCustomizations()
{
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditor.RegisterCustomClassLayout("PCGComponent", FOnGetDetailCustomizationInstance::CreateStatic(&FPCGComponentDetails::MakeInstance));
	PropertyEditor.RegisterCustomClassLayout("PCGGraph", FOnGetDetailCustomizationInstance::CreateStatic(&FPCGGraphDetails::MakeInstance));
}

void FPCGEditorModule::UnregisterDetailsCustomizations()
{
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout("PCGComponent");
		PropertyModule.UnregisterCustomClassLayout("PCGGraph");
		PropertyModule.NotifyCustomizationModuleChanged();
	}
}

void FPCGEditorModule::RegisterAssetTypeActions()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	PCGAssetCategory = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("PCG")), LOCTEXT("PCGAssetCategory", "PCG"));

	RegisteredAssetTypeActions.Emplace(MakeShareable(new FPCGGraphAssetTypeActions()));
	RegisteredAssetTypeActions.Emplace(MakeShareable(new FPCGSettingsAssetTypeActions()));

	for (auto Action : RegisteredAssetTypeActions)
	{
		AssetTools.RegisterAssetTypeActions(Action);
	}
}

void FPCGEditorModule::UnregisterAssetTypeActions()
{
	FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");

	if (!AssetToolsModule)
	{
		return;
	}

	IAssetTools& AssetTools = AssetToolsModule->Get();

	for (auto Action : RegisteredAssetTypeActions)
	{
		AssetTools.UnregisterAssetTypeActions(Action);
	}
}

void FPCGEditorModule::RegisterMenuExtensions()
{
	MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
	ToolBarExtensibilityManager = MakeShareable(new FExtensibilityManager);

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	{
		TSharedPtr<FExtender> NewMenuExtender = MakeShareable(new FExtender);
		NewMenuExtender->AddMenuExtension("LevelEditor",
			EExtensionHook::After,
			nullptr,
			FMenuExtensionDelegate::CreateRaw(this, &FPCGEditorModule::AddMenuEntry));

		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(NewMenuExtender);
	}
}

void FPCGEditorModule::UnregisterMenuExtensions()
{
	MenuExtensibilityManager.Reset();
	ToolBarExtensibilityManager.Reset();
}

void FPCGEditorModule::AddMenuEntry(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("PCGMenu", TAttribute<FText>(FText::FromString("PCG Tools")));

	MenuBuilder.AddSubMenu(
		LOCTEXT("PCGSubMenu", "PCG Framework"),
		LOCTEXT("PCGSubMenu_Tooltip", "PCG Framework related functionality"),
		FNewMenuDelegate::CreateRaw(this, &FPCGEditorModule::PopulateMenuActions));
	
	MenuBuilder.EndSection();
}

void FPCGEditorModule::PopulateMenuActions(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("DeletePCGActors", "Delete all PCG partition actors"),
		LOCTEXT("DeletePCGActors_Tooltip", "Deletes all PCG partition actors in the current world"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([]() {
				if (UWorld* World = GEditor->GetEditorWorldContext().World())
				{
					World->GetSubsystem<UPCGSubsystem>()->DeletePartitionActors();
				}
			})),
		NAME_None);
}

IMPLEMENT_MODULE(FPCGEditorModule, PCGEditor);

#undef LOCTEXT_NAMESPACE