// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraSystemToolkitModeBase.h"

#include "AdvancedPreviewSceneModule.h"
#include "NiagaraEditorModule.h"
#include "NiagaraEditorSettings.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraSystemToolkit.h"
#include "NiagaraScriptSource.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/NiagaraScriptViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/NiagaraParameterDefinitionsPanelViewModel.h"
#include "Widgets/SNiagaraSystemViewport.h"
#include "Widgets/SNiagaraSystemScript.h"
#include "Widgets/SNiagaraParameterPanel.h"
#include "Widgets/SNiagaraParameterMapView.h"
#include "Widgets/SNiagaraParameterDefinitionsPanel.h"
#include "Widgets/SNiagaraScriptGraph.h"
#include "Widgets/SNiagaraSelectedObjectsDetails.h"
#include "Widgets/SNiagaraSpreadsheetView.h"
#include "Widgets/SNiagaraGeneratedCodeView.h"

#define LOCTEXT_NAMESPACE "NiagaraSystemToolkitModeBase"

const FName FNiagaraSystemToolkitModeBase::ViewportTabID(TEXT("NiagaraSystemEditor_Viewport"));
const FName FNiagaraSystemToolkitModeBase::CurveEditorTabID(TEXT("NiagaraSystemEditor_CurveEditor"));
const FName FNiagaraSystemToolkitModeBase::SequencerTabID(TEXT("NiagaraSystemEditor_Sequencer"));
const FName FNiagaraSystemToolkitModeBase::SystemScriptTabID(TEXT("NiagaraSystemEditor_SystemScript"));
const FName FNiagaraSystemToolkitModeBase::SystemDetailsTabID(TEXT("NiagaraSystemEditor_SystemDetails"));
const FName FNiagaraSystemToolkitModeBase::SystemParametersTabID(TEXT("NiagaraSystemEditor_SystemParameters"));
const FName FNiagaraSystemToolkitModeBase::SystemParametersTabID2(TEXT("NiagaraSystemEditor_SystemParameters2"));
const FName FNiagaraSystemToolkitModeBase::SystemParameterDefinitionsTabID(TEXT("NiagaraSystemEditor_SystemParameterDefinitions"));
const FName FNiagaraSystemToolkitModeBase::SelectedEmitterStackTabID(TEXT("NiagaraSystemEditor_SelectedEmitterStack"));
const FName FNiagaraSystemToolkitModeBase::SelectedEmitterGraphTabID(TEXT("NiagaraSystemEditor_SelectedEmitterGraph"));
const FName FNiagaraSystemToolkitModeBase::DebugSpreadsheetTabID(TEXT("NiagaraSystemEditor_DebugAttributeSpreadsheet"));
const FName FNiagaraSystemToolkitModeBase::PreviewSettingsTabId(TEXT("NiagaraSystemEditor_PreviewSettings"));
const FName FNiagaraSystemToolkitModeBase::GeneratedCodeTabID(TEXT("NiagaraSystemEditor_GeneratedCode"));
const FName FNiagaraSystemToolkitModeBase::MessageLogTabID(TEXT("NiagaraSystemEditor_MessageLog"));
const FName FNiagaraSystemToolkitModeBase::SystemOverviewTabID(TEXT("NiagaraSystemEditor_SystemOverview"));
const FName FNiagaraSystemToolkitModeBase::ScratchPadTabID(TEXT("NiagaraSystemEditor_ScratchPad"));
const FName FNiagaraSystemToolkitModeBase::ScriptStatsTabID(TEXT("NiagaraSystemEditor_ScriptStats"));
const FName FNiagaraSystemToolkitModeBase::BakerTabID(TEXT("NiagaraSystemEditor_Baker"));

