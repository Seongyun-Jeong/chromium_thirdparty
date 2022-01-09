/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Google Inc. nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/modules/mediastream/media_constraints_impl.h"

#include "build/build_config.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/renderer/bindings/core/v8/array_value.h"
#include "third_party/blink/renderer/bindings/core/v8/dictionary.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_union_string_stringsequence.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_constrain_boolean_parameters.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_constrain_dom_string_parameters.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_constrain_double_range.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_constrain_long_range.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_media_track_constraints.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_typedefs.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_union_boolean_constrainbooleanparameters.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_union_boolean_constraindoublerange_double.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_union_constraindomstringparameters_string_stringsequence.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_union_constraindoublerange_double.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_union_constrainlongrange_long.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/frame/deprecation.h"
#include "third_party/blink/renderer/core/frame/web_feature.h"
#include "third_party/blink/renderer/core/inspector/console_message.h"
#include "third_party/blink/renderer/modules/mediastream/media_error_state.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/wtf/hash_map.h"
#include "third_party/blink/renderer/platform/wtf/text/string_hash.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {
namespace media_constraints_impl {

// A naked value is treated as an "ideal" value in the basic constraints,
// but as an exact value in "advanced" constraints.
// https://w3c.github.io/mediacapture-main/#constrainable-interface
enum class NakedValueDisposition { kTreatAsIdeal, kTreatAsExact };

// Old type/value form of constraint. Used in parsing old-style constraints.
struct NameValueStringConstraint {
  NameValueStringConstraint() = default;

  NameValueStringConstraint(WebString name, WebString value)
      : name_(name), value_(value) {}

