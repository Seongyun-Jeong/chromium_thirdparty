// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/peerconnection/rtc_encoded_video_underlying_sink.h"

#include "base/memory/scoped_refptr.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/platform/scheduler/test/renderer_scheduler_test_support.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_tester.h"
#include "third_party/blink/renderer/bindings/core/v8/to_v8_traits.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_testing.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_dom_exception.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_rtc_encoded_video_frame.h"
#include "third_party/blink/renderer/core/dom/dom_exception.h"
#include "third_party/blink/renderer/core/streams/writable_stream.h"
#include "third_party/blink/renderer/core/streams/writable_stream_default_writer.h"
#include "third_party/blink/renderer/modules/peerconnection/rtc_encoded_video_frame.h"
#include "third_party/blink/renderer/modules/peerconnection/rtc_encoded_video_frame_delegate.h"
#include "third_party/blink/renderer/modules/peerconnection/testing/mock_transformable_video_frame.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/peerconnection/rtc_encoded_video_stream_transformer.h"
#include "third_party/blink/renderer/platform/testing/testing_platform_support.h"
#include "third_party/webrtc/api/frame_transformer_interface.h"
#include "third_party/webrtc/api/scoped_refptr.h"
#include "third_party/webrtc/rtc_base/ref_counted_object.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace blink {

namespace {

const uint32_t kSSRC = 1;

class MockWebRtcTransformedFrameCallback
    : public webrtc::TransformedFrameCallback {
 public:
  MOCK_METHOD1(OnTransformedFrame,
               void(std::unique_ptr<webrtc::TransformableFrameInterface>));
};

bool IsDOMException(ScriptState* script_state,
                    ScriptValue value,
                    DOMExceptionCode code) {
  auto* dom_exception = V8DOMException::ToImplWithTypeCheck(
      script_state->GetIsolate(), value.V8Value());
  if (!dom_exception)
    return false;

  return dom_exception->code() == static_cast<uint16_t>(code);
}

}  // namespace

class RTCEncodedVideoUnderlyingSinkTest : public testing::Test {
 public:
  RTCEncodedVideoUnderlyingSinkTest()
      : main_task_runner_(
            blink::scheduler::GetSingleThreadTaskRunnerForTesting()),
        webrtc_callback_(
            new rtc::RefCountedObject<MockWebRtcTransformedFrameCallback>()),
        transformer_(main_task_runner_) {}

  void SetUp() override {
    EXPECT_FALSE(transformer_.HasTransformedFrameSinkCallback(kSSRC));
    transformer_.RegisterTransformedFrameSinkCallback(webrtc_callback_, kSSRC);
    EXPECT_TRUE(transformer_.HasTransformedFrameSinkCallback(kSSRC));
  }

  void TearDown() override {
    platform_->RunUntilIdle();
    transformer_.UnregisterTransformedFrameSinkCallback(kSSRC);
    EXPECT_FALSE(transformer_.HasTransformedFrameSinkCallback(kSSRC));
  }

  RTCEncodedVideoUnderlyingSink* CreateSink(
      ScriptState* script_state,
      webrtc::TransformableFrameInterface::Direction expected_direction =
          webrtc::TransformableFrameInterface::Direction::kSender) {
    return MakeGarbageCollected<RTCEncodedVideoUnderlyingSink>(
        script_state,
        WTF::BindRepeating(&RTCEncodedVideoUnderlyingSinkTest::GetTransformer,
                           WTF::Unretained(this)),
        expected_direction);
  }

  RTCEncodedVideoUnderlyingSink* CreateNullCallbackSink(
      ScriptState* script_state) {
    return MakeGarbageCollected<RTCEncodedVideoUnderlyingSink>(
        script_state,
        WTF::BindRepeating(
            []() -> RTCEncodedVideoStreamTransformer* { return nullptr; }),
        webrtc::TransformableFrameInterface::Direction::kSender);
  }

  RTCEncodedVideoStreamTransformer* GetTransformer() { return &transformer_; }

  ScriptValue CreateEncodedVideoFrameChunk(
      ScriptState* script_state,
      webrtc::TransformableFrameInterface::Direction direction =
          webrtc::TransformableFrameInterface::Direction::kSender) {
    auto mock_frame = std::make_unique<NiceMock<MockTransformableVideoFrame>>();

    ON_CALL(*mock_frame.get(), GetSsrc).WillByDefault(Return(kSSRC));
    ON_CALL(*mock_frame.get(), GetDirection).WillByDefault(Return(direction));
    RTCEncodedVideoFrame* frame =
        MakeGarbageCollected<RTCEncodedVideoFrame>(std::move(mock_frame));
    return ScriptValue(
        script_state->GetIsolate(),
        ToV8Traits<RTCEncodedVideoFrame>::ToV8(script_state, frame)
            .ToLocalChecked());
  }

 protected:
  ScopedTestingPlatformSupport<TestingPlatformSupport> platform_;
  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
  rtc::scoped_refptr<MockWebRtcTransformedFrameCallback> webrtc_callback_;
  RTCEncodedVideoStreamTransformer transformer_;
};

TEST_F(RTCEncodedVideoUnderlyingSinkTest,
       WriteToStreamForwardsToWebRtcCallback) {
  V8TestingScope v8_scope;
  ScriptState* script_state = v8_scope.GetScriptState();
  auto* sink = CreateSink(script_state);
  auto* stream =
      WritableStream::CreateWithCountQueueingStrategy(script_state, sink, 1u);

  NonThrowableExceptionState exception_state;
  auto* writer = stream->getWriter(script_state, exception_state);

  EXPECT_CALL(*webrtc_callback_, OnTransformedFrame(_));
  ScriptPromiseTester write_tester(
      script_state,
      writer->write(script_state, CreateEncodedVideoFrameChunk(script_state),
                    exception_state));
  EXPECT_FALSE(write_tester.IsFulfilled());

  writer->releaseLock(script_state);
  ScriptPromiseTester close_tester(
      script_state, stream->close(script_state, exception_state));
  close_tester.WaitUntilSettled();

  // Writing to the sink after the stream closes should fail.
  DummyExceptionStateForTesting dummy_exception_state;
  sink->write(script_state, CreateEncodedVideoFrameChunk(script_state), nullptr,
              dummy_exception_state);
  EXPECT_TRUE(dummy_exception_state.HadException());
  EXPECT_EQ(dummy_exception_state.Code(),
            static_cast<ExceptionCode>(DOMExceptionCode::kInvalidStateError));
}

TEST_F(RTCEncodedVideoUnderlyingSinkTest, WriteInvalidDataFails) {
  V8TestingScope v8_scope;
  ScriptState* script_state = v8_scope.GetScriptState();
  auto* sink = CreateSink(script_state);
  ScriptValue v8_integer = ScriptValue::From(script_state, 0);

  // Writing something that is not an RTCEncodedVideoFrame integer to the sink
  // should fail.
  DummyExceptionStateForTesting dummy_exception_state;
  sink->write(script_state, v8_integer, nullptr, dummy_exception_state);
  EXPECT_TRUE(dummy_exception_state.HadException());
}

TEST_F(RTCEncodedVideoUnderlyingSinkTest, WriteToNullCallbackSinkFails) {
  V8TestingScope v8_scope;
  ScriptState* script_state = v8_scope.GetScriptState();
  auto* sink = CreateNullCallbackSink(script_state);
  auto* stream =
      WritableStream::CreateWithCountQueueingStrategy(script_state, sink, 1u);

  NonThrowableExceptionState exception_state;
  auto* writer = stream->getWriter(script_state, exception_state);

  EXPECT_CALL(*webrtc_callback_, OnTransformedFrame(_)).Times(0);
  ScriptPromiseTester write_tester(
      script_state,
      writer->write(script_state, CreateEncodedVideoFrameChunk(script_state),
                    exception_state));
  write_tester.WaitUntilSettled();
  EXPECT_TRUE(write_tester.IsRejected());
  EXPECT_TRUE(IsDOMException(script_state, write_tester.Value(),
                             DOMExceptionCode::kInvalidStateError));
}

TEST_F(RTCEncodedVideoUnderlyingSinkTest, WriteInvalidDirectionFails) {
  V8TestingScope v8_scope;
  ScriptState* script_state = v8_scope.GetScriptState();
  auto* sink = CreateSink(
      script_state, webrtc::TransformableFrameInterface::Direction::kSender);

  // Write an encoded chunk with direction set to Receiver should fail as it
  // doesn't match the expected direction of our sink.
  DummyExceptionStateForTesting dummy_exception_state;
  sink->write(script_state,
              CreateEncodedVideoFrameChunk(
                  script_state,
                  webrtc::TransformableFrameInterface::Direction::kReceiver),
              nullptr, dummy_exception_state);
  EXPECT_TRUE(dummy_exception_state.HadException());
}

}  // namespace blink