void FNiagaraSystemToolkitModeBase::RegisterTabFactories(TSharedPtr<FTabManager> InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_NiagaraSystemEditor", "Niagara System"));

	SystemToolkit.Pin()->RegisterToolbarTab(InTabManager.ToSharedRef());
	
	InTabManager->RegisterTabSpawner(ViewportTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkitModeBase::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("Preview", "Preview"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "Tab.Viewport"));

	InTabManager->RegisterTabSpawner(CurveEditorTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkitModeBase::SpawnTab_CurveEd))
		.SetDisplayName(LOCTEXT("Curves", "Curves"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "Tab.Curves"));

	InTabManager->RegisterTabSpawner(SequencerTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkitModeBase::SpawnTab_Sequencer))
		.SetDisplayName(LOCTEXT("Timeline", "Timeline"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "Tab.Timeline"));

	InTabManager->RegisterTabSpawner(SystemScriptTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkitModeBase::SpawnTab_SystemScript))
		.SetDisplayName(LOCTEXT("SystemScript", "System Script"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetAutoGenerateMenuEntry(GbShowNiagaraDeveloperWindows != 0);

	InTabManager->RegisterTabSpawner(SystemParametersTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkitModeBase::SpawnTab_SystemParameters))
		.SetDisplayName(LOCTEXT("SystemParameters", "Parameters"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "Tab.Parameters"));

	InTabManager->RegisterTabSpawner(SystemParametersTabID2, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkitModeBase::SpawnTab_SystemParameters2))
		.SetDisplayName(LOCTEXT("SystemParameters2", "Legacy Parameters"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "Tab.Parameters"));

//@todo(ng) disable parameter definitions panel pending bug fixes
// 	InTabManager->RegisterTabSpawner(SystemParameterDefinitionsTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_SystemParameterDefinitions)) 
// 		.SetDisplayName(LOCTEXT("SystemParameterDefinitions", "Parameter Definitions"))
// 		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(SelectedEmitterStackTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkitModeBase::SpawnTab_SelectedEmitterStack))
		.SetDisplayName(LOCTEXT("SelectedEmitterStacks", "Selected Emitters"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "Tab.VisualEffects"));

	InTabManager->RegisterTabSpawner(SelectedEmitterGraphTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkitModeBase::SpawnTab_SelectedEmitterGraph))
		.SetDisplayName(LOCTEXT("SelectedEmitterGraph", "Selected Emitter Graph"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetAutoGenerateMenuEntry(GbShowNiagaraDeveloperWindows != 0);

	InTabManager->RegisterTabSpawner(DebugSpreadsheetTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkitModeBase::SpawnTab_DebugSpreadsheet))
		.SetDisplayName(LOCTEXT("DebugSpreadsheet", "Attribute Spreadsheet"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "Tab.Spreadsheet"));

	InTabManager->RegisterTabSpawner(PreviewSettingsTabId, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkitModeBase::SpawnTab_PreviewSettings))
		.SetDisplayName(LOCTEXT("PreviewSceneSettingsTab", "Preview Scene Settings"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "Tab.Settings"));

	InTabManager->RegisterTabSpawner(GeneratedCodeTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkitModeBase::SpawnTab_GeneratedCode))
		.SetDisplayName(LOCTEXT("GeneratedCode", "Generated Code"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "Tab.GeneratedCode"));

	InTabManager->RegisterTabSpawner(MessageLogTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkitModeBase::SpawnTab_MessageLog))
		.SetDisplayName(LOCTEXT("NiagaraMessageLog", "Niagara Log"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "Tab.Log"));

	InTabManager->RegisterTabSpawner(SystemOverviewTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkitModeBase::SpawnTab_SystemOverview))
		.SetDisplayName(LOCTEXT("SystemOverviewTabName", "System Overview"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "Tab.SystemOverview"));

	InTabManager->RegisterTabSpawner(ScratchPadTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkitModeBase::SpawnTab_ScratchPad))
		.SetDisplayName(LOCTEXT("ScratchPadTabName", "Scratch Pad"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "Tab.ScratchPad"));

	InTabManager->RegisterTabSpawner(ScriptStatsTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkitModeBase::SpawnTab_ScriptStats))
		.SetDisplayName(LOCTEXT("NiagaraScriptsStatsTab", "Script Stats"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "Tab.ScriptStats"));

	if (GetDefault<UNiagaraEditorSettings>()->bEnableBaker)
	{
		InTabManager->RegisterTabSpawner(BakerTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkitModeBase::SpawnTab_Baker))
			.SetDisplayName(LOCTEXT("NiagaraBakerTab", "Baker"))
			.SetGroup(WorkspaceMenuCategory.ToSharedRef());
	}
}

