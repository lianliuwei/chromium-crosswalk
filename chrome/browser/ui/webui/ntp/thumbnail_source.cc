// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/ntp/thumbnail_source.h"

#include "base/callback.h"
#include "base/memory/ref_counted_memory.h"
#include "base/message_loop.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/instant_io_context.h"
#include "chrome/browser/search/instant_service.h"
#include "chrome/browser/search/instant_service_factory.h"
#include "chrome/browser/thumbnails/thumbnail_service.h"
#include "chrome/browser/thumbnails/thumbnail_service_factory.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"
#include "grit/theme_resources.h"
#include "net/url_request/url_request.h"
#include "ui/base/resource/resource_bundle.h"

using content::BrowserThread;

// Set ThumbnailService now as Profile isn't thread safe.
ThumbnailSource::ThumbnailSource(Profile* profile)
    : thumbnail_service_(ThumbnailServiceFactory::GetForProfile(profile)),
      profile_(profile) {
}

ThumbnailSource::~ThumbnailSource() {
}

std::string ThumbnailSource::GetSource() const {
  return chrome::kChromeUIThumbnailHost;
}

void ThumbnailSource::StartDataRequest(
    const std::string& path,
    int render_process_id,
    int render_view_id,
    const content::URLDataSource::GotDataCallback& callback) {
  scoped_refptr<base::RefCountedMemory> data;
  if (thumbnail_service_->GetPageThumbnail(GURL(path), &data)) {
    // We have the thumbnail.
    callback.Run(data.get());
  } else {
    callback.Run(default_thumbnail_.get());
  }
}

std::string ThumbnailSource::GetMimeType(const std::string&) const {
  // We need to explicitly return a mime type, otherwise if the user tries to
  // drag the image they get no extension.
  return "image/png";
}

base::MessageLoop* ThumbnailSource::MessageLoopForRequestPath(
    const std::string& path) const {
  // TopSites can be accessed from the IO thread.
  return thumbnail_service_.get() ?
      NULL : content::URLDataSource::MessageLoopForRequestPath(path);
}

bool ThumbnailSource::ShouldServiceRequest(
    const net::URLRequest* request) const {
  if (request->url().SchemeIs(chrome::kChromeSearchScheme))
    return InstantIOContext::ShouldServiceRequest(request);
  return URLDataSource::ShouldServiceRequest(request);
}
