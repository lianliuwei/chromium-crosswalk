// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/resources/skpicture_content_layer_updater.h"

#include "base/debug/trace_event.h"
#include "cc/debug/rendering_stats.h"
#include "cc/resources/layer_painter.h"
#include "cc/resources/prioritized_resource.h"
#include "cc/resources/resource_update_queue.h"
#include "third_party/skia/include/core/SkCanvas.h"

namespace cc {

SkPictureContentLayerUpdater::SkPictureContentLayerUpdater(
    scoped_ptr<LayerPainter> painter,
    RenderingStatsInstrumentation* stats_instrumentation,
    int layer_id)
    : ContentLayerUpdater(painter.Pass(), stats_instrumentation, layer_id),
      layer_is_opaque_(false) {}

SkPictureContentLayerUpdater::~SkPictureContentLayerUpdater() {}

void SkPictureContentLayerUpdater::PrepareToUpdate(
    gfx::Rect content_rect,
    gfx::Size,
    float contents_width_scale,
    float contents_height_scale,
    gfx::Rect* resulting_opaque_rect,
    RenderingStats* stats) {
  SkCanvas* canvas =
      picture_.beginRecording(content_rect.width(), content_rect.height());
  base::TimeTicks record_start_time;
  if (stats)
    record_start_time = base::TimeTicks::HighResNow();
  PaintContents(canvas,
                content_rect,
                contents_width_scale,
                contents_height_scale,
                resulting_opaque_rect,
                stats);
  if (stats) {
    stats->total_record_time +=
        base::TimeTicks::HighResNow() - record_start_time;
    stats->total_pixels_recorded +=
        content_rect.width() * content_rect.height();
  }
  picture_.endRecording();
}

void SkPictureContentLayerUpdater::DrawPicture(SkCanvas* canvas) {
  TRACE_EVENT0("cc", "SkPictureContentLayerUpdater::DrawPicture");
  canvas->drawPicture(picture_);
}

void SkPictureContentLayerUpdater::SetOpaque(bool opaque) {
  layer_is_opaque_ = opaque;
}

}  // namespace cc
