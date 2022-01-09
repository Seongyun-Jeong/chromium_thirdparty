// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/widget/input/widget_input_handler_manager.h"

#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/check_op.h"
#include "base/feature_list.h"
#include "base/metrics/histogram_macros.h"
#include "base/notreached.h"
#include "cc/base/features.h"
#include "cc/metrics/event_metrics.h"
#include "cc/trees/layer_tree_host.h"
#include "cc/trees/paint_holding_reason.h"
#include "components/power_scheduler/power_mode.h"
#include "components/power_scheduler/power_mode_arbiter.h"
#include "components/power_scheduler/power_mode_voter.h"
#include "services/tracing/public/cpp/perfetto/flow_event_utils.h"
#include "third_party/blink/public/common/input/web_coalesced_input_event.h"
#include "third_party/blink/public/common/input/web_input_event_attribution.h"
#include "third_party/blink/public/common/input/web_keyboard_event.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/scheduler/web_thread_scheduler.h"
#include "third_party/blink/public/platform/scheduler/web_widget_scheduler.h"
#include "third_party/blink/renderer/platform/widget/frame_widget.h"
#include "third_party/blink/renderer/platform/widget/input/elastic_overscroll_controller.h"
#include "third_party/blink/renderer/platform/widget/input/main_thread_event_queue.h"
#include "third_party/blink/renderer/platform/widget/input/widget_input_handler_impl.h"
#include "third_party/blink/renderer/platform/widget/widget_base.h"
#include "third_party/blink/renderer/platform/widget/widget_base_client.h"

#if defined(OS_ANDROID)
#include "third_party/blink/renderer/platform/widget/compositing/android_webview/synchronous_compositor_registry.h"
#include "third_party/blink/renderer/platform/widget/input/synchronous_compositor_proxy.h"
#endif