  WebString name_;
  WebString value_;
};

// Legal constraint names.

const char kMinAspectRatio[] = "minAspectRatio";
const char kMaxAspectRatio[] = "maxAspectRatio";
const char kMaxWidth[] = "maxWidth";
const char kMinWidth[] = "minWidth";
const char kMaxHeight[] = "maxHeight";
const char kMinHeight[] = "minHeight";
const char kMaxFrameRate[] = "maxFrameRate";
const char kMinFrameRate[] = "minFrameRate";
const char kMediaStreamSource[] = "chromeMediaSource";
const char kMediaStreamSourceId[] =
    "chromeMediaSourceId";                           // mapped to deviceId
const char kMediaStreamSourceInfoId[] = "sourceId";  // mapped to deviceId
const char kMediaStreamRenderToAssociatedSink[] =
    "chromeRenderToAssociatedSink";
// RenderToAssociatedSink will be going away some time.
const char kMediaStreamAudioHotword[] = "googHotword";
const char kEchoCancellation[] = "echoCancellation";
const char kDisableLocalEcho[] = "disableLocalEcho";
const char kGoogEchoCancellation[] = "googEchoCancellation";
const char kGoogExperimentalEchoCancellation[] = "googEchoCancellation2";
const char kGoogAutoGainControl[] = "googAutoGainControl";
const char kGoogExperimentalAutoGainControl[] = "googAutoGainControl2";
const char kGoogNoiseSuppression[] = "googNoiseSuppression";
const char kGoogExperimentalNoiseSuppression[] = "googNoiseSuppression2";
const char kGoogBeamforming[] = "googBeamforming";
const char kGoogArrayGeometry[] = "googArrayGeometry";
const char kGoogHighpassFilter[] = "googHighpassFilter";
const char kGoogTypingNoiseDetection[] = "googTypingNoiseDetection";
const char kGoogAudioMirroring[] = "googAudioMirroring";
// Audio constraints.
const char kDAEchoCancellation[] = "googDAEchoCancellation";
// Google-specific constraint keys for a local video source (getUserMedia).
const char kNoiseReduction[] = "googNoiseReduction";

// Constraint keys for CreateOffer / CreateAnswer defined in W3C specification.
const char kOfferToReceiveAudio[] = "OfferToReceiveAudio";
const char kOfferToReceiveVideo[] = "OfferToReceiveVideo";
const char kVoiceActivityDetection[] = "VoiceActivityDetection";
const char kIceRestart[] = "IceRestart";
// Google specific constraint for BUNDLE enable/disable.
const char kUseRtpMux[] = "googUseRtpMUX";
// Below constraints should be used during PeerConnection construction.
const char kEnableDtlsSrtp[] = "DtlsSrtpKeyAgreement";
const char kEnableRtpDataChannels[] = "RtpDataChannels";
// Google-specific constraint keys.
// TODO(hta): These need to be made standard or deleted. crbug.com/605673
const char kEnableDscp[] = "googDscp";
const char kEnableIPv6[] = "googIPv6";
const char kEnableVideoSuspendBelowMinBitrate[] = "googSuspendBelowMinBitrate";
const char kNumUnsignalledRecvStreams[] = "googNumUnsignalledRecvStreams";
const char kCombinedAudioVideoBwe[] = "googCombinedAudioVideoBwe";
const char kScreencastMinBitrate[] = "googScreencastMinBitrate";
const char kCpuOveruseDetection[] = "googCpuOveruseDetection";
const char kCpuUnderuseThreshold[] = "googCpuUnderuseThreshold";
const char kCpuOveruseThreshold[] = "googCpuOveruseThreshold";
const char kCpuUnderuseEncodeRsdThreshold[] =
    "googCpuUnderuseEncodeRsdThreshold";
const char kCpuOveruseEncodeRsdThreshold[] = "googCpuOveruseEncodeRsdThreshold";
const char kCpuOveruseEncodeUsage[] = "googCpuOveruseEncodeUsage";
const char kHighStartBitrate[] = "googHighStartBitrate";
const char kPayloadPadding[] = "googPayloadPadding";
const char kAudioLatency[] = "latencyMs";

// Names that have been used in the past, but should now be ignored.
// Kept around for backwards compatibility.
// https://crbug.com/579729
const char kGoogLeakyBucket[] = "googLeakyBucket";
const char kPowerLineFrequency[] = "googPowerLineFrequency";
// mediacapture-depth: videoKind key and VideoKindEnum values.
const char kVideoKind[] = "videoKind";
const char kVideoKindColor[] = "color";
const char kVideoKindDepth[] = "depth";
// Names used for testing.
const char kTestConstraint1[] = "valid_and_supported_1";
const char kTestConstraint2[] = "valid_and_supported_2";

static bool ParseMandatoryConstraintsDictionary(
    const Dictionary& mandatory_constraints_dictionary,
    Vector<NameValueStringConstraint>& mandatory) {
  DummyExceptionStateForTesting exception_state;
  const HashMap<String, String>& mandatory_constraints_hash_map =
      mandatory_constraints_dictionary.GetOwnPropertiesAsStringHashMap(
          exception_state);
  if (exception_state.HadException())
    return false;

  for (const auto& iter : mandatory_constraints_hash_map)
    mandatory.push_back(NameValueStringConstraint(iter.key, iter.value));
  return true;
}

static bool ParseOptionalConstraintsVectorElement(
    const Dictionary& constraint,
    Vector<NameValueStringConstraint>& optional_constraints_vector) {
  DummyExceptionStateForTesting exception_state;
  const Vector<String>& local_names =
      constraint.GetPropertyNames(exception_state);
  if (exception_state.HadException())
    return false;
  if (local_names.size() != 1)
    return false;
  const String& key = local_names[0];
  String value;
  bool ok = DictionaryHelper::Get(constraint, key, value);
  if (!ok)
    return false;
  optional_constraints_vector.push_back(NameValueStringConstraint(key, value));
  return true;
}

// Old style parser. Deprecated.
static bool Parse(const Dictionary& constraints_dictionary,
                  Vector<NameValueStringConstraint>& optional,
                  Vector<NameValueStringConstraint>& mandatory) {
  if (constraints_dictionary.IsUndefinedOrNull())
    return true;

  DummyExceptionStateForTesting exception_state;
  const Vector<String>& names =
      constraints_dictionary.GetPropertyNames(exception_state);
  if (exception_state.HadException())
    return false;

  String mandatory_name("mandatory");
  String optional_name("optional");

  for (const auto& name : names) {
    if (name != mandatory_name && name != optional_name)
      return false;
  }

  if (names.Contains(mandatory_name)) {
    Dictionary mandatory_constraints_dictionary;
    bool ok = constraints_dictionary.Get(mandatory_name,
                                         mandatory_constraints_dictionary);
    if (!ok || mandatory_constraints_dictionary.IsUndefinedOrNull())
      return false;
    ok = ParseMandatoryConstraintsDictionary(mandatory_constraints_dictionary,
                                             mandatory);
    if (!ok)
      return false;
  }

  if (names.Contains(optional_name)) {
    ArrayValue optional_constraints;
    bool ok = DictionaryHelper::Get(constraints_dictionary, optional_name,
                                    optional_constraints);
    if (!ok || optional_constraints.IsUndefinedOrNull())
      return false;

    uint32_t number_of_constraints;
    ok = optional_constraints.length(number_of_constraints);
    if (!ok)
      return false;

    for (uint32_t i = 0; i < number_of_constraints; ++i) {
      Dictionary constraint;
      ok = optional_constraints.Get(i, constraint);
      if (!ok || constraint.IsUndefinedOrNull())
        return false;
      ok = ParseOptionalConstraintsVectorElement(constraint, optional);
      if (!ok)
        return false;
    }
  }

  return true;
}

static bool Parse(const MediaTrackConstraints* constraints_in,
                  Vector<NameValueStringConstraint>& optional,
                  Vector<NameValueStringConstraint>& mandatory) {
  Vector<NameValueStringConstraint> mandatory_constraints_vector;
  if (constraints_in->hasMandatory()) {
    bool ok = ParseMandatoryConstraintsDictionary(
        Dictionary(constraints_in->mandatory()), mandatory);
    if (!ok)
      return false;
  }

  if (constraints_in->hasOptional()) {
    for (const auto& constraint : constraints_in->optional()) {
      bool ok = ParseOptionalConstraintsVectorElement(Dictionary(constraint),
                                                      optional);
      if (!ok)
        return false;
    }
  }
  return true;
}

static bool ToBoolean(const WebString& as_web_string) {
  return as_web_string.Equals("true");
  // TODO(hta): Check against "false" and return error if it's neither.
  // https://crbug.com/576582
}

static void ParseOldStyleNames(
    ExecutionContext* context,
    const Vector<NameValueStringConstraint>& old_names,
    bool report_unknown_names,
    MediaTrackConstraintSetPlatform& result,
    MediaErrorState& error_state) {
  for (const NameValueStringConstraint& constraint : old_names) {
    if (constraint.name_.Equals(kMinAspectRatio)) {
      result.aspect_ratio.SetMin(atof(constraint.value_.Utf8().c_str()));
    } else if (constraint.name_.Equals(kMaxAspectRatio)) {
      result.aspect_ratio.SetMax(atof(constraint.value_.Utf8().c_str()));
    } else if (constraint.name_.Equals(kMaxWidth)) {
      result.width.SetMax(atoi(constraint.value_.Utf8().c_str()));
    } else if (constraint.name_.Equals(kMinWidth)) {
      result.width.SetMin(atoi(constraint.value_.Utf8().c_str()));
    } else if (constraint.name_.Equals(kMaxHeight)) {
      result.height.SetMax(atoi(constraint.value_.Utf8().c_str()));
    } else if (constraint.name_.Equals(kMinHeight)) {
      result.height.SetMin(atoi(constraint.value_.Utf8().c_str()));
    } else if (constraint.name_.Equals(kMinFrameRate)) {
      result.frame_rate.SetMin(atof(constraint.value_.Utf8().c_str()));
    } else if (constraint.name_.Equals(kMaxFrameRate)) {
      result.frame_rate.SetMax(atof(constraint.value_.Utf8().c_str()));
    } else if (constraint.name_.Equals(kEchoCancellation)) {
      result.echo_cancellation.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kMediaStreamSource)) {
      // TODO(hta): This has only a few legal values. Should be
      // represented as an enum, and cause type errors.
      // https://crbug.com/576582
      result.media_stream_source.SetExact(constraint.value_);
    } else if (constraint.name_.Equals(kDisableLocalEcho) &&
               RuntimeEnabledFeatures::
                   DesktopCaptureDisableLocalEchoControlEnabled()) {
      result.disable_local_echo.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kMediaStreamSourceId) ||
               constraint.name_.Equals(kMediaStreamSourceInfoId)) {
      result.device_id.SetExact(constraint.value_);
    } else if (constraint.name_.Equals(kMediaStreamRenderToAssociatedSink)) {
      // TODO(hta): This is a boolean represented as string.
      // Should give TypeError when it's not parseable.
      // https://crbug.com/576582
      result.render_to_associated_sink.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kGoogEchoCancellation)) {
      result.goog_echo_cancellation.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kGoogExperimentalEchoCancellation)) {
      result.goog_experimental_echo_cancellation.SetExact(
          ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kGoogAutoGainControl)) {
      result.goog_auto_gain_control.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kGoogExperimentalAutoGainControl)) {
      result.goog_experimental_auto_gain_control.SetExact(
          ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kGoogNoiseSuppression)) {
      result.goog_noise_suppression.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kGoogExperimentalNoiseSuppression)) {
      result.goog_experimental_noise_suppression.SetExact(
          ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kGoogHighpassFilter)) {
      result.goog_highpass_filter.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kGoogAudioMirroring)) {
      result.goog_audio_mirroring.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kDAEchoCancellation)) {
      result.goog_da_echo_cancellation.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kNoiseReduction)) {
      result.goog_noise_reduction.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kOfferToReceiveAudio)) {
      // This constraint has formerly been defined both as a boolean
      // and as an integer. Allow both forms.
      if (constraint.value_.Equals("true"))
        result.offer_to_receive_audio.SetExact(1);
      else if (constraint.value_.Equals("false"))
        result.offer_to_receive_audio.SetExact(0);
      else
        result.offer_to_receive_audio.SetExact(
            atoi(constraint.value_.Utf8().c_str()));
    } else if (constraint.name_.Equals(kOfferToReceiveVideo)) {
      // This constraint has formerly been defined both as a boolean
      // and as an integer. Allow both forms.
      if (constraint.value_.Equals("true"))
        result.offer_to_receive_video.SetExact(1);
      else if (constraint.value_.Equals("false"))
        result.offer_to_receive_video.SetExact(0);
      else
        result.offer_to_receive_video.SetExact(
            atoi(constraint.value_.Utf8().c_str()));
    } else if (constraint.name_.Equals(kVoiceActivityDetection)) {
      result.voice_activity_detection.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kIceRestart)) {
      result.ice_restart.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kUseRtpMux)) {
      result.goog_use_rtp_mux.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kEnableDtlsSrtp)) {
      bool value = ToBoolean(constraint.value_);
      if (value) {
        Deprecation::CountDeprecation(
            context, WebFeature::kRTCConstraintEnableDtlsSrtpTrue);
      } else {
        Deprecation::CountDeprecation(
            context, WebFeature::kRTCConstraintEnableDtlsSrtpFalse);
      }
#if BUILDFLAG(IS_FUCHSIA)
      // Special dispensation for Fuchsia to run SDES in 2002
      // TODO(crbug.com/804275): Delete when Fuchsia no longer depends on it.
      result.enable_dtls_srtp.SetExact(ToBoolean(constraint.value_));
#endif
    } else if (constraint.name_.Equals(kEnableRtpDataChannels)) {
      // This constraint does not turn on RTP data channels, but we do not
      // want it to cause an error, so we parse it and ignore it.
      bool value = ToBoolean(constraint.value_);
      if (value) {
        Deprecation::CountDeprecation(
            context, WebFeature::kRTCConstraintEnableRtpDataChannelsTrue);
      } else {
        Deprecation::CountDeprecation(
            context, WebFeature::kRTCConstraintEnableRtpDataChannelsFalse);
      }
    } else if (constraint.name_.Equals(kEnableDscp)) {
      result.enable_dscp.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kEnableIPv6)) {
      result.enable_i_pv6.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kEnableVideoSuspendBelowMinBitrate)) {
      result.goog_enable_video_suspend_below_min_bitrate.SetExact(
          ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kNumUnsignalledRecvStreams)) {
      result.goog_num_unsignalled_recv_streams.SetExact(
          atoi(constraint.value_.Utf8().c_str()));
    } else if (constraint.name_.Equals(kCombinedAudioVideoBwe)) {
      result.goog_combined_audio_video_bwe.SetExact(
          ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kScreencastMinBitrate)) {
      result.goog_screencast_min_bitrate.SetExact(
          atoi(constraint.value_.Utf8().c_str()));
    } else if (constraint.name_.Equals(kCpuOveruseDetection)) {
      result.goog_cpu_overuse_detection.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kHighStartBitrate)) {
      result.goog_high_start_bitrate.SetExact(
          atoi(constraint.value_.Utf8().c_str()));
    } else if (constraint.name_.Equals(kPayloadPadding)) {
      result.goog_payload_padding.SetExact(ToBoolean(constraint.value_));
    } else if (constraint.name_.Equals(kAudioLatency)) {
      result.goog_latency_ms.SetExact(atoi(constraint.value_.Utf8().c_str()));
    } else if (constraint.name_.Equals(kCpuUnderuseThreshold) ||
               constraint.name_.Equals(kCpuOveruseThreshold) ||
               constraint.name_.Equals(kCpuUnderuseEncodeRsdThreshold) ||
               constraint.name_.Equals(kCpuOveruseEncodeRsdThreshold) ||
               constraint.name_.Equals(kCpuOveruseEncodeUsage) ||
               constraint.name_.Equals(kGoogLeakyBucket) ||
               constraint.name_.Equals(kGoogBeamforming) ||
               constraint.name_.Equals(kGoogArrayGeometry) ||
               constraint.name_.Equals(kPowerLineFrequency) ||
               constraint.name_.Equals(kMediaStreamAudioHotword) ||
               constraint.name_.Equals(kGoogTypingNoiseDetection)) {
      // TODO(crbug.com/856176): Remove the kGoogBeamforming and
      // kGoogArrayGeometry special cases.
      context->AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
          mojom::ConsoleMessageSource::kDeprecation,
          mojom::ConsoleMessageLevel::kWarning,
          "Obsolete constraint named " + String(constraint.name_) +
              " is ignored. Please stop using it."));
    } else if (constraint.name_.Equals(kVideoKind)) {
      if (!constraint.value_.Equals(kVideoKindColor) &&
          !constraint.value_.Equals(kVideoKindDepth)) {
        error_state.ThrowConstraintError("Illegal value for constraint",
                                         constraint.name_);
      } else {
        result.video_kind.SetExact(constraint.value_);
      }
    } else if (constraint.name_.Equals(kTestConstraint1) ||
               constraint.name_.Equals(kTestConstraint2)) {
      // These constraints are only for testing parsing.
      // Values 0 and 1 are legal, all others are a ConstraintError.
      if (!constraint.value_.Equals("0") && !constraint.value_.Equals("1")) {
        error_state.ThrowConstraintError("Illegal value for constraint",
                                         constraint.name_);
      }
    } else {
      if (report_unknown_names) {
        // TODO(hta): UMA stats for unknown constraints passed.
        // https://crbug.com/576613
        context->AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
            mojom::ConsoleMessageSource::kDeprecation,
            mojom::ConsoleMessageLevel::kWarning,
            "Unknown constraint named " + String(constraint.name_) +
                " rejected"));
        // TODO(crbug.com/856176): Don't throw an error.
        error_state.ThrowConstraintError("Unknown name of constraint detected",
                                         constraint.name_);
      }
    }
  }
}

