﻿// Copyright Epic Games, Inc. All Rights Reserved.

#include "SessionBrowser/SConcertSessionBrowser.h"

#include "ConcertFrontendStyle.h"
#include "ConcertFrontendUtils.h"
#include "SessionBrowser/ConcertBrowserUtils.h"
#include "SessionBrowser/ConcertSessionBrowserSettings.h"
#include "SessionBrowser/ConcertSessionItem.h"
#include "SessionBrowser/IConcertSessionBrowserController.h"
#include "SessionBrowser/SNewSessionRow.h"
#include "SessionBrowser/SSaveRestoreSessionRow.h"
#include "SessionBrowser/SSessionRow.h"

#include "Styling/AppStyle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Internationalization/Regex.h"
#include "Misc/AsyncTaskNotification.h"
#include "Misc/MessageDialog.h"
#include "Misc/TextFilter.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SConcertBrowser"

const FName SConcertSessionBrowser::ControlButtonExtensionHooks::BeforeSeparator("BeforeSeparator");
const FName SConcertSessionBrowser::ControlButtonExtensionHooks::Separator("Separator");
const FName SConcertSessionBrowser::ControlButtonExtensionHooks::AfterSeparator("AfterSeparator");

const FName SConcertSessionBrowser::SessionContextMenuExtensionHooks::ManageSession("ManageSession");

void SConcertSessionBrowser::Construct(const FArguments& InArgs, TSharedRef<IConcertSessionBrowserController> InController, TSharedPtr<FText> InSearchText)
{
	Controller = InController;

	// Reload the persistent settings, such as the filters.
	PersistentSettings = GetMutableDefault<UConcertSessionBrowserSettings>();

	DefaultSessionName = InArgs._DefaultSessionName;
	DefaultServerUrl = InArgs._DefaultServerURL;

	ExtendSessionContextMenu = InArgs._ExtendSessionContextMenu;
	OnSessionClicked = InArgs._OnSessionClicked;
	OnSessionDoubleClicked = InArgs._OnSessionDoubleClicked;
	OnRequestedDeleteSession = InArgs._OnRequestedDeleteSession;

	// Setup search filter.
	SearchedText = InSearchText; // Reload a previous search text (in any). Useful to remember searched text between join/leave sessions, but not persistent if the tab is closed.
	SearchTextFilter = MakeShared<TTextFilter<const FConcertSessionItem&>>(TTextFilter<const FConcertSessionItem&>::FItemToStringArray::CreateSP(this, &SConcertSessionBrowser::PopulateSearchStrings));
	SearchTextFilter->OnChanged().AddSP(this, &SConcertSessionBrowser::RefreshSessionList);

	ChildSlot
	[
		MakeBrowserContent(InArgs)
	];

	if (!SearchedText->IsEmpty())
	{
		SearchBox->SetText(*SearchedText); // This trigger the chain of actions to apply the search filter.
	}
}

TSharedRef<SWidget> SConcertSessionBrowser::MakeBrowserContent(const FArguments& InArgs)
{
	TSharedRef<SWidget> SessionTable = MakeSessionTableView();
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(1.0f, 2.0f))
		[
			SNew(SVerticalBox)

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				MakeControlBar(InArgs)
			]

			// Session list.
			+SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(1.0f, 2.0f)
			[
				InArgs._ExtendSessionTable.IsBound() ? InArgs._ExtendSessionTable.Execute(SessionTable) : SessionTable
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(2.0f, 0.0f))
			[
				SNew(SSeparator)
			]

			// Session Count/View options filter.
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(2.0f, 0.0f))
			[
				MakeSessionViewOptionsBar()
			]
		];
}

void SConcertSessionBrowser::RefreshSessionList()
{
	// Remember the selected instances (if any).
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	TArray<TSharedPtr<FConcertSessionItem>> ReselectedItems;
	TSharedPtr<FConcertSessionItem> NewEditableRowParent;

	// Predicate returning true if the specified item should be re-selected.
	auto IsSelected = [&SelectedItems](const FConcertSessionItem& Item)
	{
		return SelectedItems.ContainsByPredicate([Item](const TSharedPtr<FConcertSessionItem>& Visited) { return *Visited == Item; });
	};

	// Matches the object instances before the update to the new instance after the update.
	auto ReconcileObjectInstances = [&](TSharedPtr<FConcertSessionItem> NewItem)
	{
		if (IsSelected(*NewItem))
		{
			ReselectedItems.Add(NewItem);
		}
		else if (EditableSessionRowParent.IsValid() && !NewEditableRowParent.IsValid() && *EditableSessionRowParent == *NewItem)
		{
			NewEditableRowParent = NewItem;
		}
	};

	// Clear sessions.
	Sessions.Reset();

	// Populate the live sessions.
	for (const TSharedPtr<IConcertSessionBrowserController::FActiveSessionInfo>& ActiveSession : GetController()->GetActiveSessions())
	{
		FConcertSessionItem NewItem(FConcertSessionItem::EType::ActiveSession, ActiveSession->SessionInfo.SessionName, ActiveSession->SessionInfo.SessionId, ActiveSession->ServerInfo.ServerName, ActiveSession->ServerInfo.AdminEndpointId, ActiveSession->ServerInfo.ServerFlags);
		if (!IsFilteredOut(NewItem))
		{
			Sessions.Emplace(MakeShared<FConcertSessionItem>(MoveTemp(NewItem)));
			ReconcileObjectInstances(Sessions.Last());
		}
	}

	// Populate the archived.
	for (const TSharedPtr<IConcertSessionBrowserController::FArchivedSessionInfo>& ArchivedSession : GetController()->GetArchivedSessions())
	{
		FConcertSessionItem NewItem(FConcertSessionItem::EType::ArchivedSession, ArchivedSession->SessionInfo.SessionName, ArchivedSession->SessionInfo.SessionId, ArchivedSession->ServerInfo.ServerName, ArchivedSession->ServerInfo.AdminEndpointId, ArchivedSession->ServerInfo.ServerFlags);
		if (!IsFilteredOut(NewItem))
		{
			Sessions.Emplace(MakeShared<FConcertSessionItem>(MoveTemp(NewItem)));
			ReconcileObjectInstances(Sessions.Last());
		}
	}

	// Restore the editable row state. (SortSessionList() below will ensure the parent/child relationship)
	EditableSessionRowParent = NewEditableRowParent;
	if (EditableSessionRow.IsValid())
	{
		if (EditableSessionRow->Type == FConcertSessionItem::EType::NewSession)
		{
			Sessions.Insert(EditableSessionRow, 0); // Always put 'new session' row at the top.
		}
		else if (EditableSessionRowParent.IsValid())
		{
			Sessions.Add(EditableSessionRow); // SortSessionList() called below will ensure the correct parent/child order.
		}
	}

	// Restore previous selection.
	if (ReselectedItems.Num())
	{
		for (const TSharedPtr<FConcertSessionItem>& Item : ReselectedItems)
		{
			SessionsView->SetItemSelection(Item, true);
		}
	}

	SortSessionList();
	SessionsView->RequestListRefresh();
}