namespace blink {

using ::perfetto::protos::pbzero::ChromeLatencyInfo;
using ::perfetto::protos::pbzero::TrackEvent;

namespace {
// We will count dropped pointerdown by posting a task in the main thread.
// To avoid blocking the main thread, we need a timer to send the data
// intermittently. The time delay of the timer is 10X of the threshold of
// long tasks which block the main thread 50 ms or longer.
const base::TimeDelta kEventCountsTimerDelay = base::Milliseconds(500);

mojom::blink::DidOverscrollParamsPtr ToDidOverscrollParams(
    const InputHandlerProxy::DidOverscrollParams* overscroll_params) {
  if (!overscroll_params)
    return nullptr;
  return mojom::blink::DidOverscrollParams::New(
      overscroll_params->accumulated_overscroll,
      overscroll_params->latest_overscroll_delta,
      overscroll_params->current_fling_velocity,
      overscroll_params->causal_event_viewport_point,
      overscroll_params->overscroll_behavior);
}

void CallCallback(
    mojom::blink::WidgetInputHandler::DispatchEventCallback callback,
    mojom::blink::InputEventResultState result_state,
    const ui::LatencyInfo& latency_info,
    mojom::blink::DidOverscrollParamsPtr overscroll_params,
    absl::optional<cc::TouchAction> touch_action) {
  ui::LatencyInfo::TraceIntermediateFlowEvents(
      {latency_info}, ChromeLatencyInfo::STEP_HANDLED_INPUT_EVENT_IMPL);
  std::move(callback).Run(
      mojom::blink::InputEventResultSource::kMainThread, latency_info,
      result_state, std::move(overscroll_params),
      touch_action
          ? mojom::blink::TouchActionOptional::New(touch_action.value())
          : nullptr);
}

mojom::blink::InputEventResultState InputEventDispositionToAck(
    InputHandlerProxy::EventDisposition disposition) {
  switch (disposition) {
    case InputHandlerProxy::DID_HANDLE:
      return mojom::blink::InputEventResultState::kConsumed;
    case InputHandlerProxy::DID_NOT_HANDLE:
      return mojom::blink::InputEventResultState::kNotConsumed;
    case InputHandlerProxy::DID_NOT_HANDLE_NON_BLOCKING_DUE_TO_FLING:
      return mojom::blink::InputEventResultState::kSetNonBlockingDueToFling;
    case InputHandlerProxy::DROP_EVENT:
      return mojom::blink::InputEventResultState::kNoConsumerExists;
    case InputHandlerProxy::DID_NOT_HANDLE_NON_BLOCKING:
      return mojom::blink::InputEventResultState::kSetNonBlocking;
    case InputHandlerProxy::REQUIRES_MAIN_THREAD_HIT_TEST:
    default:
      NOTREACHED();
      return mojom::blink::InputEventResultState::kUnknown;
  }
}

std::unique_ptr<blink::WebGestureEvent> ScrollBeginFromScrollUpdate(
    const WebGestureEvent& gesture_update) {
  DCHECK(gesture_update.GetType() == WebInputEvent::Type::kGestureScrollUpdate);

  auto scroll_begin = std::make_unique<WebGestureEvent>(gesture_update);
  scroll_begin->SetType(WebInputEvent::Type::kGestureScrollBegin);

  scroll_begin->data.scroll_begin.delta_x_hint =
      gesture_update.data.scroll_update.delta_x;
  scroll_begin->data.scroll_begin.delta_y_hint =
      gesture_update.data.scroll_update.delta_y;
  scroll_begin->data.scroll_begin.delta_hint_units =
      gesture_update.data.scroll_update.delta_units;
  scroll_begin->data.scroll_begin.target_viewport = false;
  scroll_begin->data.scroll_begin.inertial_phase =
      gesture_update.data.scroll_update.inertial_phase;
  scroll_begin->data.scroll_begin.synthetic = false;
  scroll_begin->data.scroll_begin.pointer_count = 0;
  scroll_begin->data.scroll_begin.scrollable_area_element_id = 0;

  return scroll_begin;
}

}  // namespace

#if defined(OS_ANDROID)
class SynchronousCompositorProxyRegistry
    : public SynchronousCompositorRegistry {
 public:
  explicit SynchronousCompositorProxyRegistry(
      scoped_refptr<base::SingleThreadTaskRunner> compositor_task_runner)
      : compositor_thread_default_task_runner_(
            std::move(compositor_task_runner)) {}

  ~SynchronousCompositorProxyRegistry() override {
    // Ensure the proxy has already been release on the compositor thread
    // before destroying this object.
    DCHECK(!proxy_);
  }

  void CreateProxy(SynchronousInputHandlerProxy* handler) {
    DCHECK(compositor_thread_default_task_runner_->BelongsToCurrentThread());
    proxy_ = std::make_unique<SynchronousCompositorProxy>(handler);
    proxy_->Init();

    if (sink_)
      proxy_->SetLayerTreeFrameSink(sink_);
  }

  SynchronousCompositorProxy* proxy() { return proxy_.get(); }

  void RegisterLayerTreeFrameSink(
      SynchronousLayerTreeFrameSink* layer_tree_frame_sink) override {
    DCHECK(compositor_thread_default_task_runner_->BelongsToCurrentThread());
    DCHECK_EQ(nullptr, sink_);
    sink_ = layer_tree_frame_sink;
    if (proxy_)
      proxy_->SetLayerTreeFrameSink(layer_tree_frame_sink);
  }

  void UnregisterLayerTreeFrameSink(
      SynchronousLayerTreeFrameSink* layer_tree_frame_sink) override {
    DCHECK(compositor_thread_default_task_runner_->BelongsToCurrentThread());
    DCHECK_EQ(layer_tree_frame_sink, sink_);
    sink_ = nullptr;
  }

  void DestroyProxy() {
    DCHECK(compositor_thread_default_task_runner_->BelongsToCurrentThread());
    proxy_.reset();
  }

 private:
  scoped_refptr<base::SingleThreadTaskRunner>
      compositor_thread_default_task_runner_;
  std::unique_ptr<SynchronousCompositorProxy> proxy_;
  SynchronousLayerTreeFrameSink* sink_ = nullptr;
};

#endif

scoped_refptr<WidgetInputHandlerManager> WidgetInputHandlerManager::Create(
    base::WeakPtr<WidgetBase> widget,
    base::WeakPtr<mojom::blink::FrameWidgetInputHandler>
        frame_widget_input_handler,
    bool never_composited,
    scheduler::WebThreadScheduler* compositor_thread_scheduler,
    scheduler::WebThreadScheduler* main_thread_scheduler,
    bool uses_input_handler) {
  scoped_refptr<WidgetInputHandlerManager> manager =
      new WidgetInputHandlerManager(
          std::move(widget), std::move(frame_widget_input_handler),
          never_composited, compositor_thread_scheduler, main_thread_scheduler);
  if (uses_input_handler)
    manager->InitInputHandler();

  // A compositor thread implies we're using an input handler.
  DCHECK(!manager->compositor_thread_default_task_runner_ ||
         uses_input_handler);
  // Conversely, if we don't use an input handler we must not have a compositor
  // thread.
  DCHECK(uses_input_handler ||
         !manager->compositor_thread_default_task_runner_);

  return manager;
}

WidgetInputHandlerManager::WidgetInputHandlerManager(
    base::WeakPtr<WidgetBase> widget,
    base::WeakPtr<mojom::blink::FrameWidgetInputHandler>
        frame_widget_input_handler,
    bool never_composited,
    scheduler::WebThreadScheduler* compositor_thread_scheduler,
    scheduler::WebThreadScheduler* main_thread_scheduler)
    : widget_(std::move(widget)),
      frame_widget_input_handler_(std::move(frame_widget_input_handler)),

      widget_scheduler_(main_thread_scheduler->CreateWidgetScheduler()),
      main_thread_scheduler_(main_thread_scheduler),
      input_event_queue_(base::MakeRefCounted<MainThreadEventQueue>(
          this,
          widget_scheduler_->InputTaskRunner(),
          main_thread_scheduler,
          /*allow_raf_aligned_input=*/!never_composited)),
      main_thread_task_runner_(widget_scheduler_->InputTaskRunner()),
      compositor_thread_default_task_runner_(
          compositor_thread_scheduler
              ? compositor_thread_scheduler->DefaultTaskRunner()
              : nullptr),
      compositor_thread_input_blocking_task_runner_(
          compositor_thread_scheduler
              ? compositor_thread_scheduler->InputTaskRunner()
              : nullptr),
      response_power_mode_voter_(
          power_scheduler::PowerModeArbiter::GetInstance()->NewVoter(
              "PowerModeVoter.Response")) {
#if defined(OS_ANDROID)
  if (compositor_thread_default_task_runner_) {
    synchronous_compositor_registry_ =
        std::make_unique<SynchronousCompositorProxyRegistry>(
            compositor_thread_default_task_runner_);
  }
#endif
}

void WidgetInputHandlerManager::InitInputHandler() {
  bool sync_compositing = false;
#if defined(OS_ANDROID)
  sync_compositing =
      Platform::Current()->IsSynchronousCompositingEnabledForAndroidWebView();
#endif
  uses_input_handler_ = true;
  base::OnceClosure init_closure = base::BindOnce(
      &WidgetInputHandlerManager::InitOnInputHandlingThread, AsWeakPtr(),
      widget_->LayerTreeHost()->GetDelegateForInput(), sync_compositing);
  InputThreadTaskRunner()->PostTask(FROM_HERE, std::move(init_closure));
}

WidgetInputHandlerManager::~WidgetInputHandlerManager() = default;

void WidgetInputHandlerManager::AddInterface(
    mojo::PendingReceiver<mojom::blink::WidgetInputHandler> receiver,
    mojo::PendingRemote<mojom::blink::WidgetInputHandlerHost> host) {
  if (compositor_thread_default_task_runner_) {
    host_ = mojo::SharedRemote<mojom::blink::WidgetInputHandlerHost>(
        std::move(host), compositor_thread_default_task_runner_);
    // Mojo channel bound on compositor thread.
    compositor_thread_default_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&WidgetInputHandlerManager::BindChannel, this,
                                  std::move(receiver)));
  } else {
    host_ = mojo::SharedRemote<mojom::blink::WidgetInputHandlerHost>(
        std::move(host));
    // Mojo channel bound on main thread.
    BindChannel(std::move(receiver));
  }
}

bool WidgetInputHandlerManager::HandleInputEvent(
    const WebCoalescedInputEvent& event,
    std::unique_ptr<cc::EventMetrics> metrics,
    HandledEventCallback handled_callback) {
  WidgetBaseInputHandler::HandledEventCallback blink_callback = base::BindOnce(
      [](HandledEventCallback callback,
         blink::mojom::InputEventResultState ack_state,
         const ui::LatencyInfo& latency_info,
         std::unique_ptr<InputHandlerProxy::DidOverscrollParams>
             overscroll_params,
         absl::optional<cc::TouchAction> touch_action) {
        if (!callback)
          return;
        std::move(callback).Run(ack_state, latency_info,
                                ToDidOverscrollParams(overscroll_params.get()),
                                touch_action);
      },
      std::move(handled_callback));
  widget_->input_handler().HandleInputEvent(event, std::move(metrics),
                                            std::move(blink_callback));
  return true;
}

void WidgetInputHandlerManager::InputEventsDispatched(bool raf_aligned) {
  DCHECK(main_thread_task_runner_->BelongsToCurrentThread());

  // Immediately after dispatching rAF-aligned events, a frame is still in
  // progress. There is no need to check and break swap promises here, because
  // when the frame is finished, they will be broken if there is no update (see
  // `LayerTreeHostImpl::BeginMainFrameAborted`). Also, unlike non-rAF-aligned
  // events, checking `RequestedMainFramePending()` would not work here, because
  // it is reset before dispatching rAF-aligned events.
  if (raf_aligned)
    return;

  // If no main frame request is pending after dispatching non-rAF-aligned
  // events, there will be no updated frame to submit to Viz; so, break
  // outstanding swap promises here due to no update.
  if (widget_ && !widget_->LayerTreeHost()->RequestedMainFramePending()) {
    widget_->LayerTreeHost()->GetSwapPromiseManager()->BreakSwapPromises(
        cc::SwapPromise::DidNotSwapReason::COMMIT_NO_UPDATE);
  }
}

void WidgetInputHandlerManager::SetNeedsMainFrame() {
  widget_->RequestAnimationAfterDelay(base::TimeDelta());
}

void WidgetInputHandlerManager::WillShutdown() {
#if defined(OS_ANDROID)
  if (synchronous_compositor_registry_)
    synchronous_compositor_registry_->DestroyProxy();
#endif
  input_handler_proxy_.reset();
  dropped_event_counts_timer_.reset();
}

void WidgetInputHandlerManager::DispatchNonBlockingEventToMainThread(
    std::unique_ptr<WebCoalescedInputEvent> event,
    const WebInputEventAttribution& attribution,
    std::unique_ptr<cc::EventMetrics> metrics) {
  DCHECK(input_event_queue_);
  input_event_queue_->HandleEvent(
      std::move(event), MainThreadEventQueue::DispatchType::kNonBlocking,
      mojom::blink::InputEventResultState::kSetNonBlocking, attribution,
      std::move(metrics), HandledEventCallback());
}

void WidgetInputHandlerManager::FindScrollTargetOnMainThread(
    const gfx::PointF& point,
    ElementAtPointCallback callback) {
  TRACE_EVENT2("input",
               "WidgetInputHandlerManager::FindScrollTargetOnMainThread",
               "point.x", point.x(), "point.y", point.y());
  DCHECK(main_thread_task_runner_->BelongsToCurrentThread());
  DCHECK(base::FeatureList::IsEnabled(::features::kScrollUnification));

  uint64_t element_id =
      widget_->client()->FrameWidget()->GetScrollableContainerIdAt(point);

  InputThreadTaskRunner(TaskRunnerType::kInputBlocking)
      ->PostTask(FROM_HERE, base::BindOnce(std::move(callback), element_id));
}

void WidgetInputHandlerManager::DidAnimateForInput() {
  main_thread_scheduler_->DidAnimateForInputOnCompositorThread();
}

void WidgetInputHandlerManager::DidStartScrollingViewport() {
  mojom::blink::WidgetInputHandlerHost* host = GetWidgetInputHandlerHost();
  if (!host)
    return;
  host->DidStartScrollingViewport();
}

