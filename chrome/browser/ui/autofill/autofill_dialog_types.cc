// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/autofill/autofill_dialog_types.h"

#include "base/logging.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

namespace autofill {

int const kSplashDisplayDurationMs = 1200;
int const kSplashFadeOutDurationMs = 200;
int const kSplashFadeInDialogDurationMs = 150;

DialogNotification::DialogNotification() : type_(NONE) {}

DialogNotification::DialogNotification(Type type, const string16& display_text)
    : type_(type),
      display_text_(display_text),
      checked_(false),
      interactive_(true) {}

SkColor DialogNotification::GetBackgroundColor() const {
  switch (type_) {
    case DialogNotification::EXPLANATORY_MESSAGE:
    case DialogNotification::WALLET_USAGE_CONFIRMATION:
      return SkColorSetRGB(0x47, 0x89, 0xfa);
    case DialogNotification::REQUIRED_ACTION:
    case DialogNotification::WALLET_ERROR:
    case DialogNotification::AUTOCHECKOUT_ERROR:
      return SkColorSetRGB(0xfc, 0xf3, 0xbf);
    case DialogNotification::DEVELOPER_WARNING:
    case DialogNotification::SECURITY_WARNING:
    case DialogNotification::VALIDATION_ERROR:
      return kWarningColor;
    case DialogNotification::AUTOCHECKOUT_SUCCESS:
    case DialogNotification::NONE:
      return SK_ColorTRANSPARENT;
  }

  NOTREACHED();
  return SK_ColorTRANSPARENT;
}

SkColor DialogNotification::GetTextColor() const {
  switch (type_) {
    case DialogNotification::AUTOCHECKOUT_SUCCESS:
    case DialogNotification::REQUIRED_ACTION:
    case DialogNotification::WALLET_ERROR:
    case DialogNotification::AUTOCHECKOUT_ERROR:
      return SK_ColorBLACK;
    case DialogNotification::DEVELOPER_WARNING:
    case DialogNotification::EXPLANATORY_MESSAGE:
    case DialogNotification::WALLET_USAGE_CONFIRMATION:
    case DialogNotification::SECURITY_WARNING:
    case DialogNotification::VALIDATION_ERROR:
      return SK_ColorWHITE;
    case DialogNotification::NONE:
      return SK_ColorTRANSPARENT;
  }

  NOTREACHED();
  return SK_ColorTRANSPARENT;
}

bool DialogNotification::HasArrow() const {
  return type_ == DialogNotification::EXPLANATORY_MESSAGE ||
         type_ == DialogNotification::WALLET_ERROR ||
         type_ == DialogNotification::WALLET_USAGE_CONFIRMATION;
}

bool DialogNotification::HasCheckbox() const {
  return type_ == DialogNotification::WALLET_USAGE_CONFIRMATION;
}

DialogAutocheckoutStep::DialogAutocheckoutStep(AutocheckoutStepType type,
                                               AutocheckoutStepStatus status)
    : type_(type),
      status_(status) {}

SkColor DialogAutocheckoutStep::GetTextColor() const {
  switch (status_) {
    case AUTOCHECKOUT_STEP_UNSTARTED:
      return SK_ColorGRAY;

    case AUTOCHECKOUT_STEP_STARTED:
    case AUTOCHECKOUT_STEP_COMPLETED:
      return SK_ColorBLACK;

    case AUTOCHECKOUT_STEP_FAILED:
      return SK_ColorRED;
  }

  NOTREACHED();
  return SK_ColorTRANSPARENT;
}

gfx::Font DialogAutocheckoutStep::GetTextFont() const {
  gfx::Font::FontStyle font_style = gfx::Font::NORMAL;
  switch (status_) {
    case AUTOCHECKOUT_STEP_UNSTARTED:
    case AUTOCHECKOUT_STEP_STARTED:
      font_style = gfx::Font::NORMAL;
      break;

    case AUTOCHECKOUT_STEP_COMPLETED:
    case AUTOCHECKOUT_STEP_FAILED:
      font_style = gfx::Font::BOLD;
      break;
  }

  return ui::ResourceBundle::GetSharedInstance().GetFont(
      ui::ResourceBundle::BaseFont).DeriveFont(0, font_style);
}

bool DialogAutocheckoutStep::IsIconVisible() const {
  return status_ == AUTOCHECKOUT_STEP_COMPLETED;
}

string16 DialogAutocheckoutStep::GetDisplayText() const {
  int description_id = -1;
  switch (status_) {
    case AUTOCHECKOUT_STEP_UNSTARTED:
      switch (type_) {
        case AUTOCHECKOUT_STEP_SHIPPING:
          description_id = IDS_AUTOFILL_STEP_SHIPPING_DETAILS_UNSTARTED;
          break;

        case AUTOCHECKOUT_STEP_DELIVERY:
          description_id = IDS_AUTOFILL_STEP_DELIVERY_DETAILS_UNSTARTED;
          break;

        case AUTOCHECKOUT_STEP_BILLING:
          description_id = IDS_AUTOFILL_STEP_BILLING_DETAILS_UNSTARTED;
          break;

        case AUTOCHECKOUT_STEP_PROXY_CARD:
          description_id = IDS_AUTOFILL_STEP_PROXY_CARD_UNSTARTED;
          break;
      }
      break;

    case AUTOCHECKOUT_STEP_STARTED:
      switch (type_) {
        case AUTOCHECKOUT_STEP_SHIPPING:
          description_id = IDS_AUTOFILL_STEP_SHIPPING_DETAILS_STARTED;
          break;

        case AUTOCHECKOUT_STEP_DELIVERY:
          description_id = IDS_AUTOFILL_STEP_DELIVERY_DETAILS_STARTED;
          break;

        case AUTOCHECKOUT_STEP_BILLING:
          description_id = IDS_AUTOFILL_STEP_BILLING_DETAILS_STARTED;
          break;

        case AUTOCHECKOUT_STEP_PROXY_CARD:
          description_id = IDS_AUTOFILL_STEP_PROXY_CARD_STARTED;
          break;
      }
      break;

    case AUTOCHECKOUT_STEP_COMPLETED:
      switch (type_) {
        case AUTOCHECKOUT_STEP_SHIPPING:
          description_id = IDS_AUTOFILL_STEP_SHIPPING_DETAILS_COMPLETE;
          break;

        case AUTOCHECKOUT_STEP_DELIVERY:
          description_id = IDS_AUTOFILL_STEP_DELIVERY_DETAILS_COMPLETE;
          break;

        case AUTOCHECKOUT_STEP_BILLING:
          description_id = IDS_AUTOFILL_STEP_BILLING_DETAILS_COMPLETE;
          break;

        case AUTOCHECKOUT_STEP_PROXY_CARD:
          description_id = IDS_AUTOFILL_STEP_PROXY_CARD_COMPLETE;
          break;
      }
      break;

    case AUTOCHECKOUT_STEP_FAILED:
      switch (type_) {
        case AUTOCHECKOUT_STEP_SHIPPING:
          description_id = IDS_AUTOFILL_STEP_SHIPPING_DETAILS_FAILED;
          break;

        case AUTOCHECKOUT_STEP_DELIVERY:
          description_id = IDS_AUTOFILL_STEP_DELIVERY_DETAILS_FAILED;
          break;

        case AUTOCHECKOUT_STEP_BILLING:
          description_id = IDS_AUTOFILL_STEP_BILLING_DETAILS_FAILED;
          break;

        case AUTOCHECKOUT_STEP_PROXY_CARD:
          description_id = IDS_AUTOFILL_STEP_PROXY_CARD_FAILED;
          break;
      }
      break;
  }

  return l10n_util::GetStringUTF16(description_id);
}

SkColor const kWarningColor = SkColorSetRGB(0xde, 0x49, 0x32);

SuggestionState::SuggestionState(const string16& text,
                                 gfx::Font::FontStyle text_style,
                                 const gfx::Image& icon,
                                 const string16& extra_text,
                                 const gfx::Image& extra_icon)
    : text(text),
      text_style(text_style),
      icon(icon),
      extra_text(extra_text),
      extra_icon(extra_icon) {}
SuggestionState::~SuggestionState() {}

AutofillMetrics::DialogUiEvent DialogSectionToUiEditEvent(
    DialogSection section) {
  switch (section) {
    case SECTION_EMAIL:
      return AutofillMetrics::DIALOG_UI_EMAIL_EDIT_UI_SHOWN;

    case SECTION_BILLING:
      return AutofillMetrics::DIALOG_UI_BILLING_EDIT_UI_SHOWN;

    case SECTION_CC_BILLING:
      return AutofillMetrics::DIALOG_UI_CC_BILLING_EDIT_UI_SHOWN;

    case SECTION_SHIPPING:
      return AutofillMetrics::DIALOG_UI_SHIPPING_EDIT_UI_SHOWN;

    case SECTION_CC:
      return AutofillMetrics::DIALOG_UI_CC_EDIT_UI_SHOWN;
  }

  NOTREACHED();
  return AutofillMetrics::NUM_DIALOG_UI_EVENTS;
}

AutofillMetrics::DialogUiEvent DialogSectionToUiItemAddedEvent(
    DialogSection section) {
  switch (section) {
    case SECTION_EMAIL:
      return AutofillMetrics::DIALOG_UI_EMAIL_ITEM_ADDED;

    case SECTION_BILLING:
      return AutofillMetrics::DIALOG_UI_BILLING_ITEM_ADDED;

    case SECTION_CC_BILLING:
      return AutofillMetrics::DIALOG_UI_CC_BILLING_ITEM_ADDED;

    case SECTION_SHIPPING:
      return AutofillMetrics::DIALOG_UI_SHIPPING_ITEM_ADDED;

    case SECTION_CC:
      return AutofillMetrics::DIALOG_UI_CC_ITEM_ADDED;
  }

  NOTREACHED();
  return AutofillMetrics::NUM_DIALOG_UI_EVENTS;
}

AutofillMetrics::DialogUiEvent DialogSectionToUiSelectionChangedEvent(
    DialogSection section) {
  switch (section) {
    case SECTION_EMAIL:
      return AutofillMetrics::DIALOG_UI_EMAIL_SELECTED_SUGGESTION_CHANGED;

    case SECTION_BILLING:
      return AutofillMetrics::DIALOG_UI_BILLING_SELECTED_SUGGESTION_CHANGED;

    case SECTION_CC_BILLING:
      return AutofillMetrics::DIALOG_UI_CC_BILLING_SELECTED_SUGGESTION_CHANGED;

    case SECTION_SHIPPING:
      return AutofillMetrics::DIALOG_UI_SHIPPING_SELECTED_SUGGESTION_CHANGED;

    case SECTION_CC:
      return AutofillMetrics::DIALOG_UI_CC_SELECTED_SUGGESTION_CHANGED;
  }

  NOTREACHED();
  return AutofillMetrics::NUM_DIALOG_UI_EVENTS;
}

}  // namespace autofill