TSharedPtr<IConcertSessionBrowserController> SConcertSessionBrowser::GetController() const
{
	TSharedPtr<IConcertSessionBrowserController> Result = Controller.Pin();
	check(Result);
	return Result;
}

void SConcertSessionBrowser::OnSearchTextChanged(const FText& InFilterText)
{
	SearchTextFilter->SetRawFilterText(InFilterText);
	SearchBox->SetError( SearchTextFilter->GetFilterErrorText() );
	*SearchedText = InFilterText;

	bRefreshSessionFilter = true;
}

void SConcertSessionBrowser::OnSearchTextCommitted(const FText& InFilterText, ETextCommit::Type CommitType)
{
	if (!InFilterText.EqualTo(*SearchedText))
	{
		OnSearchTextChanged(InFilterText);
	}
}

void SConcertSessionBrowser::PopulateSearchStrings(const FConcertSessionItem& Item, TArray<FString>& OutSearchStrings) const
{
	OutSearchStrings.Add(Item.ServerName);
	OutSearchStrings.Add(Item.SessionName);
}

bool SConcertSessionBrowser::IsFilteredOut(const FConcertSessionItem& Item) const
{
	bool bIsDefaultServer = LastDefaultServerUrl.IsEmpty() || Item.ServerName == LastDefaultServerUrl;

	return (!PersistentSettings->bShowActiveSessions && (Item.Type == FConcertSessionItem::EType::ActiveSession || Item.Type == FConcertSessionItem::EType::SaveSession)) ||
	       (!PersistentSettings->bShowArchivedSessions && (Item.Type == FConcertSessionItem::EType::ArchivedSession || Item.Type == FConcertSessionItem::EType::RestoreSession)) ||
	       (PersistentSettings->bShowDefaultServerSessionsOnly && !bIsDefaultServer) ||
	       !SearchTextFilter->PassesFilter(Item);
}

FText SConcertSessionBrowser::HighlightSearchText() const
{
	return *SearchedText;
}

TSharedRef<SWidget> SConcertSessionBrowser::MakeControlBar(const FArguments& InArgs)
{
	return SNew(SHorizontalBox)
		// The New/Join/Restore/Delete/Archive buttons
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeButtonBar(InArgs)
		]

		// The search text.
		+SHorizontalBox::Slot()
		.FillWidth(1.0)
		.Padding(4.0f, 5.0f, 8.0f, 5.0f)
		[
			SAssignNew(SearchBox, SSearchBox)
			.HintText(LOCTEXT("SearchHint", "Search Session"))
			.OnTextChanged(this, &SConcertSessionBrowser::OnSearchTextChanged)
			.OnTextCommitted(this, &SConcertSessionBrowser::OnSearchTextCommitted)
			.DelayChangeNotificationsWhileTyping(true)
		]

		// Optional: everything to the right of the search bar, e.g. user name and settings combo button
		+SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			InArgs._RightOfSearchBar.Widget
		];
}

TSharedRef<SWidget> SConcertSessionBrowser::MakeButtonBar(const FArguments& InArgs)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();
	InArgs._ExtendControllButtons.ExecuteIfBound(Extender.Get());
	FToolBarBuilder RowBuilder(nullptr, FMultiBoxCustomization::None, Extender);
	
	RowBuilder.BeginSection(ControlButtonExtensionHooks::BeforeSeparator);
	// New Session
	RowBuilder.AddWidget(
		ConcertBrowserUtils::MakeIconButton(
			TEXT("SimpleButton"),
			FConcertFrontendStyle::Get()->GetBrush("Concert.NewSession"),
			LOCTEXT("NewButtonTooltip", "Create a new session"),
			TAttribute<bool>(this, &SConcertSessionBrowser::IsNewButtonEnabledInternal),
			FOnClicked::CreateSP(this, &SConcertSessionBrowser::OnNewButtonClicked)
		)
	);
	RowBuilder.EndSection();
	
	RowBuilder.AddSeparator(ControlButtonExtensionHooks::Separator);
	RowBuilder.BeginSection(ControlButtonExtensionHooks::AfterSeparator);
	
	// Restore (Share the same slot as Join)
	RowBuilder.AddWidget(
		ConcertBrowserUtils::MakeIconButton(
			TEXT("SimpleButton"),
			FConcertFrontendStyle::Get()->GetBrush("Concert.RestoreSession"),
			LOCTEXT("RestoreButtonTooltip", "Restore the selected session"),
			TAttribute<bool>(this, &SConcertSessionBrowser::IsRestoreButtonEnabledInternal),
			FOnClicked::CreateSP(this, &SConcertSessionBrowser::OnRestoreButtonClicked),
			TAttribute<EVisibility>::Create([this]() { return IsRestoreButtonEnabledInternal() ? EVisibility::Visible : EVisibility::Collapsed; })
			)
	);
	// Archive.
	RowBuilder.AddWidget(
		ConcertBrowserUtils::MakeIconButton(TEXT("SimpleButton"), FConcertFrontendStyle::Get()->GetBrush("Concert.ArchiveSession"), LOCTEXT("ArchiveButtonTooltip", "Archive the selected session"),
			TAttribute<bool>(this, &SConcertSessionBrowser::IsArchiveButtonEnabledInternal),
			FOnClicked::CreateSP(this, &SConcertSessionBrowser::OnArchiveButtonClicked))
	);
	// Delete.
	RowBuilder.AddWidget(
		ConcertBrowserUtils::MakeIconButton(TEXT("SimpleButton"), FConcertFrontendStyle::Get()->GetBrush("Concert.DeleteSession"), LOCTEXT("DeleteButtonTooltip", "Delete the selected session if permitted"),
			TAttribute<bool>(this, &SConcertSessionBrowser::IsDeleteButtonEnabledInternal),
			FOnClicked::CreateSP(this, &SConcertSessionBrowser::OnDeleteButtonClicked))
	);
	RowBuilder.EndSection();

	return RowBuilder.MakeWidget();
}