void WidgetInputHandlerManager::GenerateScrollBeginAndSendToMainThread(
    const WebGestureEvent& update_event,
    const WebInputEventAttribution& attribution,
    const cc::EventMetrics* update_metrics) {
  DCHECK_EQ(update_event.GetType(), WebInputEvent::Type::kGestureScrollUpdate);
  auto gesture_event = ScrollBeginFromScrollUpdate(update_event);

  // TODO(crbug.com/1137870): Scroll-begin events should not normally be
  // inertial. Here, the scroll-begin is created from the first scroll-update
  // event of a sequence and the first scroll-update should not be inertial,
  // either. Consider setting `is_inertial` to `false` and adding
  // DCHECKs here to make sure `gesture_event` is not inertial.
  const bool is_inertial = gesture_event->InertialPhase() ==
                           WebGestureEvent::InertialPhaseState::kMomentum;
  std::unique_ptr<cc::EventMetrics> metrics =
      cc::ScrollEventMetrics::CreateFromExisting(
          gesture_event->GetTypeAsUiEventType(),
          gesture_event->GetScrollInputType(), is_inertial,
          cc::EventMetrics::DispatchStage::kRendererCompositorFinished,
          update_metrics);

  auto event = std::make_unique<WebCoalescedInputEvent>(
      std::move(gesture_event), ui::LatencyInfo());

  DispatchNonBlockingEventToMainThread(std::move(event), attribution,
                                       std::move(metrics));
}

void WidgetInputHandlerManager::SetAllowedTouchAction(
    cc::TouchAction touch_action,
    uint32_t unique_touch_event_id,
    InputHandlerProxy::EventDisposition event_disposition) {
  compositor_allowed_touch_action_ = touch_action;
}

void WidgetInputHandlerManager::ProcessTouchAction(
    cc::TouchAction touch_action) {
  if (mojom::blink::WidgetInputHandlerHost* host = GetWidgetInputHandlerHost())
    host->SetTouchActionFromMain(touch_action);
}

mojom::blink::WidgetInputHandlerHost*
WidgetInputHandlerManager::GetWidgetInputHandlerHost() {
  if (host_)
    return host_.get();
  return nullptr;
}

#if defined(OS_ANDROID)
void WidgetInputHandlerManager::AttachSynchronousCompositor(
    mojo::PendingRemote<mojom::blink::SynchronousCompositorControlHost>
        control_host,
    mojo::PendingAssociatedRemote<mojom::blink::SynchronousCompositorHost> host,
    mojo::PendingAssociatedReceiver<mojom::blink::SynchronousCompositor>
        compositor_request) {
  DCHECK(synchronous_compositor_registry_);
  if (synchronous_compositor_registry_->proxy()) {
    synchronous_compositor_registry_->proxy()->BindChannel(
        std::move(control_host), std::move(host),
        std::move(compositor_request));
  }
}
#endif

void WidgetInputHandlerManager::ObserveGestureEventOnMainThread(
    const WebGestureEvent& gesture_event,
    const cc::InputHandlerScrollResult& scroll_result) {
  base::OnceClosure observe_gesture_event_closure = base::BindOnce(
      &WidgetInputHandlerManager::ObserveGestureEventOnInputHandlingThread,
      this, gesture_event, scroll_result);
  InputThreadTaskRunner()->PostTask(FROM_HERE,
                                    std::move(observe_gesture_event_closure));
}

void WidgetInputHandlerManager::LogInputTimingUMA() {
  if (!have_emitted_uma_) {
    InitialInputTiming lifecycle_state = InitialInputTiming::kBeforeLifecycle;
    if (!(renderer_deferral_state_ &
          (unsigned)RenderingDeferralBits::kDeferMainFrameUpdates)) {
      if (renderer_deferral_state_ &
          (unsigned)RenderingDeferralBits::kDeferCommits) {
        lifecycle_state = InitialInputTiming::kBeforeCommit;
      } else {
        lifecycle_state = InitialInputTiming::kAfterCommit;
      }
    }
    UMA_HISTOGRAM_ENUMERATION("PaintHolding.InputTiming2", lifecycle_state);
    have_emitted_uma_ = true;
  }
}

void WidgetInputHandlerManager::DispatchScrollGestureToCompositor(
    std::unique_ptr<WebGestureEvent> event) {
  DCHECK(base::FeatureList::IsEnabled(features::kScrollUnification));
  std::unique_ptr<WebCoalescedInputEvent> web_scoped_gesture_event =
      std::make_unique<WebCoalescedInputEvent>(std::move(event),
                                               ui::LatencyInfo());
  // input thread task runner is |main_thread_task_runner_| only in tests
  InputThreadTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(&WidgetInputHandlerManager::
                         HandleInputEventWithLatencyOnInputHandlingThread,
                     this, std::move(web_scoped_gesture_event)));
}

void WidgetInputHandlerManager::
    HandleInputEventWithLatencyOnInputHandlingThread(
        std::unique_ptr<WebCoalescedInputEvent> event) {
  DCHECK(base::FeatureList::IsEnabled(features::kScrollUnification));
  DCHECK(input_handler_proxy_);
  input_handler_proxy_->HandleInputEventWithLatencyInfo(
      std::move(event), nullptr, base::DoNothing());
}

