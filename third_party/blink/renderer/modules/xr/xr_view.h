// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_XR_XR_VIEW_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_XR_XR_VIEW_H_

#include "device/vr/public/mojom/vr_service.mojom-blink.h"
#include "third_party/blink/renderer/core/typed_arrays/dom_typed_array.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/heap/handle.h"
#include "third_party/blink/renderer/platform/transforms/transformation_matrix.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "ui/gfx/geometry/point3_f.h"

#include "third_party/blink/renderer/modules/xr/xr_rigid_transform.h"

namespace blink {

class XRCamera;
class XRFrame;
class XRSession;
class XRViewData;

class MODULES_EXPORT XRView final : public ScriptWrappable {
  DEFINE_WRAPPERTYPEINFO();

 public:
  XRView(XRFrame*, XRViewData*, const TransformationMatrix&);

  const String& eye() const { return eye_string_; }
  device::mojom::blink::XREye EyeValue() const { return eye_; }
  XRViewData* ViewData() const { return view_data_; }

  XRFrame* frame() const;
  XRSession* session() const;
  DOMFloat32Array* projectionMatrix() const;
  XRRigidTransform* refSpaceFromView() const;
  XRCamera* camera() const;

  // isFirstPersonObserver is only true for views that composed with a video
  // feed that is not directly displayed on the viewer device. Primarily this is
  // used for video streams from optically transparent AR headsets. Since Chrome
  // does not directly support any such headset at this time we return false
  // unconditionally.
  bool isFirstPersonObserver() const { return false; }

  absl::optional<double> recommendedViewportScale() const;
  void requestViewportScale(absl::optional<double> scale);

  void Trace(Visitor*) const override;

 private:
  device::mojom::blink::XREye eye_;
  String eye_string_;
  Member<XRFrame> frame_;
  Member<XRViewData> view_data_;
  // The transform from the view to the reference space requested by
  // XRFrame::getViewerPose.
  Member<XRRigidTransform> ref_space_from_view_;
  Member<DOMFloat32Array> projection_matrix_;
};

class MODULES_EXPORT XRViewData final : public GarbageCollected<XRViewData> {
 public:
  explicit XRViewData(device::mojom::blink::XREye eye) : eye_(eye) {}
  XRViewData(const device::mojom::blink::XRViewPtr& view,
             double depth_near,
             double depth_far);

  void UpdateView(const device::mojom::blink::XRViewPtr& view,
                  double depth_near,
                  double depth_far);

  void UpdateProjectionMatrixFromFoV(float up_rad,
                                     float down_rad,
                                     float left_rad,
                                     float right_rad,
                                     float near_depth,
                                     float far_depth);
  void UpdateProjectionMatrixFromAspect(float fovy,
                                        float aspect,
                                        float near_depth,
                                        float far_depth);

  TransformationMatrix UnprojectPointer(double x,
                                        double y,
                                        double canvas_width,
                                        double canvas_height);

  device::mojom::blink::XREye Eye() const { return eye_; }
  const TransformationMatrix& MojoFromView() const { return mojo_from_view_; }
  const TransformationMatrix& ProjectionMatrix() const {
    return projection_matrix_;
  }

  absl::optional<double> recommendedViewportScale() const;
  void SetRecommendedViewportScale(absl::optional<double> scale) {
    recommended_viewport_scale_ = scale;
  }

  void requestViewportScale(absl::optional<double> scale);

  bool ViewportModifiable() const { return viewport_modifiable_; }
  void SetViewportModifiable(bool modifiable) {
    viewport_modifiable_ = modifiable;
  }
  double CurrentViewportScale() const { return current_viewport_scale_; }
  void SetCurrentViewportScale(double scale) {
    current_viewport_scale_ = scale;
  }
  double RequestedViewportScale() const { return requested_viewport_scale_; }

  void Trace(Visitor*) const {}

 private:
  const device::mojom::blink::XREye eye_;
  TransformationMatrix mojo_from_view_;
  TransformationMatrix projection_matrix_;
  TransformationMatrix inv_projection_;
  bool inv_projection_dirty_ = true;
  absl::optional<double> recommended_viewport_scale_ = absl::nullopt;
  double requested_viewport_scale_ = 1.0;
  double current_viewport_scale_ = 1.0;
  bool viewport_modifiable_ = false;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_XR_XR_VIEW_H_