class SNiagaraSelectedEmitterGraph : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraSelectedEmitterGraph)
	{}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, TSharedRef<FNiagaraSystemViewModel> InSystemViewModel)
	{
		SystemViewModel = InSystemViewModel;
		SystemViewModel->GetSelectionViewModel()->OnEmitterHandleIdSelectionChanged().AddSP(this, &SNiagaraSelectedEmitterGraph::SystemSelectionChanged);
		ChildSlot
		[
			SAssignNew(GraphWidgetContainer, SBox)
		];
		UpdateGraphWidget();
	}

	~SNiagaraSelectedEmitterGraph()
	{
		if (SystemViewModel.IsValid() && SystemViewModel->GetSelectionViewModel())
		{
			SystemViewModel->GetSelectionViewModel()->OnEmitterHandleIdSelectionChanged().RemoveAll(this);
		}
	}

private:
	void SystemSelectionChanged()
	{
		UpdateGraphWidget();
	}

	void UpdateGraphWidget()
	{
		TArray<FGuid> SelectedEmitterHandleIds = SystemViewModel->GetSelectionViewModel()->GetSelectedEmitterHandleIds();
		if (SelectedEmitterHandleIds.Num() == 1)
		{
			TSharedPtr<FNiagaraEmitterHandleViewModel> SelectedEmitterHandle = SystemViewModel->GetEmitterHandleViewModelById(SelectedEmitterHandleIds[0]);
			TSharedRef<SWidget> EmitterWidget = 
				SNew(SSplitter)
				+ SSplitter::Slot()
				.Value(.25f)
				[
					SNew(SNiagaraSelectedObjectsDetails, SelectedEmitterHandle->GetEmitterViewModel()->GetSharedScriptViewModel()->GetGraphViewModel()->GetNodeSelection())
				]
				+ SSplitter::Slot()
				.Value(.75f)
				[
					SNew(SNiagaraScriptGraph, SelectedEmitterHandle->GetEmitterViewModel()->GetSharedScriptViewModel()->GetGraphViewModel())
				];

			UNiagaraEmitter* LastMergedEmitter = SelectedEmitterHandle->GetEmitterViewModel()->GetEmitter()->GetParentAtLastMerge();
			if (LastMergedEmitter != nullptr)
			{
				UNiagaraScriptSource* LastMergedScriptSource = CastChecked<UNiagaraScriptSource>(LastMergedEmitter->GraphSource);
				bool bIsForDataProcessingOnly = false;
				TSharedRef<FNiagaraScriptGraphViewModel> LastMergedScriptGraphViewModel = MakeShared<FNiagaraScriptGraphViewModel>(FText(), bIsForDataProcessingOnly);
				LastMergedScriptGraphViewModel->SetScriptSource(LastMergedScriptSource);
				TSharedRef<SWidget> LastMergedEmitterWidget = 
					SNew(SSplitter)
					+ SSplitter::Slot()
					.Value(.25f)
					[
						SNew(SNiagaraSelectedObjectsDetails, LastMergedScriptGraphViewModel->GetNodeSelection())
					]
					+ SSplitter::Slot()
					.Value(.75f)
					[
						SNew(SNiagaraScriptGraph, LastMergedScriptGraphViewModel)
					];

				GraphWidgetContainer->SetContent
				(
					SNew(SSplitter)
					.Orientation(Orient_Vertical)
					+ SSplitter::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Emitter")))
						]
						+ SVerticalBox::Slot()
						[
							EmitterWidget
						]
					]
					+ SSplitter::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Last Merged Emitter")))
						]
						+ SVerticalBox::Slot()
						[
							LastMergedEmitterWidget
						]
					]
				);
			}
			else
			{
				GraphWidgetContainer->SetContent(EmitterWidget);
			}
		}
		else
		{
			GraphWidgetContainer->SetContent(SNullWidget::NullWidget);
		}
	}

private:
	TSharedPtr<FNiagaraSystemViewModel> SystemViewModel;
	TSharedPtr<SBox> GraphWidgetContainer;
};

TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == ViewportTabID);

	TSharedPtr<FNiagaraSystemToolkit> Toolkit = StaticCastSharedPtr<FNiagaraSystemToolkit>(SystemToolkit.Pin());

	if(!Toolkit->Viewport.IsValid())
	{
		Toolkit->Viewport = SNew(SNiagaraSystemViewport)
			.OnThumbnailCaptured(Toolkit.Get(), &FNiagaraSystemToolkit::OnThumbnailCaptured)
			.Sequencer(Toolkit->GetSystemViewModel()->GetSequencer());
	}

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			Toolkit->Viewport.ToSharedRef()
		];

	Toolkit->Viewport->SetPreviewComponent(Toolkit->GetSystemViewModel()->GetPreviewComponent());
	Toolkit->Viewport->OnAddedToTab(SpawnedTab);

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_PreviewSettings(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == PreviewSettingsTabId);

	TSharedRef<SWidget> InWidget = SNullWidget::NullWidget;
	if (SystemToolkit.Pin()->Viewport.IsValid())
	{
		FAdvancedPreviewSceneModule& AdvancedPreviewSceneModule = FModuleManager::LoadModuleChecked<FAdvancedPreviewSceneModule>("AdvancedPreviewScene");
		InWidget = AdvancedPreviewSceneModule.CreateAdvancedPreviewSceneSettingsWidget(SystemToolkit.Pin()->Viewport->GetPreviewScene());
	}

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("PreviewSceneSettingsTab", "Preview Scene Settings"))
		[
			InWidget
		];

	return SpawnedTab;
}


TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_CurveEd(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == CurveEditorTabID);

	TSharedPtr<FNiagaraSystemToolkit> Toolkit = StaticCastSharedPtr<FNiagaraSystemToolkit>(SystemToolkit.Pin());

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			FNiagaraEditorModule::Get().GetWidgetProvider()->CreateCurveOverview(SystemToolkit.Pin()->GetSystemViewModel().ToSharedRef())
		];

	return SpawnedTab;
}


TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_Sequencer(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == SequencerTabID);

	TSharedPtr<FNiagaraSystemToolkit> Toolkit = StaticCastSharedPtr<FNiagaraSystemToolkit>(SystemToolkit.Pin());

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			Toolkit->GetSystemViewModel()->GetSequencer()->GetSequencerWidget()
		];

	return SpawnedTab;
}


TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_SystemScript(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == SystemScriptTabID);

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			SNew(SNiagaraSystemScript, SystemToolkit.Pin()->GetSystemViewModel().ToSharedRef())
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_SystemParameters(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == SystemParametersTabID);


	TArray<TSharedRef<FNiagaraObjectSelection>> ObjectSelections;
	ObjectSelections.Add(SystemToolkit.Pin()->ObjectSelectionForParameterMapView.ToSharedRef());

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			SAssignNew(SystemToolkit.Pin()->ParameterPanel, SNiagaraParameterPanel, SystemToolkit.Pin()->ParameterPanelViewModel, SystemToolkit.Pin()->GetToolkitCommands())
			.ShowParameterSynchronizingWithLibraryIconExternallyReferenced(false)
		];
	SystemToolkit.Pin()->RefreshParameters();

	return SpawnedTab;
}