void WidgetInputHandlerManager::DispatchEvent(
    std::unique_ptr<WebCoalescedInputEvent> event,
    mojom::blink::WidgetInputHandler::DispatchEventCallback callback) {
  // Since we can't easily track the end of processing for a non-blocking input
  // event from here, we just temporarily bump kResponse mode for every
  // dispatched event.
  response_power_mode_voter_->VoteFor(power_scheduler::PowerMode::kResponse);
  response_power_mode_voter_->ResetVoteAfterTimeout(
      power_scheduler::PowerModeVoter::kResponseTimeout);

  bool event_is_move =
      event->Event().GetType() == WebInputEvent::Type::kMouseMove ||
      event->Event().GetType() == WebInputEvent::Type::kPointerMove;
  if (!event_is_move)
    LogInputTimingUMA();

  // Drop input if we are deferring a rendering pipeline phase, unless it's a
  // move event.
  // We don't want users interacting with stuff they can't see, so we drop it.
  // We allow moves because we need to keep the current pointer location up
  // to date. Tests and other code can allow pre-commit input through the
  // "allow-pre-commit-input" command line flag.
  // TODO(schenney): Also allow scrolls? This would make some tests not flaky,
  // it seems, because they sometimes crash on seeing a scroll update/end
  // without a begin. Scrolling, pinch-zoom etc. don't seem dangerous.
  if (renderer_deferral_state_ && !allow_pre_commit_input_ && !event_is_move) {
    if (callback) {
      std::move(callback).Run(
          mojom::blink::InputEventResultSource::kMainThread, ui::LatencyInfo(),
          mojom::blink::InputEventResultState::kNotConsumed, nullptr, nullptr);
    }
    return;
  }

  // If TimeTicks is not consistent across processes we cannot use the event's
  // platform timestamp in this process. Instead use the time that the event is
  // received as the event's timestamp.
  if (!base::TimeTicks::IsConsistentAcrossProcesses()) {
    event->EventPointer()->SetTimeStamp(base::TimeTicks::Now());
  }

  std::unique_ptr<cc::EventMetrics> metrics;
  if (event->Event().IsGestureScroll()) {
    const auto& gesture_event =
        static_cast<const WebGestureEvent&>(event->Event());
    const bool is_inertial = gesture_event.InertialPhase() ==
                             WebGestureEvent::InertialPhaseState::kMomentum;
    if (gesture_event.GetType() == WebInputEvent::Type::kGestureScrollUpdate) {
      metrics = cc::ScrollUpdateEventMetrics::Create(
          gesture_event.GetTypeAsUiEventType(),
          gesture_event.GetScrollInputType(), is_inertial,
          has_seen_first_gesture_scroll_update_after_begin_
              ? cc::ScrollUpdateEventMetrics::ScrollUpdateType::kContinued
              : cc::ScrollUpdateEventMetrics::ScrollUpdateType::kStarted,
          gesture_event.data.scroll_update.delta_y, event->Event().TimeStamp());
      has_seen_first_gesture_scroll_update_after_begin_ = true;
    } else {
      metrics = cc::ScrollEventMetrics::Create(
          gesture_event.GetTypeAsUiEventType(),
          gesture_event.GetScrollInputType(), is_inertial,
          event->Event().TimeStamp());
      has_seen_first_gesture_scroll_update_after_begin_ = false;
    }
  } else if (WebInputEvent::IsPinchGestureEventType(event->Event().GetType())) {
    const auto& gesture_event =
        static_cast<const WebGestureEvent&>(event->Event());
    metrics = cc::PinchEventMetrics::Create(
        gesture_event.GetTypeAsUiEventType(),
        gesture_event.GetScrollInputType(), event->Event().TimeStamp());
  } else {
    metrics = cc::EventMetrics::Create(event->Event().GetTypeAsUiEventType(),
                                       event->Event().TimeStamp());
  }

  if (uses_input_handler_) {
    // If the input_handler_proxy has disappeared ensure we just ack event.
    if (!input_handler_proxy_) {
      if (callback) {
        std::move(callback).Run(
            mojom::blink::InputEventResultSource::kMainThread,
            ui::LatencyInfo(),
            mojom::blink::InputEventResultState::kNotConsumed, nullptr,
            nullptr);
      }
      return;
    }

    // The InputHandlerProxy will be the first to try handling the event on the
    // compositor thread. It will respond to this class by calling
    // DidHandleInputEventSentToCompositor with the result of its attempt. Based
    // on the resulting disposition, DidHandleInputEventSentToCompositor will
    // either ACK the event as handled to the browser or forward it to the main
    // thread.
    input_handler_proxy_->HandleInputEventWithLatencyInfo(
        std::move(event), std::move(metrics),
        base::BindOnce(
            &WidgetInputHandlerManager::DidHandleInputEventSentToCompositor,
            this, std::move(callback)));
  } else {
    DCHECK(!input_handler_proxy_);
    DispatchDirectlyToWidget(std::move(event), std::move(metrics),
                             std::move(callback));
  }
}

void WidgetInputHandlerManager::InvokeInputProcessedCallback() {
  DCHECK(main_thread_task_runner_->BelongsToCurrentThread());

  // We can call this method even if we didn't request a callback (e.g. when
  // the renderer becomes hidden).
  if (!input_processed_callback_)
    return;

  // The handler's method needs to respond to the mojo message so it needs to
  // run on the input handling thread.  Even if we're already on the correct
  // thread, we PostTask for symmetry.
  InputThreadTaskRunner()->PostTask(FROM_HERE,
                                    std::move(input_processed_callback_));
}