TSharedRef<SWidget> SConcertSessionBrowser::MakeSessionTableView()
{
	PrimarySortedColumn = ConcertBrowserUtils::IconColName;
	PrimarySortMode = EColumnSortMode::Ascending;
	SecondarySortedColumn = ConcertBrowserUtils::SessionColName;
	SecondarySortMode = EColumnSortMode::Ascending;

	return SAssignNew(SessionsView, SListView<TSharedPtr<FConcertSessionItem>>)
		.SelectionMode(ESelectionMode::Single)
		.ListItemsSource(&Sessions)
		.OnGenerateRow(this, &SConcertSessionBrowser::OnGenerateSessionRowWidget)
		.SelectionMode(ESelectionMode::Single)
		.OnSelectionChanged(this, &SConcertSessionBrowser::OnSessionSelectionChanged)
		.OnContextMenuOpening(this, &SConcertSessionBrowser::MakeContextualMenu)
		.HeaderRow(
			SNew(SHeaderRow)

			+SHeaderRow::Column(ConcertBrowserUtils::IconColName)
			.DefaultLabel(FText::GetEmpty())
			.SortPriority(this, &SConcertSessionBrowser::GetColumnSortPriority, ConcertBrowserUtils::IconColName)
			.SortMode(this, &SConcertSessionBrowser::GetColumnSortMode, ConcertBrowserUtils::IconColName)
			.OnSort(this, &SConcertSessionBrowser::OnColumnSortModeChanged)
			.FixedWidth(20)

			+SHeaderRow::Column(ConcertBrowserUtils::SessionColName)
			.DefaultLabel(LOCTEXT("SessioName", "Session"))
			.SortPriority(this, &SConcertSessionBrowser::GetColumnSortPriority, ConcertBrowserUtils::SessionColName)
			.SortMode(this, &SConcertSessionBrowser::GetColumnSortMode, ConcertBrowserUtils::SessionColName)
			.OnSort(this, &SConcertSessionBrowser::OnColumnSortModeChanged)

			+SHeaderRow::Column(ConcertBrowserUtils::ServerColName)
			.DefaultLabel(LOCTEXT("Server", "Server"))
			.SortPriority(this, &SConcertSessionBrowser::GetColumnSortPriority, ConcertBrowserUtils::ServerColName)
			.SortMode(this, &SConcertSessionBrowser::GetColumnSortMode, ConcertBrowserUtils::ServerColName)
			.OnSort(this, &SConcertSessionBrowser::OnColumnSortModeChanged));
}

EColumnSortMode::Type SConcertSessionBrowser::GetColumnSortMode(const FName ColumnId) const
{
	if (ColumnId == PrimarySortedColumn)
	{
		return PrimarySortMode;
	}
	else if (ColumnId == SecondarySortedColumn)
	{
		return SecondarySortMode;
	}

	return EColumnSortMode::None;
}

EColumnSortPriority::Type SConcertSessionBrowser::GetColumnSortPriority(const FName ColumnId) const
{
	if (ColumnId == PrimarySortedColumn)
	{
		return EColumnSortPriority::Primary;
	}
	else if (ColumnId == SecondarySortedColumn)
	{
		return EColumnSortPriority::Secondary;
	}

	return EColumnSortPriority::Max; // No specific priority.
}

void SConcertSessionBrowser::OnColumnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type InSortMode)
{
	if (SortPriority == EColumnSortPriority::Primary)
	{
		PrimarySortedColumn = ColumnId;
		PrimarySortMode = InSortMode;

		if (ColumnId == SecondarySortedColumn) // Cannot be primary and secondary at the same time.
		{
			SecondarySortedColumn = FName();
			SecondarySortMode = EColumnSortMode::None;
		}
	}
	else if (SortPriority == EColumnSortPriority::Secondary)
	{
		SecondarySortedColumn = ColumnId;
		SecondarySortMode = InSortMode;
	}

	SortSessionList();
	SessionsView->RequestListRefresh();
}