static MediaConstraints CreateFromNamedConstraints(
    ExecutionContext* context,
    Vector<NameValueStringConstraint>& mandatory,
    const Vector<NameValueStringConstraint>& optional,
    MediaErrorState& error_state) {
  MediaTrackConstraintSetPlatform basic;
  MediaTrackConstraintSetPlatform advanced;
  MediaConstraints constraints;
  ParseOldStyleNames(context, mandatory, true, basic, error_state);
  if (error_state.HadException())
    return constraints;
  // We ignore unknow names and syntax errors in optional constraints.
  MediaErrorState ignored_error_state;
  Vector<MediaTrackConstraintSetPlatform> advanced_vector;
  for (const auto& optional_constraint : optional) {
    MediaTrackConstraintSetPlatform advanced_element;
    Vector<NameValueStringConstraint> element_as_list(1, optional_constraint);
    ParseOldStyleNames(context, element_as_list, false, advanced_element,
                       ignored_error_state);
    if (!advanced_element.IsUnconstrained())
      advanced_vector.push_back(advanced_element);
  }
  constraints.Initialize(basic, advanced_vector);
  return constraints;
}

// Deprecated.
MediaConstraints Create(ExecutionContext* context,
                        const Dictionary& constraints_dictionary,
                        MediaErrorState& error_state) {
  Vector<NameValueStringConstraint> optional;
  Vector<NameValueStringConstraint> mandatory;
  if (!Parse(constraints_dictionary, optional, mandatory)) {
    error_state.ThrowTypeError("Malformed constraints object.");
    return MediaConstraints();
  }
  UseCounter::Count(context, WebFeature::kMediaStreamConstraintsFromDictionary);
  return CreateFromNamedConstraints(context, mandatory, optional, error_state);
}