static void WaitForInputProcessedFromMain(base::WeakPtr<WidgetBase> widget) {
  // If the widget is destroyed while we're posting to the main thread, the
  // Mojo message will be acked in WidgetInputHandlerImpl's destructor.
  if (!widget)
    return;

  WidgetInputHandlerManager* manager = widget->widget_input_handler_manager();

  // TODO(bokan): Temporary to unblock synthetic gesture events running under
  // VR. https://crbug.com/940063
  bool ack_immediately = widget->client()->ShouldAckSyntheticInputImmediately();

  // If the WidgetBase is hidden, we won't produce compositor frames for it
  // so just ACK the input to prevent blocking the browser indefinitely.
  if (widget->is_hidden() || ack_immediately) {
    manager->InvokeInputProcessedCallback();
    return;
  }

  auto redraw_complete_callback =
      base::BindOnce(&WidgetInputHandlerManager::InvokeInputProcessedCallback,
                     manager->AsWeakPtr());

  // Since wheel-events can kick off animations, we can not consider
  // all observable effects of an input gesture to be processed
  // when the CompositorFrame caused by that input has been produced, send, and
  // displayed. Therefore, explicitly request the presentation *after* any
  // ongoing scroll-animation ends. After the scroll-animation ends (if any),
  // the call will force a commit and redraw and callback when the
  // CompositorFrame has been displayed in the display service. Some examples of
  // non-trivial effects that require waiting that long: committing
  // NonFastScrollRegions to the compositor, sending touch-action rects to the
  // browser, and sending updated surface information to the display compositor
  // for up-to-date OOPIF hit-testing.

  widget->RequestPresentationAfterScrollAnimationEnd(
      std::move(redraw_complete_callback));
}

void WidgetInputHandlerManager::WaitForInputProcessed(
    base::OnceClosure callback) {
  // Note, this will be called from the mojo-bound thread which could be either
  // main or compositor.
  DCHECK(!input_processed_callback_);
  input_processed_callback_ = std::move(callback);

  // We mustn't touch widget_ from the impl thread so post all the setup
  // to the main thread. Make sure the callback runs after all the queued events
  // are dispatched.
  input_event_queue_->QueueClosure(
      base::BindOnce(&WaitForInputProcessedFromMain, widget_));
}

void WidgetInputHandlerManager::DidNavigate() {
  renderer_deferral_state_ = 0;
  have_emitted_uma_ = false;
}

void WidgetInputHandlerManager::OnDeferMainFrameUpdatesChanged(bool status) {
  if (status) {
    renderer_deferral_state_ |=
        static_cast<uint16_t>(RenderingDeferralBits::kDeferMainFrameUpdates);
  } else {
    renderer_deferral_state_ &=
        ~static_cast<uint16_t>(RenderingDeferralBits::kDeferMainFrameUpdates);
  }
}

void WidgetInputHandlerManager::OnDeferCommitsChanged(
    bool status,
    cc::PaintHoldingReason reason) {
  if (status && reason == cc::PaintHoldingReason::kFirstContentfulPaint) {
    renderer_deferral_state_ |=
        static_cast<uint16_t>(RenderingDeferralBits::kDeferCommits);
  } else {
    renderer_deferral_state_ &=
        ~static_cast<uint16_t>(RenderingDeferralBits::kDeferCommits);
  }
}

void WidgetInputHandlerManager::InitOnInputHandlingThread(
    const base::WeakPtr<cc::CompositorDelegateForInput>& compositor_delegate,
    bool sync_compositing) {
  DCHECK(InputThreadTaskRunner()->BelongsToCurrentThread());
  DCHECK(uses_input_handler_);

  // It is possible that the input_handler has already been destroyed before
  // this Init() call was invoked. If so, early out.
  if (!compositor_delegate)
    return;

  // The input handler is created and ownership is passed to the compositor
  // delegate; hence we only receive a WeakPtr back.
  base::WeakPtr<cc::InputHandler> input_handler =
      cc::InputHandler::Create(*compositor_delegate);
  DCHECK(input_handler);

  input_handler_proxy_ =
      std::make_unique<InputHandlerProxy>(*input_handler.get(), this);

#if defined(OS_ANDROID)
  if (sync_compositing) {
    DCHECK(synchronous_compositor_registry_);
    synchronous_compositor_registry_->CreateProxy(input_handler_proxy_.get());
  }
#endif
}

void WidgetInputHandlerManager::BindChannel(
    mojo::PendingReceiver<mojom::blink::WidgetInputHandler> receiver) {
  if (!receiver.is_valid())
    return;
  // Passing null for |input_event_queue_| tells the handler that we don't have
  // a compositor thread. (Single threaded-mode shouldn't use the queue, or else
  // events might get out of order - see crrev.com/519829).
  WidgetInputHandlerImpl* handler = new WidgetInputHandlerImpl(
      this,
      compositor_thread_default_task_runner_ ? input_event_queue_ : nullptr,
      widget_, frame_widget_input_handler_);
  handler->SetReceiver(std::move(receiver));
}

void WidgetInputHandlerManager::DispatchDirectlyToWidget(
    std::unique_ptr<WebCoalescedInputEvent> event,
    std::unique_ptr<cc::EventMetrics> metrics,
    mojom::blink::WidgetInputHandler::DispatchEventCallback callback) {
  // This path should only be taken by non-frame WidgetBase that don't use a
  // compositor (e.g. popups, plugins). Events bounds for a frame WidgetBase
  // must be passed through the InputHandlerProxy first.
  DCHECK(!uses_input_handler_);

  // Input messages must not be processed if the WidgetBase was destroyed or
  // was just recreated for a provisional frame.
  if (!widget_ || widget_->IsForProvisionalFrame()) {
    if (callback) {
      std::move(callback).Run(mojom::blink::InputEventResultSource::kMainThread,
                              event->latency_info(),
                              mojom::blink::InputEventResultState::kNotConsumed,
                              nullptr, nullptr);
    }
    return;
  }

  auto send_callback = base::BindOnce(
      &WidgetInputHandlerManager::DidHandleInputEventSentToMainFromWidgetBase,
      this, std::move(callback));

  widget_->input_handler().HandleInputEvent(*event, std::move(metrics),
                                            std::move(send_callback));
  InputEventsDispatched(/*raf_aligned=*/false);
}