void SConcertSessionBrowser::SortSessionList()
{
	check(!PrimarySortedColumn.IsNone()); // Should always have a primary column. User cannot clear this one.

	auto Compare = [this](const TSharedPtr<FConcertSessionItem>& Lhs, const TSharedPtr<FConcertSessionItem>& Rhs, const FName& ColName, EColumnSortMode::Type SortMode)
	{
		if (Lhs->Type == FConcertSessionItem::EType::NewSession) // Always keep editable 'new session' row at the top.
		{
			return true;
		}
		else if (Rhs->Type == FConcertSessionItem::EType::NewSession)
		{
			return false;
		}

		if (ColName == ConcertBrowserUtils::IconColName)
		{
			return SortMode == EColumnSortMode::Ascending ? Lhs->Type < Rhs->Type : Lhs->Type > Rhs->Type;
		}
		else if (ColName == ConcertBrowserUtils::SessionColName)
		{
			return SortMode == EColumnSortMode::Ascending ? Lhs->SessionName < Rhs->SessionName : Lhs->SessionName > Rhs->SessionName;
		}
		else
		{
			return SortMode == EColumnSortMode::Ascending ? Lhs->ServerName < Rhs->ServerName : Lhs->ServerName > Rhs->ServerName;
		}
	};

	Sessions.Sort([&](const TSharedPtr<FConcertSessionItem>& Lhs, const TSharedPtr<FConcertSessionItem>& Rhs)
	{
		if (Compare(Lhs, Rhs, PrimarySortedColumn, PrimarySortMode))
		{
			return true; // Lhs must be before Rhs based on the primary sort order.
		}
		else if (Compare(Rhs, Lhs, PrimarySortedColumn, PrimarySortMode)) // Invert operands order (goal is to check if operands are equal or not)
		{
			return false; // Rhs must be before Lhs based on the primary sort.
		}
		else // Lhs == Rhs on the primary column, need to order accoding the secondary column if one is set.
		{
			return SecondarySortedColumn.IsNone() ? false : Compare(Lhs, Rhs, SecondarySortedColumn, SecondarySortMode);
		}
	});

	EnsureEditableParentChildOrder();
}

void SConcertSessionBrowser::EnsureEditableParentChildOrder()
{
	// This is for Archiving or Restoring a session. We keep the editable row below the session to archive or restore and visually link them with small wires in UI.
	if (EditableSessionRowParent.IsValid())
	{
		check(EditableSessionRow.IsValid());
		Sessions.Remove(EditableSessionRow);

		int32 ParentIndex = Sessions.IndexOfByKey(EditableSessionRowParent);
		if (ParentIndex != INDEX_NONE)
		{
			Sessions.Insert(EditableSessionRow, ParentIndex + 1); // Insert the 'child' below its parent.
		}
	}
}

TSharedRef<ITableRow> SConcertSessionBrowser::OnGenerateSessionRowWidget(TSharedPtr<FConcertSessionItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	switch (Item->Type)
	{
		case FConcertSessionItem::EType::ActiveSession:
			return MakeActiveSessionRowWidget(Item, OwnerTable);

		case FConcertSessionItem::EType::ArchivedSession:
			return MakeArchivedSessionRowWidget(Item, OwnerTable);

		case FConcertSessionItem::EType::NewSession:
			return MakeNewSessionRowWidget(Item, OwnerTable);

		case FConcertSessionItem::EType::RestoreSession:
			return MakeRestoreSessionRowWidget(Item, OwnerTable);

		default:
			check(Item->Type == FConcertSessionItem::EType::SaveSession);
			return MakeSaveSessionRowWidget(Item, OwnerTable);
	}
}

TSharedRef<ITableRow> SConcertSessionBrowser::MakeActiveSessionRowWidget(const TSharedPtr<FConcertSessionItem>& ActiveItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	const FConcertSessionInfo* SessionInfo = GetController()->GetActiveSessionInfo(ActiveItem->ServerAdminEndpointId, ActiveItem->SessionId);

	// Add an 'Active Session' row. Clicking the row icon joins the session.
	return SNew(SSessionRow, ActiveItem, OwnerTable)
		.OnDoubleClickFunc([this](const TSharedPtr<FConcertSessionItem>& Item) { OnSessionDoubleClicked.ExecuteIfBound(Item); })
		.OnRenameFunc([this](const TSharedPtr<FConcertSessionItem>& Item, const FString& NewName) { RequestRenameSession(Item, NewName); })
		.IsDefaultSession([this](TSharedPtr<FConcertSessionItem> ItemPin)
		{
			return ItemPin->Type == FConcertSessionItem::EType::ActiveSession
				&& DefaultSessionName.IsSet() && ItemPin->SessionName == DefaultSessionName.Get()
				&& DefaultServerUrl.IsSet() && ItemPin->ServerName == DefaultServerUrl.Get();
		})
		.HighlightText(this, &SConcertSessionBrowser::HighlightSearchText)
		.ToolTipText(SessionInfo ? SessionInfo->ToDisplayString() : FText::GetEmpty())
		.IsSelected_Lambda([this, ActiveItem]() { return SessionsView->GetSelectedItems().Num() == 1 && SessionsView->GetSelectedItems()[0] == ActiveItem; });
}

TSharedRef<ITableRow> SConcertSessionBrowser::MakeArchivedSessionRowWidget(const TSharedPtr<FConcertSessionItem>& ArchivedItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	const FConcertSessionInfo* SessionInfo = GetController()->GetArchivedSessionInfo(ArchivedItem->ServerAdminEndpointId, ArchivedItem->SessionId);

	// Add an 'Archived Session' row. Clicking the row icon adds a 'Restore as' row to the table.
	return SNew(SSessionRow, ArchivedItem, OwnerTable)
		.OnDoubleClickFunc([this](const TSharedPtr<FConcertSessionItem>& Item) { InsertRestoreSessionAsEditableRowInternal(Item); })
		.OnRenameFunc([this](const TSharedPtr<FConcertSessionItem>& Item, const FString& NewName) { RequestRenameSession(Item, NewName); })
		.IsDefaultSession([this](TSharedPtr<FConcertSessionItem> ItemPin)
		{
			return ItemPin->Type == FConcertSessionItem::EType::ActiveSession
				&& DefaultSessionName.IsSet() && ItemPin->SessionName == DefaultSessionName.Get()
				&& DefaultServerUrl.IsSet() && ItemPin->ServerName == DefaultServerUrl.Get();
		})
		.HighlightText(this, &SConcertSessionBrowser::HighlightSearchText)
		.ToolTipText(SessionInfo ? SessionInfo->ToDisplayString() : FText::GetEmpty())
		.IsSelected_Lambda([this, ArchivedItem]() { return SessionsView->GetSelectedItems().Num() == 1 && SessionsView->GetSelectedItems()[0] == ArchivedItem; });
}