void CopyLongConstraint(const V8ConstrainLong* blink_union_form,
                        NakedValueDisposition naked_treatment,
                        LongConstraint& web_form) {
  web_form.SetIsPresent(true);
  switch (blink_union_form->GetContentType()) {
    case V8ConstrainLong::ContentType::kConstrainLongRange: {
      const auto* blink_form = blink_union_form->GetAsConstrainLongRange();
      if (blink_form->hasMin()) {
        web_form.SetMin(blink_form->min());
      }
      if (blink_form->hasMax()) {
        web_form.SetMax(blink_form->max());
      }
      if (blink_form->hasIdeal()) {
        web_form.SetIdeal(blink_form->ideal());
      }
      if (blink_form->hasExact()) {
        web_form.SetExact(blink_form->exact());
      }
      break;
    }
    case V8ConstrainLong::ContentType::kLong:
      switch (naked_treatment) {
        case NakedValueDisposition::kTreatAsIdeal:
          web_form.SetIdeal(blink_union_form->GetAsLong());
          break;
        case NakedValueDisposition::kTreatAsExact:
          web_form.SetExact(blink_union_form->GetAsLong());
          break;
      }
      break;
  }
}

void CopyDoubleConstraint(const V8ConstrainDouble* blink_union_form,
                          NakedValueDisposition naked_treatment,
                          DoubleConstraint& web_form) {
  web_form.SetIsPresent(true);
  switch (blink_union_form->GetContentType()) {
    case V8ConstrainDouble::ContentType::kConstrainDoubleRange: {
      const auto* blink_form = blink_union_form->GetAsConstrainDoubleRange();
      if (blink_form->hasMin()) {
        web_form.SetMin(blink_form->min());
      }
      if (blink_form->hasMax()) {
        web_form.SetMax(blink_form->max());
      }
      if (blink_form->hasIdeal()) {
        web_form.SetIdeal(blink_form->ideal());
      }
      if (blink_form->hasExact()) {
        web_form.SetExact(blink_form->exact());
      }
      break;
    }
    case V8ConstrainDouble::ContentType::kDouble:
      switch (naked_treatment) {
        case NakedValueDisposition::kTreatAsIdeal:
          web_form.SetIdeal(blink_union_form->GetAsDouble());
          break;
        case NakedValueDisposition::kTreatAsExact:
          web_form.SetExact(blink_union_form->GetAsDouble());
          break;
      }
      break;
  }
}