void WidgetInputHandlerManager::FindScrollTargetReply(
    std::unique_ptr<WebCoalescedInputEvent> event,
    std::unique_ptr<cc::EventMetrics> metrics,
    mojom::blink::WidgetInputHandler::DispatchEventCallback browser_callback,
    uint64_t hit_test_result) {
  TRACE_EVENT1("input", "WidgetInputHandlerManager::FindScrollTargetReply",
               "hit_test_result", hit_test_result);
  DCHECK(InputThreadTaskRunner()->BelongsToCurrentThread());
  DCHECK(base::FeatureList::IsEnabled(::features::kScrollUnification));

  // If the input_handler was destroyed in the mean time just ACK the event as
  // unconsumed to the browser and drop further handling.
  if (!input_handler_proxy_) {
    std::move(browser_callback)
        .Run(mojom::blink::InputEventResultSource::kMainThread,
             ui::LatencyInfo(),
             mojom::blink::InputEventResultState::kNotConsumed, nullptr,
             nullptr);
    return;
  }

  input_handler_proxy_->ContinueScrollBeginAfterMainThreadHitTest(
      std::move(event), std::move(metrics),
      base::BindOnce(
          &WidgetInputHandlerManager::DidHandleInputEventSentToCompositor, this,
          std::move(browser_callback)),
      hit_test_result);
}

void WidgetInputHandlerManager::SendDroppedPointerDownCounts() {
  main_thread_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&WidgetBase::CountDroppedPointerDownForEventTiming,
                     widget_, dropped_pointer_down_));
  dropped_pointer_down_ = 0;
}

void WidgetInputHandlerManager::DidHandleInputEventSentToCompositor(
    mojom::blink::WidgetInputHandler::DispatchEventCallback callback,
    InputHandlerProxy::EventDisposition event_disposition,
    std::unique_ptr<WebCoalescedInputEvent> event,
    std::unique_ptr<InputHandlerProxy::DidOverscrollParams> overscroll_params,
    const WebInputEventAttribution& attribution,
    std::unique_ptr<cc::EventMetrics> metrics) {
  TRACE_EVENT1("input",
               "WidgetInputHandlerManager::DidHandleInputEventSentToCompositor",
               "Disposition", event_disposition);
  DCHECK(InputThreadTaskRunner()->BelongsToCurrentThread());

  if (event_disposition == InputHandlerProxy::DROP_EVENT &&
      event->Event().GetType() == blink::WebInputEvent::Type::kTouchStart) {
    const WebTouchEvent touch_event =
        static_cast<const WebTouchEvent&>(event->Event());
    for (unsigned i = 0; i < touch_event.touches_length; ++i) {
      const WebTouchPoint& touch_point = touch_event.touches[i];
      if (touch_point.state == WebTouchPoint::State::kStatePressed) {
        dropped_pointer_down_++;
      }
    }
    if (dropped_pointer_down_ > 0) {
      if (!dropped_event_counts_timer_) {
        dropped_event_counts_timer_ = std::make_unique<base::OneShotTimer>();
      }

      if (!dropped_event_counts_timer_->IsRunning()) {
        dropped_event_counts_timer_->Start(
            FROM_HERE, kEventCountsTimerDelay,
            base::BindOnce(
                &WidgetInputHandlerManager::SendDroppedPointerDownCounts,
                this));
      }
    }
  }

  ui::LatencyInfo::TraceIntermediateFlowEvents(
      {event->latency_info()},
      ChromeLatencyInfo::STEP_DID_HANDLE_INPUT_AND_OVERSCROLL);

  if (event_disposition == InputHandlerProxy::REQUIRES_MAIN_THREAD_HIT_TEST) {
    TRACE_EVENT_INSTANT0("input", "PostingHitTestToMainThread",
                         TRACE_EVENT_SCOPE_THREAD);
    // TODO(bokan): We're going to need to perform a hit test on the main thread
    // before we can continue handling the event. This is the critical path of a
    // scroll so we should probably ensure the scheduler can prioritize it
    // accordingly. https://crbug.com/1082618.
    DCHECK(base::FeatureList::IsEnabled(::features::kScrollUnification));
    DCHECK_EQ(event->Event().GetType(),
              WebInputEvent::Type::kGestureScrollBegin);
    DCHECK(input_handler_proxy_);

    gfx::PointF event_position =
        static_cast<const WebGestureEvent&>(event->Event()).PositionInWidget();

    ElementAtPointCallback result_callback = base::BindOnce(
        &WidgetInputHandlerManager::FindScrollTargetReply, this,
        std::move(event), std::move(metrics), std::move(callback));

    main_thread_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&WidgetInputHandlerManager::FindScrollTargetOnMainThread,
                       this, event_position, std::move(result_callback)));
    return;
  }

  absl::optional<cc::TouchAction> touch_action =
      compositor_allowed_touch_action_;
  compositor_allowed_touch_action_.reset();

  mojom::blink::InputEventResultState ack_state =
      InputEventDispositionToAck(event_disposition);
  if (ack_state == mojom::blink::InputEventResultState::kConsumed) {
    main_thread_scheduler_->DidHandleInputEventOnCompositorThread(
        event->Event(), scheduler::WebThreadScheduler::InputEventState::
                            EVENT_CONSUMED_BY_COMPOSITOR);
  } else if (MainThreadEventQueue::IsForwardedAndSchedulerKnown(ack_state)) {
    main_thread_scheduler_->DidHandleInputEventOnCompositorThread(
        event->Event(), scheduler::WebThreadScheduler::InputEventState::
                            EVENT_FORWARDED_TO_MAIN_THREAD);
  }

  if (ack_state == mojom::blink::InputEventResultState::kSetNonBlocking ||
      ack_state ==
          mojom::blink::InputEventResultState::kSetNonBlockingDueToFling ||
      ack_state == mojom::blink::InputEventResultState::kNotConsumed) {
    DCHECK(!overscroll_params);
    DCHECK(!event->latency_info().coalesced());
    MainThreadEventQueue::DispatchType dispatch_type =
        callback.is_null() ? MainThreadEventQueue::DispatchType::kNonBlocking
                           : MainThreadEventQueue::DispatchType::kBlocking;
    HandledEventCallback handled_event = base::BindOnce(
        &WidgetInputHandlerManager::DidHandleInputEventSentToMain, this,
        std::move(callback), touch_action);
    input_event_queue_->HandleEvent(std::move(event), dispatch_type, ack_state,
                                    attribution, std::move(metrics),
                                    std::move(handled_event));
    return;
  }

  if (callback) {
    std::move(callback).Run(
        mojom::blink::InputEventResultSource::kCompositorThread,
        event->latency_info(), ack_state,
        ToDidOverscrollParams(overscroll_params.get()),
        touch_action
            ? mojom::blink::TouchActionOptional::New(touch_action.value())
            : nullptr);
  }
}

