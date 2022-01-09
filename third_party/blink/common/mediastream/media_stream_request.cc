// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/public/common/mediastream/media_stream_request.h"

#include "base/check.h"
#include "build/build_config.h"

namespace blink {

bool IsAudioInputMediaType(mojom::MediaStreamType type) {
  return (type == mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE ||
          type == mojom::MediaStreamType::GUM_TAB_AUDIO_CAPTURE ||
          type == mojom::MediaStreamType::GUM_DESKTOP_AUDIO_CAPTURE ||
          type == mojom::MediaStreamType::DISPLAY_AUDIO_CAPTURE);
}

bool IsVideoInputMediaType(mojom::MediaStreamType type) {
  return (type == mojom::MediaStreamType::DEVICE_VIDEO_CAPTURE ||
          type == mojom::MediaStreamType::GUM_TAB_VIDEO_CAPTURE ||
          type == mojom::MediaStreamType::GUM_DESKTOP_VIDEO_CAPTURE ||
          type == mojom::MediaStreamType::DISPLAY_VIDEO_CAPTURE ||
          type == mojom::MediaStreamType::DISPLAY_VIDEO_CAPTURE_THIS_TAB);
}

bool IsScreenCaptureMediaType(mojom::MediaStreamType type) {
  return IsDesktopCaptureMediaType(type) || IsTabCaptureMediaType(type);
}

bool IsVideoScreenCaptureMediaType(mojom::MediaStreamType type) {
  return IsVideoDesktopCaptureMediaType(type) ||
         type == mojom::MediaStreamType::GUM_TAB_VIDEO_CAPTURE;
}

bool IsDesktopCaptureMediaType(mojom::MediaStreamType type) {
  return (type == mojom::MediaStreamType::GUM_DESKTOP_AUDIO_CAPTURE ||
          IsVideoDesktopCaptureMediaType(type));
}

bool IsVideoDesktopCaptureMediaType(mojom::MediaStreamType type) {
  return (type == mojom::MediaStreamType::DISPLAY_VIDEO_CAPTURE ||
          type == mojom::MediaStreamType::DISPLAY_VIDEO_CAPTURE_THIS_TAB ||
          type == mojom::MediaStreamType::GUM_DESKTOP_VIDEO_CAPTURE);
}

bool IsTabCaptureMediaType(mojom::MediaStreamType type) {
  return (type == mojom::MediaStreamType::GUM_TAB_AUDIO_CAPTURE ||
          type == mojom::MediaStreamType::GUM_TAB_VIDEO_CAPTURE ||
          type == mojom::MediaStreamType::DISPLAY_VIDEO_CAPTURE_THIS_TAB);
}

bool IsDeviceMediaType(mojom::MediaStreamType type) {
  return (type == mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE ||
          type == mojom::MediaStreamType::DEVICE_VIDEO_CAPTURE);
}

MediaStreamDevice::MediaStreamDevice()
    : type(mojom::MediaStreamType::NO_SERVICE),
      video_facing(media::MEDIA_VIDEO_FACING_NONE) {}

MediaStreamDevice::MediaStreamDevice(mojom::MediaStreamType type,
                                     const std::string& id,
                                     const std::string& name)
    : type(type),
      id(id),
      video_facing(media::MEDIA_VIDEO_FACING_NONE),
      name(name) {}

MediaStreamDevice::MediaStreamDevice(
    mojom::MediaStreamType type,
    const std::string& id,
    const std::string& name,
    const media::VideoCaptureControlSupport& control_support,
    media::VideoFacingMode facing,
    const absl::optional<std::string>& group_id)
    : type(type),
      id(id),
      video_control_support(control_support),
      video_facing(facing),
      group_id(group_id),
      name(name) {}

MediaStreamDevice::MediaStreamDevice(mojom::MediaStreamType type,
                                     const std::string& id,
                                     const std::string& name,
                                     int sample_rate,
                                     int channel_layout,
                                     int frames_per_buffer)
    : type(type),
      id(id),
      video_facing(media::MEDIA_VIDEO_FACING_NONE),
      name(name),
      input(media::AudioParameters::AUDIO_FAKE,
            static_cast<media::ChannelLayout>(channel_layout),
            sample_rate,
            frames_per_buffer) {
  DCHECK(input.IsValid());
}

MediaStreamDevice::MediaStreamDevice(const MediaStreamDevice& other)
    : type(other.type),
      id(other.id),
      video_control_support(other.video_control_support),
      video_facing(other.video_facing),
      group_id(other.group_id),
      matched_output_device_id(other.matched_output_device_id),
      name(other.name),
      input(other.input),
      session_id_(other.session_id_) {
  DCHECK(!session_id_.has_value() || !session_id_->is_empty());
  if (other.display_media_info.has_value())
    display_media_info = other.display_media_info->Clone();
}

MediaStreamDevice::~MediaStreamDevice() = default;

MediaStreamDevice& MediaStreamDevice::operator=(
    const MediaStreamDevice& other) {
  if (&other == this)
    return *this;
  type = other.type;
  id = other.id;
  video_control_support = other.video_control_support;
  video_facing = other.video_facing;
  group_id = other.group_id;
  matched_output_device_id = other.matched_output_device_id;
  name = other.name;
  input = other.input;
  session_id_ = other.session_id_;
  DCHECK(!session_id_.has_value() || !session_id_->is_empty());
  if (other.display_media_info.has_value())
    display_media_info = other.display_media_info->Clone();
  return *this;
}

bool MediaStreamDevice::IsSameDevice(
    const MediaStreamDevice& other_device) const {
  return type == other_device.type && name == other_device.name &&
         id == other_device.id &&
         input.sample_rate() == other_device.input.sample_rate() &&
         input.channel_layout() == other_device.input.channel_layout() &&
         session_id_ == other_device.session_id_;
}

}  // namespace blink