void CopyBooleanOrDoubleConstraint(
    const V8UnionBooleanOrConstrainDouble* blink_union_form,
    NakedValueDisposition naked_treatment,
    DoubleConstraint& web_form) {
  switch (blink_union_form->GetContentType()) {
    case V8UnionBooleanOrConstrainDouble::ContentType::kBoolean:
      web_form.SetIsPresent(blink_union_form->GetAsBoolean());
      break;
    case V8UnionBooleanOrConstrainDouble::ContentType::kConstrainDoubleRange:
    case V8UnionBooleanOrConstrainDouble::ContentType::kDouble:
      CopyDoubleConstraint(blink_union_form->GetAsV8ConstrainDouble(),
                           naked_treatment, web_form);
      break;
  }
}

bool ValidateString(const String& str, MediaErrorState& error_state) {
  DCHECK(!error_state.HadException());

  if (str.length() > kMaxConstraintStringLength) {
    error_state.ThrowTypeError("Constraint string too long.");
    return false;
  }
  return true;
}

bool ValidateStringSeq(const Vector<String>& strs,
                       MediaErrorState& error_state) {
  DCHECK(!error_state.HadException());

  if (strs.size() > kMaxConstraintStringSeqLength) {
    error_state.ThrowTypeError("Constraint string sequence too long.");
    return false;
  }

  for (const String& str : strs) {
    if (!ValidateString(str, error_state)) {
      DCHECK(error_state.HadException());
      return false;
    }
  }

  return true;
}

bool ValidateStringConstraint(
    V8UnionStringOrStringSequence* string_or_string_seq,
    MediaErrorState& error_state) {
  DCHECK(!error_state.HadException());

  switch (string_or_string_seq->GetContentType()) {
    case V8UnionStringOrStringSequence::ContentType::kString: {
      return ValidateString(string_or_string_seq->GetAsString(), error_state);
    }
    case V8UnionStringOrStringSequence::ContentType::kStringSequence: {
      return ValidateStringSeq(string_or_string_seq->GetAsStringSequence(),
                               error_state);
    }
  }
  NOTREACHED();
  return false;
}

bool ValidateStringConstraint(const V8ConstrainDOMString* blink_union_form,
                              MediaErrorState& error_state) {
  DCHECK(!error_state.HadException());

  switch (blink_union_form->GetContentType()) {
    case V8ConstrainDOMString::ContentType::kConstrainDOMStringParameters: {
      const auto* blink_form =
          blink_union_form->GetAsConstrainDOMStringParameters();
      if (blink_form->hasIdeal() &&
          !ValidateStringConstraint(blink_form->ideal(), error_state)) {
        return false;
      }
      if (blink_form->hasExact() &&
          !ValidateStringConstraint(blink_form->exact(), error_state)) {
        return false;
      }
      return true;
    }
    case V8ConstrainDOMString::ContentType::kString:
      return ValidateString(blink_union_form->GetAsString(), error_state);
    case V8ConstrainDOMString::ContentType::kStringSequence:
      return ValidateStringSeq(blink_union_form->GetAsStringSequence(),
                               error_state);
  }
  NOTREACHED();
  return false;
}

WARN_UNUSED_RESULT bool ValidateAndCopyStringConstraint(
    const V8ConstrainDOMString* blink_union_form,
    NakedValueDisposition naked_treatment,
    StringConstraint& web_form,
    MediaErrorState& error_state) {
  DCHECK(!error_state.HadException());

  if (!ValidateStringConstraint(blink_union_form, error_state)) {
    return false;
  }
  web_form.SetIsPresent(true);
  switch (blink_union_form->GetContentType()) {
    case V8ConstrainDOMString::ContentType::kConstrainDOMStringParameters: {
      const auto* blink_form =
          blink_union_form->GetAsConstrainDOMStringParameters();
      if (blink_form->hasIdeal()) {
        switch (blink_form->ideal()->GetContentType()) {
          case V8UnionStringOrStringSequence::ContentType::kString:
            web_form.SetIdeal(
                Vector<String>(1, blink_form->ideal()->GetAsString()));
            break;
          case V8UnionStringOrStringSequence::ContentType::kStringSequence:
            web_form.SetIdeal(blink_form->ideal()->GetAsStringSequence());
            break;
        }
      }
      if (blink_form->hasExact()) {
        switch (blink_form->exact()->GetContentType()) {
          case V8UnionStringOrStringSequence::ContentType::kString:
            web_form.SetExact(
                Vector<String>(1, blink_form->exact()->GetAsString()));
            break;
          case V8UnionStringOrStringSequence::ContentType::kStringSequence:
            web_form.SetExact(blink_form->exact()->GetAsStringSequence());
            break;
        }
      }
      break;
    }
    case V8ConstrainDOMString::ContentType::kString:
      switch (naked_treatment) {
        case NakedValueDisposition::kTreatAsIdeal:
          web_form.SetIdeal(Vector<String>(1, blink_union_form->GetAsString()));
          break;
        case NakedValueDisposition::kTreatAsExact:
          web_form.SetExact(Vector<String>(1, blink_union_form->GetAsString()));
          break;
      }
      break;
    case V8ConstrainDOMString::ContentType::kStringSequence:
      switch (naked_treatment) {
        case NakedValueDisposition::kTreatAsIdeal:
          web_form.SetIdeal(blink_union_form->GetAsStringSequence());
          break;
        case NakedValueDisposition::kTreatAsExact:
          web_form.SetExact(blink_union_form->GetAsStringSequence());
          break;
      }
      break;
  }
  return true;
}