TSharedRef<ITableRow> SConcertSessionBrowser::MakeNewSessionRowWidget(const TSharedPtr<FConcertSessionItem>& NewItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Add an editable 'New Session' row in the table to let user pick a name and a server.
	TAttribute<FString> DefaultServerUrlArg = DefaultServerUrl.IsSet()
		? TAttribute<FString>::Create([this](){ return DefaultServerUrl.Get(); }) : TAttribute<FString>();
	return SNew(SNewSessionRow, NewItem, OwnerTable)
		.GetServerFunc([this]() { return GetController()->GetServers(); }) // Let the row pull the servers for the combo box.
		.OnAcceptFunc([this](const TSharedPtr<FConcertSessionItem>& Item) { RequestCreateSession(Item); }) // Accepting creates the session.
		.OnDeclineFunc([this](const TSharedPtr<FConcertSessionItem>& Item) { RemoveSessionRow(Item); })  // Declining removes the editable 'new session' row from the view.
		.HighlightText(this, &SConcertSessionBrowser::HighlightSearchText)
		.DefaultServerURL(DefaultServerUrlArg);
}

TSharedRef<ITableRow> SConcertSessionBrowser::MakeSaveSessionRowWidget(const TSharedPtr<FConcertSessionItem>& SaveItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Add an editable 'Save Session' row in the table to let the user enter an archive name.
	return SNew(SSaveRestoreSessionRow, SaveItem, OwnerTable)
		.OnAcceptFunc([this](const TSharedPtr<FConcertSessionItem>& Item, const FString& ArchiveName) { RequestArchiveSession(Item, ArchiveName); }) // Accepting archive the session.
		.OnDeclineFunc([this](const TSharedPtr<FConcertSessionItem>& Item) { RemoveSessionRow(Item); }) // Declining removes the editable 'save session as' row from the view.
		.HighlightText(this, &SConcertSessionBrowser::HighlightSearchText);
}

TSharedRef<ITableRow> SConcertSessionBrowser::MakeRestoreSessionRowWidget(const TSharedPtr<FConcertSessionItem>& RestoreItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Add an editable 'Restore Session' row in the table to let the user enter a session name.
	return SNew(SSaveRestoreSessionRow, RestoreItem, OwnerTable)
		.OnAcceptFunc([this](const TSharedPtr<FConcertSessionItem>& Item, const FString& SessionName) { RequestRestoreSession(Item, SessionName); }) // Accepting restores the session.
		.OnDeclineFunc([this](const TSharedPtr<FConcertSessionItem>& Item) { RemoveSessionRow(Item); }) // Declining removes the editable 'restore session as' row from the view.
		.HighlightText(this, &SConcertSessionBrowser::HighlightSearchText);
}

void SConcertSessionBrowser::InsertNewSessionEditableRowInternal()
{
	// Insert a 'new session' editable row.
	InsertEditableSessionRow(MakeShared<FConcertSessionItem>(FConcertSessionItem::EType::NewSession, FString(), FGuid(), FString(), FGuid(), EConcertServerFlags::None), nullptr);
}

void SConcertSessionBrowser::InsertRestoreSessionAsEditableRowInternal(const TSharedPtr<FConcertSessionItem>& ArchivedItem)
{
	// Insert the 'restore session as ' editable row just below the 'archived' item to restore.
	InsertEditableSessionRow(MakeShared<FConcertSessionItem>(FConcertSessionItem::EType::RestoreSession, ArchivedItem->SessionName, ArchivedItem->SessionId, ArchivedItem->ServerName, ArchivedItem->ServerAdminEndpointId, ArchivedItem->ServerFlags), ArchivedItem);
}

void SConcertSessionBrowser::InsertArchiveSessionAsEditableRow(const TSharedPtr<FConcertSessionItem>& LiveItem)
{
	// Insert the 'save session as' editable row just below the 'active' item to save.
	InsertEditableSessionRow(MakeShared<FConcertSessionItem>(FConcertSessionItem::EType::SaveSession, LiveItem->SessionName, LiveItem->SessionId, LiveItem->ServerName, LiveItem->ServerAdminEndpointId, LiveItem->ServerFlags), LiveItem);
}

void SConcertSessionBrowser::InsertEditableSessionRow(TSharedPtr<FConcertSessionItem> EditableItem, TSharedPtr<FConcertSessionItem> ParentItem)
{
	// Insert the new row below its parent (if any).
	int32 ParentIndex = Sessions.IndexOfByKey(ParentItem);
	Sessions.Insert(EditableItem, ParentIndex != INDEX_NONE ? ParentIndex + 1 : 0);

	// Ensure there is only one editable row at the time, removing the row being edited (if any).
	Sessions.Remove(EditableSessionRow);
	EditableSessionRow = EditableItem;
	EditableSessionRowParent = ParentItem;

	// Ensure the editable row added is selected and visible.
	SessionsView->SetSelection(EditableItem);
	SessionsView->RequestListRefresh();

	// NOTE: Ideally, I would only use RequestScrollIntoView() to scroll the new item into view, but it did not work. If an item was added into an hidden part,
	//       it was not always scrolled correctly into view. RequestNavigateToItem() worked much better, except when inserting the very first row in the list, in
	//       such case calling the function would give the focus to the list view (showing a white dashed line around it).
	if (ParentIndex == INDEX_NONE)
	{
		SessionsView->ScrollToTop(); // Item is inserted at 0. (New session)
	}
	else
	{
		SessionsView->RequestNavigateToItem(EditableItem);
	}
}

