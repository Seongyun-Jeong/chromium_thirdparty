// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/cascade_layer.h"

#include "testing/gtest/include/gtest/gtest.h"

#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"

namespace blink {

class CascadeLayerTest : public testing::Test,
                         private ScopedCSSCascadeLayersForTest {
 public:
  CascadeLayerTest()
      : ScopedCSSCascadeLayersForTest(true),
        root_layer_(MakeGarbageCollected<CascadeLayer>()) {}

  using LayerName = StyleRuleBase::LayerName;

 protected:
  String LayersToString() const { return root_layer_->ToStringForTesting(); }

  Persistent<CascadeLayer> root_layer_;
};

TEST_F(CascadeLayerTest, Basic) {
  CascadeLayer* one = root_layer_->GetOrAddSubLayer(LayerName({"one"}));
  one->GetOrAddSubLayer(LayerName({"two"}));
  root_layer_->GetOrAddSubLayer(LayerName({"three", "four"}));
  root_layer_->GetOrAddSubLayer(LayerName({g_empty_atom}));
  root_layer_->GetOrAddSubLayer(LayerName({"five"}));

  EXPECT_EQ(
      "one,"
      "one.two,"
      "three,"
      "three.four,"
      "(anonymous),"
      "five",
      LayersToString());
}

TEST_F(CascadeLayerTest, RepeatedGetOrAdd) {
  // GetOrAddSubLayer() does not add duplicate layers.

  root_layer_->GetOrAddSubLayer(LayerName({"one", "two"}));
  root_layer_->GetOrAddSubLayer(LayerName({"three"}));

  root_layer_->GetOrAddSubLayer(LayerName({"one"}))
      ->GetOrAddSubLayer(LayerName({"two"}));
  root_layer_->GetOrAddSubLayer(LayerName({"three"}));

  EXPECT_EQ(
      "one,"
      "one.two,"
      "three",
      LayersToString());
}

TEST_F(CascadeLayerTest, RepeatedGetOrAddAnonymous) {
  // All anonymous layers are distinct and are hence not duplicates.

  // Two distinct anonymous layers
  root_layer_->GetOrAddSubLayer(LayerName({g_empty_atom}));
  root_layer_->GetOrAddSubLayer(LayerName({g_empty_atom}));

  // Two distinct anonymous sublayers of "one"
  CascadeLayer* one = root_layer_->GetOrAddSubLayer(LayerName({"one"}));
  root_layer_->GetOrAddSubLayer(LayerName({"one", g_empty_atom}));
  CascadeLayer* anonymous = one->GetOrAddSubLayer(LayerName({g_empty_atom}));

  anonymous->GetOrAddSubLayer(LayerName({"two"}));

  // This is a different layer "two" from the previously inserted "two" because
  // the parent layers are different anonymous layers.
  root_layer_->GetOrAddSubLayer(LayerName({"one", g_empty_atom, "two"}));

  EXPECT_EQ(
      "(anonymous),"
      "(anonymous),"
      "one,"
      "one.(anonymous),"
      "one.(anonymous),"
      "one.(anonymous).two,"
      "one.(anonymous),"
      "one.(anonymous).two",
      LayersToString());
}

TEST_F(CascadeLayerTest, LayerOrderNotInsertionOrder) {
  // Layer order and insertion order can be different.

  root_layer_->GetOrAddSubLayer(LayerName({"one"}));
  root_layer_->GetOrAddSubLayer(LayerName({"two"}));
  root_layer_->GetOrAddSubLayer(LayerName({"one", "three"}));

  EXPECT_EQ(
      "one,"
      "one.three,"
      "two",
      LayersToString());
}

}  // namespace blink
