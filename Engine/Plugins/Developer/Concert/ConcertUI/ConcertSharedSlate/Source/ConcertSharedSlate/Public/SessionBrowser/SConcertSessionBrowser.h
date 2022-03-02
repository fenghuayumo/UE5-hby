﻿// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/TextFilter.h"
#include "SlateFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"

class FConcertSessionItem;
class FExtender;
class IConcertSessionBrowserController;
class ITableRow;
class UConcertSessionBrowserSettings;
class SGridPanel;
class STableViewBase;

struct FConcertSessionClientInfo;
struct FConcertSessionInfo;

DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<SWidget>, FExtendSessionTable, TSharedRef<SWidget> /* TableView */);
DECLARE_DELEGATE_OneParam(FExtenderDelegate, FExtender&);
DECLARE_DELEGATE_TwoParams(FExtendSessionContextMenu, TSharedPtr<FConcertSessionItem>, FExtender&)
DECLARE_DELEGATE_OneParam(FSessionDelegate, TSharedPtr<FConcertSessionItem> /*Session*/);

/**
 * Enables the user to browse/search/filter/sort active and archived sessions, create new session,
 * archive active sessions, restore archived sessions, join a session and open the settings dialog.
 */
class CONCERTSHAREDSLATE_API SConcertSessionBrowser : public SCompoundWidget
{
public:
	
	struct CONCERTSHAREDSLATE_API ControlButtonExtensionHooks
	{
		/** Contains: Create Session */
		static const FName BeforeSeparator;
		/** Just separates the two */
		static const FName Separator;
		/** Contains: Restore, Archive, Delete */
		static const FName AfterSeparator;
	};

	struct CONCERTSHAREDSLATE_API SessionContextMenuExtensionHooks
	{
		/** Contains: Archive (ActiveSession), Restore (ArchivedSession), Rename, Delete  */
		static const FName ManageSession;
	};
	
	SLATE_BEGIN_ARGS(SConcertSessionBrowser) { }

	/** Optional name of the default session - relevant for highlighting */
	SLATE_ATTRIBUTE(FString, DefaultSessionName)
	/** Optional url of the default server - relevant for highlighting */
	SLATE_ATTRIBUTE(FString, DefaultServerURL)

	/** Used during construction to override how the session table view is created, e.g. to embed it into an overlay */
	SLATE_EVENT(FExtendSessionTable, ExtendSessionTable)
	/** Extends the buttons to the left of the search bar */
	SLATE_EVENT(FExtenderDelegate, ExtendControllButtons)
	/** Extends the menu when the user right-clicks a session */
	SLATE_EVENT(FExtendSessionContextMenu, ExtendSessionContextMenu)
	/** Custom slot placed to the right of the search bar */
	SLATE_NAMED_SLOT(FArguments, RightOfSearchBar)
	
	/** Called when this session is clicked */
	SLATE_EVENT(FSessionDelegate, OnSessionClicked)
	/** Called when this session is double-clicked */
	SLATE_EVENT(FSessionDelegate, OnSessionDoubleClicked)
	/** Called after a user has requested to delete a session */
	SLATE_EVENT(FSessionDelegate, OnRequestedDeleteSession)
	
	SLATE_END_ARGS();

	/**
	* Constructs the Browser.
	* @param InArgs The Slate argument list.
	* @param InController The controller used to send queries from the UI - represents controller in mode-view-controller pattern.
	* @param[in,out] InSearchText The text to set in the search box and to remember (as output). Cannot be null.
	*/
	void Construct(const FArguments& InArgs, TSharedRef<IConcertSessionBrowserController> InController, TSharedPtr<FText> InSearchText);

	//~ Begin SWidget Interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	//~ End SWidget Interface
	
	void RefreshSessionList();
	TArray<TSharedPtr<FConcertSessionItem>> GetSessions() const { return Sessions; }
	TArray<TSharedPtr<FConcertSessionItem>> GetSelectedItems() const { return SessionsView->GetSelectedItems(); }

	bool IsNewButtonEnabled() const { return IsNewButtonEnabledInternal(); }
	bool IsRestoreButtonEnabled() const { return IsRestoreButtonEnabledInternal(); }
	bool IsArchiveButtonEnabled() const { return IsArchiveButtonEnabledInternal(); }
	bool IsRenameButtonEnabled() const { return IsRenameButtonEnabledInternal(); }
	bool IsDeleteButtonEnabled() const { return IsDeleteButtonEnabledInternal(); }
	
	/** Adds row for creating new session. Exposed for other widgets, e.g. discovery overlay to create a new session. */
	void InsertNewSessionEditableRow() { InsertNewSessionEditableRowInternal(); }
	/** Creates row under the given (archived) session with which session can be restored. */
	void InsertRestoreSessionAsEditableRow(const TSharedPtr<FConcertSessionItem>& ArchivedItem) { InsertRestoreSessionAsEditableRowInternal(ArchivedItem); };
	
private:
	
	// Layout the 'session|details' split view.
	TSharedRef<SWidget> MakeBrowserContent(const FArguments& InArgs);

	// Layout the sessions view and controls.
	TSharedRef<SWidget> MakeControlBar(const FArguments& InArgs);
	TSharedRef<SWidget> MakeButtonBar(const FArguments& InArgs);
	TSharedRef<SWidget> MakeSessionTableView();
	TSharedRef<SWidget> MakeSessionViewOptionsBar();
	TSharedRef<ITableRow> OnGenerateSessionRowWidget(TSharedPtr<FConcertSessionItem> Item, const TSharedRef<STableViewBase>& OwnerTable);

