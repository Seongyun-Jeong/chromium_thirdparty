// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/webcodecs/codec_logger.h"
#include "base/strings/string_util.h"

namespace blink {

namespace internal {

std::string SanitizeStringProperty(WebString value) {
  std::string converted = value.Utf8();
  return base::IsStringUTF8(converted) ? converted : "[invalid property]";
}

void SendPlayerNameInformationInternal(media::MediaLog* media_log,
                                       const ExecutionContext& context,
                                       std::string loadedAs) {
  media_log->AddEvent<media::MediaLogEvent::kLoad>("Webcodecs::" + loadedAs);
  WebString frame_title;
  if (context.IsWindow()) {
    const auto& dom_context = To<LocalDOMWindow>(context);
    frame_title = dom_context.name();
    if (!frame_title.length()) {
      auto* frame = WebLocalFrameImpl::FromFrame(dom_context.GetFrame());
      if (frame)
        frame_title = frame->GetDocument().Title();
    }
  } else if (context.IsWorkerOrWorkletGlobalScope()) {
    const auto& worker_context = To<WorkerOrWorkletGlobalScope>(context);
    frame_title = worker_context.Name();
    if (!frame_title.length())
      frame_title = worker_context.Url().GetString();
  }
  media_log->SetProperty<media::MediaLogProperty::kFrameTitle>(
      internal::SanitizeStringProperty(frame_title));
}

}  // namespace internal

}  // namespace blink
