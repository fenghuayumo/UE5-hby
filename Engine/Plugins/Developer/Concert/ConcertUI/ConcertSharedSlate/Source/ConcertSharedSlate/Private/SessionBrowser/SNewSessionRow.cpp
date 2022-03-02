﻿// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNewSessionRow.h"

#include "SessionBrowser/ConcertBrowserUtils.h"
#include "SessionBrowser/ConcertSessionItem.h"

#include "EditorFontGlyphs.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SConcertBrowser"

void SNewSessionRow::Construct(const FArguments& InArgs, TSharedPtr<FConcertSessionItem> InItem, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	Item = MoveTemp(InItem);
	GetServersFunc = InArgs._GetServerFunc;
	AcceptFunc = InArgs._OnAcceptFunc;
	DeclineFunc = InArgs._OnDeclineFunc;
	HighlightText = InArgs._HighlightText;
	DefaultServerURL = InArgs._DefaultServerURL;

	GetServersFunc.CheckCallable();
	AcceptFunc.CheckCallable();
	DeclineFunc.CheckCallable();

	// Construct base class
	SMultiColumnTableRow<TSharedPtr<FConcertSessionItem>>::Construct(FSuperRowType::FArguments(), InOwnerTableView);

	// Fill the server combo.
	UpdateServerList();
}

void SNewSessionRow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Performance could be improved by only updating it when the server list has changed but it's no biggy...
	UpdateServerList();

	// Should give the focus to an editable text.
	if (!bInitialFocusTaken)
	{
		bInitialFocusTaken = FSlateApplication::Get().SetKeyboardFocus(EditableSessionName.ToSharedRef());
	}
}

TSharedRef<SWidget> SNewSessionRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	TSharedPtr<FConcertSessionItem> ItemPin = Item.Pin();

	if (ColumnName == ConcertBrowserUtils::IconColName)
	{
		// 'New' icon
		return SNew(SBox)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
					.Font(FAppStyle::Get().GetFontStyle(ConcertBrowserUtils::IconColumnFontName))
					.Text(FEditorFontGlyphs::Plus_Circle)
			];
	}
	else if (ColumnName == ConcertBrowserUtils::SessionColName)
	{
		return SNew(SBox)
			.VAlign(VAlign_Center)
			.Padding(FMargin(0, 0, 2, 0))
			[
				SAssignNew(EditableSessionName, SEditableTextBox)
				.HintText(LOCTEXT("EnterSessionNameHint", "Enter a session name"))
				.OnTextCommitted(this, &SNewSessionRow::OnSessionNameCommitted)
				.OnKeyDownHandler(this, &SNewSessionRow::OnKeyDownHandler)
				.OnTextChanged(this, &SNewSessionRow::OnSessionNameChanged)
			];
	}
	else
	{
		return SNew(SHorizontalBox)

			// 'Server' combo
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 1)
			[
				SAssignNew(ServersComboBox, SComboBox<TSharedPtr<FConcertServerInfo>>)
				.OptionsSource(&Servers)
				.OnGenerateWidget(this, &SNewSessionRow::OnGenerateServersComboOptionWidget)
				.ToolTipText(LOCTEXT("SelectServerTooltip", "Select the server on which the session should be created"))
				[
					MakeSelectedServerWidget()
				]
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f)
			.HAlign(HAlign_Left)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(1.0f, 0.0f))

				// 'Accept' button
				+SUniformGridPanel::Slot(0, 0)
				[
					ConcertBrowserUtils::MakeIconButton(
						TEXT("FlatButton.Success"),
						FEditorFontGlyphs::Check,
						LOCTEXT("CreateCheckIconTooltip", "Create the session"),
						TAttribute<bool>::Create([this]() { return !EditableSessionName->GetText().IsEmpty(); }),
						FOnClicked::CreateRaw(this, &SNewSessionRow::OnAccept),
						FSlateColor(FLinearColor::White))
				]

				// 'Decline' button
				+SUniformGridPanel::Slot(1, 0)
				[
					ConcertBrowserUtils::MakeIconButton(
						TEXT("FlatButton.Danger"),
						FEditorFontGlyphs::Times,
						LOCTEXT("CancelIconTooltip", "Cancel"),
						true, // Always enabled.
						FOnClicked::CreateRaw(this, &SNewSessionRow::OnDecline),
						FSlateColor(FLinearColor::White))
				]
			];
	}
}