	// Creates row widgets for session list view, validates user inputs and forward user requests for processing to a delegate function implemented by this class.
	TSharedRef<ITableRow> MakeActiveSessionRowWidget(const TSharedPtr<FConcertSessionItem>& ActiveItem, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> MakeArchivedSessionRowWidget(const TSharedPtr<FConcertSessionItem>& ArchivedItem, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> MakeNewSessionRowWidget(const TSharedPtr<FConcertSessionItem>& NewItem, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> MakeRestoreSessionRowWidget(const TSharedPtr<FConcertSessionItem>& RestoreItem, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> MakeSaveSessionRowWidget(const TSharedPtr<FConcertSessionItem>& ArchivedItem, const TSharedRef<STableViewBase>& OwnerTable);

	// Creates the contextual menu when right clicking a session list view row.
	TSharedPtr<SWidget> MakeContextualMenu();

	// The buttons above the session view.
	bool IsNewButtonEnabledInternal() const;
	bool IsRestoreButtonEnabledInternal() const;
	bool IsArchiveButtonEnabledInternal() const;
	bool IsRenameButtonEnabledInternal() const;
	bool IsDeleteButtonEnabledInternal() const;
	FReply OnNewButtonClicked();
	FReply OnRestoreButtonClicked();
	FReply OnArchiveButtonClicked();
	FReply OnDeleteButtonClicked();
	void OnBeginEditingSessionName(TSharedPtr<FConcertSessionItem> Item);

	// Manipulates the sessions view (the array and the UI).
	void OnSessionSelectionChanged(TSharedPtr<FConcertSessionItem> SelectedSession, ESelectInfo::Type SelectInfo);
	void InsertNewSessionEditableRowInternal();
	void InsertRestoreSessionAsEditableRowInternal(const TSharedPtr<FConcertSessionItem>& ArchivedItem);
	void InsertArchiveSessionAsEditableRow(const TSharedPtr<FConcertSessionItem>& ActiveItem);
	void InsertEditableSessionRow(TSharedPtr<FConcertSessionItem> EditableItem, TSharedPtr<FConcertSessionItem> ParentItem);
	void RemoveSessionRow(const TSharedPtr<FConcertSessionItem>& Item);

	// Sessions sorting. (Sorts the session view)
	EColumnSortMode::Type GetColumnSortMode(const FName ColumnId) const;
	EColumnSortPriority::Type GetColumnSortPriority(const FName ColumnId) const;
	void OnColumnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type InSortMode);
	void SortSessionList();
	void EnsureEditableParentChildOrder();

	// Sessions filtering. (Filters the session view)
	void OnSearchTextChanged(const FText& InFilterText);
	void OnSearchTextCommitted(const FText& InFilterText, ETextCommit::Type CommitType);
	void OnFilterMenuChecked(const FName MenuName);
	void PopulateSearchStrings(const FConcertSessionItem& Item, TArray<FString>& OutSearchStrings) const;
	bool IsFilteredOut(const FConcertSessionItem& Item) const;
	FText HighlightSearchText() const;

	// Passes the user requests to FConcertBrowserController.
	void RequestCreateSession(const TSharedPtr<FConcertSessionItem>& NewItem);
	void RequestArchiveSession(const TSharedPtr<FConcertSessionItem>& ActiveItem, const FString& ArchiveName);
	void RequestRestoreSession(const TSharedPtr<FConcertSessionItem>& RestoreItem, const FString& SessionName);
	void RequestRenameSession(const TSharedPtr<FConcertSessionItem>& RenamedItem, const FString& NewName);
	void RequestDeleteSession(const TSharedPtr<FConcertSessionItem>& DeletedItem);

	TSharedPtr<IConcertSessionBrowserController> GetController() const;

private:
	
	// Gives access to the concert data (servers, sessions, clients, etc).
	TWeakPtr<IConcertSessionBrowserController> Controller;

	// Keeps persistent user preferences, like the filters.
	UConcertSessionBrowserSettings* PersistentSettings = nullptr;

	/** Optional default session name - relevant for highlighting */
	TAttribute<FString> DefaultSessionName;
	/** Optional default server url - relevant for highlighting */
	TAttribute<FString> DefaultServerUrl;
	
	FExtendSessionContextMenu ExtendSessionContextMenu;
	FSessionDelegate OnSessionClicked;
	FSessionDelegate OnSessionDoubleClicked;
	FSessionDelegate OnRequestedDeleteSession;

	// The items displayed in the session list view. It might be filtered and sorted compared to the full list hold by the controller.
	TArray<TSharedPtr<FConcertSessionItem>> Sessions;

	// The session list view.
	TSharedPtr<SListView<TSharedPtr<FConcertSessionItem>>> SessionsView;

	// The item corresponding to a row used to create/archive/restore a session. There is only one at the time
	TSharedPtr<FConcertSessionItem> EditableSessionRow;
	TSharedPtr<FConcertSessionItem> EditableSessionRowParent; // For archive/restore, indicate which element is archived or restored.

	// Sorting.
	EColumnSortMode::Type PrimarySortMode = EColumnSortMode::None;
	EColumnSortMode::Type SecondarySortMode = EColumnSortMode::None;
	FName PrimarySortedColumn;
	FName SecondarySortedColumn;

	// Filtering.
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<TTextFilter<const FConcertSessionItem&>> SearchTextFilter;
	TSharedPtr<FText> SearchedText;
	bool bRefreshSessionFilter = true;
	FString LastDefaultServerUrl;

	// Selected Session Details.
	TSharedPtr<SBorder> SessionDetailsView;
	TSharedPtr<SExpandableArea> DetailsArea;
	TArray<TSharedPtr<FConcertSessionClientInfo>> Clients;
	TSharedPtr<SExpandableArea> ClientsArea;
};
