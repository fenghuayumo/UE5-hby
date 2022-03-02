﻿// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSessionRow.h"

#include "SessionBrowser/ConcertBrowserUtils.h"
#include "SessionBrowser/ConcertSessionItem.h"

#include "EditorFontGlyphs.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SConcertBrowser"

void SSessionRow::Construct(const FArguments& InArgs, TSharedPtr<FConcertSessionItem> InItem, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	Item = MoveTemp(InItem);
	DoubleClickFunc = InArgs._OnDoubleClickFunc; // This function should join a session or add a row to restore an archive.
	RenameFunc = InArgs._OnRenameFunc; // Function invoked to send a rename request to the server.
	IsDefaultSession = InArgs._IsDefaultSession;
	HighlightText = InArgs._HighlightText;
	IsSelected = InArgs._IsSelected;

	DoubleClickFunc.CheckCallable();
	RenameFunc.CheckCallable();
	IsDefaultSession.CheckCallable();

	// Construct base class
	SMultiColumnTableRow<TSharedPtr<FConcertSessionItem>>::Construct(FSuperRowType::FArguments(), InOwnerTableView);

	// Listen and handle rename request.
	InItem->OnBeginEditSessionNameRequest.AddSP(this, &SSessionRow::OnBeginEditingSessionName);
}

TSharedRef<SWidget> SSessionRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	TSharedPtr<FConcertSessionItem> ItemPin = Item.Pin();

	if (ColumnName == ConcertBrowserUtils::IconColName)
	{
		return SNew(SBox)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.Padding(2)
		.ToolTipText(ItemPin->Type == FConcertSessionItem::EType::ActiveSession ? LOCTEXT("ActiveIconTooltip", "Active session") : LOCTEXT("ArchivedIconTooltip", "Archived Session"))
		[
			SNew(STextBlock)
			.Font(FAppStyle::Get().GetFontStyle(ConcertBrowserUtils::IconColumnFontName))
			.Text(ItemPin->Type == FConcertSessionItem::EType::ActiveSession ? FEditorFontGlyphs::Circle : FEditorFontGlyphs::Archive)
			.ColorAndOpacity(ItemPin->Type == FConcertSessionItem::EType::ActiveSession ? FAppStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton.Success").Normal.TintColor : FSlateColor::UseSubduedForeground())
		];
	}
	
	const bool bIsDefaultConfig = IsDefaultSession(ItemPin);
	FSlateFontInfo FontInfo;
	FSlateColor FontColor;
	if (ItemPin->Type == FConcertSessionItem::EType::ActiveSession)
	{
		FontColor = bIsDefaultConfig ? FSlateColor(FLinearColor::White) : FSlateColor(FLinearColor::White * 0.8f);
		FontInfo = FAppStyle::Get().GetFontStyle("NormalFont");
	}
	else
	{
		FontColor = FSlateColor::UseSubduedForeground();
		FontInfo = FCoreStyle::GetDefaultFontStyle("Italic", 9);
	}

	if (ColumnName == ConcertBrowserUtils::SessionColName)
	{
		return SNew(SBox)
			.VAlign(VAlign_Center)
			[
				SAssignNew(SessionNameText, SInlineEditableTextBlock)
				.Text_Lambda([this]() { return FText::AsCultureInvariant(Item.Pin()->SessionName); })
				.HighlightText(HighlightText)
				.OnTextCommitted(this, &SSessionRow::OnSessionNameCommitted)
				.IsReadOnly(false)
				.IsSelected(FIsSelected::CreateLambda([this]() { return IsSelected.Get(); }))
				.OnVerifyTextChanged(this, &SSessionRow::OnValidatingSessionName)
				.Font(FontInfo)
				.ColorAndOpacity(FontColor)
			];
	}

	check(ColumnName == ConcertBrowserUtils::ServerColName);

	if (bIsDefaultConfig)
	{
		return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::Format(INVTEXT("{0} * "), FText::AsCultureInvariant(ItemPin->ServerName)))
				.HighlightText(HighlightText)
				.Font(FontInfo)
				.ColorAndOpacity(FontColor)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DefaultServerSession", "(Default Session/Server)"))
				.HighlightText(HighlightText)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FontColor)
			]
			+SHorizontalBox::Slot()
			[
				SNew(SSpacer)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				ConcertBrowserUtils::MakeServerVersionIgnoredWidget(ItemPin->ServerFlags)
			];
	}
	else
	{
		return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::AsCultureInvariant(ItemPin->ServerName))
				.HighlightText(HighlightText)
				.Font(FontInfo)
				.ColorAndOpacity(FontColor)
			]
			+SHorizontalBox::Slot()
			[
				SNew(SSpacer)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				ConcertBrowserUtils::MakeServerVersionIgnoredWidget(ItemPin->ServerFlags)
			];
	}
}

FReply SSessionRow::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (TSharedPtr<FConcertSessionItem> ItemPin = Item.Pin())
	{
		DoubleClickFunc(ItemPin);
	}
	return FReply::Handled();
}

bool SSessionRow::OnValidatingSessionName(const FText& NewSessionName, FText& OutError)
{
	OutError = ConcertSettingsUtils::ValidateSessionName(NewSessionName.ToString());
	return OutError.IsEmpty();
}

void SSessionRow::OnSessionNameCommitted(const FText& NewSessionName, ETextCommit::Type CommitType)
{
	if (TSharedPtr<FConcertSessionItem> ItemPin = Item.Pin())
	{
		FString NewName = NewSessionName.ToString();
		if (NewName != ItemPin->SessionName) // Was renamed?
		{
			if (ConcertSettingsUtils::ValidateSessionName(NewName).IsEmpty()) // Name is valid?
			{
				RenameFunc(ItemPin, NewName); // Send the rename request to the server. (Server may still refuse at this point)
			}
			else
			{
				// NOTE: Error are interactively detected and raised by OnValidatingSessionName()
				FSlateApplication::Get().SetKeyboardFocus(SessionNameText);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE