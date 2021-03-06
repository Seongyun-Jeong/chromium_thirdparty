// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/animation/compositor_color_keyframe.h"

#include "third_party/blink/renderer/platform/animation/timing_function.h"

namespace blink {

CompositorColorKeyframe::CompositorColorKeyframe(
    double time,
    SkColor value,
    const TimingFunction& timing_function)
    : color_keyframe_(gfx::ColorKeyframe::Create(base::Seconds(time),
                                                 value,
                                                 timing_function.CloneToCC())) {
}

CompositorColorKeyframe::CompositorColorKeyframe(
    std::unique_ptr<gfx::ColorKeyframe> color_keyframe)
    : color_keyframe_(std::move(color_keyframe)) {}

CompositorColorKeyframe::~CompositorColorKeyframe() = default;

base::TimeDelta CompositorColorKeyframe::Time() const {
  return color_keyframe_->Time();
}

const gfx::TimingFunction* CompositorColorKeyframe::CcTimingFunction() const {
  return color_keyframe_->timing_function();
}

std::unique_ptr<gfx::ColorKeyframe> CompositorColorKeyframe::CloneToCC() const {
  return color_keyframe_->Clone();
}

}  // namespace blink
