// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/frame/ad_tracker.h"

#include <memory>

#include "base/feature_list.h"
#include "third_party/blink/renderer/bindings/core/v8/source_location.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_core.h"
#include "third_party/blink/renderer/core/core_probe_sink.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/platform/bindings/v8_binding.h"
#include "third_party/blink/renderer/platform/loader/fetch/fetch_initiator_type_names.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_request.h"
#include "third_party/blink/renderer/platform/weborigin/kurl.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"
#include "v8/include/v8.h"

namespace blink {

namespace {

bool IsKnownAdExecutionContext(ExecutionContext* execution_context) {
  // TODO(jkarlin): Do the same check for worker contexts.
  if (auto* window = DynamicTo<LocalDOMWindow>(execution_context)) {
    LocalFrame* frame = window->GetFrame();
    if (frame && frame->IsAdSubframe())
      return true;
  }
  return false;
}

String GenerateFakeUrlFromScriptId(int script_id) {
  // Null string is used to represent scripts with neither a name nor an ID.
  if (script_id == v8::Message::kNoScriptIdInfo)
    return String();

  // The prefix cannot appear in real URLs.
  return String::Format("{ id %d }", script_id);
}

}  // namespace

namespace features {
// Controls whether the AdTracker will look across async stacks to determine if
// the currently running stack is ad related.
const base::Feature kAsyncStackAdTagging{"AsyncStackAdTagging",
                                         base::FEATURE_ENABLED_BY_DEFAULT};
}  // namespace features

// static
AdTracker* AdTracker::FromExecutionContext(
    ExecutionContext* execution_context) {
  if (!execution_context)
    return nullptr;
  if (auto* window = DynamicTo<LocalDOMWindow>(execution_context)) {
    if (LocalFrame* frame = window->GetFrame()) {
      return frame->GetAdTracker();
    }
  }
  return nullptr;
}

// static
bool AdTracker::IsAdScriptExecutingInDocument(Document* document,
                                              StackType stack_type) {
  AdTracker* ad_tracker =
      document->GetFrame() ? document->GetFrame()->GetAdTracker() : nullptr;
  return ad_tracker && ad_tracker->IsAdScriptInStack(stack_type);
}

AdTracker::AdTracker(LocalFrame* local_root)
    : local_root_(local_root),
      async_stack_enabled_(
          base::FeatureList::IsEnabled(features::kAsyncStackAdTagging)) {
  local_root_->GetProbeSink()->AddAdTracker(this);
}

AdTracker::~AdTracker() {
  DCHECK(!local_root_);
}

void AdTracker::Shutdown() {
  if (!local_root_)
    return;
  local_root_->GetProbeSink()->RemoveAdTracker(this);
  local_root_ = nullptr;
}

String AdTracker::ScriptAtTopOfStack() {
  // CurrentStackTrace is 10x faster than CaptureStackTrace if all that you need
  // is the url of the script at the top of the stack. See crbug.com/1057211 for
  // more detail.
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  DCHECK(isolate);

  v8::Local<v8::StackTrace> stack_trace =
      v8::StackTrace::CurrentStackTrace(isolate, /*frame_limit=*/1);
  if (stack_trace.IsEmpty() || stack_trace->GetFrameCount() < 1)
    return String();

  v8::Local<v8::StackFrame> frame = stack_trace->GetFrame(isolate, 0);
  v8::Local<v8::String> script_name = frame->GetScriptName();
  if (script_name.IsEmpty() || !script_name->Length())
    return GenerateFakeUrlFromScriptId(frame->GetScriptId());

  return ToCoreString(script_name);
}

ExecutionContext* AdTracker::GetCurrentExecutionContext() {
  // Determine the current ExecutionContext.
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  return context.IsEmpty() ? nullptr : ToExecutionContext(context);
}

void AdTracker::WillExecuteScript(ExecutionContext* execution_context,
                                  const String& script_url,
                                  int script_id) {
  bool is_ad = false;

  // We track scripts with no URL (i.e. dynamically inserted scripts with no
  // src) by IDs instead. We also check the stack as they are executed
  // immediately and should be tagged based on the script inserting them.
  bool should_track_with_id =
      script_url.IsEmpty() && script_id != v8::Message::kNoScriptIdInfo;
  if (should_track_with_id) {
    // This primarily checks if |execution_context| is a known ad context as we
    // don't need to keep track of scripts in ad contexts. However, two scripts
    // with identical text content can be assigned the same ID.
    String fake_url = GenerateFakeUrlFromScriptId(script_id);
    if (IsKnownAdScript(execution_context, fake_url)) {
      is_ad = true;
    } else if (IsAdScriptInStack(StackType::kBottomAndTop)) {
      AppendToKnownAdScripts(*execution_context, fake_url);
      is_ad = true;
    }
  }

  if (!should_track_with_id)
    is_ad = IsKnownAdScript(execution_context, script_url);

  stack_frame_is_ad_.push_back(is_ad);
  if (is_ad)
    num_ads_in_stack_ += 1;
}

void AdTracker::DidExecuteScript() {
  if (stack_frame_is_ad_.back()) {
    DCHECK_LT(0, num_ads_in_stack_);
    num_ads_in_stack_ -= 1;
  }
  stack_frame_is_ad_.pop_back();
}

void AdTracker::Will(const probe::ExecuteScript& probe) {
  WillExecuteScript(probe.context, probe.script_url, probe.script_id);
}

void AdTracker::Did(const probe::ExecuteScript& probe) {
  DidExecuteScript();
}

void AdTracker::Will(const probe::CallFunction& probe) {
  // Do not process nested microtasks as that might potentially lead to a
  // slowdown of custom element callbacks.
  if (probe.depth)
    return;

  v8::Local<v8::Value> resource_name =
      probe.function->GetScriptOrigin().ResourceName();
  String script_url;
  if (!resource_name.IsEmpty()) {
    v8::MaybeLocal<v8::String> resource_name_string =
        resource_name->ToString(ToIsolate(local_root_)->GetCurrentContext());
    // Rarely, ToString() can return an empty result, even if |resource_name|
    // isn't empty (crbug.com/1086832).
    if (!resource_name_string.IsEmpty())
      script_url = ToCoreString(resource_name_string.ToLocalChecked());
  }
  WillExecuteScript(probe.context, script_url, probe.function->ScriptId());
}

void AdTracker::Did(const probe::CallFunction& probe) {
  if (probe.depth)
    return;

  DidExecuteScript();
}

bool AdTracker::CalculateIfAdSubresource(
    ExecutionContext* execution_context,
    const KURL& request_url,
    ResourceType resource_type,
    const FetchInitiatorInfo& initiator_info,
    bool known_ad) {
  // Check if the document loading the resource is an ad.
  const bool is_ad_execution_context =
      IsKnownAdExecutionContext(execution_context);
  known_ad = known_ad || is_ad_execution_context;

  // We skip script checking for stylesheet-initiated resource requests as the
  // stack may represent the cause of a style recalculation rather than the
  // actual resources themselves. Instead, the ad bit is set according to the
  // CSSParserContext when the request is made. See crbug.com/1051605.
  if (initiator_info.name == fetch_initiator_type_names::kCSS ||
      initiator_info.name == fetch_initiator_type_names::kUacss) {
    return known_ad;
  }

  // Check if any executing script is an ad.
  known_ad = known_ad || IsAdScriptInStack(StackType::kBottomAndTop);

  // If it is a script marked as an ad and it's not in an ad context, append it
  // to the known ad script set. We don't need to keep track of ad scripts in ad
  // contexts, because any script executed inside an ad context is considered an
  // ad script by IsKnownAdScript.
  if (resource_type == ResourceType::kScript && known_ad &&
      !is_ad_execution_context) {
    AppendToKnownAdScripts(*execution_context, request_url.GetString());
  }

  return known_ad;
}

void AdTracker::DidCreateAsyncTask(probe::AsyncTaskContext* task_context) {
  DCHECK(task_context);
  if (!async_stack_enabled_)
    return;

  if (IsAdScriptInStack(StackType::kBottomAndTop))
    task_context->SetAdTask();
}

void AdTracker::DidStartAsyncTask(probe::AsyncTaskContext* task_context) {
  DCHECK(task_context);
  if (!async_stack_enabled_)
    return;

  if (task_context->IsAdTask())
    running_ad_async_tasks_ += 1;
}

void AdTracker::DidFinishAsyncTask(probe::AsyncTaskContext* task_context) {
  DCHECK(task_context);
  if (!async_stack_enabled_)
    return;

  if (task_context->IsAdTask())
    running_ad_async_tasks_ -= 1;
}

bool AdTracker::IsAdScriptInStack(StackType stack_type) {
  if (num_ads_in_stack_ > 0 || running_ad_async_tasks_ > 0)
    return true;

  ExecutionContext* execution_context = GetCurrentExecutionContext();
  if (!execution_context)
    return false;

  // If we're in an ad context, then no matter what the executing script is it's
  // considered an ad.
  if (IsKnownAdExecutionContext(execution_context))
    return true;

  if (stack_type == StackType::kBottomOnly)
    return false;

  // The stack scanned by the AdTracker contains entry points into the stack
  // (e.g., when v8 is executed) but not the entire stack. For a small cost we
  // can also check the top of the stack (this is much cheaper than getting the
  // full stack from v8).
  return IsKnownAdScriptForCheckedContext(*execution_context, String());
}

bool AdTracker::IsKnownAdScript(ExecutionContext* execution_context,
                                const String& url) {
  if (!execution_context)
    return false;

  if (IsKnownAdExecutionContext(execution_context))
    return true;

  return IsKnownAdScriptForCheckedContext(*execution_context, url);
}

bool AdTracker::IsKnownAdScriptForCheckedContext(
    ExecutionContext& execution_context,
    const String& url) {
  DCHECK(!IsKnownAdExecutionContext(&execution_context));
  auto it = known_ad_scripts_.find(&execution_context);
  if (it == known_ad_scripts_.end())
    return false;

  if (it->value.IsEmpty())
    return false;

  // Delay calling ScriptAtTopOfStack() as much as possible due to its cost.
  String script_url = url.IsNull() ? ScriptAtTopOfStack() : url;
  if (script_url.IsEmpty())
    return false;
  return it->value.Contains(script_url);
}

// This is a separate function for testing purposes.
void AdTracker::AppendToKnownAdScripts(ExecutionContext& execution_context,
                                       const String& url) {
  DCHECK(!url.IsEmpty());
  auto add_result =
      known_ad_scripts_.insert(&execution_context, HashSet<String>());
  add_result.stored_value->value.insert(url);
}

void AdTracker::Trace(Visitor* visitor) const {
  visitor->Trace(local_root_);
  visitor->Trace(known_ad_scripts_);
}

}  // namespace blink