void CopyBooleanConstraint(const V8ConstrainBoolean* blink_union_form,
                           NakedValueDisposition naked_treatment,
                           BooleanConstraint& web_form) {
  web_form.SetIsPresent(true);
  switch (blink_union_form->GetContentType()) {
    case V8ConstrainBoolean::ContentType::kBoolean:
      switch (naked_treatment) {
        case NakedValueDisposition::kTreatAsIdeal:
          web_form.SetIdeal(blink_union_form->GetAsBoolean());
          break;
        case NakedValueDisposition::kTreatAsExact:
          web_form.SetExact(blink_union_form->GetAsBoolean());
          break;
      }
      break;
    case V8ConstrainBoolean::ContentType::kConstrainBooleanParameters: {
      const auto* blink_form =
          blink_union_form->GetAsConstrainBooleanParameters();
      if (blink_form->hasIdeal()) {
        web_form.SetIdeal(blink_form->ideal());
      }
      if (blink_form->hasExact()) {
        web_form.SetExact(blink_form->exact());
      }
      break;
    }
  }
}

bool ValidateAndCopyConstraintSet(
    const MediaTrackConstraintSet* constraints_in,
    NakedValueDisposition naked_treatment,
    MediaTrackConstraintSetPlatform& constraint_buffer,
    MediaErrorState& error_state) {
  DCHECK(!error_state.HadException());

  if (constraints_in->hasWidth()) {
    CopyLongConstraint(constraints_in->width(), naked_treatment,
                       constraint_buffer.width);
  }
  if (constraints_in->hasHeight()) {
    CopyLongConstraint(constraints_in->height(), naked_treatment,
                       constraint_buffer.height);
  }
  if (constraints_in->hasAspectRatio()) {
    CopyDoubleConstraint(constraints_in->aspectRatio(), naked_treatment,
                         constraint_buffer.aspect_ratio);
  }
  if (constraints_in->hasFrameRate()) {
    CopyDoubleConstraint(constraints_in->frameRate(), naked_treatment,
                         constraint_buffer.frame_rate);
  }
  if (constraints_in->hasFacingMode()) {
    if (!ValidateAndCopyStringConstraint(
            constraints_in->facingMode(), naked_treatment,
            constraint_buffer.facing_mode, error_state)) {
      DCHECK(error_state.HadException());
      return false;
    }
  }
  if (constraints_in->hasResizeMode()) {
    if (!ValidateAndCopyStringConstraint(
            constraints_in->resizeMode(), naked_treatment,
            constraint_buffer.resize_mode, error_state)) {
      DCHECK(error_state.HadException());
      return false;
    }
  }
  if (constraints_in->hasSampleRate()) {
    CopyLongConstraint(constraints_in->sampleRate(), naked_treatment,
                       constraint_buffer.sample_rate);
  }
  if (constraints_in->hasSampleSize()) {
    CopyLongConstraint(constraints_in->sampleSize(), naked_treatment,
                       constraint_buffer.sample_size);
  }
  if (constraints_in->hasEchoCancellation()) {
    CopyBooleanConstraint(constraints_in->echoCancellation(), naked_treatment,
                          constraint_buffer.echo_cancellation);
  }
  if (constraints_in->hasAutoGainControl()) {
    CopyBooleanConstraint(constraints_in->autoGainControl(), naked_treatment,
                          constraint_buffer.goog_auto_gain_control);
  }
  if (constraints_in->hasNoiseSuppression()) {
    CopyBooleanConstraint(constraints_in->noiseSuppression(), naked_treatment,
                          constraint_buffer.goog_noise_suppression);
  }
  if (constraints_in->hasLatency()) {
    CopyDoubleConstraint(constraints_in->latency(), naked_treatment,
                         constraint_buffer.latency);
  }
  if (constraints_in->hasChannelCount()) {
    CopyLongConstraint(constraints_in->channelCount(), naked_treatment,
                       constraint_buffer.channel_count);
  }
  if (constraints_in->hasDeviceId()) {
    if (!ValidateAndCopyStringConstraint(
            constraints_in->deviceId(), naked_treatment,
            constraint_buffer.device_id, error_state)) {
      DCHECK(error_state.HadException());
      return false;
    }
  }
  if (constraints_in->hasGroupId()) {
    if (!ValidateAndCopyStringConstraint(
            constraints_in->groupId(), naked_treatment,
            constraint_buffer.group_id, error_state)) {
      DCHECK(error_state.HadException());
      return false;
    }
  }
  if (constraints_in->hasVideoKind()) {
    if (!ValidateAndCopyStringConstraint(
            constraints_in->videoKind(), naked_treatment,
            constraint_buffer.video_kind, error_state)) {
      DCHECK(error_state.HadException());
      return false;
    }
  }
  if (constraints_in->hasPan()) {
    CopyBooleanOrDoubleConstraint(constraints_in->pan(), naked_treatment,
                                  constraint_buffer.pan);
  }
  if (constraints_in->hasTilt()) {
    CopyBooleanOrDoubleConstraint(constraints_in->tilt(), naked_treatment,
                                  constraint_buffer.tilt);
  }
  if (constraints_in->hasZoom()) {
    CopyBooleanOrDoubleConstraint(constraints_in->zoom(), naked_treatment,
                                  constraint_buffer.zoom);
  }
  return true;
}

MediaConstraints ConvertTrackConstraintsToMediaConstraints(
    const MediaTrackConstraints* constraints_in,
    MediaErrorState& error_state) {
  MediaConstraints constraints;
  MediaTrackConstraintSetPlatform constraint_buffer;
  Vector<MediaTrackConstraintSetPlatform> advanced_buffer;
  if (!ValidateAndCopyConstraintSet(constraints_in,
                                    NakedValueDisposition::kTreatAsIdeal,
                                    constraint_buffer, error_state)) {
    DCHECK(error_state.HadException());
    return constraints;
  }
  if (constraints_in->hasAdvanced()) {
    for (const auto& element : constraints_in->advanced()) {
      MediaTrackConstraintSetPlatform advanced_element;
      if (!ValidateAndCopyConstraintSet(element,
                                        NakedValueDisposition::kTreatAsExact,
                                        advanced_element, error_state)) {
        DCHECK(error_state.HadException());
        return constraints;
      }
      advanced_buffer.push_back(advanced_element);
    }
  }
  constraints.Initialize(constraint_buffer, advanced_buffer);
  return constraints;
}