void SConcertSessionBrowser::RemoveSessionRow(const TSharedPtr<FConcertSessionItem>& Item)
{
	Sessions.Remove(Item);

	// Don't keep the editable row if its 'parent' is removed. (if the session to restore or archive gets deleted in the meantime)
	if (Item == EditableSessionRowParent)
	{
		Sessions.Remove(EditableSessionRow);
		EditableSessionRow.Reset();
	}

	// Clear the editable row state if its the one removed.
	if (Item == EditableSessionRow)
	{
		EditableSessionRow.Reset();
		EditableSessionRowParent.Reset();
	}

	SessionsView->RequestListRefresh();
}

TSharedRef<SWidget> SConcertSessionBrowser::MakeSessionViewOptionsBar()
{
	auto AddFilterMenu = [this]()
	{
		FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ActiveSessions_Label", "Active Sessions"),
			LOCTEXT("ActiveSessions_Tooltip", "Displays Active Sessions"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SConcertSessionBrowser::OnFilterMenuChecked, ConcertBrowserUtils::ActiveSessionsCheckBoxMenuName),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FIsActionChecked::CreateLambda([this] { return PersistentSettings->bShowActiveSessions; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ArchivedSessions_Label", "Archived Sessions"),
			LOCTEXT("ArchivedSessions_Tooltip", "Displays Archived Sessions"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SConcertSessionBrowser::OnFilterMenuChecked, ConcertBrowserUtils::ArchivedSessionsCheckBoxMenuName),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FIsActionChecked::CreateLambda([this] { return PersistentSettings->bShowArchivedSessions; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("DefaultServer_Label", "Default Server Sessions"),
			LOCTEXT("DefaultServer_Tooltip", "Displays Sessions Hosted By the Default Server"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SConcertSessionBrowser::OnFilterMenuChecked, ConcertBrowserUtils::DefaultServerCheckBoxMenuName),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FIsActionChecked::CreateLambda([this] { return PersistentSettings->bShowDefaultServerSessionsOnly; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		return MenuBuilder.MakeWidget();
	};

	return SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text_Lambda([this]()
			{
				// Don't count the 'New Session', 'Restore Session' and 'Archive Session' editable row, they are transient rows used for inline input only.
				int32 DisplayedSessionNum = Sessions.Num() - (EditableSessionRow.IsValid() ? 1 : 0);
				int32 AvailableSessionNum = GetController()->GetActiveSessions().Num() + GetController()->GetArchivedSessions().Num();
				int32 ServerNum = GetController()->GetServers().Num();

				// If all discovered session are displayed (none excluded by a filter).
				if (DisplayedSessionNum == AvailableSessionNum)
				{
					if (GetController()->GetServers().Num() == 0)
					{
						return LOCTEXT("NoServerNoFilter", "No servers found");
					}
					else
					{
						return FText::Format(LOCTEXT("NSessionNServerNoFilter", "{0} {0}|plural(one=session,other=sessions) on {1} {1}|plural(one=server,other=servers)"),
							DisplayedSessionNum, ServerNum);
					}
				}
				else // A filter is excluding at least one session.
				{
					if (DisplayedSessionNum == 0)
					{
						return FText::Format(LOCTEXT("NoSessionMatchNServer", "No matching sessions ({0} total on {1} {1}|plural(one=server,other=servers))"),
							AvailableSessionNum, ServerNum);
					}
					else
					{
						return FText::Format(LOCTEXT("NSessionNServer", "Showing {0} of {1} {1}|plural(one=session,other=sessions) on {2} {2}|plural(one=server,other=servers)"),
							DisplayedSessionNum, AvailableSessionNum, ServerNum);
					}
				}
			})
		]

		+SHorizontalBox::Slot()
		.FillWidth(1.0)
		[
			SNew(SSpacer)
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SComboButton)
			.ComboButtonStyle(FAppStyle::Get(), "GenericFilters.ComboButtonStyle")
			.ForegroundColor(FLinearColor::White)
			.ContentPadding(0)
			.OnGetMenuContent_Lambda(AddFilterMenu)
			.HasDownArrow(true)
			.ContentPadding(FMargin(1, 0))
			.ButtonContent()
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage).Image(FAppStyle::Get().GetBrush("GenericViewButton")) // The eye ball image.
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(LOCTEXT("ViewOptions", "View Options"))
				]
			]
		];
}

void SConcertSessionBrowser::OnFilterMenuChecked(const FName MenuName)
{
	if (MenuName == ConcertBrowserUtils::ActiveSessionsCheckBoxMenuName)
	{
		PersistentSettings->bShowActiveSessions = !PersistentSettings->bShowActiveSessions;
	}
	else if (MenuName == ConcertBrowserUtils::ArchivedSessionsCheckBoxMenuName)
	{
		PersistentSettings->bShowArchivedSessions = !PersistentSettings->bShowArchivedSessions;
	}
	else if (MenuName == ConcertBrowserUtils::DefaultServerCheckBoxMenuName)
	{
		PersistentSettings->bShowDefaultServerSessionsOnly = !PersistentSettings->bShowDefaultServerSessionsOnly;
	}
	bRefreshSessionFilter = true;

	PersistentSettings->SaveConfig();
}

TSharedPtr<SWidget> SConcertSessionBrowser::MakeContextualMenu()
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	if (SelectedItems.Num() == 0 || (SelectedItems[0]->Type != FConcertSessionItem::EType::ActiveSession && SelectedItems[0]->Type != FConcertSessionItem::EType::ArchivedSession))
	{
		return nullptr; // No menu for editable rows.
	}

	TSharedPtr<FConcertSessionItem> Item = SelectedItems[0];

	TSharedRef<FExtender> Extender = MakeShared<FExtender>();
	ExtendSessionContextMenu.ExecuteIfBound(Item, Extender.Get());
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr, Extender);

	// Section title.
	MenuBuilder.BeginSection(SessionContextMenuExtensionHooks::ManageSession, Item->Type == FConcertSessionItem::EType::ActiveSession ?
		LOCTEXT("ActiveSessionSection", "Active Session"):
		LOCTEXT("ArchivedSessionSection", "Archived Session"));

	if (Item->Type == FConcertSessionItem::EType::ActiveSession)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CtxMenuArchive", "Archive"),
			LOCTEXT("CtxMenuArchive_Tooltip", "Archived the Session"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this](){ OnArchiveButtonClicked(); }),
				FCanExecuteAction::CreateLambda([SelectedCount = SelectedItems.Num()] { return SelectedCount == 1; }),
				FIsActionChecked::CreateLambda([this] { return false; })),
			NAME_None,
			EUserInterfaceActionType::Button);
	}
	else // Archive
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CtxMenuRestore", "Restore"),
			LOCTEXT("CtxMenuRestore_Tooltip", "Restore the Session"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this](){ OnRestoreButtonClicked(); }),
				FCanExecuteAction::CreateLambda([SelectedCount = SelectedItems.Num()] { return SelectedCount == 1; }),
				FIsActionChecked::CreateLambda([this] { return false; })),
			NAME_None,
			EUserInterfaceActionType::Button);
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CtxMenuRename", "Rename"),
		LOCTEXT("CtxMenuRename_Tooltip", "Rename the Session"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, Item](){ OnBeginEditingSessionName(Item); }),
			FCanExecuteAction::CreateLambda([this] { return IsRenameButtonEnabledInternal(); }),
			FIsActionChecked::CreateLambda([this] { return false; })),
		NAME_None,
		EUserInterfaceActionType::Button);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CtxMenuDelete", "Delete"),
		FText::Format(LOCTEXT("CtxMenuDelete_Tooltip", "Delete the {0}|plural(one=Session,other=Sessions)"), SelectedItems.Num()),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this](){ OnDeleteButtonClicked(); }),
			FCanExecuteAction::CreateLambda([this] { return IsDeleteButtonEnabledInternal(); }),
			FIsActionChecked::CreateLambda([this] { return false; })),
		NAME_None,
		EUserInterfaceActionType::Button);

	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SConcertSessionBrowser::OnSessionSelectionChanged(TSharedPtr<FConcertSessionItem> SelectedSession, ESelectInfo::Type SelectInfo)
{
	// Cancel editing the row to create, archive or restore a session (if any), unless the row was selected in code.
	if (EditableSessionRow.IsValid() && SelectInfo != ESelectInfo::Direct)
	{
		check (SelectedSession != EditableSessionRow); // User should not be able to reselect an editable row as we remove it as soon as it is unselected.
		RemoveSessionRow(EditableSessionRow);
		check(!EditableSessionRow.IsValid() && !EditableSessionRowParent.IsValid()); // Expect to be cleared by RemoveSessionRow().
	}

	// Clear the list of clients (if any)
	Clients.Reset();

	OnSessionClicked.ExecuteIfBound(SelectedSession);
}