void WidgetInputHandlerManager::DidHandleInputEventSentToMainFromWidgetBase(
    mojom::blink::WidgetInputHandler::DispatchEventCallback callback,
    mojom::blink::InputEventResultState ack_state,
    const ui::LatencyInfo& latency_info,
    std::unique_ptr<blink::InputHandlerProxy::DidOverscrollParams>
        overscroll_params,
    absl::optional<cc::TouchAction> touch_action) {
  DidHandleInputEventSentToMain(
      std::move(callback), absl::nullopt, ack_state, latency_info,
      ToDidOverscrollParams(overscroll_params.get()), touch_action);
}

void WidgetInputHandlerManager::DidHandleInputEventSentToMain(
    mojom::blink::WidgetInputHandler::DispatchEventCallback callback,
    absl::optional<cc::TouchAction> touch_action_from_compositor,
    mojom::blink::InputEventResultState ack_state,
    const ui::LatencyInfo& latency_info,
    mojom::blink::DidOverscrollParamsPtr overscroll_params,
    absl::optional<cc::TouchAction> touch_action_from_main) {
  if (!callback)
    return;

  TRACE_EVENT1("input",
               "WidgetInputHandlerManager::DidHandleInputEventSentToMain",
               "ack_state", ack_state);
  ui::LatencyInfo::TraceIntermediateFlowEvents(
      {latency_info}, ChromeLatencyInfo::STEP_HANDLED_INPUT_EVENT_MAIN_OR_IMPL);

  absl::optional<cc::TouchAction> touch_action_for_ack = touch_action_from_main;
  if (!touch_action_for_ack.has_value()) {
    TRACE_EVENT_INSTANT0("input", "Using allowed_touch_action",
                         TRACE_EVENT_SCOPE_THREAD);
    touch_action_for_ack = touch_action_from_compositor;
  }

  // This method is called from either the main thread or the compositor thread.
  bool is_compositor_thread =
      compositor_thread_default_task_runner_ &&
      compositor_thread_default_task_runner_->BelongsToCurrentThread();

  // If there is a compositor task runner and the current thread isn't the
  // compositor thread proxy it over to the compositor thread.
  if (compositor_thread_default_task_runner_ && !is_compositor_thread) {
    TRACE_EVENT_INSTANT0("input", "PostingToCompositor",
                         TRACE_EVENT_SCOPE_THREAD);
    compositor_thread_default_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(CallCallback, std::move(callback), ack_state,
                                  latency_info, std::move(overscroll_params),
                                  touch_action_for_ack));
  } else {
    // Otherwise call the callback immediately.
    std::move(callback).Run(
        is_compositor_thread
            ? mojom::blink::InputEventResultSource::kCompositorThread
            : mojom::blink::InputEventResultSource::kMainThread,
        latency_info, ack_state, std::move(overscroll_params),
        touch_action_for_ack ? mojom::blink::TouchActionOptional::New(
                                   touch_action_for_ack.value())
                             : nullptr);
  }
}

void WidgetInputHandlerManager::ObserveGestureEventOnInputHandlingThread(
    const WebGestureEvent& gesture_event,
    const cc::InputHandlerScrollResult& scroll_result) {
  if (!input_handler_proxy_)
    return;
  // The elastic overscroll controller on android can be dynamically created or
  // removed by changing prefers-reduced-motion. When removed, we do not need to
  // observe the event.
  if (!input_handler_proxy_->elastic_overscroll_controller())
    return;
  input_handler_proxy_->elastic_overscroll_controller()
      ->ObserveGestureEventAndResult(gesture_event, scroll_result);
}

const scoped_refptr<base::SingleThreadTaskRunner>&
WidgetInputHandlerManager::InputThreadTaskRunner(TaskRunnerType type) const {
  if (compositor_thread_input_blocking_task_runner_ &&
      type == TaskRunnerType::kInputBlocking) {
    return compositor_thread_input_blocking_task_runner_;
  } else if (compositor_thread_default_task_runner_) {
    return compositor_thread_default_task_runner_;
  }
  return main_thread_task_runner_;
}

#if defined(OS_ANDROID)
SynchronousCompositorRegistry*
WidgetInputHandlerManager::GetSynchronousCompositorRegistry() {
  DCHECK(synchronous_compositor_registry_);
  return synchronous_compositor_registry_.get();
}
#endif

void WidgetInputHandlerManager::ClearClient() {
  input_event_queue_->ClearClient();
}

}  // namespace blink