//@todo(ng) cleanup
TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_SystemParameters2(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == SystemParametersTabID2);

	TArray<TSharedRef<FNiagaraObjectSelection>> ObjectSelections;
	ObjectSelections.Add(SystemToolkit.Pin()->ObjectSelectionForParameterMapView.ToSharedRef());

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			SAssignNew(SystemToolkit.Pin()->ParameterMapView, SNiagaraParameterMapView, ObjectSelections, SNiagaraParameterMapView::EToolkitType::SYSTEM, SystemToolkit.Pin()->GetToolkitCommands())
		];
	SystemToolkit.Pin()->RefreshParameters();

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_SystemParameterDefinitions(const FSpawnTabArgs& Args)
{
	checkf(Args.GetTabId().TabType == SystemParameterDefinitionsTabID, TEXT("Wrong tab ID in NiagaraScriptToolkit"));

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			SNew(SNiagaraParameterDefinitionsPanel, SystemToolkit.Pin()->ParameterDefinitionsPanelViewModel, SystemToolkit.Pin()->GetToolkitCommands())
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_SelectedEmitterStack(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == SelectedEmitterStackTabID);

	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("SystemOverviewSelection", "Selection"))
		[
			NiagaraEditorModule.GetWidgetProvider()->CreateStackView(*SystemToolkit.Pin()->GetSystemViewModel()->GetSelectionViewModel()->GetSelectionStackViewModel())
		];

	SDockTab::FOnTabClosedCallback TabClosedCallback = SDockTab::FOnTabClosedCallback::CreateLambda([=](TSharedRef<SDockTab> DockTab)
	{
		SystemToolkit.Pin()->GetSystemViewModel()->GetSelectionViewModel()->GetSelectionStackViewModel()->ResetSearchText();
	});
	
	SpawnedTab->SetOnTabClosed(TabClosedCallback);
	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_SelectedEmitterGraph(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == SelectedEmitterGraphTabID);

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			SNew(SNiagaraSelectedEmitterGraph, SystemToolkit.Pin()->SystemViewModel.ToSharedRef())
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_DebugSpreadsheet(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == DebugSpreadsheetTabID);

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			SNew(SNiagaraSpreadsheetView, SystemToolkit.Pin()->GetSystemViewModel().ToSharedRef())
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_GeneratedCode(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == GeneratedCodeTabID);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab);
	SpawnedTab->SetContent(SNew(SNiagaraGeneratedCodeView, SystemToolkit.Pin()->GetSystemViewModel().ToSharedRef(), SpawnedTab));
	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_MessageLog(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == MessageLogTabID);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("NiagaraMessageLogTitle", "Niagara Log"))
		[
			SNew(SBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("NiagaraLog")))
			[
				SystemToolkit.Pin()->NiagaraMessageLog.ToSharedRef()
			]
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_SystemOverview(const FSpawnTabArgs& Args)
{
	if(!SystemToolkit.Pin()->GetSystemOverview().IsValid())
	{
		SystemToolkit.Pin()->SetSystemOverview(FNiagaraEditorModule::Get().GetWidgetProvider()->CreateSystemOverview(SystemToolkit.Pin()->GetSystemViewModel().ToSharedRef()));
	}
	
	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("SystemOverviewTabLabel", "System Overview"))
		[
			SystemToolkit.Pin()->GetSystemOverview().ToSharedRef()
		];

	SpawnedTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda([this](TSharedRef<SDockTab>)
	{
		SystemToolkit.Pin()->SetSystemOverview(nullptr);
	}));
	
	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_ScratchPad(const FSpawnTabArgs& Args)
{
	if(!SystemToolkit.Pin()->GetScriptScratchpad().IsValid())
	{
		SystemToolkit.Pin()->SetScriptScratchpad(FNiagaraEditorModule::Get().GetWidgetProvider()->CreateScriptScratchPad(*SystemToolkit.Pin()->GetSystemViewModel()->GetScriptScratchPadViewModel()));
	}
	
	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("ScratchPadTabLabel", "Scratch Pad"))
		[
			SystemToolkit.Pin()->GetScriptScratchpad().ToSharedRef()
		];

	SpawnedTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda([this](TSharedRef<SDockTab>)
	{
		SystemToolkit.Pin()->SetScriptScratchpad(nullptr);
	}));

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_ScriptStats(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == ScriptStatsTabID);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("NiagaraScriptStatsTitle", "Script Stats"))
		[
			SNew(SBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ScriptStats")))
			[
				SystemToolkit.Pin()->ScriptStats->GetWidget().ToSharedRef()
			]
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkitModeBase::SpawnTab_Baker(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == BakerTabID);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("NiagaraBakerTitle", "Baker"))
		[
			SNew(SBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Baker")))
			[
				SystemToolkit.Pin()->BakerViewModel->GetWidget().ToSharedRef()
			]
		];

	return SpawnedTab;
}

#undef LOCTEXT_NAMESPACE