// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_PEERCONNECTION_RTC_ICE_TRANSPORT_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_PEERCONNECTION_RTC_ICE_TRANSPORT_H_

#include <memory>
#include <utility>

#include "third_party/blink/renderer/bindings/core/v8/active_script_wrappable.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_rtc_ice_candidate_pair.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_rtc_ice_parameters.h"
#include "third_party/blink/renderer/core/execution_context/execution_context_lifecycle_observer.h"
#include "third_party/blink/renderer/modules/event_target_modules.h"
#include "third_party/blink/renderer/modules/peerconnection/adapters/ice_transport_proxy.h"
#include "third_party/blink/renderer/platform/heap/prefinalizer.h"
#include "third_party/blink/renderer/platform/scheduler/public/frame_scheduler.h"
#include "third_party/webrtc/api/transport/enums.h"

namespace webrtc {
class IceTransportInterface;
}

namespace blink {

class ExceptionState;
class RTCIceCandidate;
class RTCIceGatherOptions;
class IceTransportAdapterCrossThreadFactory;
class RTCPeerConnection;

// Blink bindings for the RTCIceTransport JavaScript object.
//
// This class uses the IceTransportProxy to run and interact with the WebRTC
// ICE implementation running on the WebRTC worker thread managed by //content
// (called network_thread here).
//
// This object inherits from ActiveScriptWrappable since it must be kept alive
// while the ICE implementation is active, regardless of the number of
// JavaScript references held to it.
class MODULES_EXPORT RTCIceTransport final
    : public EventTargetWithInlineData,
      public ActiveScriptWrappable<RTCIceTransport>,
      public ExecutionContextLifecycleObserver,
      public IceTransportProxy::Delegate {
  DEFINE_WRAPPERTYPEINFO();
  USING_PRE_FINALIZER(RTCIceTransport, Dispose);

 public:
  enum class CloseReason {
    // stop() was called.
    kStopped,
    // The ExecutionContext is being destroyed.
    kContextDestroyed,
    // The object is being garbage collected.
    kDisposed,
  };

  static RTCIceTransport* Create(
      ExecutionContext* context,
      rtc::scoped_refptr<webrtc::IceTransportInterface> ice_transport_channel,
      RTCPeerConnection* peer_connection);
  static RTCIceTransport* Create(
      ExecutionContext* context,
      scoped_refptr<base::SingleThreadTaskRunner> proxy_thread,
      scoped_refptr<base::SingleThreadTaskRunner> host_thread,
      std::unique_ptr<IceTransportAdapterCrossThreadFactory> adapter_factory);

  RTCIceTransport(
      ExecutionContext* context,
      scoped_refptr<base::SingleThreadTaskRunner> proxy_thread,
      scoped_refptr<base::SingleThreadTaskRunner> host_thread,
      std::unique_ptr<IceTransportAdapterCrossThreadFactory> adapter_factory,
      RTCPeerConnection* peer_connection);
  RTCIceTransport(
      ExecutionContext* context,
      scoped_refptr<base::SingleThreadTaskRunner> proxy_thread,
      scoped_refptr<base::SingleThreadTaskRunner> host_thread,
      std::unique_ptr<IceTransportAdapterCrossThreadFactory> adapter_factory);
  ~RTCIceTransport() override;

  // Returns true if start() has been called.
  bool IsStarted() const { return role_ != cricket::ICEROLE_UNKNOWN; }

  // Returns the role specified in start().
  cricket::IceRole GetRole() const { return role_; }

  webrtc::IceTransportState GetState() const { return state_; }

  // Returns true if the RTCIceTransport is in a terminal state.
  bool IsClosed() const { return state_ == webrtc::IceTransportState::kClosed; }

  // If |this| was created from an RTCPeerConnection.
  //
  // Background: This is because we don't reuse an RTCIceTransport that has been
  // created from an RTCPeerConnection for an RTCQuicTransport (see
  // bugs.webrtc.org/10591). The core issue here is that the source of truth for
  // connecting a consumer to ICE is at the P2PTransportChannel. In the case of
  // RTCPeerConnection, the P2PTransportChannel is already connected and given
  // to the RTCIceTransport. In the case of the RTCQuicTransport it uses the
  // RTCIceTransport as the source of truth for enforcing just one connected
  // consumer. Possible fixes to this issue could include: -Use the
  // P2PTransportChannel as the source of truth directly (calling this
  // synchronously from the main thread)
  // -Asynchronously connect to the P2PTransport - if the count of connected
  // transports to the P2PTransportChannel is > 1, then throw an exception.
  bool IsFromPeerConnection() const;

  // rtc_ice_transport.idl
  String role() const;
  String state() const;
  String gatheringState() const;
  const HeapVector<Member<RTCIceCandidate>>& getLocalCandidates() const;
  const HeapVector<Member<RTCIceCandidate>>& getRemoteCandidates() const;
  RTCIceCandidatePair* getSelectedCandidatePair() const;
  RTCIceParameters* getLocalParameters() const;
  RTCIceParameters* getRemoteParameters() const;
  void gather(RTCIceGatherOptions* options, ExceptionState& exception_state);
  void start(RTCIceParameters* raw_remote_parameters,
             const String& role,
             ExceptionState& exception_state);
  void stop();
  void addRemoteCandidate(RTCIceCandidate* remote_candidate,
                          ExceptionState& exception_state);
  DEFINE_ATTRIBUTE_EVENT_LISTENER(statechange, kStatechange)
  DEFINE_ATTRIBUTE_EVENT_LISTENER(gatheringstatechange, kGatheringstatechange)
  DEFINE_ATTRIBUTE_EVENT_LISTENER(selectedcandidatepairchange,
                                  kSelectedcandidatepairchange)
  DEFINE_ATTRIBUTE_EVENT_LISTENER(icecandidate, kIcecandidate)

  // EventTarget overrides.
  const AtomicString& InterfaceName() const override;
  ExecutionContext* GetExecutionContext() const override;

  // ExecutionContextLifecycleObserver overrides.
  void ContextDestroyed() override;

  // ActiveScriptWrappable overrides.
  bool HasPendingActivity() const final;

  // For garbage collection.
  void Trace(Visitor* visitor) const override;

 private:
  // IceTransportProxy::Delegate overrides.
  void OnGatheringStateChanged(cricket::IceGatheringState new_state) override;
  void OnCandidateGathered(const cricket::Candidate& candidate) override;
  void OnStateChanged(webrtc::IceTransportState new_state) override;
  void OnSelectedCandidatePairChanged(
      const std::pair<cricket::Candidate, cricket::Candidate>&
          selected_candidate_pair) override;

  // Fills in |local_parameters_| with a random usernameFragment and a random
  // password.
  void GenerateLocalParameters();

  // Permenantly closes the RTCIceTransport with the given reason.
  // The RTCIceTransport must not already be closed.
  // This will transition the state to closed.
  void Close(CloseReason reason);

  bool RaiseExceptionIfClosed(ExceptionState& exception_state) const;
  void Dispose();

  cricket::IceRole role_ = cricket::ICEROLE_UNKNOWN;
  webrtc::IceTransportState state_ = webrtc::IceTransportState::kNew;
  cricket::IceGatheringState gathering_state_ = cricket::kIceGatheringNew;

  HeapVector<Member<RTCIceCandidate>> local_candidates_;
  HeapVector<Member<RTCIceCandidate>> remote_candidates_;

  Member<RTCIceParameters> local_parameters_;
  Member<RTCIceParameters> remote_parameters_;
  Member<RTCIceCandidatePair> selected_candidate_pair_;

  const WeakMember<RTCPeerConnection> peer_connection_;

  // Handle to the WebRTC ICE transport. Created when this binding is
  // constructed and deleted once network traffic should be stopped.
  std::unique_ptr<IceTransportProxy> proxy_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_PEERCONNECTION_RTC_ICE_TRANSPORT_H_