bool SConcertSessionBrowser::IsNewButtonEnabledInternal() const
{
	return GetController()->GetServers().Num() > 0;
}

bool SConcertSessionBrowser::IsRestoreButtonEnabledInternal() const
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	return SelectedItems.Num() == 1 && SelectedItems[0]->Type == FConcertSessionItem::EType::ArchivedSession;
}

bool SConcertSessionBrowser::IsArchiveButtonEnabledInternal() const
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	return SelectedItems.Num() == 1 && SelectedItems[0]->Type == FConcertSessionItem::EType::ActiveSession;
}

bool SConcertSessionBrowser::IsRenameButtonEnabledInternal() const
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	if (SelectedItems.Num() != 1)
	{
		return false;
	}

	return (SelectedItems[0]->Type == FConcertSessionItem::EType::ActiveSession && GetController()->CanRenameActiveSession(SelectedItems[0]->ServerAdminEndpointId, SelectedItems[0]->SessionId)) ||
	       (SelectedItems[0]->Type == FConcertSessionItem::EType::ArchivedSession && GetController()->CanRenameArchivedSession(SelectedItems[0]->ServerAdminEndpointId, SelectedItems[0]->SessionId));
}

bool SConcertSessionBrowser::IsDeleteButtonEnabledInternal() const
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	if (SelectedItems.Num() == 0)
	{
		return false;
	}

	return (SelectedItems[0]->Type == FConcertSessionItem::EType::ActiveSession && GetController()->CanDeleteActiveSession(SelectedItems[0]->ServerAdminEndpointId, SelectedItems[0]->SessionId)) ||
	       (SelectedItems[0]->Type == FConcertSessionItem::EType::ArchivedSession && GetController()->CanDeleteArchivedSession(SelectedItems[0]->ServerAdminEndpointId, SelectedItems[0]->SessionId));
}

FReply SConcertSessionBrowser::OnNewButtonClicked()
{
	InsertNewSessionEditableRowInternal();
	return FReply::Handled();
}

FReply SConcertSessionBrowser::OnRestoreButtonClicked()
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	if (SelectedItems.Num() == 1)
	{
		InsertRestoreSessionAsEditableRowInternal(SelectedItems[0]);
	}

	return FReply::Handled();
}