TSharedRef<SWidget> SNewSessionRow::OnGenerateServersComboOptionWidget(TSharedPtr<FConcertServerInfo> ServerItem)
{
	bool bIsDefaultServer = ServerItem->ServerName == DefaultServerURL.Get();

	FText Tooltip;
	if (bIsDefaultServer)
	{
		Tooltip = LOCTEXT("DefaultServerTooltip", "Default Configured Server");
	}
	else if (ServerItem->ServerName == FPlatformProcess::ComputerName())
	{
		Tooltip = LOCTEXT("LocalServerTooltip", "Local Server Running on This Computer");
	}
	else
	{
		Tooltip = LOCTEXT("OnlineServerTooltip", "Online Server");
	}

	return SNew(SHorizontalBox)
		.ToolTipText(Tooltip)

		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Font(bIsDefaultServer ? FAppStyle::Get().GetFontStyle("BoldFont") : FAppStyle::Get().GetFontStyle("NormalFont"))
			.Text(GetServerDisplayName(ServerItem->ServerName))
		]
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			ConcertBrowserUtils::MakeServerVersionIgnoredWidget(ServerItem->ServerFlags)
		];
}

void SNewSessionRow::UpdateServerList()
{
	// Remember the currently selected item (if any).
	TSharedPtr<FConcertServerInfo> SelectedItem = ServersComboBox->GetSelectedItem(); // Instance in current list.

	// Clear the current list. The list is rebuilt from scratch.
	Servers.Reset();

	TSharedPtr<FConcertServerInfo> LocalServerInfo;
	TSharedPtr<FConcertServerInfo> DefaultServerInfo;
	TSharedPtr<FConcertServerInfo> SelectedServerInfo; // Instance in the new list.

	// Convert to shared ptr (slate needs that) and find if the latest list contains a default/local server.
	for (const FConcertServerInfo& ServerInfo : GetServersFunc())
	{
		TSharedPtr<FConcertServerInfo> ComboItem = MakeShared<FConcertServerInfo>(ServerInfo);
		
		// Default server is deemed more important than local server to display the icon aside the server.
		if (ComboItem->ServerName == DefaultServerURL.Get()) 
		{
			DefaultServerInfo = ComboItem;
		}
		else if (ComboItem->ServerName == FPlatformProcess::ComputerName())
		{
			LocalServerInfo = ComboItem;
		}

		if (SelectedItem.IsValid() && SelectedItem->ServerName == ComboItem->ServerName)
		{
			SelectedServerInfo = ComboItem; // Preserve user selection using the new instance.
		}

		Servers.Emplace(MoveTemp(ComboItem));
	}

	// Sort the server list alphabetically.
	Servers.Sort([](const TSharedPtr<FConcertServerInfo>& Lhs, const TSharedPtr<FConcertServerInfo>& Rhs) { return Lhs->ServerName < Rhs->ServerName; });

	// If a server is running on this machine, put it first in the list.
	if (LocalServerInfo.IsValid() && Servers[0] != LocalServerInfo)
	{
		Servers.Remove(LocalServerInfo); // Keep sort order.
		Servers.Insert(LocalServerInfo, 0);
	}

	// If a 'default server' is configured and available, put it first in the list. (Possibly overruling the local one)
	if (DefaultServerInfo.IsValid() && Servers[0] != DefaultServerInfo)
	{
		Servers.Remove(DefaultServerInfo); // Keep sort order.
		Servers.Insert(DefaultServerInfo, 0);
	}

	// If a server was selected and is still in the updated list.
	if (SelectedServerInfo.IsValid())
	{
		// Preserve user selection.
		ServersComboBox->SetSelectedItem(SelectedServerInfo);
	}
	else if (Servers.Num())
	{
		// Select the very first item in the list which is most likely be the default or the local server as they were put first above.
		ServersComboBox->SetSelectedItem(Servers[0]);
	}
	else // Server list is empty.
	{
		ServersComboBox->ClearSelection();
		Servers.Reset();
	}

	ServersComboBox->RefreshOptions();
}

