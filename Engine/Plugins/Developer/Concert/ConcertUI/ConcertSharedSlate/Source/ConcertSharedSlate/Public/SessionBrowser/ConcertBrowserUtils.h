﻿// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ConcertMessageData.h"

#include "EditorFontGlyphs.h"
#include "Styling/AppStyle.h"
#include "Internationalization/Regex.h"
#include "Layout/Visibility.h"
#include "Misc/Attribute.h"
#include "Misc/AsyncTaskNotification.h"
#include "Styling/SlateColor.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

namespace ConcertBrowserUtils
{
	// Defines the sessions list view column tag names.
	static const FName IconColName(TEXT("Icon"));
	static const FName SessionColName(TEXT("Session"));
	static const FName ServerColName(TEXT("Server"));

	// Name of the filter box in the View option.
	static const FName ActiveSessionsCheckBoxMenuName(TEXT("ActiveSessions"));
	static const FName ArchivedSessionsCheckBoxMenuName(TEXT("ArchivedSessions"));
	static const FName DefaultServerCheckBoxMenuName(TEXT("DefaultServer"));

	// The awesome font used to pick the icon displayed in the session list view 'Icon' column.
	static const FName IconColumnFontName(TEXT("FontAwesome.9"));

	/** Utility function used to create buttons displaying only an icon (using FontAwesome). */
	inline TSharedRef<SButton> MakeIconButton(const FName& ButtonStyle, const TAttribute<FText>& GlyphIcon, const TAttribute<FText>& Tooltip, const TAttribute<bool>& EnabledAttribute, const FOnClicked& OnClicked,
		const FSlateColor& ForegroundColor, const TAttribute<EVisibility>& Visibility = EVisibility::Visible, const TAttribute<FMargin>& ContentPadding = FMargin(3.0, 2.0), const FName FontStyle = IconColumnFontName)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), ButtonStyle)
			.OnClicked(OnClicked)
			.ToolTipText(Tooltip)
			.ContentPadding(ContentPadding)
			.Visibility(Visibility)
			.IsEnabled(EnabledAttribute)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::Get().GetFontStyle(FontStyle))
				.Text(GlyphIcon)
			];
	}

	/** Utility function used to create buttons displaying only an icon (using a brush) */
	inline TSharedRef<SButton> MakeIconButton(const FName& ButtonStyle, const TAttribute<const FSlateBrush*>& Icon, const TAttribute<FText>& Tooltip, const TAttribute<bool>& EnabledAttribute, const FOnClicked& OnClicked, const TAttribute<EVisibility>& Visibility = EVisibility::Visible)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), ButtonStyle)
			.OnClicked(OnClicked)
			.ToolTipText(Tooltip)
			.ContentPadding(FMargin(0, 0))
			.Visibility(Visibility)
			.IsEnabled(EnabledAttribute)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(SImage)
				.Image(Icon)
				.ColorAndOpacity(FSlateColor::UseForeground())
			];
	}

	/** Returns the tooltip shown when hovering the triangle with an exclamation icon when a server doesn't validate the version requirements. */
	inline FText GetServerVersionIgnoredTooltip()
	{
#define LOCTEXT_NAMESPACE "SConcertBrowser"
		return LOCTEXT("ServerIgnoreSessionRequirementsTooltip", "Careful this server won't verify that you have the right requirements before you join a session");
#undef LOCTEXT_NAMESPACE
	}

	/** Create a widget displaying the triangle with an exclamation icon in case the server flags include IgnoreSessionRequirement. */
	inline TSharedRef<SWidget> MakeServerVersionIgnoredWidget(EConcertServerFlags InServerFlags)
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
			.ColorAndOpacity(FAppStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton.Warning").Normal.TintColor.GetSpecifiedColor())
			[
				SNew(STextBlock)
				.Font(FAppStyle::Get().GetFontStyle("FontAwesome.9"))
				.Text(FEditorFontGlyphs::Exclamation_Triangle)
				.ToolTipText(GetServerVersionIgnoredTooltip())
				.Visibility((InServerFlags & EConcertServerFlags::IgnoreSessionRequirement) != EConcertServerFlags::None ? EVisibility::Visible : EVisibility::Collapsed)
			];
	}
}