MediaConstraints Create(ExecutionContext* context,
                        const MediaTrackConstraints* constraints_in,
                        MediaErrorState& error_state) {
  MediaConstraints standard_form =
      ConvertTrackConstraintsToMediaConstraints(constraints_in, error_state);
  if (error_state.HadException()) {
    return standard_form;
  }
  if (constraints_in->hasOptional() || constraints_in->hasMandatory()) {
    if (!standard_form.IsUnconstrained()) {
      UseCounter::Count(context, WebFeature::kMediaStreamConstraintsOldAndNew);
      error_state.ThrowTypeError(
          "Malformed constraint: Cannot use both optional/mandatory and "
          "specific or advanced constraints.");
      return MediaConstraints();
    }
    Vector<NameValueStringConstraint> optional;
    Vector<NameValueStringConstraint> mandatory;
    if (!Parse(constraints_in, optional, mandatory)) {
      error_state.ThrowTypeError("Malformed constraints object.");
      return MediaConstraints();
    }
    UseCounter::Count(context, WebFeature::kMediaStreamConstraintsNameValue);
    return CreateFromNamedConstraints(context, mandatory, optional,
                                      error_state);
  }
  UseCounter::Count(context, WebFeature::kMediaStreamConstraintsConformant);
  return standard_form;
}

MediaConstraints Create() {
  MediaConstraints constraints;
  constraints.Initialize();
  return constraints;
}

template <class T>
bool UseNakedNumeric(T input, NakedValueDisposition which) {
  switch (which) {
    case NakedValueDisposition::kTreatAsIdeal:
      return input.HasIdeal() &&
             !(input.HasExact() || input.HasMin() || input.HasMax());
      break;
    case NakedValueDisposition::kTreatAsExact:
      return input.HasExact() &&
             !(input.HasIdeal() || input.HasMin() || input.HasMax());
      break;
  }
  NOTREACHED();
  return false;
}

template <class T>
bool UseNakedNonNumeric(T input, NakedValueDisposition which) {
  switch (which) {
    case NakedValueDisposition::kTreatAsIdeal:
      return input.HasIdeal() && !input.HasExact();
      break;
    case NakedValueDisposition::kTreatAsExact:
      return input.HasExact() && !input.HasIdeal();
      break;
  }
  NOTREACHED();
  return false;
}

template <typename U, class T>
U GetNakedValue(T input, NakedValueDisposition which) {
  switch (which) {
    case NakedValueDisposition::kTreatAsIdeal:
      return input.Ideal();
      break;
    case NakedValueDisposition::kTreatAsExact:
      return input.Exact();
      break;
  }
  NOTREACHED();
  return input.Exact();
}

V8ConstrainLong* ConvertLong(const LongConstraint& input,
                             NakedValueDisposition naked_treatment) {
  if (UseNakedNumeric(input, naked_treatment)) {
    return MakeGarbageCollected<V8ConstrainLong>(
        GetNakedValue<uint32_t>(input, naked_treatment));
  } else if (!input.IsUnconstrained()) {
    ConstrainLongRange* output = ConstrainLongRange::Create();
    if (input.HasExact())
      output->setExact(input.Exact());
    if (input.HasMin())
      output->setMin(input.Min());
    if (input.HasMax())
      output->setMax(input.Max());
    if (input.HasIdeal())
      output->setIdeal(input.Ideal());
    return MakeGarbageCollected<V8ConstrainLong>(output);
  }
  return nullptr;
}

V8ConstrainDouble* ConvertDouble(const DoubleConstraint& input,
                                 NakedValueDisposition naked_treatment) {
  if (UseNakedNumeric(input, naked_treatment)) {
    return MakeGarbageCollected<V8ConstrainDouble>(
        GetNakedValue<double>(input, naked_treatment));
  } else if (!input.IsUnconstrained()) {
    ConstrainDoubleRange* output = ConstrainDoubleRange::Create();
    if (input.HasExact())
      output->setExact(input.Exact());
    if (input.HasIdeal())
      output->setIdeal(input.Ideal());
    if (input.HasMin())
      output->setMin(input.Min());
    if (input.HasMax())
      output->setMax(input.Max());
    return MakeGarbageCollected<V8ConstrainDouble>(output);
  }
  return nullptr;
}

V8UnionBooleanOrConstrainDouble* ConvertBooleanOrDouble(
    const DoubleConstraint& input,
    NakedValueDisposition naked_treatment) {
  if (UseNakedNumeric(input, naked_treatment)) {
    return MakeGarbageCollected<V8UnionBooleanOrConstrainDouble>(
        GetNakedValue<double>(input, naked_treatment));
  } else if (!input.IsUnconstrained()) {
    ConstrainDoubleRange* output = ConstrainDoubleRange::Create();
    if (input.HasExact())
      output->setExact(input.Exact());
    if (input.HasIdeal())
      output->setIdeal(input.Ideal());
    if (input.HasMin())
      output->setMin(input.Min());
    if (input.HasMax())
      output->setMax(input.Max());
    return MakeGarbageCollected<V8UnionBooleanOrConstrainDouble>(output);
  }
  return nullptr;
}

V8UnionStringOrStringSequence* ConvertStringSequence(
    const WebVector<WebString>& input) {
  if (input.size() > 1) {
    Vector<String> buffer;
    for (const auto& scanner : input)
      buffer.push_back(scanner);
    return MakeGarbageCollected<V8UnionStringOrStringSequence>(
        std::move(buffer));
  } else if (!input.empty()) {
    return MakeGarbageCollected<V8UnionStringOrStringSequence>(input[0]);
  }
  return nullptr;
}