TSharedRef<SWidget> SNewSessionRow::MakeSelectedServerWidget()
{
	return SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(this, &SNewSessionRow::GetSelectedServerText)
			.HighlightText(HighlightText)
		]
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0, 0, 0)
		[
			SNew(STextBlock)
			.Font(FAppStyle::Get().GetFontStyle("FontAwesome.9"))
			.Text(this, &SNewSessionRow::GetSelectedServerIgnoreVersionText)
			.ToolTipText(this, &SNewSessionRow::GetSelectedServerIgnoreVersionTooltip)
		];
}

FText SNewSessionRow::GetSelectedServerText() const
{
	TSharedPtr<FConcertServerInfo> SelectedServer = ServersComboBox->GetSelectedItem();
	if (SelectedServer.IsValid())
	{
		return GetServerDisplayName(SelectedServer->ServerName);
	}
	return LOCTEXT("SelectAServer", "Select a Server");
}

FText SNewSessionRow::GetServerDisplayName(const FString& ServerName) const
{
	const bool bIsDefaultServer = ServerName == DefaultServerURL.Get();
	if (bIsDefaultServer)
	{
		return FText::Format(LOCTEXT("DefaultServer", "{0} (Default)"), FText::FromString(FPlatformProcess::ComputerName()));
	}
	if (ServerName == FPlatformProcess::ComputerName())
	{
		return FText::Format(LOCTEXT("MyComputer", "{0} (My Computer)"), FText::FromString(FPlatformProcess::ComputerName()));
	}
	return FText::FromString(ServerName);
}

FText SNewSessionRow::GetSelectedServerIgnoreVersionText() const
{
	if (ServersComboBox->GetSelectedItem() && (ServersComboBox->GetSelectedItem()->ServerFlags & EConcertServerFlags::IgnoreSessionRequirement) != EConcertServerFlags::None)
	{
		return FEditorFontGlyphs::Exclamation_Triangle;
	}
	return FText();
}

FText SNewSessionRow::GetSelectedServerIgnoreVersionTooltip() const
{
	if (ServersComboBox->GetSelectedItem() && (ServersComboBox->GetSelectedItem()->ServerFlags & EConcertServerFlags::IgnoreSessionRequirement) != EConcertServerFlags::None)
	{
		return ConcertBrowserUtils::GetServerVersionIgnoredTooltip();
	}
	return FText();
}

FReply SNewSessionRow::OnAccept()
{
	TSharedPtr<FConcertSessionItem> ItemPin = Item.Pin();
	FString NewSessionName = EditableSessionName->GetText().ToString();

	FText InvalidNameErrorMsg = ConcertSettingsUtils::ValidateSessionName(NewSessionName);
	if (InvalidNameErrorMsg.IsEmpty())
	{
		ItemPin->SessionName = EditableSessionName->GetText().ToString();
		ItemPin->ServerName = ServersComboBox->GetSelectedItem()->ServerName;
		ItemPin->ServerAdminEndpointId = ServersComboBox->GetSelectedItem()->AdminEndpointId;
		AcceptFunc(ItemPin); // Delegate to create the session.
	}
	else
	{
		EditableSessionName->SetError(InvalidNameErrorMsg);
		FSlateApplication::Get().SetKeyboardFocus(EditableSessionName);
	}

	return FReply::Handled();
}

FReply SNewSessionRow::OnDecline()
{
	DeclineFunc(Item.Pin()); // Decline the creation and remove the row from the table.
	return FReply::Handled();
}

void SNewSessionRow::OnSessionNameChanged(const FText& NewName)
{
	EditableSessionName->SetError(ConcertSettingsUtils::ValidateSessionName(NewName.ToString()));
}

void SNewSessionRow::OnSessionNameCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		OnAccept(); // Create the session.
	}
}

FReply SNewSessionRow::OnKeyDownHandler(const FGeometry&, const FKeyEvent& KeyEvent)
{
	// NOTE: This is invoked when the editable text field has the focus.
	return KeyEvent.GetKey() == EKeys::Escape ? OnDecline() : FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE