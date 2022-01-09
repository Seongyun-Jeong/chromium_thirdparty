// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/ng/geometry/ng_fragment_geometry.h"
#include "third_party/blink/renderer/core/layout/ng/ng_layout_result.h"
#include "third_party/blink/renderer/core/layout/ng/ng_layout_test.h"
#include "third_party/blink/renderer/core/layout/ng/ng_layout_utils.h"

namespace blink {
namespace {

// These tests exercise the caching logic of |NGLayoutResult|s. They are
// rendering tests which contain two children: "test" and "src".
//
// Both have layout initially performed on them, however the "src" will have a
// different |NGConstraintSpace| which is then used to test either a cache hit
// or miss.
class NGLayoutResultCachingTest : public NGLayoutTest,
                                  private ScopedLayoutNGGridForTest {
 protected:
  NGLayoutResultCachingTest() : ScopedLayoutNGGridForTest(true) {}
};

TEST_F(NGLayoutResultCachingTest, HitDifferentExclusionSpace) {
  // Same BFC offset, different exclusion space.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .float { float: left; width: 50px; }
    </style>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 20px;"></div>
      </div>
      <div id="test" style="height: 20px;"></div>
    </div>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 30px;"></div>
      </div>
      <div id="src" style="height: 20px;"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
  EXPECT_EQ(result->BfcBlockOffset().value(), LayoutUnit(50));
  EXPECT_EQ(result->BfcLineOffset(), LayoutUnit());
}

TEST_F(NGLayoutResultCachingTest, HitDifferentBFCOffset) {
  // Different BFC offset, same exclusion space.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .float { float: left; width: 50px; }
    </style>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 20px;"></div>
      </div>
      <div id="test" style="height: 20px; padding-top: 5px;">
        <div class="float" style="height: 20px;"></div>
      </div>
    </div>
    <div class="bfc">
      <div style="height: 40px;">
        <div class="float" style="height: 20px;"></div>
      </div>
      <div id="src" style="height: 20px; padding-top: 5px;">
        <div class="float" style="height: 20px;"></div>
      </div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
  EXPECT_EQ(result->BfcBlockOffset().value(), LayoutUnit(40));
  EXPECT_EQ(result->BfcLineOffset(), LayoutUnit());

  // Also check that the exclusion(s) got moved correctly.
  LayoutOpportunityVector opportunities =
      result->ExclusionSpace().AllLayoutOpportunities(
          /* offset */ {LayoutUnit(), LayoutUnit()},
          /* available_inline_size */ LayoutUnit(100));

  EXPECT_EQ(opportunities.size(), 4u);

  // This first opportunity is superfluous, but harmless. It isn't needed for
  // correct positioning, but having it in the opportunity list shouldn't
  // trigger any bad behaviour (if something doesn't fit, in this case it'll
  // proceed to the next layout opportunity).
  EXPECT_EQ(opportunities[0].rect.start_offset,
            NGBfcOffset(LayoutUnit(50), LayoutUnit()));
  EXPECT_EQ(opportunities[0].rect.end_offset,
            NGBfcOffset(LayoutUnit(100), LayoutUnit(45)));

  EXPECT_EQ(opportunities[1].rect.start_offset,
            NGBfcOffset(LayoutUnit(50), LayoutUnit()));
  EXPECT_EQ(opportunities[1].rect.end_offset,
            NGBfcOffset(LayoutUnit(100), LayoutUnit::Max()));

  EXPECT_EQ(opportunities[2].rect.start_offset,
            NGBfcOffset(LayoutUnit(), LayoutUnit(20)));
  EXPECT_EQ(opportunities[2].rect.end_offset,
            NGBfcOffset(LayoutUnit(100), LayoutUnit(45)));

  EXPECT_EQ(opportunities[3].rect.start_offset,
            NGBfcOffset(LayoutUnit(), LayoutUnit(65)));
  EXPECT_EQ(opportunities[3].rect.end_offset,
            NGBfcOffset(LayoutUnit(100), LayoutUnit::Max()));
}

TEST_F(NGLayoutResultCachingTest, HitDifferentBFCOffsetSameMarginStrut) {
  // Different BFC offset, same margin-strut.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
    </style>
    <div class="bfc">
      <div style="height: 50px; margin-bottom: 20px;"></div>
      <div id="test" style="height: 20px;"></div>
    </div>
    <div class="bfc">
      <div style="height: 40px; margin-bottom: 20px;"></div>
      <div id="src" style="height: 20px;"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissDescendantAboveBlockStart1) {
  // Same BFC offset, different exclusion space, descendant above
  // block start.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .float { float: left; width: 50px; }
    </style>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 20px;"></div>
      </div>
      <div id="test" style="height: 20px; padding-top: 5px;">
        <div style="height: 10px; margin-top: -10px;"></div>
      </div>
    </div>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 30px;"></div>
      </div>
      <div id="src" style="height: 20px;"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissDescendantAboveBlockStart2) {
  // Different BFC offset, same exclusion space, descendant above
  // block start.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .float { float: left; width: 50px; }
    </style>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 20px;"></div>
      </div>
      <div id="test" style="height: 20px; padding-top: 5px;">
        <div style="height: 10px; margin-top: -10px;"></div>
      </div>
    </div>
    <div class="bfc">
      <div style="height: 40px;">
        <div class="float" style="height: 20px;"></div>
      </div>
      <div id="src" style="height: 20px;"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitOOFDescendantAboveBlockStart) {
  // Different BFC offset, same exclusion space, OOF-descendant above
  // block start.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .float { float: left; width: 50px; }
    </style>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 20px;"></div>
      </div>
      <div id="test" style="position: relative; height: 20px; padding-top: 5px;">
        <div style="position: absolute; height: 10px; top: -10px;"></div>
      </div>
    </div>
    <div class="bfc">
      <div style="height: 40px;">
        <div class="float" style="height: 20px;"></div>
      </div>
      <div id="src" style="height: 20px;"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitLineBoxDescendantAboveBlockStart) {
  // Different BFC offset, same exclusion space, line-box descendant above
  // block start.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .float { float: left; width: 50px; }
    </style>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 20px;"></div>
      </div>
      <div id="test" style="font-size: 12px;">
        text
        <span style="margin: 0 1px;">
          <span style="display: inline-block; vertical-align: text-bottom; width: 16px; height: 16px;"></span>
        </span>
      </div>
    </div>
    <div class="bfc">
      <div style="height: 40px;">
        <div class="float" style="height: 20px;"></div>
      </div>
      <div id="src" style="font-size: 12px;">
        text
      </div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissFloatInitiallyIntruding1) {
  // Same BFC offset, different exclusion space, float initially
  // intruding.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .float { float: left; width: 50px; }
    </style>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 60px;"></div>
      </div>
      <div id="test" style="height: 20px;"></div>
    </div>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 30px;"></div>
      </div>
      <div id="src" style="height: 20px;"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissFloatInitiallyIntruding2) {
  // Different BFC offset, same exclusion space, float initially
  // intruding.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .float { float: left; width: 50px; }
    </style>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 60px;"></div>
      </div>
      <div id="test" style="height: 60px;"></div>
    </div>
    <div class="bfc">
      <div style="height: 70px;">
        <div class="float" style="height: 60px;"></div>
      </div>
      <div id="src" style="height: 20px;"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissFloatWillIntrude1) {
  // Same BFC offset, different exclusion space, float will intrude.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .float { float: left; width: 50px; }
    </style>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 40px;"></div>
      </div>
      <div id="test" style="height: 20px;"></div>
    </div>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 60px;"></div>
      </div>
      <div id="src" style="height: 20px;"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissFloatWillIntrude2) {
  // Different BFC offset, same exclusion space, float will intrude.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .float { float: left; width: 50px; }
    </style>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 40px;"></div>
      </div>
      <div id="test" style="height: 60px;"></div>
    </div>
    <div class="bfc">
      <div style="height: 30px;">
        <div class="float" style="height: 40px;"></div>
      </div>
      <div id="src" style="height: 20px;"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitPushedByFloats1) {
  // Same BFC offset, different exclusion space, pushed by floats.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .float { float: left; width: 50px; }
    </style>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 60px;"></div>
      </div>
      <div id="test" style="height: 20px; clear: left;"></div>
    </div>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 70px;"></div>
      </div>
      <div id="src" style="height: 20px; clear: left;"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitPushedByFloats2) {
  // Different BFC offset, same exclusion space, pushed by floats.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .float { float: left; width: 50px; }
    </style>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 60px;"></div>
      </div>
      <div id="test" style="height: 20px; clear: left;"></div>
    </div>
    <div class="bfc">
      <div style="height: 30px;">
        <div class="float" style="height: 60px;"></div>
      </div>
      <div id="src" style="height: 20px; clear: left;"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissPushedByFloats1) {
  // Same BFC offset, different exclusion space, pushed by floats.
  // Miss due to shrinking offset.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .float { float: left; width: 50px; }
    </style>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 70px;"></div>
      </div>
      <div id="test" style="height: 20px; clear: left;"></div>
    </div>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 60px;"></div>
      </div>
      <div id="src" style="height: 20px; clear: left;"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissPushedByFloats2) {
  // Different BFC offset, same exclusion space, pushed by floats.
  // Miss due to shrinking offset.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .float { float: left; width: 50px; }
    </style>
    <div class="bfc">
      <div style="height: 30px;">
        <div class="float" style="height: 60px;"></div>
      </div>
      <div id="test" style="height: 20px; clear: left;"></div>
    </div>
    <div class="bfc">
      <div style="height: 50px;">
        <div class="float" style="height: 60px;"></div>
      </div>
      <div id="src" style="height: 20px; clear: left;"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitDifferentRareData) {
  // Same absolute fixed constraints.
  SetBodyInnerHTML(R"HTML(
    <style>
      .container { position: relative; width: 100px; height: 100px; }
      .abs { position: absolute; width: 100px; height: 100px; top: 0; left: 0; }
    </style>
    <div class="container">
      <div id="test" class="abs"></div>
    </div>
    <div class="container" style="width: 200px; height: 200px;">
      <div id="src" class="abs"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitPercentageMinWidth) {
  // min-width calculates to different values, but doesn't change size.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .inflow { width: 100px; min-width: 25%; }
    </style>
    <div class="bfc">
      <div id="test" class="inflow"></div>
    </div>
    <div class="bfc" style="width: 200px; height: 200px;">
      <div id="src" class="inflow"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitFixedMinWidth) {
  // min-width is always larger than the available size.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .inflow { min-width: 300px; }
    </style>
    <div class="bfc">
      <div id="test" class="inflow"></div>
    </div>
    <div class="bfc" style="width: 200px; height: 200px;">
      <div id="src" class="inflow"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitShrinkToFit) {
  SetBodyInnerHTML(R"HTML(
    <div style="display: flow-root; width: 300px; height: 100px;">
      <div id="test1" style="float: left;">
        <div style="display: inline-block; width: 150px;"></div>
        <div style="display: inline-block; width: 50px;"></div>
      </div>
      <div id="test2" style="float: left;">
        <div style="display: inline-block; width: 350px;"></div>
        <div style="display: inline-block; width: 250px;"></div>
      </div>
    </div>
    <div style="display: flow-root; width: 400px; height: 100px;">
      <div id="src1" style="float: left;">
        <div style="display: inline-block; width: 150px;"></div>
        <div style="display: inline-block; width: 50px;"></div>
      </div>
    </div>
    <div style="display: flow-root; width: 200px; height: 100px;">
      <div id="src2" style="float: left;">
        <div style="display: inline-block; width: 350px;"></div>
        <div style="display: inline-block; width: 250px;"></div>
      </div>
    </div>
  )HTML");

  auto* test1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test1"));
  auto* test2 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test2"));
  auto* src1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src1"));
  auto* src2 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src2"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  NGConstraintSpace space =
      src1->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test1->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);
  // test1 was sized to its max-content size, passing an available size larger
  // than the fragment should hit the cache.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);

  fragment_geometry.reset();
  space = src2->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  result = test2->CachedLayoutResult(space, nullptr, nullptr,
                                     &fragment_geometry, &cache_status);
  // test2 was sized to its min-content size in, passing an available size
  // smaller than the fragment should hit the cache.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissShrinkToFit) {
  SetBodyInnerHTML(R"HTML(
    <div style="display: flow-root; width: 300px; height: 100px;">
      <div id="test1" style="float: left;">
        <div style="display: inline-block; width: 150px;"></div>
        <div style="display: inline-block; width: 50px;"></div>
      </div>
      <div id="test2" style="float: left;">
        <div style="display: inline-block; width: 350px;"></div>
        <div style="display: inline-block; width: 250px;"></div>
      </div>
      <div id="test3" style="float: left; min-width: 80%;">
        <div style="display: inline-block; width: 150px;"></div>
        <div style="display: inline-block; width: 250px;"></div>
      </div>
      <div id="test4" style="float: left; margin-left: 75px;">
        <div style="display: inline-block; width: 150px;"></div>
        <div style="display: inline-block; width: 50px;"></div>
      </div>
    </div>
    <div style="display: flow-root; width: 100px; height: 100px;">
      <div id="src1" style="float: left;">
        <div style="display: inline-block; width: 150px;"></div>
        <div style="display: inline-block; width: 50px;"></div>
      </div>
    </div>
    <div style="display: flow-root; width: 400px; height: 100px;">
      <div id="src2" style="float: left;">
        <div style="display: inline-block; width: 350px;"></div>
        <div style="display: inline-block; width: 250px;"></div>
      </div>
      <div id="src3" style="float: left; min-width: 80%;">
        <div style="display: inline-block; width: 150px;"></div>
        <div style="display: inline-block; width: 250px;"></div>
      </div>
    </div>
    <div style="display: flow-root; width: 250px; height: 100px;">
      <div id="src4" style="float: left; margin-left: 75px;">
        <div style="display: inline-block; width: 150px;"></div>
        <div style="display: inline-block; width: 50px;"></div>
      </div>
    </div>
  )HTML");

  auto* test1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test1"));
  auto* test2 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test2"));
  auto* test3 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test3"));
  auto* test4 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test4"));
  auto* src1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src1"));
  auto* src2 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src2"));
  auto* src3 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src3"));
  auto* src4 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src4"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  NGConstraintSpace space =
      src1->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test1->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);
  // test1 was sized to its max-content size, passing an available size smaller
  // than the fragment should miss the cache.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);

  fragment_geometry.reset();
  space = src2->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  result = test2->CachedLayoutResult(space, nullptr, nullptr,
                                     &fragment_geometry, &cache_status);
  // test2 was sized to its min-content size, passing an available size
  // larger than the fragment should miss the cache.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);

  fragment_geometry.reset();
  space = src3->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  result = test3->CachedLayoutResult(space, nullptr, nullptr,
                                     &fragment_geometry, &cache_status);
  // test3 was sized to its min-content size, however it should miss the cache
  // as it has a %-min-size.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);

  fragment_geometry.reset();
  space = src4->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  result = test4->CachedLayoutResult(space, nullptr, nullptr,
                                     &fragment_geometry, &cache_status);
  // test4 was sized to its max-content size, however it should miss the cache
  // due to its margin.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitShrinkToFitSameIntrinsicSizes) {
  // We have a shrink-to-fit node, with the min, and max intrinsic sizes being
  // equal (the available size doesn't affect the final size).
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .shrink { width: fit-content; }
      .child { width: 250px; }
    </style>
    <div class="bfc">
      <div id="test" class="shrink">
        <div class="child"></div>
      </div>
    </div>
    <div class="bfc" style="width: 200px; height: 200px;">
      <div id="src" class="shrink">
        <div class="child"></div>
      </div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitShrinkToFitDifferentParent) {
  // The parent "bfc" node changes from shrink-to-fit, to a fixed width. But
  // these calculate as the same available space to the "test" element.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; }
      .child { width: 250px; }
    </style>
    <div class="bfc" style="width: fit-content; height: 100px;">
      <div id="test">
        <div class="child"></div>
      </div>
    </div>
    <div class="bfc" style="width: 250px; height: 100px;">
      <div id="src">
        <div class="child"></div>
      </div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissQuirksModePercentageBasedChild) {
  // Quirks-mode %-block-size child.
  GetDocument().SetCompatibilityMode(Document::kQuirksMode);
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .child { height: 50%; }
    </style>
    <div class="bfc">
      <div id="test">
        <div class="child"></div>
      </div>
    </div>
    <div class="bfc" style="height: 200px;">
      <div id="src">
        <div class="child"></div>
      </div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitQuirksModePercentageBasedParentAndChild) {
  // Quirks-mode %-block-size parent *and* child. Here we mark the parent as
  // depending on %-block-size changes, however itself doesn't change in
  // height.
  // We are able to hit the cache as we detect that the height for the child
  // *isn't* indefinite, and results in the same height as before.
  GetDocument().SetCompatibilityMode(Document::kQuirksMode);
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .parent { height: 50%; min-height: 200px; }
      .child { height: 50%; }
    </style>
    <div class="bfc">
      <div id="test" class="parent">
        <div class="child"></div>
      </div>
    </div>
    <div class="bfc" style="height: 200px;">
      <div id="src" class="parent">
        <div class="child"></div>
      </div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitStandardsModePercentageBasedChild) {
  // Standards-mode %-block-size child.
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .child { height: 50%; }
    </style>
    <div class="bfc">
      <div id="test">
        <div class="child"></div>
      </div>
    </div>
    <div class="bfc" style="height: 200px;">
      <div id="src">
        <div class="child"></div>
      </div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, ChangeTableCellBlockSizeConstrainedness) {
  SetBodyInnerHTML(R"HTML(
    <style>
      .table { display: table; width: 300px; }
      .cell { display: table-cell; }
      .child1 { height: 100px; }
      .child2, .child3 { overflow:auto; height:10%; }
    </style>
    <div class="table">
      <div class="cell">
        <div class="child1" id="test1"></div>
        <div class="child2" id="test2">
          <div style="height:30px;"></div>
        </div>
        <div class="child3" id="test3"></div>
      </div>
    </div>
    <div class="table" style="height:300px;">
      <div class="cell">
        <div class="child1" id="src1"></div>
        <div class="child2" id="src2">
          <div style="height:30px;"></div>
        </div>
        <div class="child3" id="src3"></div>
      </div>
    </div>
  )HTML");

  auto* test1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test1"));
  auto* test2 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test2"));
  auto* test3 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test3"));
  auto* src1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src1"));
  auto* src2 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src2"));
  auto* src3 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src3"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  NGConstraintSpace space =
      src1->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test1->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);
  // The first child has a fixed height, and shouldn't be affected by the cell
  // height.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);

  fragment_geometry.reset();
  space = src2->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  result = test2->CachedLayoutResult(space, nullptr, nullptr,
                                     &fragment_geometry, &cache_status);
  // The second child has overflow:auto and a percentage height, but its
  // intrinsic height is identical to its extrinsic height (when the cell has a
  // height). So it won't need layout, either.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);

  fragment_geometry.reset();
  space = src3->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  result = test3->CachedLayoutResult(space, nullptr, nullptr,
                                     &fragment_geometry, &cache_status);
  // The third child has overflow:auto and a percentage height, and its
  // intrinsic height is 0 (no children), so it matters whether the cell has a
  // height or not. We're only going to need simplified layout, though, since no
  // children will be affected by its height change.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsSimplifiedLayout);
}

TEST_F(NGLayoutResultCachingTest, OptimisticFloatPlacementNoRelayout) {
  SetBodyInnerHTML(R"HTML(
    <style>
      .root { display: flow-root; width: 300px; }
      .float { float: left; width: 10px; height: 10px; }
    </style>
    <div class="root">
      <div id="empty">
        <div class="float"></div>
      </div>
    </div>
  )HTML");

  auto* empty = To<LayoutBlockFlow>(GetLayoutObjectByElementId("empty"));

  NGConstraintSpace space =
      empty->GetCachedLayoutResult()->GetConstraintSpaceForCaching();

  // We shouldn't have a "forced" BFC block-offset, as the "empty"
  // self-collapsing block should have its "expected" BFC block-offset at the
  // correct place.
  EXPECT_EQ(space.ForcedBfcBlockOffset(), absl::nullopt);
}

TEST_F(NGLayoutResultCachingTest, SelfCollapsingShifting) {
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .float { float: left; width: 10px; height: 10px; }
      .adjoining-oof { position: absolute; display: inline; }
    </style>
    <div class="bfc">
      <div class="float"></div>
      <div id="test1"></div>
    </div>
    <div class="bfc">
      <div class="float" style="height; 20px;"></div>
      <div id="src1"></div>
    </div>
    <div class="bfc">
      <div class="float"></div>
      <div id="test2">
        <div class="adjoining-oof"></div>
      </div>
    </div>
    <div class="bfc">
      <div class="float" style="height; 20px;"></div>
      <div id="src2">
        <div class="adjoining-oof"></div>
      </div>
    </div>
    <div class="bfc">
      <div class="float"></div>
      <div style="height: 30px;"></div>
      <div id="test3">
        <div class="adjoining-oof"></div>
      </div>
    </div>
    <div class="bfc">
      <div class="float" style="height; 20px;"></div>
      <div style="height: 30px;"></div>
      <div id="src3">
        <div class="adjoining-oof"></div>
      </div>
    </div>
  )HTML");

  auto* test1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test1"));
  auto* test2 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test2"));
  auto* test3 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test3"));
  auto* src1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src1"));
  auto* src2 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src2"));
  auto* src3 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src3"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;

  NGConstraintSpace space =
      src1->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test1->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  // Case 1: We have a different set of constraints, but as the child has no
  // adjoining descendants it can be shifted anywhere.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);

  fragment_geometry.reset();
  space = src2->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  result = test2->CachedLayoutResult(space, nullptr, nullptr,
                                     &fragment_geometry, &cache_status);

  // Case 2: We have a different set of constraints, but the child has an
  // adjoining object and isn't "past" the floats - it can't be reused.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);

  fragment_geometry.reset();
  space = src3->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  result = test3->CachedLayoutResult(space, nullptr, nullptr,
                                     &fragment_geometry, &cache_status);

  // Case 3: We have a different set of constraints, and adjoining descendants,
  // but have a position past where they might affect us.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, ClearancePastAdjoiningFloatsMovement) {
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
      .float-left { float: left; width: 10px; height: 10px; }
      .float-right { float: right; width: 10px; height: 20px; }
    </style>
    <div class="bfc">
      <div>
        <div class="float-left"></div>
        <div class="float-right"></div>
        <div id="test1" style="clear: both;">text</div>
      </div>
    </div>
    <div class="bfc">
      <div>
        <div class="float-left" style="height; 20px;"></div>
        <div class="float-right"></div>
        <div id="src1" style="clear: both;">text</div>
      </div>
    </div>
    <div class="bfc">
      <div>
        <div class="float-left"></div>
        <div class="float-right"></div>
        <div id="test2" style="clear: left;">text</div>
      </div>
    </div>
    <div class="bfc">
      <div>
        <div class="float-left" style="height; 20px;"></div>
        <div class="float-right"></div>
        <div id="src2" style="clear: left;">text</div>
      </div>
    </div>
  )HTML");

  auto* test1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test1"));
  auto* test2 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test2"));
  auto* src1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src1"));
  auto* src2 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src2"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;

  NGConstraintSpace space =
      src1->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test1->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  // Case 1: We have forced clearance, but floats won't impact our children.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);

  fragment_geometry.reset();
  space = src2->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  result = test2->CachedLayoutResult(space, nullptr, nullptr,
                                     &fragment_geometry, &cache_status);

  // Case 2: We have forced clearance, and floats will impact our children.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MarginStrutMovementSelfCollapsing) {
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
    </style>
    <div class="bfc">
      <div style="margin-top: 10px;">
        <div id="test1">
          <div></div>
        </div>
      </div>
    </div>
    <div class="bfc">
      <div style="margin-top: 5px;">
        <div id="src1">
          <div></div>
        </div>
      </div>
    </div>
    <div class="bfc">
      <div style="margin-top: 10px;">
        <div id="test2">
          <div style="margin-bottom: 8px;"></div>
        </div>
      </div>
    </div>
    <div class="bfc">
      <div style="margin-top: 5px;">
        <div id="src2">
          <div style="margin-bottom: 8px;"></div>
        </div>
      </div>
    </div>
  )HTML");

  auto* test1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test1"));
  auto* test2 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test2"));
  auto* src1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src1"));
  auto* src2 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src2"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;

  NGConstraintSpace space =
      src1->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test1->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  // Case 1: We can safely re-use this fragment as it doesn't append anything
  // to the margin-strut within the sub-tree.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);

  // The "end" margin-strut should be updated.
  NGMarginStrut expected_margin_strut;
  expected_margin_strut.Append(LayoutUnit(5), false /* is_quirky */);
  EXPECT_EQ(expected_margin_strut, result->EndMarginStrut());

  fragment_geometry.reset();
  space = src2->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  result = test2->CachedLayoutResult(space, nullptr, nullptr,
                                     &fragment_geometry, &cache_status);

  // Case 2: We can't re-use this fragment as it appended a non-zero value to
  // the margin-strut within the sub-tree.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MarginStrutMovementInFlow) {
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
    </style>
    <div class="bfc">
      <div style="margin-top: 10px;">
        <div id="test1">
          <div>text</div>
        </div>
      </div>
    </div>
    <div class="bfc">
      <div style="margin-top: 5px;">
        <div id="src1">
          <div>text</div>
        </div>
      </div>
    </div>
    <div class="bfc">
      <div style="margin-top: 10px;">
        <div id="test2">
          <div style="margin-top: 8px;">text</div>
        </div>
      </div>
    </div>
    <div class="bfc">
      <div style="margin-top: 5px;">
        <div id="src2">
          <div style="margin-top: 8px;">text</div>
        </div>
      </div>
    </div>
    <div class="bfc">
      <div style="margin-top: 10px;">
        <div id="test3">
          <div>
            <div style="margin-top: 8px;"></div>
          </div>
          <div>text</div>
        </div>
      </div>
    </div>
    <div class="bfc">
      <div style="margin-top: 5px;">
        <div id="src3">
          <div>
            <div style="margin-top: 8px;"></div>
          </div>
          <div>text</div>
        </div>
      </div>
    </div>
  )HTML");

  auto* test1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test1"));
  auto* test2 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test2"));
  auto* test3 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test3"));
  auto* src1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src1"));
  auto* src2 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src2"));
  auto* src3 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src3"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;

  NGConstraintSpace space =
      src1->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test1->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  // Case 1: We can safely re-use this fragment as it doesn't append anything
  // to the margin-strut within the sub-tree.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);

  fragment_geometry.reset();
  space = src2->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  result = test2->CachedLayoutResult(space, nullptr, nullptr,
                                     &fragment_geometry, &cache_status);

  // Case 2: We can't re-use this fragment as it appended a non-zero value to
  // the margin-strut within the sub-tree.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);

  fragment_geometry.reset();
  space = src3->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  result = test3->CachedLayoutResult(space, nullptr, nullptr,
                                     &fragment_geometry, &cache_status);

  // Case 3: We can't re-use this fragment as a (inner) self-collapsing block
  // appended a non-zero value to the margin-strut within the sub-tree.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MarginStrutMovementPercentage) {
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 300px; height: 300px; }
    </style>
    <div class="bfc">
      <div style="margin-top: 10px;">
        <div id="test1" style="width: 0px;">
          <div style="margin-top: 50%;">text</div>
        </div>
      </div>
    </div>
    <div class="bfc">
      <div style="margin-top: 5px;">
        <div id="src1" style="width: 0px;">
          <div style="margin-top: 50%;">text</div>
        </div>
      </div>
    </div>
  )HTML");

  auto* test1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test1"));
  auto* src1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src1"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;

  NGConstraintSpace space =
      src1->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test1->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  // We can't re-use this fragment as it appended a non-zero value (50%) to the
  // margin-strut within the sub-tree.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitIsFixedBlockSizeIndefinite) {
  SetBodyInnerHTML(R"HTML(
    <div style="display: flex; width: 100px; height: 100px;">
      <div id="test1" style="flex-grow: 1; min-height: 100px;">
        <div style="height: 50px;">text</div>
      </div>
    </div>
    <div style="display: flex; width: 100px; height: 100px; align-items: stretch;">
      <div id="src1" style="flex-grow: 1; min-height: 100px;">
        <div style="height: 50px;">text</div>
      </div>
    </div>
  )HTML");

  auto* test1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test1"));
  auto* src1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src1"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;

  NGConstraintSpace space =
      src1->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test1->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  // Even though the "align-items: stretch" will make the final fixed
  // block-size indefinite, we don't have any %-block-size children, so we can
  // hit the cache.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissIsFixedBlockSizeIndefinite) {
  SetBodyInnerHTML(R"HTML(
    <!DOCTYPE html>
    <div style="display: flex; width: 100px; height: 100px; align-items: start;">
      <div id="src1" style="flex-grow: 1; min-height: 100px;">
        <div style="height: 50%;">text</div>
      </div>
    </div>
    <div style="display: flex; width: 100px; height: 100px; align-items: stretch;">
      <div id="test1" style="flex-grow: 1; min-height: 100px;">
        <div style="height: 50%;">text</div>
      </div>
    </div>
  )HTML");

  auto* test1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test1"));
  auto* src1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src1"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;

  NGConstraintSpace space =
      src1->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test1->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  // The "align-items: stretch" will make the final fixed block-size
  // indefinite, and we have a %-block-size child, so we need to miss the
  // cache.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitColumnFlexBoxMeasureAndLayout) {
  SetBodyInnerHTML(R"HTML(
    <!DOCTYPE html>
    <style>
      .bfc { display: flex; flex-direction: column; width: 100px; height: 100px; }
    </style>
    <div class="bfc">
      <div id="src1" style="flex-grow: 0;">
        <div style="height: 50px;"></div>
      </div>
    </div>
    <div class="bfc">
      <div id="src2" style="flex-grow: 1;">
        <div style="height: 50px;"></div>
      </div>
    </div>
    <div class="bfc">
      <div id="test1" style="flex-grow: 2;">
        <div style="height: 50px;"></div>
      </div>
    </div>
  )HTML");

  auto* test1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test1"));
  auto* src1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src1"));
  auto* src2 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src2"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;

  // "src1" only had one "measure" pass performed, and should hit the "measure"
  // cache-slot for "test1".
  NGConstraintSpace space =
      src1->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test1->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(space.CacheSlot(), NGCacheSlot::kMeasure);
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);

  // "src2" had both a "measure" and "layout" pass performed, and should hit
  // the "layout" cache-slot for "test1".
  space = src2->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  result = test1->CachedLayoutResult(space, nullptr, nullptr,
                                     &fragment_geometry, &cache_status);

  EXPECT_EQ(space.CacheSlot(), NGCacheSlot::kLayout);
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitRowFlexBoxMeasureAndLayout) {
  SetBodyInnerHTML(R"HTML(
    <!DOCTYPE html>
    <style>
      .bfc { display: flex; width: 100px; }
    </style>
    <div class="bfc">
      <div id="src1">
        <div style="height: 50px;"></div>
      </div>
    </div>
    <div class="bfc">
      <div id="src2">
        <div style="height: 70px;"></div>
      </div>
      <div style="width: 0px; height: 100px;"></div>
    </div>
    <div class="bfc">
      <div id="test1">
        <div style="height: 50px;"></div>
      </div>
      <div style="width: 0px; height: 100px;"></div>
    </div>
  )HTML");

  auto* test1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test1"));
  auto* src1 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src1"));
  auto* src2 = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src2"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;

  // "src1" only had one "measure" pass performed, and should hit the "measure"
  // cache-slot for "test1".
  NGConstraintSpace space =
      src1->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test1->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(space.CacheSlot(), NGCacheSlot::kMeasure);
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);

  // "src2" had both a "measure" and "layout" pass performed, and should hit
  // the "layout" cache-slot for "test1".
  space = src2->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  result = test1->CachedLayoutResult(space, nullptr, nullptr,
                                     &fragment_geometry, &cache_status);

  EXPECT_EQ(space.CacheSlot(), NGCacheSlot::kLayout);
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitFlexLegacyImg) {
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flex; flex-direction: column; width: 300px; }
      .bfc > * { display: flex; }
    </style>
    <div class="bfc">
      <div id="test">
        <img />
      </div>
    </div>
    <div class="bfc" style="height: 200px;">
      <div id="src">
        <img />
      </div>
    </div>
  )HTML");

  auto* test = To<LayoutBlock>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlock>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitFlexLegacyGrid) {
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flex; flex-direction: column; width: 300px; }
      .bfc > * { display: flex; }
      .grid { display: grid; }
    </style>
    <div class="bfc">
      <div id="test">
        <div class="grid"></div>
      </div>
    </div>
    <div class="bfc" style="height: 200px;">
      <div id="src">
        <div class="grid"></div>
      </div>
    </div>
  )HTML");

  auto* test = To<LayoutBlock>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlock>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitFlexDefiniteChange) {
  SetBodyInnerHTML(R"HTML(
    <div style="display: flex; flex-direction: column;">
      <div style="height: 200px;" id=target1>
        <div style="height: 100px"></div>
      </div>
    </div>
  )HTML");

  auto* target1 = To<LayoutBlock>(GetLayoutObjectByElementId("target1"));

  scoped_refptr<const NGLayoutResult> result1 =
      target1->GetCachedLayoutResult();
  scoped_refptr<const NGLayoutResult> measure1 =
      target1->GetCachedMeasureResult();
  EXPECT_EQ(measure1->IntrinsicBlockSize(), 100);
  EXPECT_EQ(result1->PhysicalFragment().Size().height, 200);

  EXPECT_EQ(result1->GetConstraintSpaceForCaching().CacheSlot(),
            NGCacheSlot::kMeasure);
  EXPECT_EQ(result1.get(), measure1.get());
}

TEST_F(NGLayoutResultCachingTest, HitOrthogonalRoot) {
  SetBodyInnerHTML(R"HTML(
    <style>
      span { display: inline-block; width: 20px; height: 250px }
    </style>
    <div id="target" style="display: flex;">
      <div style="writing-mode: vertical-rl; line-height: 0;">
        <span></span><span></span>
      </div>
    </div>
  )HTML");

  auto* target = To<LayoutBlock>(GetLayoutObjectByElementId("target"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      target->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = target->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  // We should hit the cache using the same constraint space.
  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, SimpleTable) {
  SetBodyInnerHTML(R"HTML(
    <table>
      <td id="target1">abc</td>
      <td id="target2">abc</td>
    </table>
  )HTML");

  auto* target1 = To<LayoutBlock>(GetLayoutObjectByElementId("target1"));
  auto* target2 = To<LayoutBlock>(GetLayoutObjectByElementId("target2"));

  // Both "target1", and "target1" should have  only had one "measure" pass
  // performed.
  scoped_refptr<const NGLayoutResult> result1 =
      target1->GetCachedLayoutResult();
  scoped_refptr<const NGLayoutResult> measure1 =
      target1->GetCachedMeasureResult();
  EXPECT_EQ(result1->GetConstraintSpaceForCaching().CacheSlot(),
            NGCacheSlot::kMeasure);
  EXPECT_NE(result1.get(), nullptr);
  EXPECT_EQ(result1.get(), measure1.get());

  scoped_refptr<const NGLayoutResult> result2 =
      target2->GetCachedLayoutResult();
  scoped_refptr<const NGLayoutResult> measure2 =
      target2->GetCachedMeasureResult();
  EXPECT_EQ(result2->GetConstraintSpaceForCaching().CacheSlot(),
            NGCacheSlot::kMeasure);
  EXPECT_NE(result2.get(), nullptr);
  EXPECT_EQ(result2.get(), measure2.get());
}

TEST_F(NGLayoutResultCachingTest, MissTableCellMiddleAlignment) {
  SetBodyInnerHTML(R"HTML(
    <table>
      <td id="target" style="vertical-align: middle;">abc</td>
      <td>abc<br>abc</td>
    </table>
  )HTML");

  auto* target = To<LayoutBlock>(GetLayoutObjectByElementId("target"));

  // "target" should be stretched, and miss the measure cache.
  scoped_refptr<const NGLayoutResult> result = target->GetCachedLayoutResult();
  scoped_refptr<const NGLayoutResult> measure =
      target->GetCachedMeasureResult();
  EXPECT_NE(measure.get(), nullptr);
  EXPECT_NE(result.get(), nullptr);
  EXPECT_EQ(measure->GetConstraintSpaceForCaching().CacheSlot(),
            NGCacheSlot::kMeasure);
  EXPECT_EQ(result->GetConstraintSpaceForCaching().CacheSlot(),
            NGCacheSlot::kLayout);
  EXPECT_NE(result.get(), measure.get());
}

TEST_F(NGLayoutResultCachingTest, MissTableCellBottomAlignment) {
  SetBodyInnerHTML(R"HTML(
    <table>
      <td id="target" style="vertical-align: bottom;">abc</td>
      <td>abc<br>abc</td>
    </table>
  )HTML");

  auto* target = To<LayoutBlock>(GetLayoutObjectByElementId("target"));

  // "target" should be stretched, and miss the measure cache.
  scoped_refptr<const NGLayoutResult> result = target->GetCachedLayoutResult();
  scoped_refptr<const NGLayoutResult> measure =
      target->GetCachedMeasureResult();
  EXPECT_NE(measure.get(), nullptr);
  EXPECT_NE(result.get(), nullptr);
  EXPECT_EQ(measure->GetConstraintSpaceForCaching().CacheSlot(),
            NGCacheSlot::kMeasure);
  EXPECT_EQ(result->GetConstraintSpaceForCaching().CacheSlot(),
            NGCacheSlot::kLayout);
  EXPECT_NE(result.get(), measure.get());
}

TEST_F(NGLayoutResultCachingTest, HitTableCellBaselineAlignment) {
  SetBodyInnerHTML(R"HTML(
    <style>
      td { vertical-align: baseline; }
    </style>
    <table>
      <td id="target">abc</td>
      <td>def</td>
    </table>
  )HTML");

  auto* target = To<LayoutBlock>(GetLayoutObjectByElementId("target"));

  // "target" should align to the baseline, but hit the cache.
  scoped_refptr<const NGLayoutResult> result = target->GetCachedLayoutResult();
  scoped_refptr<const NGLayoutResult> measure =
      target->GetCachedMeasureResult();
  EXPECT_EQ(result->GetConstraintSpaceForCaching().CacheSlot(),
            NGCacheSlot::kMeasure);
  EXPECT_NE(result.get(), nullptr);
  EXPECT_EQ(result.get(), measure.get());
}

TEST_F(NGLayoutResultCachingTest, MissTableCellBaselineAlignment) {
  SetBodyInnerHTML(R"HTML(
    <style>
      td { vertical-align: baseline; }
    </style>
    <table>
      <td id="target">abc</td>
      <td><span style="font-size: 32px">def</span></td>
    </table>
  )HTML");

  auto* target = To<LayoutBlock>(GetLayoutObjectByElementId("target"));

  // "target" should align to the baseline, but miss the cache.
  scoped_refptr<const NGLayoutResult> result = target->GetCachedLayoutResult();
  scoped_refptr<const NGLayoutResult> measure =
      target->GetCachedMeasureResult();
  EXPECT_NE(measure.get(), nullptr);
  EXPECT_NE(result.get(), nullptr);
  EXPECT_EQ(measure->GetConstraintSpaceForCaching().CacheSlot(),
            NGCacheSlot::kMeasure);
  EXPECT_EQ(result->GetConstraintSpaceForCaching().CacheSlot(),
            NGCacheSlot::kLayout);
  EXPECT_NE(result.get(), measure.get());
}

TEST_F(NGLayoutResultCachingTest, MissTablePercent) {
  SetBodyInnerHTML(R"HTML(
    <style>
      .bfc { display: flow-root; width: 100px; }
      table { height: 100%; }
      caption { height: 50px; }
    </style>
    <div class="bfc" style="height: 50px;">
      <table id="test">
        <caption></caption>
        <td></td>
      </table>
    </div>
    <div class="bfc" style="height: 100px;">
      <table id="src">
        <caption></caption>
        <td></td>
      </table>
    </div>
  )HTML");

  auto* test = To<LayoutBlock>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlock>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitTableRowAdd) {
  SetBodyInnerHTML(R"HTML(
    <table>
      <tr><td>a</td><td>b</td></tr>
      <tr id="test"><td>text</td><td>more text</td></tr>
    </table>
    <table>
      <tr id="src"><td>text</td><td>more text</td></tr>
    </table>
  )HTML");

  auto* test = To<LayoutBlock>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlock>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissTableRowAdd) {
  SetBodyInnerHTML(R"HTML(
    <table>
      <tr><td>longwordhere</td><td>b</td></tr>
      <tr id="test"><td>text</td><td>more text</td></tr>
    </table>
    <table>
      <tr id="src"><td>text</td><td>more text</td></tr>
    </table>
  )HTML");

  auto* test = To<LayoutBlock>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlock>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitTableRowRemove) {
  SetBodyInnerHTML(R"HTML(
    <table>
      <tr id="test"><td>text</td><td>more text</td></tr>
    </table>
    <table>
      <tr><td>a</td><td>b</td></tr>
      <tr id="src"><td>text</td><td>more text</td></tr>
    </table>
  )HTML");

  auto* test = To<LayoutBlock>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlock>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissTableRowRemove) {
  SetBodyInnerHTML(R"HTML(
    <table>
      <tr id="test"><td>text</td><td>more text</td></tr>
    </table>
    <table>
      <tr><td>longwordhere</td><td>b</td></tr>
      <tr id="src"><td>text</td><td>more text</td></tr>
    </table>
  )HTML");

  auto* test = To<LayoutBlock>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlock>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitTableSectionAdd) {
  SetBodyInnerHTML(R"HTML(
    <table>
      <tbody><tr><td>a</td><td>b</td></tr></tbody>
      <tbody id="test"><tr><td>text</td><td>more text</td></tr></tbody>
    </table>
    <table>
      <tbody id="src"><tr><td>text</td><td>more text</td></tr></tbody>
    </table>
  )HTML");

  auto* test = To<LayoutBlock>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlock>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitTableSectionRemove) {
  SetBodyInnerHTML(R"HTML(
    <table>
      <tbody id="test"><tr><td>text</td><td>more text</td></tr></tbody>
    </table>
    <table>
      <tbody><tr><td>a</td><td>b</td></tr></tbody>
      <tbody id="src"><tr><td>text</td><td>more text</td></tr></tbody>
    </table>
  )HTML");

  auto* test = To<LayoutBlock>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlock>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result.get(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissFragmentainerSizeChange) {
  ScopedLayoutNGBlockFragmentationForTest block_frag(true);

  SetBodyInnerHTML(R"HTML(
    <style>
      .multicol { columns:2; column-fill:auto; height:100px; }
      .child { height:80px; }
    </style>
    <div class="multicol" style="height:50px;">
      <div id="test" class="child"></div>
    </div>
    <div class="multicol">
      <div id="src" class="child"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  EXPECT_NE(src->GetCachedLayoutResult(), nullptr);
  // Block-fragmented nodes aren't cacheable.
  EXPECT_EQ(test->GetCachedLayoutResult(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissBfcOffsetChangeInFragmentainer) {
  ScopedLayoutNGBlockFragmentationForTest block_frag(true);

  SetBodyInnerHTML(R"HTML(
    <style>
      .multicol { columns:2; column-fill:auto; height:100px; }
      .first { height:10px; }
      .second { display: flow-root; height:80px; }
    </style>
    <div class="multicol">
      <div class="first" style="height:50px;"></div>
      <div id="test" class="second"></div>
    </div>
    <div class="multicol">
      <div class="first"></div>
      <div id="src" class="second"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  EXPECT_NE(src->GetCachedLayoutResult(), nullptr);
  // Block-fragmented nodes aren't cached at all.
  EXPECT_EQ(test->GetCachedLayoutResult(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissBlockOffsetChangeInFragmentainer) {
  ScopedLayoutNGBlockFragmentationForTest block_frag(true);

  SetBodyInnerHTML(R"HTML(
    <style>
      .multicol { columns:2; column-fill:auto; height:100px; }
      .first { height:10px; }
      .second { height:80px; }
    </style>
    <div class="multicol">
      <div class="first" style="height:50px;"></div>
      <div id="test" class="second"></div>
    </div>
    <div class="multicol">
      <div class="first"></div>
      <div id="src" class="second"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  EXPECT_NE(src->GetCachedLayoutResult(), nullptr);
  // Block-fragmented nodes aren't cached at all.
  EXPECT_EQ(test->GetCachedLayoutResult(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, HitBlockOffsetUnchangedInFragmentainer) {
  ScopedLayoutNGBlockFragmentationForTest block_frag(true);

  SetBodyInnerHTML(R"HTML(
    <style>
      .multicol { columns:2; column-fill:auto; height:100px; }
      .third { height:50px; }
    </style>
    <div class="multicol">
      <div height="10px;"></div>
      <div height="20px;"></div>
      <div id="test" class="third"></div>
    </div>
    <div class="multicol">
      <div height="20px;"></div>
      <div height="10px;"></div>
      <div id="src" class="third"></div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;
  ASSERT_NE(src->GetCachedLayoutResult(), nullptr);
  ASSERT_NE(test->GetCachedLayoutResult(), nullptr);
  const NGConstraintSpace& space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kHit);
  EXPECT_NE(result, nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissMonolithicChangeInFragmentainer) {
  ScopedLayoutNGBlockFragmentationForTest block_frag(true);

  SetBodyInnerHTML(R"HTML(
    <style>
      .multicol { columns:2; column-fill:auto; height:100px; }
      .container { height:150px; }
      .child { height:150px; }
    </style>
    <div class="multicol">
      <div class="container">
        <div id="test" class="child"></div>
      </div>
    </div>
    <div class="multicol">
      <div class="container" style="contain:size;">
        <div id="src" class="child"></div>
      </div>
    </div>
  )HTML");

  auto* test = To<LayoutBlockFlow>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlockFlow>(GetLayoutObjectByElementId("src"));

  EXPECT_NE(src->GetCachedLayoutResult(), nullptr);
  // Block-fragmented nodes aren't cached at all.
  EXPECT_EQ(test->GetCachedLayoutResult(), nullptr);
}

TEST_F(NGLayoutResultCachingTest, MissGridIncorrectIntrinsicSize) {
  SetBodyInnerHTML(R"HTML(
    <!DOCTYPE html>
    <div style="display: flex; width: 100px; height: 200px; align-items: stretch;">
      <div id="test" style="flex-grow: 1; min-height: 100px; display: grid;">
        <div></div>
      </div>
    </div>
    <div style="display: flex; width: 100px; height: 200px; align-items: start;">
      <div id="src" style="flex-grow: 1; min-height: 100px; display: grid;">
        <div></div>
      </div>
    </div>
  )HTML");

  auto* test = To<LayoutBlock>(GetLayoutObjectByElementId("test"));
  auto* src = To<LayoutBlock>(GetLayoutObjectByElementId("src"));

  NGLayoutCacheStatus cache_status;
  absl::optional<NGFragmentGeometry> fragment_geometry;

  NGConstraintSpace space =
      src->GetCachedLayoutResult()->GetConstraintSpaceForCaching();
  scoped_refptr<const NGLayoutResult> result = test->CachedLayoutResult(
      space, nullptr, nullptr, &fragment_geometry, &cache_status);

  EXPECT_EQ(cache_status, NGLayoutCacheStatus::kNeedsLayout);
  EXPECT_EQ(result.get(), nullptr);
}

}  // namespace
}  // namespace blink