FReply SConcertSessionBrowser::OnArchiveButtonClicked()
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	if (SelectedItems.Num() == 1)
	{
		InsertArchiveSessionAsEditableRow(SelectedItems[0]);
	}

	return FReply::Handled();
}

FReply SConcertSessionBrowser::OnDeleteButtonClicked()
{
	TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
	for (const TSharedPtr<FConcertSessionItem>& Item : SelectedItems)
	{
		RequestDeleteSession(Item);
	}

	return FReply::Handled();
}

void SConcertSessionBrowser::OnBeginEditingSessionName(TSharedPtr<FConcertSessionItem> Item)
{
	Item->OnBeginEditSessionNameRequest.Broadcast(); // Signal the row widget to enter in edit mode.
}

void SConcertSessionBrowser::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Ensure the 'default server' filter is updated when the configuration of the default server changes.
	if (DefaultServerUrl.IsSet() && LastDefaultServerUrl != DefaultServerUrl.Get())
	{
		LastDefaultServerUrl = DefaultServerUrl.Get();
		bRefreshSessionFilter = true;
	}

	// Should refresh the session filter?
	if (bRefreshSessionFilter)
	{
		RefreshSessionList();
		bRefreshSessionFilter = false;
	}
}

FReply SConcertSessionBrowser::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// NOTE: When an 'editable row' text box has the focus the keys are grabbed by the text box but
	//       if the editable row is still selected, but the text field doesn't have the focus anymore
	//       the keys will end up here if the browser has the focus.

	if (InKeyEvent.GetKey() == EKeys::Delete && !EditableSessionRow.IsValid()) // Delete selected row(s) unless the selected row is an 'editable' one.
	{
		for (TSharedPtr<FConcertSessionItem>& Item : SessionsView->GetSelectedItems())
		{
			RequestDeleteSession(Item);
		}
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Escape && EditableSessionRow.IsValid()) // Cancel 'new session', 'archive session' or 'restore session' action.
	{
		RemoveSessionRow(EditableSessionRow);
		check(!EditableSessionRow.IsValid() && !EditableSessionRowParent.IsValid()); // Expect to be cleared by RemoveSessionRow().
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::F2 && !EditableSessionRow.IsValid())
	{
		TArray<TSharedPtr<FConcertSessionItem>> SelectedItems = SessionsView->GetSelectedItems();
		if (SelectedItems.Num() == 1)
		{
			SelectedItems[0]->OnBeginEditSessionNameRequest.Broadcast(); // Broadcast the request.
		}
	}

	return FReply::Unhandled();
}

void SConcertSessionBrowser::RequestCreateSession(const TSharedPtr<FConcertSessionItem>& NewItem)
{
	GetController()->CreateSession(NewItem->ServerAdminEndpointId, NewItem->SessionName);
	RemoveSessionRow(NewItem); // The row used to edit the session name and pick the server.
}

void SConcertSessionBrowser::RequestArchiveSession(const TSharedPtr<FConcertSessionItem>& SaveItem, const FString& ArchiveName)
{
	GetController()->ArchiveSession(SaveItem->ServerAdminEndpointId, SaveItem->SessionId, ArchiveName, FConcertSessionFilter());
	RemoveSessionRow(SaveItem); // The row used to edit the archive name.
}

void SConcertSessionBrowser::RequestRestoreSession(const TSharedPtr<FConcertSessionItem>& RestoreItem, const FString& SessionName)
{
	GetController()->RestoreSession(RestoreItem->ServerAdminEndpointId, RestoreItem->SessionId, SessionName, FConcertSessionFilter());
	RemoveSessionRow(RestoreItem); // The row used to edit the restore as name.
}

void SConcertSessionBrowser::RequestRenameSession(const TSharedPtr<FConcertSessionItem>& RenamedItem, const FString& NewName)
{
	if (RenamedItem->Type == FConcertSessionItem::EType::ActiveSession)
	{
		GetController()->RenameActiveSession(RenamedItem->ServerAdminEndpointId, RenamedItem->SessionId, NewName);
	}
	else if (RenamedItem->Type == FConcertSessionItem::EType::ArchivedSession)
	{
		GetController()->RenameArchivedSession(RenamedItem->ServerAdminEndpointId, RenamedItem->SessionId, NewName);
	}

	// Display the new name until the server response is received. If the server refuses the new name, the discovery will reset the
	// name (like if another client renamed it back) and the user will get a toast saying the rename failed.
	RenamedItem->SessionName = NewName;
}

void SConcertSessionBrowser::RequestDeleteSession(const TSharedPtr<FConcertSessionItem>& DeletedItem)
{
	const FText SessionNameInText = FText::FromString(DeletedItem->SessionName);
	const FText SeverNameInText = FText::FromString(DeletedItem->ServerName);
	const FText ConfirmationMessage = FText::Format(LOCTEXT("DeleteSessionConfirmationMessage", "Do you really want to delete the session \"{0}\" from the server \"{1}\"?"), SessionNameInText, SeverNameInText);
	const FText ConfirmationTitle = LOCTEXT("DeleteSessionConfirmationTitle", "Delete Session Confirmation");

	if (FMessageDialog::Open(EAppMsgType::YesNo, ConfirmationMessage, &ConfirmationTitle) == EAppReturnType::Yes) // Confirmed?
	{
		if (DeletedItem->Type == FConcertSessionItem::EType::ActiveSession)
		{
			GetController()->DeleteActiveSession(DeletedItem->ServerAdminEndpointId, DeletedItem->SessionId);
		}
		else if (DeletedItem->Type == FConcertSessionItem::EType::ArchivedSession)
		{
			GetController()->DeleteArchivedSession(DeletedItem->ServerAdminEndpointId, DeletedItem->SessionId);
		}

		OnRequestedDeleteSession.ExecuteIfBound(DeletedItem);
	}
}

#undef LOCTEXT_NAMESPACE