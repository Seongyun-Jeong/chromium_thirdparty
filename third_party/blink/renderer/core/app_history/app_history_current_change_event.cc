// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/app_history/app_history_current_change_event.h"

#include "third_party/blink/renderer/bindings/core/v8/v8_app_history_current_change_event_init.h"
#include "third_party/blink/renderer/core/app_history/app_history_entry.h"
#include "third_party/blink/renderer/core/event_interface_names.h"

namespace blink {

AppHistoryCurrentChangeEvent::AppHistoryCurrentChangeEvent(
    const AtomicString& type,
    AppHistoryCurrentChangeEventInit* init)
    : Event(type, init), from_(init->from()) {
  if (init->navigationType())
    navigation_type_ = *init->navigationType();
}

const AtomicString& AppHistoryCurrentChangeEvent::InterfaceName() const {
  return event_interface_names::kAppHistoryCurrentChangeEvent;
}

void AppHistoryCurrentChangeEvent::Trace(Visitor* visitor) const {
  Event::Trace(visitor);
  visitor->Trace(from_);
}

}  // namespace blink