V8ConstrainDOMString* ConvertString(const StringConstraint& input,
                                    NakedValueDisposition naked_treatment) {
  if (UseNakedNonNumeric(input, naked_treatment)) {
    WebVector<WebString> input_buffer(
        GetNakedValue<WebVector<WebString>>(input, naked_treatment));
    if (input_buffer.size() > 1) {
      Vector<String> buffer;
      for (const auto& scanner : input_buffer)
        buffer.push_back(scanner);
      return MakeGarbageCollected<V8ConstrainDOMString>(std::move(buffer));
    } else if (!input_buffer.empty()) {
      return MakeGarbageCollected<V8ConstrainDOMString>(input_buffer[0]);
    }
    return nullptr;
  } else if (!input.IsUnconstrained()) {
    ConstrainDOMStringParameters* output =
        ConstrainDOMStringParameters::Create();
    if (input.HasExact())
      output->setExact(ConvertStringSequence(input.Exact()));
    if (input.HasIdeal())
      output->setIdeal(ConvertStringSequence(input.Ideal()));
    return MakeGarbageCollected<V8ConstrainDOMString>(output);
  }
  return nullptr;
}

V8ConstrainBoolean* ConvertBoolean(const BooleanConstraint& input,
                                   NakedValueDisposition naked_treatment) {
  if (UseNakedNonNumeric(input, naked_treatment)) {
    return MakeGarbageCollected<V8ConstrainBoolean>(
        GetNakedValue<bool>(input, naked_treatment));
  } else if (!input.IsUnconstrained()) {
    ConstrainBooleanParameters* output = ConstrainBooleanParameters::Create();
    if (input.HasExact())
      output->setExact(input.Exact());
    if (input.HasIdeal())
      output->setIdeal(input.Ideal());
    return MakeGarbageCollected<V8ConstrainBoolean>(output);
  }
  return nullptr;
}

void ConvertConstraintSet(const MediaTrackConstraintSetPlatform& input,
                          NakedValueDisposition naked_treatment,
                          MediaTrackConstraintSet* output) {
  if (!input.width.IsUnconstrained())
    output->setWidth(ConvertLong(input.width, naked_treatment));
  if (!input.height.IsUnconstrained())
    output->setHeight(ConvertLong(input.height, naked_treatment));
  if (!input.aspect_ratio.IsUnconstrained())
    output->setAspectRatio(ConvertDouble(input.aspect_ratio, naked_treatment));
  if (!input.frame_rate.IsUnconstrained())
    output->setFrameRate(ConvertDouble(input.frame_rate, naked_treatment));
  if (!input.facing_mode.IsUnconstrained())
    output->setFacingMode(ConvertString(input.facing_mode, naked_treatment));
  if (!input.resize_mode.IsUnconstrained())
    output->setResizeMode(ConvertString(input.resize_mode, naked_treatment));
  if (!input.sample_rate.IsUnconstrained())
    output->setSampleRate(ConvertLong(input.sample_rate, naked_treatment));
  if (!input.sample_size.IsUnconstrained())
    output->setSampleSize(ConvertLong(input.sample_size, naked_treatment));
  if (!input.echo_cancellation.IsUnconstrained()) {
    output->setEchoCancellation(
        ConvertBoolean(input.echo_cancellation, naked_treatment));
  }
  if (!input.goog_auto_gain_control.IsUnconstrained()) {
    output->setAutoGainControl(
        ConvertBoolean(input.goog_auto_gain_control, naked_treatment));
  }
  if (!input.goog_noise_suppression.IsUnconstrained()) {
    output->setNoiseSuppression(
        ConvertBoolean(input.goog_noise_suppression, naked_treatment));
  }
  if (!input.latency.IsUnconstrained())
    output->setLatency(ConvertDouble(input.latency, naked_treatment));
  if (!input.channel_count.IsUnconstrained())
    output->setChannelCount(ConvertLong(input.channel_count, naked_treatment));
  if (!input.device_id.IsUnconstrained())
    output->setDeviceId(ConvertString(input.device_id, naked_treatment));
  if (!input.group_id.IsUnconstrained())
    output->setGroupId(ConvertString(input.group_id, naked_treatment));
  if (!input.video_kind.IsUnconstrained())
    output->setVideoKind(ConvertString(input.video_kind, naked_treatment));
  if (!input.pan.IsUnconstrained())
    output->setPan(ConvertBooleanOrDouble(input.pan, naked_treatment));
  if (!input.tilt.IsUnconstrained())
    output->setTilt(ConvertBooleanOrDouble(input.tilt, naked_treatment));
  if (!input.zoom.IsUnconstrained())
    output->setZoom(ConvertBooleanOrDouble(input.zoom, naked_treatment));
  // TODO(hta): Decide the future of the nonstandard constraints.
  // If they go forward, they need to be added here.
  // https://crbug.com/605673
}

MediaTrackConstraints* ConvertConstraints(const MediaConstraints& input) {
  MediaTrackConstraints* output = MediaTrackConstraints::Create();
  if (input.IsNull())
    return output;
  ConvertConstraintSet(input.Basic(), NakedValueDisposition::kTreatAsIdeal,
                       output);

  HeapVector<Member<MediaTrackConstraintSet>> advanced_vector;
  for (const auto& it : input.Advanced()) {
    if (it.IsUnconstrained())
      continue;
    MediaTrackConstraintSet* element = MediaTrackConstraintSet::Create();
    ConvertConstraintSet(it, NakedValueDisposition::kTreatAsExact, element);
    advanced_vector.push_back(element);
  }
  if (!advanced_vector.IsEmpty())
    output->setAdvanced(advanced_vector);

  return output;
}

}  // namespace media_constraints_impl
}  // namespace blink