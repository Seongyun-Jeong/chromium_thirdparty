// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/style_engine.h"

#include <memory>

#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/css/forced_colors.h"
#include "third_party/blink/public/common/css/navigation_controls.h"
#include "third_party/blink/public/platform/web_theme_engine.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_core.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_shadow_root_init.h"
#include "third_party/blink/renderer/core/animation/element_animations.h"
#include "third_party/blink/renderer/core/css/cascade_layer.h"
#include "third_party/blink/renderer/core/css/cascade_layer_map.h"
#include "third_party/blink/renderer/core/css/counter_style.h"
#include "third_party/blink/renderer/core/css/css_font_selector.h"
#include "third_party/blink/renderer/core/css/css_media_rule.h"
#include "third_party/blink/renderer/core/css/css_rule_list.h"
#include "third_party/blink/renderer/core/css/css_style_rule.h"
#include "third_party/blink/renderer/core/css/css_style_sheet.h"
#include "third_party/blink/renderer/core/css/css_test_helpers.h"
#include "third_party/blink/renderer/core/css/media_query_list.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_context.h"
#include "third_party/blink/renderer/core/css/properties/css_property_ref.h"
#include "third_party/blink/renderer/core/css/resolver/scoped_style_resolver.h"
#include "third_party/blink/renderer/core/css/style_sheet_contents.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/first_letter_pseudo_element.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/dom/slot_assignment_engine.h"
#include "third_party/blink/renderer/core/dom/text.h"
#include "third_party/blink/renderer/core/frame/frame_test_helpers.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/viewport_data.h"
#include "third_party/blink/renderer/core/html/forms/html_input_element.h"
#include "third_party/blink/renderer/core/html/forms/html_select_element.h"
#include "third_party/blink/renderer/core/html/html_collection.h"
#include "third_party/blink/renderer/core/html/html_element.h"
#include "third_party/blink/renderer/core/html/html_iframe_element.h"
#include "third_party/blink/renderer/core/html/html_span_element.h"
#include "third_party/blink/renderer/core/html/html_style_element.h"
#include "third_party/blink/renderer/core/layout/layout_counter.h"
#include "third_party/blink/renderer/core/layout/layout_custom_scrollbar_part.h"
#include "third_party/blink/renderer/core/layout/layout_list_marker.h"
#include "third_party/blink/renderer/core/layout/layout_text_fragment.h"
#include "third_party/blink/renderer/core/layout/layout_theme.h"
#include "third_party/blink/renderer/core/layout/list_marker.h"
#include "third_party/blink/renderer/core/page/viewport_description.h"
#include "third_party/blink/renderer/core/testing/color_scheme_helper.h"
#include "third_party/blink/renderer/core/testing/core_unit_test_helper.h"
#include "third_party/blink/renderer/core/testing/dummy_page_holder.h"
#include "third_party/blink/renderer/core/testing/sim/sim_request.h"
#include "third_party/blink/renderer/core/testing/sim/sim_test.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/thread_state.h"
#include "third_party/blink/renderer/platform/network/network_state_notifier.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"
#include "third_party/blink/renderer/platform/testing/testing_platform_support.h"
#include "third_party/blink/renderer/platform/testing/unit_test_helpers.h"
#include "ui/gfx/geometry/size_f.h"

namespace blink {

class StyleEngineTest : public testing::Test {
 protected:
  void SetUp() override;

  Document& GetDocument() { return dummy_page_holder_->GetDocument(); }
  StyleEngine& GetStyleEngine() { return GetDocument().GetStyleEngine(); }

  bool IsDocumentStyleSheetCollectionClean() {
    return !GetStyleEngine().ShouldUpdateDocumentStyleSheetCollection();
  }

  enum RuleSetInvalidation {
    kRuleSetInvalidationsScheduled,
    kRuleSetInvalidationFullRecalc
  };
  RuleSetInvalidation ScheduleInvalidationsForRules(TreeScope&,
                                                    const String& css_text);

  // A wrapper to add a reason for UpdateAllLifecyclePhases
  void UpdateAllLifecyclePhases() {
    GetDocument().View()->UpdateAllLifecyclePhasesForTest();
  }

  Node* GetStyleRecalcRoot() {
    return GetStyleEngine().style_recalc_root_.GetRootNode();
  }

  LayoutObject* GetParentForDetachedSubtree() {
    return GetStyleEngine().parent_for_detached_subtree_.Get();
  }

  const CSSValue* ComputedValue(Element* element, String property_name) {
    CSSPropertyRef ref(property_name, GetDocument());
    DCHECK(ref.IsValid());
    return ref.GetProperty().CSSValueFromComputedStyle(
        element->ComputedStyleRef(),
        /* layout_object */ nullptr,
        /* allow_visited_style */ false);
  }

  void InjectSheet(String key, WebDocument::CSSOrigin origin, String text) {
    auto* context = MakeGarbageCollected<CSSParserContext>(GetDocument());
    auto* sheet = MakeGarbageCollected<StyleSheetContents>(context);
    sheet->ParseString(text);
    GetStyleEngine().InjectSheet(StyleSheetKey(key), sheet, origin);
  }

  bool IsUseCounted(mojom::WebFeature feature) {
    return GetDocument().IsUseCounted(feature);
  }

  void ClearUseCounter(mojom::WebFeature feature) {
    GetDocument().ClearUseCounterForTesting(feature);
    DCHECK(!IsUseCounted(feature));
  }

  String GetListMarkerText(LayoutObject* list_item) {
    LayoutObject* marker = ListMarker::MarkerFromListItem(list_item);
    if (auto* legacy_marker = DynamicTo<LayoutListMarker>(marker)) {
      const CounterStyle& counter_style = legacy_marker->GetCounterStyle();
      return counter_style.GetPrefix() + legacy_marker->GetText() +
             counter_style.GetSuffix();
    }
    return ListMarker::Get(marker)->GetTextChild(*marker).GetText();
  }

  StyleRuleScrollTimeline* FindScrollTimelineRule(AtomicString name) {
    CSSScrollTimeline* timeline = GetStyleEngine().FindScrollTimeline(name);
    if (!timeline)
      return nullptr;
    return timeline->GetRule();
  }

 private:
  std::unique_ptr<DummyPageHolder> dummy_page_holder_;
};

class StyleEngineContainerQueryTest : public StyleEngineTest,
                                      private ScopedCSSContainerQueriesForTest,
                                      private ScopedLayoutNGForTest {
 public:
  StyleEngineContainerQueryTest()
      : ScopedCSSContainerQueriesForTest(true), ScopedLayoutNGForTest(true) {}
};

void StyleEngineTest::SetUp() {
  dummy_page_holder_ = std::make_unique<DummyPageHolder>(gfx::Size(800, 600));
}

StyleEngineTest::RuleSetInvalidation
StyleEngineTest::ScheduleInvalidationsForRules(TreeScope& tree_scope,
                                               const String& css_text) {
  auto* sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(
          kHTMLStandardMode, SecureContextMode::kInsecureContext));
  sheet->ParseString(css_text);
  HeapHashSet<Member<RuleSet>> rule_sets;
  RuleSet& rule_set =
      sheet->EnsureRuleSet(MediaQueryEvaluator(GetDocument().GetFrame()),
                           kRuleHasDocumentSecurityOrigin);
  rule_set.CompactRulesIfNeeded();
  if (rule_set.NeedsFullRecalcForRuleSetInvalidation())
    return kRuleSetInvalidationFullRecalc;
  rule_sets.insert(&rule_set);
  GetStyleEngine().ScheduleInvalidationsForRuleSets(tree_scope, rule_sets);
  return kRuleSetInvalidationsScheduled;
}

TEST_F(StyleEngineTest, DocumentDirtyAfterInject) {
  auto* parsed_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  parsed_sheet->ParseString("div {}");
  GetStyleEngine().InjectSheet("", parsed_sheet);
  EXPECT_FALSE(IsDocumentStyleSheetCollectionClean());
  UpdateAllLifecyclePhases();
  EXPECT_TRUE(IsDocumentStyleSheetCollectionClean());
}

TEST_F(StyleEngineTest, AnalyzedInject) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
     @font-face {
      font-family: 'Cool Font';
      src: url(dummy);
      font-weight: bold;
     }
     :root {
      --stop-color: black !important;
      --go-color: white;
     }
     #t1 { color: red !important }
     #t2 { color: black }
     #t4 { font-family: 'Cool Font'; font-weight: bold; font-style: italic }
     #t5 { animation-name: dummy-animation }
     #t6 { color: var(--stop-color); }
     #t7 { color: var(--go-color); }
     .red { color: red; }
     #t11 { color: white; }
    </style>
    <div id='t1'>Green</div>
    <div id='t2'>White</div>
    <div id='t3' style='color: black !important'>White</div>
    <div id='t4'>I look cool.</div>
    <div id='t5'>I animate!</div>
    <div id='t6'>Stop!</div>
    <div id='t7'>Go</div>
    <div id='t8' style='color: white !important'>screen: Red; print: Black</div>
    <div id='t9' class='red'>Green</div>
    <div id='t10' style='color: black !important'>Black</div>
    <div id='t11'>White</div>
    <div></div>
  )HTML");
  UpdateAllLifecyclePhases();

  Element* t1 = GetDocument().getElementById("t1");
  Element* t2 = GetDocument().getElementById("t2");
  Element* t3 = GetDocument().getElementById("t3");
  ASSERT_TRUE(t1);
  ASSERT_TRUE(t2);
  ASSERT_TRUE(t3);
  ASSERT_TRUE(t1->GetComputedStyle());
  ASSERT_TRUE(t2->GetComputedStyle());
  ASSERT_TRUE(t3->GetComputedStyle());
  EXPECT_EQ(MakeRGB(255, 0, 0), t1->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
  EXPECT_EQ(MakeRGB(0, 0, 0), t2->GetComputedStyle()->VisitedDependentColor(
                                  GetCSSPropertyColor()));
  EXPECT_EQ(MakeRGB(0, 0, 0), t3->GetComputedStyle()->VisitedDependentColor(
                                  GetCSSPropertyColor()));

  const unsigned initial_count = GetStyleEngine().StyleForElementCount();

  auto* green_parsed_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  green_parsed_sheet->ParseString(
      "#t1 { color: green !important }"
      "#t2 { color: white !important }"
      "#t3 { color: white }");
  StyleSheetKey green_key("green");
  GetStyleEngine().InjectSheet(green_key, green_parsed_sheet,
                               WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();

  EXPECT_EQ(3u, GetStyleEngine().StyleForElementCount() - initial_count);

  ASSERT_TRUE(t1->GetComputedStyle());
  ASSERT_TRUE(t2->GetComputedStyle());
  ASSERT_TRUE(t3->GetComputedStyle());

  // Important user rules override both regular and important author rules.
  EXPECT_EQ(MakeRGB(0, 128, 0), t1->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
  EXPECT_EQ(
      MakeRGB(255, 255, 255),
      t2->GetComputedStyle()->VisitedDependentColor(GetCSSPropertyColor()));
  EXPECT_EQ(MakeRGB(0, 0, 0), t3->GetComputedStyle()->VisitedDependentColor(
                                  GetCSSPropertyColor()));

  auto* blue_parsed_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  blue_parsed_sheet->ParseString(
      "#t1 { color: blue !important }"
      "#t2 { color: silver }"
      "#t3 { color: silver !important }");
  StyleSheetKey blue_key("blue");
  GetStyleEngine().InjectSheet(blue_key, blue_parsed_sheet,
                               WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();

  EXPECT_EQ(6u, GetStyleEngine().StyleForElementCount() - initial_count);

  ASSERT_TRUE(t1->GetComputedStyle());
  ASSERT_TRUE(t2->GetComputedStyle());
  ASSERT_TRUE(t3->GetComputedStyle());

  // Only important user rules override previously set important user rules.
  EXPECT_EQ(MakeRGB(0, 0, 255), t1->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
  EXPECT_EQ(
      MakeRGB(255, 255, 255),
      t2->GetComputedStyle()->VisitedDependentColor(GetCSSPropertyColor()));
  // Important user rules override inline author rules.
  EXPECT_EQ(
      MakeRGB(192, 192, 192),
      t3->GetComputedStyle()->VisitedDependentColor(GetCSSPropertyColor()));

  GetStyleEngine().RemoveInjectedSheet(green_key, WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(9u, GetStyleEngine().StyleForElementCount() - initial_count);
  ASSERT_TRUE(t1->GetComputedStyle());
  ASSERT_TRUE(t2->GetComputedStyle());
  ASSERT_TRUE(t3->GetComputedStyle());

  // Regular user rules do not override author rules.
  EXPECT_EQ(MakeRGB(0, 0, 255), t1->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
  EXPECT_EQ(MakeRGB(0, 0, 0), t2->GetComputedStyle()->VisitedDependentColor(
                                  GetCSSPropertyColor()));
  EXPECT_EQ(
      MakeRGB(192, 192, 192),
      t3->GetComputedStyle()->VisitedDependentColor(GetCSSPropertyColor()));

  GetStyleEngine().RemoveInjectedSheet(blue_key, WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(12u, GetStyleEngine().StyleForElementCount() - initial_count);
  ASSERT_TRUE(t1->GetComputedStyle());
  ASSERT_TRUE(t2->GetComputedStyle());
  ASSERT_TRUE(t3->GetComputedStyle());
  EXPECT_EQ(MakeRGB(255, 0, 0), t1->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
  EXPECT_EQ(MakeRGB(0, 0, 0), t2->GetComputedStyle()->VisitedDependentColor(
                                  GetCSSPropertyColor()));
  EXPECT_EQ(MakeRGB(0, 0, 0), t3->GetComputedStyle()->VisitedDependentColor(
                                  GetCSSPropertyColor()));

  // @font-face rules

  Element* t4 = GetDocument().getElementById("t4");
  ASSERT_TRUE(t4);
  ASSERT_TRUE(t4->GetComputedStyle());

  // There's only one font and it's bold and normal.
  EXPECT_EQ(1u, GetStyleEngine().GetFontSelector()->GetFontFaceCache()
                ->GetNumSegmentedFacesForTesting());
  CSSSegmentedFontFace* font_face =
      GetStyleEngine().GetFontSelector()->GetFontFaceCache()
      ->Get(t4->GetComputedStyle()->GetFontDescription(),
            AtomicString("Cool Font"));
  EXPECT_TRUE(font_face);
  FontSelectionCapabilities capabilities =
      font_face->GetFontSelectionCapabilities();
  ASSERT_EQ(capabilities.weight,
            FontSelectionRange({BoldWeightValue(), BoldWeightValue()}));
  ASSERT_EQ(capabilities.slope,
            FontSelectionRange({NormalSlopeValue(), NormalSlopeValue()}));

  auto* font_face_parsed_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  font_face_parsed_sheet->ParseString(
      "@font-face {"
      " font-family: 'Cool Font';"
      " src: url(dummy);"
      " font-weight: bold;"
      " font-style: italic;"
      "}");
  StyleSheetKey font_face_key("font_face");
  GetStyleEngine().InjectSheet(font_face_key, font_face_parsed_sheet,
                               WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();

  // After injecting a more specific font, now there are two and the
  // bold-italic one is selected.
  EXPECT_EQ(2u, GetStyleEngine().GetFontSelector()->GetFontFaceCache()
                ->GetNumSegmentedFacesForTesting());
  font_face = GetStyleEngine().GetFontSelector()->GetFontFaceCache()
              ->Get(t4->GetComputedStyle()->GetFontDescription(),
                    AtomicString("Cool Font"));
  EXPECT_TRUE(font_face);
  capabilities = font_face->GetFontSelectionCapabilities();
  ASSERT_EQ(capabilities.weight,
            FontSelectionRange({BoldWeightValue(), BoldWeightValue()}));
  ASSERT_EQ(capabilities.slope,
            FontSelectionRange({ItalicSlopeValue(), ItalicSlopeValue()}));

  auto* style_element = MakeGarbageCollected<HTMLStyleElement>(
      GetDocument(), CreateElementFlags());
  style_element->setInnerHTML(
      "@font-face {"
      " font-family: 'Cool Font';"
      " src: url(dummy);"
      " font-weight: normal;"
      " font-style: italic;"
      "}");
  GetDocument().body()->AppendChild(style_element);
  UpdateAllLifecyclePhases();

  // Now there are three fonts, but the newest one does not override the older,
  // better matching one.
  EXPECT_EQ(3u, GetStyleEngine().GetFontSelector()->GetFontFaceCache()
                ->GetNumSegmentedFacesForTesting());
  font_face = GetStyleEngine().GetFontSelector()->GetFontFaceCache()
              ->Get(t4->GetComputedStyle()->GetFontDescription(),
                    AtomicString("Cool Font"));
  EXPECT_TRUE(font_face);
  capabilities = font_face->GetFontSelectionCapabilities();
  ASSERT_EQ(capabilities.weight,
            FontSelectionRange({BoldWeightValue(), BoldWeightValue()}));
  ASSERT_EQ(capabilities.slope,
            FontSelectionRange({ItalicSlopeValue(), ItalicSlopeValue()}));

  GetStyleEngine().RemoveInjectedSheet(font_face_key, WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();

  // After removing the injected style sheet we're left with a bold-normal and
  // a normal-italic font, and the latter is selected by the matching algorithm
  // as font-style trumps font-weight.
  EXPECT_EQ(2u, GetStyleEngine().GetFontSelector()->GetFontFaceCache()
                ->GetNumSegmentedFacesForTesting());
  font_face = GetStyleEngine().GetFontSelector()->GetFontFaceCache()
              ->Get(t4->GetComputedStyle()->GetFontDescription(),
                    AtomicString("Cool Font"));
  EXPECT_TRUE(font_face);
  capabilities = font_face->GetFontSelectionCapabilities();
  ASSERT_EQ(capabilities.weight,
            FontSelectionRange({NormalWeightValue(), NormalWeightValue()}));
  ASSERT_EQ(capabilities.slope,
            FontSelectionRange({ItalicSlopeValue(), ItalicSlopeValue()}));

  // @keyframes rules

  Element* t5 = GetDocument().getElementById("t5");
  ASSERT_TRUE(t5);

  // There's no @keyframes rule named dummy-animation
  ASSERT_FALSE(GetStyleEngine().GetStyleResolver().FindKeyframesRule(
      t5, t5, AtomicString("dummy-animation")));

  auto* keyframes_parsed_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  keyframes_parsed_sheet->ParseString("@keyframes dummy-animation { from {} }");
  StyleSheetKey keyframes_key("keyframes");
  GetStyleEngine().InjectSheet(keyframes_key, keyframes_parsed_sheet,
                               WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();

  // After injecting the style sheet, a @keyframes rule named dummy-animation
  // is found with one keyframe.
  StyleRuleKeyframes* keyframes =
      GetStyleEngine().GetStyleResolver().FindKeyframesRule(
          t5, t5, AtomicString("dummy-animation"));
  ASSERT_TRUE(keyframes);
  EXPECT_EQ(1u, keyframes->Keyframes().size());

  style_element = MakeGarbageCollected<HTMLStyleElement>(GetDocument(),
                                                         CreateElementFlags());
  style_element->setInnerHTML("@keyframes dummy-animation { from {} to {} }");
  GetDocument().body()->AppendChild(style_element);
  UpdateAllLifecyclePhases();

  // Author @keyframes rules take precedence; now there are two keyframes (from
  // and to).
  keyframes = GetStyleEngine().GetStyleResolver().FindKeyframesRule(
      t5, t5, AtomicString("dummy-animation"));
  ASSERT_TRUE(keyframes);
  EXPECT_EQ(2u, keyframes->Keyframes().size());

  GetDocument().body()->RemoveChild(style_element);
  UpdateAllLifecyclePhases();

  keyframes = GetStyleEngine().GetStyleResolver().FindKeyframesRule(
      t5, t5, AtomicString("dummy-animation"));
  ASSERT_TRUE(keyframes);
  EXPECT_EQ(1u, keyframes->Keyframes().size());

  GetStyleEngine().RemoveInjectedSheet(keyframes_key, WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();

  // Injected @keyframes rules are no longer available once removed.
  ASSERT_FALSE(GetStyleEngine().GetStyleResolver().FindKeyframesRule(
      t5, t5, AtomicString("dummy-animation")));

  // Custom properties

  Element* t6 = GetDocument().getElementById("t6");
  Element* t7 = GetDocument().getElementById("t7");
  ASSERT_TRUE(t6);
  ASSERT_TRUE(t7);
  ASSERT_TRUE(t6->GetComputedStyle());
  ASSERT_TRUE(t7->GetComputedStyle());
  EXPECT_EQ(MakeRGB(0, 0, 0), t6->GetComputedStyle()->VisitedDependentColor(
                                  GetCSSPropertyColor()));
  EXPECT_EQ(
      MakeRGB(255, 255, 255),
      t7->GetComputedStyle()->VisitedDependentColor(GetCSSPropertyColor()));

  auto* custom_properties_parsed_sheet =
      MakeGarbageCollected<StyleSheetContents>(
          MakeGarbageCollected<CSSParserContext>(GetDocument()));
  custom_properties_parsed_sheet->ParseString(
      ":root {"
      " --stop-color: red !important;"
      " --go-color: green;"
      "}");
  StyleSheetKey custom_properties_key("custom_properties");
  GetStyleEngine().InjectSheet(custom_properties_key,
                               custom_properties_parsed_sheet,
                               WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();
  ASSERT_TRUE(t6->GetComputedStyle());
  ASSERT_TRUE(t7->GetComputedStyle());
  EXPECT_EQ(MakeRGB(255, 0, 0), t6->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
  EXPECT_EQ(
      MakeRGB(255, 255, 255),
      t7->GetComputedStyle()->VisitedDependentColor(GetCSSPropertyColor()));

  GetStyleEngine().RemoveInjectedSheet(custom_properties_key,
                                       WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();
  ASSERT_TRUE(t6->GetComputedStyle());
  ASSERT_TRUE(t7->GetComputedStyle());
  EXPECT_EQ(MakeRGB(0, 0, 0), t6->GetComputedStyle()->VisitedDependentColor(
                                  GetCSSPropertyColor()));
  EXPECT_EQ(
      MakeRGB(255, 255, 255),
      t7->GetComputedStyle()->VisitedDependentColor(GetCSSPropertyColor()));

  // Media queries

  Element* t8 = GetDocument().getElementById("t8");
  ASSERT_TRUE(t8);
  ASSERT_TRUE(t8->GetComputedStyle());
  EXPECT_EQ(
      MakeRGB(255, 255, 255),
      t8->GetComputedStyle()->VisitedDependentColor(GetCSSPropertyColor()));

  auto* media_queries_parsed_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  media_queries_parsed_sheet->ParseString(
      "@media screen {"
      " #t8 {"
      "  color: red !important;"
      " }"
      "}"
      "@media print {"
      " #t8 {"
      "  color: black !important;"
      " }"
      "}");
  StyleSheetKey media_queries_sheet_key("media_queries_sheet");
  GetStyleEngine().InjectSheet(media_queries_sheet_key,
                               media_queries_parsed_sheet,
                               WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();
  ASSERT_TRUE(t8->GetComputedStyle());
  EXPECT_EQ(MakeRGB(255, 0, 0), t8->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));

  gfx::SizeF page_size(400, 400);
  GetDocument().GetFrame()->StartPrinting(page_size, page_size, 1);
  ASSERT_TRUE(t8->GetComputedStyle());
  EXPECT_EQ(MakeRGB(0, 0, 0), t8->GetComputedStyle()->VisitedDependentColor(
                                  GetCSSPropertyColor()));

  GetDocument().GetFrame()->EndPrinting();
  ASSERT_TRUE(t8->GetComputedStyle());
  EXPECT_EQ(MakeRGB(255, 0, 0), t8->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));

  GetStyleEngine().RemoveInjectedSheet(media_queries_sheet_key,
                                       WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();
  ASSERT_TRUE(t8->GetComputedStyle());
  EXPECT_EQ(
      MakeRGB(255, 255, 255),
      t8->GetComputedStyle()->VisitedDependentColor(GetCSSPropertyColor()));

  // Author style sheets

  Element* t9 = GetDocument().getElementById("t9");
  Element* t10 = GetDocument().getElementById("t10");
  ASSERT_TRUE(t9);
  ASSERT_TRUE(t10);
  ASSERT_TRUE(t9->GetComputedStyle());
  ASSERT_TRUE(t10->GetComputedStyle());
  EXPECT_EQ(MakeRGB(255, 0, 0), t9->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
  EXPECT_EQ(MakeRGB(0, 0, 0), t10->GetComputedStyle()->VisitedDependentColor(
                                   GetCSSPropertyColor()));

  auto* parsed_author_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  parsed_author_sheet->ParseString(
      "#t9 {"
      " color: green;"
      "}"
      "#t10 {"
      " color: white !important;"
      "}");
  StyleSheetKey author_sheet_key("author_sheet");
  GetStyleEngine().InjectSheet(author_sheet_key, parsed_author_sheet,
                               WebDocument::kAuthorOrigin);
  UpdateAllLifecyclePhases();
  ASSERT_TRUE(t9->GetComputedStyle());
  ASSERT_TRUE(t10->GetComputedStyle());

  // Specificity works within author origin.
  EXPECT_EQ(MakeRGB(0, 128, 0), t9->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
  // Important author rules do not override important inline author rules.
  EXPECT_EQ(MakeRGB(0, 0, 0), t10->GetComputedStyle()->VisitedDependentColor(
                                   GetCSSPropertyColor()));

  GetStyleEngine().RemoveInjectedSheet(author_sheet_key,
                                       WebDocument::kAuthorOrigin);
  UpdateAllLifecyclePhases();
  ASSERT_TRUE(t9->GetComputedStyle());
  ASSERT_TRUE(t10->GetComputedStyle());
  EXPECT_EQ(MakeRGB(255, 0, 0), t9->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
  EXPECT_EQ(MakeRGB(0, 0, 0), t10->GetComputedStyle()->VisitedDependentColor(
                                   GetCSSPropertyColor()));

  // Style sheet removal

  Element* t11 = GetDocument().getElementById("t11");
  ASSERT_TRUE(t11);
  ASSERT_TRUE(t11->GetComputedStyle());
  EXPECT_EQ(
      MakeRGB(255, 255, 255),
      t11->GetComputedStyle()->VisitedDependentColor(GetCSSPropertyColor()));

  auto* parsed_removable_red_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  parsed_removable_red_sheet->ParseString("#t11 { color: red !important; }");
  StyleSheetKey removable_red_sheet_key("removable_red_sheet");
  GetStyleEngine().InjectSheet(removable_red_sheet_key,
                               parsed_removable_red_sheet,
                               WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();
  ASSERT_TRUE(t11->GetComputedStyle());

  EXPECT_EQ(MakeRGB(255, 0, 0), t11->GetComputedStyle()->VisitedDependentColor(
                                     GetCSSPropertyColor()));

  auto* parsed_removable_green_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  parsed_removable_green_sheet->ParseString(
      "#t11 { color: green !important; }");
  StyleSheetKey removable_green_sheet_key("removable_green_sheet");
  GetStyleEngine().InjectSheet(removable_green_sheet_key,
                               parsed_removable_green_sheet,
                               WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();
  ASSERT_TRUE(t11->GetComputedStyle());

  EXPECT_EQ(MakeRGB(0, 128, 0), t11->GetComputedStyle()->VisitedDependentColor(
                                     GetCSSPropertyColor()));

  auto* parsed_removable_red_sheet2 = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  parsed_removable_red_sheet2->ParseString("#t11 { color: red !important; }");
  GetStyleEngine().InjectSheet(removable_red_sheet_key,
                               parsed_removable_red_sheet2,
                               WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();
  ASSERT_TRUE(t11->GetComputedStyle());

  EXPECT_EQ(MakeRGB(255, 0, 0), t11->GetComputedStyle()->VisitedDependentColor(
                                     GetCSSPropertyColor()));

  GetStyleEngine().RemoveInjectedSheet(removable_red_sheet_key,
                                       WebDocument::kAuthorOrigin);
  UpdateAllLifecyclePhases();
  ASSERT_TRUE(t11->GetComputedStyle());

  // Removal works only within the same origin.
  EXPECT_EQ(MakeRGB(255, 0, 0), t11->GetComputedStyle()->VisitedDependentColor(
                                     GetCSSPropertyColor()));

  GetStyleEngine().RemoveInjectedSheet(removable_red_sheet_key,
                                       WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();
  ASSERT_TRUE(t11->GetComputedStyle());

  // The last sheet with the given key is removed.
  EXPECT_EQ(MakeRGB(0, 128, 0), t11->GetComputedStyle()->VisitedDependentColor(
                                     GetCSSPropertyColor()));

  GetStyleEngine().RemoveInjectedSheet(removable_green_sheet_key,
                                       WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();
  ASSERT_TRUE(t11->GetComputedStyle());

  // Only the last sheet with the given key is removed.
  EXPECT_EQ(MakeRGB(255, 0, 0), t11->GetComputedStyle()->VisitedDependentColor(
                                     GetCSSPropertyColor()));

  GetStyleEngine().RemoveInjectedSheet(removable_red_sheet_key,
                                       WebDocument::kUserOrigin);
  UpdateAllLifecyclePhases();
  ASSERT_TRUE(t11->GetComputedStyle());

  EXPECT_EQ(
      MakeRGB(255, 255, 255),
      t11->GetComputedStyle()->VisitedDependentColor(GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, InjectedUserNoAuthorFontFace) {
  UpdateAllLifecyclePhases();

  FontDescription font_description;
  FontFaceCache* cache = GetStyleEngine().GetFontSelector()->GetFontFaceCache();
  EXPECT_FALSE(cache->Get(font_description, "User"));

  auto* user_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  user_sheet->ParseString(
      "@font-face {"
      "  font-family: 'User';"
      "  src: url(font.ttf);"
      "}");

  StyleSheetKey user_key("user");
  GetStyleEngine().InjectSheet(user_key, user_sheet, WebDocument::kUserOrigin);

  UpdateAllLifecyclePhases();

  EXPECT_TRUE(cache->Get(font_description, "User"));
}

TEST_F(StyleEngineTest, InjectedFontFace) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
     @font-face {
      font-family: 'Author';
      src: url(user);
     }
    </style>
  )HTML");
  UpdateAllLifecyclePhases();

  FontDescription font_description;
  FontFaceCache* cache = GetStyleEngine().GetFontSelector()->GetFontFaceCache();
  EXPECT_TRUE(cache->Get(font_description, "Author"));
  EXPECT_FALSE(cache->Get(font_description, "User"));

  auto* user_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  user_sheet->ParseString(
      "@font-face {"
      "  font-family: 'User';"
      "  src: url(author);"
      "}");

  StyleSheetKey user_key("user");
  GetStyleEngine().InjectSheet(user_key, user_sheet, WebDocument::kUserOrigin);

  UpdateAllLifecyclePhases();

  EXPECT_TRUE(cache->Get(font_description, "Author"));
  EXPECT_TRUE(cache->Get(font_description, "User"));
}

TEST_F(StyleEngineTest, IgnoreInvalidPropertyValue) {
  GetDocument().body()->setInnerHTML(
      "<section><div id='t1'>Red</div></section>"
      "<style id='s1'>div { color: red; } section div#t1 { color:rgb(0");
  UpdateAllLifecyclePhases();

  Element* t1 = GetDocument().getElementById("t1");
  ASSERT_TRUE(t1);
  ASSERT_TRUE(t1->GetComputedStyle());
  EXPECT_EQ(MakeRGB(255, 0, 0), t1->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, TextToSheetCache) {
  auto* element = MakeGarbageCollected<HTMLStyleElement>(GetDocument(),
                                                         CreateElementFlags());

  String sheet_text("div {}");
  TextPosition min_pos = TextPosition::MinimumPosition();
  StyleEngineContext context;

  CSSStyleSheet* sheet1 =
      GetStyleEngine().CreateSheet(*element, sheet_text, min_pos, context);

  // Check that the first sheet is not using a cached StyleSheetContents.
  EXPECT_FALSE(sheet1->Contents()->IsUsedFromTextCache());

  CSSStyleSheet* sheet2 =
      GetStyleEngine().CreateSheet(*element, sheet_text, min_pos, context);

  // Check that the second sheet uses the cached StyleSheetContents for the
  // first.
  EXPECT_EQ(sheet1->Contents(), sheet2->Contents());
  EXPECT_TRUE(sheet2->Contents()->IsUsedFromTextCache());

  sheet1 = nullptr;
  sheet2 = nullptr;
  element = nullptr;

  // Garbage collection should clear the weak reference in the
  // StyleSheetContents cache.
  ThreadState::Current()->CollectAllGarbageForTesting();

  element = MakeGarbageCollected<HTMLStyleElement>(GetDocument(),
                                                   CreateElementFlags());
  sheet1 = GetStyleEngine().CreateSheet(*element, sheet_text, min_pos, context);

  // Check that we did not use a cached StyleSheetContents after the garbage
  // collection.
  EXPECT_FALSE(sheet1->Contents()->IsUsedFromTextCache());
}

TEST_F(StyleEngineTest, RuleSetInvalidationTypeSelectors) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <div>
      <span></span>
      <div></div>
    </div>
    <b></b><b></b><b></b><b></b>
    <i id=i>
      <i>
        <b></b>
      </i>
    </i>
  )HTML");

  UpdateAllLifecyclePhases();

  unsigned before_count = GetStyleEngine().StyleForElementCount();
  EXPECT_EQ(kRuleSetInvalidationsScheduled,
            ScheduleInvalidationsForRules(GetDocument(),
                                          "span { background: green}"));
  UpdateAllLifecyclePhases();
  unsigned after_count = GetStyleEngine().StyleForElementCount();
  EXPECT_EQ(1u, after_count - before_count);

  before_count = after_count;
  EXPECT_EQ(kRuleSetInvalidationsScheduled,
            ScheduleInvalidationsForRules(GetDocument(),
                                          "body div { background: green}"));
  UpdateAllLifecyclePhases();
  after_count = GetStyleEngine().StyleForElementCount();
  EXPECT_EQ(2u, after_count - before_count);

  EXPECT_EQ(kRuleSetInvalidationFullRecalc,
            ScheduleInvalidationsForRules(GetDocument(),
                                          "div * { background: green}"));
  UpdateAllLifecyclePhases();

  before_count = GetStyleEngine().StyleForElementCount();
  EXPECT_EQ(kRuleSetInvalidationsScheduled,
            ScheduleInvalidationsForRules(GetDocument(),
                                          "#i b { background: green}"));
  UpdateAllLifecyclePhases();
  after_count = GetStyleEngine().StyleForElementCount();
  EXPECT_EQ(1u, after_count - before_count);
}

TEST_F(StyleEngineTest, RuleSetInvalidationCustomPseudo) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>progress { -webkit-appearance:none }</style>
    <progress></progress>
    <div></div><div></div><div></div><div></div><div></div><div></div>
  )HTML");

  UpdateAllLifecyclePhases();

  unsigned before_count = GetStyleEngine().StyleForElementCount();
  EXPECT_EQ(ScheduleInvalidationsForRules(
                GetDocument(), "::-webkit-progress-bar { background: green }"),
            kRuleSetInvalidationsScheduled);
  UpdateAllLifecyclePhases();
  unsigned after_count = GetStyleEngine().StyleForElementCount();
  EXPECT_EQ(3u, after_count - before_count);
}

TEST_F(StyleEngineTest, RuleSetInvalidationHost) {
  GetDocument().body()->setInnerHTML(
      "<div id=nohost></div><div id=host></div>");
  Element* host = GetDocument().getElementById("host");
  ASSERT_TRUE(host);

  ShadowRoot& shadow_root =
      host->AttachShadowRootInternal(ShadowRootType::kOpen);

  shadow_root.setInnerHTML("<div></div><div></div><div></div>");
  UpdateAllLifecyclePhases();

  unsigned before_count = GetStyleEngine().StyleForElementCount();
  EXPECT_EQ(ScheduleInvalidationsForRules(
                shadow_root, ":host(#nohost), #nohost { background: green}"),
            kRuleSetInvalidationsScheduled);
  UpdateAllLifecyclePhases();
  unsigned after_count = GetStyleEngine().StyleForElementCount();
  EXPECT_EQ(0u, after_count - before_count);

  before_count = after_count;
  EXPECT_EQ(ScheduleInvalidationsForRules(shadow_root,
                                          ":host(#host) { background: green}"),
            kRuleSetInvalidationsScheduled);
  UpdateAllLifecyclePhases();
  after_count = GetStyleEngine().StyleForElementCount();
  EXPECT_EQ(1u, after_count - before_count);
  EXPECT_EQ(ScheduleInvalidationsForRules(shadow_root,
                                          ":host(div) { background: green}"),
            kRuleSetInvalidationsScheduled);

  EXPECT_EQ(ScheduleInvalidationsForRules(shadow_root,
                                          ":host(*) { background: green}"),
            kRuleSetInvalidationFullRecalc);
  EXPECT_EQ(ScheduleInvalidationsForRules(
                shadow_root, ":host(*) :hover { background: green}"),
            kRuleSetInvalidationFullRecalc);
}

TEST_F(StyleEngineTest, RuleSetInvalidationSlotted) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <div id=host>
      <span slot=other class=s1></span>
      <span class=s2></span>
      <span class=s1></span>
      <span></span>
    </div>
  )HTML");

  Element* host = GetDocument().getElementById("host");
  ASSERT_TRUE(host);

  ShadowRoot& shadow_root =
      host->AttachShadowRootInternal(ShadowRootType::kOpen);

  shadow_root.setInnerHTML("<slot name=other></slot><slot></slot>");
  UpdateAllLifecyclePhases();

  unsigned before_count = GetStyleEngine().StyleForElementCount();
  EXPECT_EQ(ScheduleInvalidationsForRules(
                shadow_root, "::slotted(.s1) { background: green}"),
            kRuleSetInvalidationsScheduled);
  UpdateAllLifecyclePhases();
  unsigned after_count = GetStyleEngine().StyleForElementCount();
  EXPECT_EQ(4u, after_count - before_count);

  EXPECT_EQ(ScheduleInvalidationsForRules(shadow_root,
                                          "::slotted(*) { background: green}"),
            kRuleSetInvalidationFullRecalc);
}

TEST_F(StyleEngineTest, RuleSetInvalidationHostContext) {
  GetDocument().body()->setInnerHTML("<div id=host></div>");
  Element* host = GetDocument().getElementById("host");
  ASSERT_TRUE(host);

  ShadowRoot& shadow_root =
      host->AttachShadowRootInternal(ShadowRootType::kOpen);

  shadow_root.setInnerHTML("<div></div><div class=a></div><div></div>");
  UpdateAllLifecyclePhases();

  unsigned before_count = GetStyleEngine().StyleForElementCount();
  EXPECT_EQ(ScheduleInvalidationsForRules(
                shadow_root, ":host-context(.nomatch) .a { background: green}"),
            kRuleSetInvalidationsScheduled);
  UpdateAllLifecyclePhases();
  unsigned after_count = GetStyleEngine().StyleForElementCount();
  EXPECT_EQ(1u, after_count - before_count);

  EXPECT_EQ(ScheduleInvalidationsForRules(
                shadow_root, ":host-context(:hover) { background: green}"),
            kRuleSetInvalidationFullRecalc);
  EXPECT_EQ(ScheduleInvalidationsForRules(
                shadow_root, ":host-context(#host) { background: green}"),
            kRuleSetInvalidationFullRecalc);
}

TEST_F(StyleEngineTest, HasViewportDependentMediaQueries) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>div {}</style>
    <style id='sheet' media='(min-width: 200px)'>
      div {}
    </style>
  )HTML");

  Element* style_element = GetDocument().getElementById("sheet");

  for (unsigned i = 0; i < 10; i++) {
    GetDocument().body()->RemoveChild(style_element);
    UpdateAllLifecyclePhases();
    GetDocument().body()->AppendChild(style_element);
    UpdateAllLifecyclePhases();
  }

  EXPECT_TRUE(GetStyleEngine().HasViewportDependentMediaQueries());

  GetDocument().body()->RemoveChild(style_element);
  UpdateAllLifecyclePhases();

  EXPECT_FALSE(GetStyleEngine().HasViewportDependentMediaQueries());
}

TEST_F(StyleEngineTest, StyleMediaAttributeStyleChange) {
  GetDocument().body()->setInnerHTML(
      "<style id='s1' media='(max-width: 1px)'>#t1 { color: green }</style>"
      "<div id='t1'>Green</div><div></div>");
  UpdateAllLifecyclePhases();

  Element* t1 = GetDocument().getElementById("t1");
  ASSERT_TRUE(t1);
  ASSERT_TRUE(t1->GetComputedStyle());
  EXPECT_EQ(MakeRGB(0, 0, 0), t1->GetComputedStyle()->VisitedDependentColor(
                                  GetCSSPropertyColor()));

  unsigned before_count = GetStyleEngine().StyleForElementCount();

  Element* s1 = GetDocument().getElementById("s1");
  s1->setAttribute(blink::html_names::kMediaAttr, "(max-width: 2000px)");
  UpdateAllLifecyclePhases();

  unsigned after_count = GetStyleEngine().StyleForElementCount();
  EXPECT_EQ(1u, after_count - before_count);

  ASSERT_TRUE(t1->GetComputedStyle());
  EXPECT_EQ(MakeRGB(0, 128, 0), t1->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, StyleMediaAttributeNoStyleChange) {
  GetDocument().body()->setInnerHTML(
      "<style id='s1' media='(max-width: 1000px)'>#t1 { color: green }</style>"
      "<div id='t1'>Green</div><div></div>");
  UpdateAllLifecyclePhases();

  Element* t1 = GetDocument().getElementById("t1");
  ASSERT_TRUE(t1);
  ASSERT_TRUE(t1->GetComputedStyle());
  EXPECT_EQ(MakeRGB(0, 128, 0), t1->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));

  unsigned before_count = GetStyleEngine().StyleForElementCount();

  Element* s1 = GetDocument().getElementById("s1");
  s1->setAttribute(blink::html_names::kMediaAttr, "(max-width: 2000px)");
  UpdateAllLifecyclePhases();

  unsigned after_count = GetStyleEngine().StyleForElementCount();
  EXPECT_EQ(0u, after_count - before_count);

  ASSERT_TRUE(t1->GetComputedStyle());
  EXPECT_EQ(MakeRGB(0, 128, 0), t1->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, ModifyStyleRuleMatchedPropertiesCache) {
  // Test that the MatchedPropertiesCache is cleared when a StyleRule is
  // modified. The MatchedPropertiesCache caches results based on
  // CSSPropertyValueSet pointers. When a mutable CSSPropertyValueSet is
  // modified, the pointer doesn't change, yet the declarations do.

  GetDocument().body()->setInnerHTML(
      "<style id='s1'>#t1 { color: blue }</style>"
      "<div id='t1'>Green</div>");
  UpdateAllLifecyclePhases();

  Element* t1 = GetDocument().getElementById("t1");
  ASSERT_TRUE(t1);
  ASSERT_TRUE(t1->GetComputedStyle());
  EXPECT_EQ(MakeRGB(0, 0, 255), t1->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));

  auto* sheet = To<CSSStyleSheet>(GetDocument().StyleSheets().item(0));
  ASSERT_TRUE(sheet);
  DummyExceptionStateForTesting exception_state;
  ASSERT_TRUE(sheet->cssRules(exception_state));
  CSSStyleRule* style_rule =
      To<CSSStyleRule>(sheet->cssRules(exception_state)->item(0));
  ASSERT_FALSE(exception_state.HadException());
  ASSERT_TRUE(style_rule);
  ASSERT_TRUE(style_rule->style());

  // Modify the CSSPropertyValueSet once to make it a mutable set. Subsequent
  // modifications will not change the CSSPropertyValueSet pointer and cache
  // hash value will be the same.
  style_rule->style()->setProperty(GetDocument().GetExecutionContext(), "color",
                                   "red", "", ASSERT_NO_EXCEPTION);
  UpdateAllLifecyclePhases();

  ASSERT_TRUE(t1->GetComputedStyle());
  EXPECT_EQ(MakeRGB(255, 0, 0), t1->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));

  style_rule->style()->setProperty(GetDocument().GetExecutionContext(), "color",
                                   "green", "", ASSERT_NO_EXCEPTION);
  UpdateAllLifecyclePhases();

  ASSERT_TRUE(t1->GetComputedStyle());
  EXPECT_EQ(MakeRGB(0, 128, 0), t1->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, VisitedExplicitInheritanceMatchedPropertiesCache) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      :visited { overflow: inherit }
    </style>
    <span id="span"><a href></a></span>
  )HTML");
  UpdateAllLifecyclePhases();

  Element* span = GetDocument().getElementById("span");
  const ComputedStyle* style = span->GetComputedStyle();
  EXPECT_FALSE(style->ChildHasExplicitInheritance());

  style = span->firstChild()->GetComputedStyle();
  EXPECT_TRUE(MatchedPropertiesCache::IsStyleCacheable(*style));

  span->SetInlineStyleProperty(CSSPropertyID::kColor, "blue");

  // Should not DCHECK on applying overflow:inherit on cached matched properties
  UpdateAllLifecyclePhases();
}

TEST_F(StyleEngineTest, ScheduleInvalidationAfterSubtreeRecalc) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style id='s1'>
      .t1 span { color: green }
      .t2 span { color: green }
    </style>
    <style id='s2'>div { background: lime }</style>
    <div id='t1'></div>
    <div id='t2'></div>
  )HTML");
  UpdateAllLifecyclePhases();

  Element* t1 = GetDocument().getElementById("t1");
  Element* t2 = GetDocument().getElementById("t2");
  ASSERT_TRUE(t1);
  ASSERT_TRUE(t2);

  // Sanity test.
  t1->setAttribute(blink::html_names::kClassAttr, "t1");
  EXPECT_FALSE(GetDocument().NeedsStyleInvalidation());
  EXPECT_TRUE(GetDocument().ChildNeedsStyleInvalidation());
  EXPECT_TRUE(t1->NeedsStyleInvalidation());

  UpdateAllLifecyclePhases();

  // platformColorsChanged() triggers SubtreeStyleChange on document(). If that
  // for some reason should change, this test will start failing and the
  // SubtreeStyleChange must be set another way.
  // Calling setNeedsStyleRecalc() explicitly with an arbitrary reason instead
  // requires us to CORE_EXPORT the reason strings.
  GetStyleEngine().PlatformColorsChanged();

  // Check that no invalidations sets are scheduled when the document node is
  // already SubtreeStyleChange.
  t2->setAttribute(blink::html_names::kClassAttr, "t2");
  EXPECT_FALSE(GetDocument().NeedsStyleInvalidation());
  EXPECT_FALSE(GetDocument().ChildNeedsStyleInvalidation());

  UpdateAllLifecyclePhases();
  auto* s2 = To<HTMLStyleElement>(GetDocument().getElementById("s2"));
  ASSERT_TRUE(s2);
  s2->setDisabled(true);
  GetStyleEngine().UpdateActiveStyle();
  EXPECT_FALSE(GetDocument().ChildNeedsStyleInvalidation());
  EXPECT_TRUE(GetDocument().NeedsStyleInvalidation());

  UpdateAllLifecyclePhases();
  GetStyleEngine().PlatformColorsChanged();
  s2->setDisabled(false);
  GetStyleEngine().UpdateActiveStyle();
  EXPECT_FALSE(GetDocument().ChildNeedsStyleInvalidation());
  EXPECT_FALSE(GetDocument().NeedsStyleInvalidation());

  UpdateAllLifecyclePhases();
  auto* s1 = To<HTMLStyleElement>(GetDocument().getElementById("s1"));
  ASSERT_TRUE(s1);
  s1->setDisabled(true);
  GetStyleEngine().UpdateActiveStyle();
  EXPECT_TRUE(GetDocument().ChildNeedsStyleInvalidation());
  EXPECT_FALSE(GetDocument().NeedsStyleInvalidation());
  EXPECT_TRUE(t1->NeedsStyleInvalidation());
  EXPECT_TRUE(t2->NeedsStyleInvalidation());

  UpdateAllLifecyclePhases();
  GetStyleEngine().PlatformColorsChanged();
  s1->setDisabled(false);
  GetStyleEngine().UpdateActiveStyle();
  EXPECT_FALSE(GetDocument().ChildNeedsStyleInvalidation());
  EXPECT_FALSE(GetDocument().NeedsStyleInvalidation());
  EXPECT_FALSE(t1->NeedsStyleInvalidation());
  EXPECT_FALSE(t2->NeedsStyleInvalidation());
}

TEST_F(StyleEngineTest, ScheduleRuleSetInvalidationsOnNewShadow) {
  GetDocument().body()->setInnerHTML("<div id='host'></div>");
  Element* host = GetDocument().getElementById("host");
  ASSERT_TRUE(host);

  UpdateAllLifecyclePhases();
  ShadowRoot& shadow_root =
      host->AttachShadowRootInternal(ShadowRootType::kOpen);

  shadow_root.setInnerHTML(R"HTML(
    <style>
      span { color: green }
      t1 { color: green }
    </style>
    <div id='t1'></div>
    <span></span>
  )HTML");

  GetStyleEngine().UpdateActiveStyle();
  EXPECT_TRUE(GetDocument().ChildNeedsStyleInvalidation());
  EXPECT_FALSE(GetDocument().NeedsStyleInvalidation());
  EXPECT_TRUE(shadow_root.NeedsStyleInvalidation());
}

TEST_F(StyleEngineTest, EmptyHttpEquivDefaultStyle) {
  GetDocument().body()->setInnerHTML(
      "<style>div { color:pink }</style><div id=container></div>");
  UpdateAllLifecyclePhases();

  EXPECT_FALSE(GetStyleEngine().NeedsActiveStyleUpdate());

  Element* container = GetDocument().getElementById("container");
  ASSERT_TRUE(container);
  container->setInnerHTML("<meta http-equiv='default-style' content=''>");
  EXPECT_FALSE(GetStyleEngine().NeedsActiveStyleUpdate());

  container->setInnerHTML(
      "<meta http-equiv='default-style' content='preferred'>");
  EXPECT_TRUE(GetStyleEngine().NeedsActiveStyleUpdate());
}

TEST_F(StyleEngineTest, StyleSheetsForStyleSheetList_Document) {
  GetDocument().body()->setInnerHTML("<style>span { color: green }</style>");
  EXPECT_TRUE(GetStyleEngine().NeedsActiveStyleUpdate());

  const auto& sheet_list =
      GetStyleEngine().StyleSheetsForStyleSheetList(GetDocument());
  EXPECT_EQ(1u, sheet_list.size());
  EXPECT_TRUE(GetStyleEngine().NeedsActiveStyleUpdate());

  GetDocument().body()->setInnerHTML(
      "<style>span { color: green }</style><style>div { color: pink }</style>");
  EXPECT_TRUE(GetStyleEngine().NeedsActiveStyleUpdate());

  const auto& second_sheet_list =
      GetStyleEngine().StyleSheetsForStyleSheetList(GetDocument());
  EXPECT_EQ(2u, second_sheet_list.size());
  EXPECT_TRUE(GetStyleEngine().NeedsActiveStyleUpdate());
}

TEST_F(StyleEngineTest, StyleSheetsForStyleSheetList_ShadowRoot) {
  GetDocument().body()->setInnerHTML("<div id='host'></div>");
  Element* host = GetDocument().getElementById("host");
  ASSERT_TRUE(host);

  UpdateAllLifecyclePhases();
  ShadowRoot& shadow_root =
      host->AttachShadowRootInternal(ShadowRootType::kOpen);

  shadow_root.setInnerHTML("<style>span { color: green }</style>");
  EXPECT_TRUE(GetStyleEngine().NeedsActiveStyleUpdate());

  const auto& sheet_list =
      GetStyleEngine().StyleSheetsForStyleSheetList(shadow_root);
  EXPECT_EQ(1u, sheet_list.size());
  EXPECT_TRUE(GetStyleEngine().NeedsActiveStyleUpdate());

  shadow_root.setInnerHTML(
      "<style>span { color: green }</style><style>div { color: pink }</style>");
  EXPECT_TRUE(GetStyleEngine().NeedsActiveStyleUpdate());

  const auto& second_sheet_list =
      GetStyleEngine().StyleSheetsForStyleSheetList(shadow_root);
  EXPECT_EQ(2u, second_sheet_list.size());
  EXPECT_TRUE(GetStyleEngine().NeedsActiveStyleUpdate());
}

TEST_F(StyleEngineTest, ViewportDescriptionForZoomDSF) {
  ScopedTestingPlatformSupport<TestingPlatformSupport> platform;
  platform->SetUseZoomForDSF(true);
  frame_test_helpers::WebViewHelper web_view_helper;
  WebViewImpl* web_view_impl = web_view_helper.Initialize();
  web_view_impl->MainFrameWidget()->SetDeviceScaleFactorForTesting(1.f);
  web_view_impl->MainFrameWidget()->UpdateAllLifecyclePhases(
      DocumentUpdateReason::kTest);

  Document* document =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame())->GetDocument();

  auto desc = document->GetViewportData().GetViewportDescription();
  float min_width = desc.min_width.GetFloatValue();
  float max_width = desc.max_width.GetFloatValue();
  float min_height = desc.min_height.GetFloatValue();
  float max_height = desc.max_height.GetFloatValue();

  const float device_scale = 3.5f;
  web_view_impl->MainFrameWidget()->SetDeviceScaleFactorForTesting(
      device_scale);
  web_view_impl->MainFrameWidget()->UpdateAllLifecyclePhases(
      DocumentUpdateReason::kTest);

  desc = document->GetViewportData().GetViewportDescription();
  EXPECT_FLOAT_EQ(device_scale * min_width, desc.min_width.GetFloatValue());
  EXPECT_FLOAT_EQ(device_scale * max_width, desc.max_width.GetFloatValue());
  EXPECT_FLOAT_EQ(device_scale * min_height, desc.min_height.GetFloatValue());
  EXPECT_FLOAT_EQ(device_scale * max_height, desc.max_height.GetFloatValue());
}

TEST_F(StyleEngineTest, MediaQueryAffectingValueChanged_StyleElementNoMedia) {
  GetDocument().body()->setInnerHTML("<style>div{color:pink}</style>");
  UpdateAllLifecyclePhases();
  GetStyleEngine().MediaQueryAffectingValueChanged(MediaValueChange::kOther);
  EXPECT_FALSE(GetStyleEngine().NeedsActiveStyleUpdate());
}

TEST_F(StyleEngineTest,
       MediaQueryAffectingValueChanged_StyleElementMediaNoValue) {
  GetDocument().body()->setInnerHTML("<style media>div{color:pink}</style>");
  UpdateAllLifecyclePhases();
  GetStyleEngine().MediaQueryAffectingValueChanged(MediaValueChange::kOther);
  EXPECT_FALSE(GetStyleEngine().NeedsActiveStyleUpdate());
}

TEST_F(StyleEngineTest,
       MediaQueryAffectingValueChanged_StyleElementMediaEmpty) {
  GetDocument().body()->setInnerHTML("<style media=''>div{color:pink}</style>");
  UpdateAllLifecyclePhases();
  GetStyleEngine().MediaQueryAffectingValueChanged(MediaValueChange::kOther);
  EXPECT_FALSE(GetStyleEngine().NeedsActiveStyleUpdate());
}

// TODO(futhark@chromium.org): The test cases below where all queries are either
// "all" or "not all", we could have detected those and not trigger an active
// stylesheet update for those cases.

TEST_F(StyleEngineTest,
       MediaQueryAffectingValueChanged_StyleElementMediaNoValid) {
  GetDocument().body()->setInnerHTML(
      "<style media=',,'>div{color:pink}</style>");
  UpdateAllLifecyclePhases();
  GetStyleEngine().MediaQueryAffectingValueChanged(MediaValueChange::kOther);
  EXPECT_TRUE(GetStyleEngine().NeedsActiveStyleUpdate());
}

TEST_F(StyleEngineTest, MediaQueryAffectingValueChanged_StyleElementMediaAll) {
  GetDocument().body()->setInnerHTML(
      "<style media='all'>div{color:pink}</style>");
  UpdateAllLifecyclePhases();
  GetStyleEngine().MediaQueryAffectingValueChanged(MediaValueChange::kOther);
  EXPECT_TRUE(GetStyleEngine().NeedsActiveStyleUpdate());
}

TEST_F(StyleEngineTest,
       MediaQueryAffectingValueChanged_StyleElementMediaNotAll) {
  GetDocument().body()->setInnerHTML(
      "<style media='not all'>div{color:pink}</style>");
  UpdateAllLifecyclePhases();
  GetStyleEngine().MediaQueryAffectingValueChanged(MediaValueChange::kOther);
  EXPECT_TRUE(GetStyleEngine().NeedsActiveStyleUpdate());
}

TEST_F(StyleEngineTest, MediaQueryAffectingValueChanged_StyleElementMediaType) {
  GetDocument().body()->setInnerHTML(
      "<style media='print'>div{color:pink}</style>");
  UpdateAllLifecyclePhases();
  GetStyleEngine().MediaQueryAffectingValueChanged(MediaValueChange::kOther);
  EXPECT_TRUE(GetStyleEngine().NeedsActiveStyleUpdate());
}

TEST_F(StyleEngineTest, EmptyPseudo_RemoveLast) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      .empty:empty + span { color: purple }
    </style>
    <div id=t1 class=empty>Text</div>
    <span></span>
    <div id=t2 class=empty><span></span></div>
    <span></span>
  )HTML");

  UpdateAllLifecyclePhases();

  Element* t1 = GetDocument().getElementById("t1");
  t1->firstChild()->remove();
  EXPECT_TRUE(t1->NeedsStyleInvalidation());

  Element* t2 = GetDocument().getElementById("t2");
  t2->firstChild()->remove();
  EXPECT_TRUE(t2->NeedsStyleInvalidation());
}

TEST_F(StyleEngineTest, EmptyPseudo_RemoveNotLast) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      .empty:empty + span { color: purple }
    </style>
    <div id=t1 class=empty>Text<span></span></div>
    <span></span>
    <div id=t2 class=empty><span></span><span></span></div>
    <span></span>
  )HTML");

  UpdateAllLifecyclePhases();

  Element* t1 = GetDocument().getElementById("t1");
  t1->firstChild()->remove();
  EXPECT_FALSE(t1->NeedsStyleInvalidation());

  Element* t2 = GetDocument().getElementById("t2");
  t2->firstChild()->remove();
  EXPECT_FALSE(t2->NeedsStyleInvalidation());
}

TEST_F(StyleEngineTest, EmptyPseudo_InsertFirst) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      .empty:empty + span { color: purple }
    </style>
    <div id=t1 class=empty></div>
    <span></span>
    <div id=t2 class=empty></div>
    <span></span>
  )HTML");

  UpdateAllLifecyclePhases();

  Element* t1 = GetDocument().getElementById("t1");
  t1->appendChild(Text::Create(GetDocument(), "Text"));
  EXPECT_TRUE(t1->NeedsStyleInvalidation());

  Element* t2 = GetDocument().getElementById("t2");
  t2->appendChild(MakeGarbageCollected<HTMLSpanElement>(GetDocument()));
  EXPECT_TRUE(t2->NeedsStyleInvalidation());
}

TEST_F(StyleEngineTest, EmptyPseudo_InsertNotFirst) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      .empty:empty + span { color: purple }
    </style>
    <div id=t1 class=empty>Text</div>
    <span></span>
    <div id=t2 class=empty><span></span></div>
    <span></span>
  )HTML");

  UpdateAllLifecyclePhases();

  Element* t1 = GetDocument().getElementById("t1");
  t1->appendChild(Text::Create(GetDocument(), "Text"));
  EXPECT_FALSE(t1->NeedsStyleInvalidation());

  Element* t2 = GetDocument().getElementById("t2");
  t2->appendChild(MakeGarbageCollected<HTMLSpanElement>(GetDocument()));
  EXPECT_FALSE(t2->NeedsStyleInvalidation());
}

TEST_F(StyleEngineTest, EmptyPseudo_ModifyTextData_SingleNode) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      .empty:empty + span { color: purple }
    </style>
    <div id=t1 class=empty>Text</div>
    <span></span>
    <div id=t2 class=empty></div>
    <span></span>
    <div id=t3 class=empty>Text</div>
    <span></span>
  )HTML");

  Element* t1 = GetDocument().getElementById("t1");
  Element* t2 = GetDocument().getElementById("t2");
  Element* t3 = GetDocument().getElementById("t3");

  t2->appendChild(Text::Create(GetDocument(), ""));

  UpdateAllLifecyclePhases();

  To<Text>(t1->firstChild())->setData("");
  EXPECT_TRUE(t1->NeedsStyleInvalidation());

  To<Text>(t2->firstChild())->setData("Text");
  EXPECT_TRUE(t2->NeedsStyleInvalidation());

  // This is not optimal. We do not detect that we change text to/from
  // non-empty string.
  To<Text>(t3->firstChild())->setData("NewText");
  EXPECT_TRUE(t3->NeedsStyleInvalidation());
}

TEST_F(StyleEngineTest, EmptyPseudo_ModifyTextData_HasSiblings) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      .empty:empty + span { color: purple }
    </style>
    <div id=t1 class=empty>Text<span></span></div>
    <span></span>
    <div id=t2 class=empty><span></span></div>
    <span></span>
    <div id=t3 class=empty>Text<span></span></div>
    <span></span>
  )HTML");

  Element* t1 = GetDocument().getElementById("t1");
  Element* t2 = GetDocument().getElementById("t2");
  Element* t3 = GetDocument().getElementById("t3");

  t2->appendChild(Text::Create(GetDocument(), ""));

  UpdateAllLifecyclePhases();

  To<Text>(t1->firstChild())->setData("");
  EXPECT_FALSE(t1->NeedsStyleInvalidation());

  To<Text>(t2->lastChild())->setData("Text");
  EXPECT_FALSE(t2->NeedsStyleInvalidation());

  To<Text>(t3->firstChild())->setData("NewText");
  EXPECT_FALSE(t3->NeedsStyleInvalidation());
}

TEST_F(StyleEngineTest, MediaQueriesChangeDefaultFontSize) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      body { color: red }
      @media (max-width: 40em) {
        body { color: green }
      }
    </style>
    <body></body>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  GetDocument().GetSettings()->SetDefaultFontSize(40);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 128, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, MediaQueriesChangeColorScheme) {
  ColorSchemeHelper color_scheme_helper(GetDocument());
  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kLight);

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      body { color: red }
      @media (prefers-color-scheme: dark) {
        body { color: green }
      }
    </style>
    <body></body>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kDark);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 128, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, MediaQueriesChangeColorSchemeForcedDarkMode) {
  GetDocument().GetSettings()->SetForceDarkModeEnabled(true);
  ColorSchemeHelper color_scheme_helper(GetDocument());
  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kDark);

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @media (prefers-color-scheme: dark) {
        body { color: green }
      }
      @media (prefers-color-scheme: light) {
        body { color: red }
      }
    </style>
    <body></body>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 128, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, MediaQueriesChangePrefersContrast) {
  ScopedForcedColorsForTest forced_scoped_feature(true);
  ScopedPrefersContrastForTest contrast_scoped_feature(true);

  ColorSchemeHelper color_scheme_helper(GetDocument());
  color_scheme_helper.SetPreferredContrast(
      mojom::blink::PreferredContrast::kNoPreference);

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      body { color: red; forced-color-adjust: none; }
      @media (prefers-contrast: no-preference) {
        body { color: green }
      }
      @media (prefers-contrast) {
        body { color: blue }
      }
    </style>
    <body></body>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 128, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  color_scheme_helper.SetPreferredContrast(
      mojom::blink::PreferredContrast::kMore);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 0, 255),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  color_scheme_helper.SetPreferredContrast(
      mojom::blink::PreferredContrast::kLess);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 0, 255),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  color_scheme_helper.SetPreferredContrast(
      mojom::blink::PreferredContrast::kCustom);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 0, 255),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, MediaQueriesChangeSpecificPrefersContrast) {
  ScopedForcedColorsForTest forced_scoped_feature(true);
  ScopedPrefersContrastForTest contrast_scoped_feature(true);

  ColorSchemeHelper color_scheme_helper(GetDocument());
  color_scheme_helper.SetPreferredContrast(
      mojom::blink::PreferredContrast::kNoPreference);

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      body { color: red; forced-color-adjust: none; }
      @media (prefers-contrast: more) {
        body { color: blue }
      }
      @media (prefers-contrast: less) {
        body { color: orange }
      }
      @media (prefers-contrast: custom) {
        body { color: yellow }
      }
    </style>
    <body></body>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  color_scheme_helper.SetPreferredContrast(
      mojom::blink::PreferredContrast::kMore);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 0, 255),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  color_scheme_helper.SetPreferredContrast(
      mojom::blink::PreferredContrast::kLess);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 165, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  color_scheme_helper.SetPreferredContrast(
      mojom::blink::PreferredContrast::kCustom);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 255, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, MediaQueriesChangePrefersContrastOverride) {
  ScopedForcedColorsForTest forced_scoped_feature(true);
  ScopedPrefersContrastForTest contrast_scoped_feature(true);

  ColorSchemeHelper color_scheme_helper(GetDocument());
  color_scheme_helper.SetPreferredContrast(
      mojom::blink::PreferredContrast::kNoPreference);

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      body { color: red; forced-color-adjust: none; }
      @media (prefers-contrast: more) {
        body { color: blue }
      }
      @media (prefers-contrast: less) {
        body { color: orange }
      }
      @media (prefers-contrast: custom) {
        body { color: yellow }
      }
    </style>
    <body></body>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  GetDocument().GetPage()->SetMediaFeatureOverride("prefers-contrast", "more");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 0, 255),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  GetDocument().GetPage()->SetMediaFeatureOverride("prefers-contrast",
                                                   "no-preference");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  GetDocument().GetPage()->SetMediaFeatureOverride("prefers-contrast", "less");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 165, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  GetDocument().GetPage()->SetMediaFeatureOverride("prefers-contrast",
                                                   "custom");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 255, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  GetDocument().GetPage()->ClearMediaFeatureOverrides();

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, MediaQueriesChangePrefersReducedMotion) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      body { color: red }
      @media (prefers-reduced-motion: reduce) {
        body { color: green }
      }
    </style>
    <body></body>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  GetDocument().GetSettings()->SetPrefersReducedMotion(true);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 128, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, MediaQueriesChangePrefersReducedDataOn) {
  GetNetworkStateNotifier().SetSaveDataEnabled(true);

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      body { color: red }
      @media (prefers-reduced-data: reduce) {
        body { color: green }
      }
    </style>
    <body></body>
  )HTML");

  UpdateAllLifecyclePhases();

  EXPECT_TRUE(GetNetworkStateNotifier().SaveDataEnabled());
  EXPECT_EQ(MakeRGB(0, 128, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, MediaQueriesChangePrefersReducedDataOff) {
  GetNetworkStateNotifier().SetSaveDataEnabled(false);

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      body { color: red }
      @media (prefers-reduced-data: reduce) {
        body { color: green }
      }
    </style>
    <body></body>
  )HTML");

  UpdateAllLifecyclePhases();

  EXPECT_FALSE(GetNetworkStateNotifier().SaveDataEnabled());
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, MediaQueriesChangeForcedColors) {
  ScopedForcedColorsForTest scoped_feature(true);
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      body {
        forced-color-adjust: none;
      }
      @media (forced-colors: none) {
        body { color: red }
      }
      @media (forced-colors: active) {
        body { color: green }
      }
    </style>
    <body></body>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  ColorSchemeHelper color_scheme_helper(GetDocument());
  color_scheme_helper.SetForcedColors(GetDocument(), ForcedColors::kActive);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 128, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, MediaQueriesChangeForcedColorsAndPreferredColorScheme) {
  ScopedForcedColorsForTest scoped_feature(true);
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      body {
        forced-color-adjust: none;
      }
      @media (forced-colors: none) and (prefers-color-scheme: light) {
        body { color: red }
      }
      @media (forced-colors: none) and (prefers-color-scheme: dark) {
        body { color: green }
      }
      @media (forced-colors: active) and (prefers-color-scheme: dark) {
        body { color: orange }
      }
      @media (forced-colors: active) and (prefers-color-scheme: light) {
        body { color: blue }
      }
    </style>
    <body></body>
  )HTML");

  // ForcedColors = kNone, PreferredColorScheme = kLight
  ColorSchemeHelper color_scheme_helper(GetDocument());
  color_scheme_helper.SetForcedColors(GetDocument(), ForcedColors::kNone);
  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kLight);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  // ForcedColors = kNone, PreferredColorScheme = kDark
  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kDark);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 128, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  // ForcedColors = kActive, PreferredColorScheme = kDark
  color_scheme_helper.SetForcedColors(GetDocument(), ForcedColors::kActive);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 165, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  // ForcedColors = kActive, PreferredColorScheme = kLight
  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kLight);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 0, 255),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, MediaQueriesForcedColorsOverride) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      body {
        forced-color-adjust: none;
      }
      @media (forced-colors: none) {
        body { color: red }
      }
      @media (forced-colors: active) {
        body { color: green }
      }
    </style>
    <body></body>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  ColorSchemeHelper color_scheme_helper(GetDocument());
  GetDocument().GetPage()->SetMediaFeatureOverride("forced-colors", "active");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 128, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  GetDocument().GetPage()->SetMediaFeatureOverride("forced-colors", "none");
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  GetDocument().GetPage()->ClearMediaFeatureOverrides();
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, MediaQueriesColorSchemeOverride) {
  ColorSchemeHelper color_scheme_helper(GetDocument());
  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kLight);
  EXPECT_EQ(mojom::blink::PreferredColorScheme::kLight,
            GetDocument().GetSettings()->GetPreferredColorScheme());

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      body { color: red }
      @media (prefers-color-scheme: dark) {
        body { color: green }
      }
    </style>
    <body></body>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  GetDocument().GetPage()->SetMediaFeatureOverride("prefers-color-scheme",
                                                   "dark");
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 128, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  GetDocument().GetPage()->ClearMediaFeatureOverrides();
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, PreferredColorSchemeMetric) {
  ColorSchemeHelper color_scheme_helper(GetDocument());
  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kLight);
  EXPECT_FALSE(IsUseCounted(WebFeature::kPreferredColorSchemeDark));
  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kDark);
  EXPECT_TRUE(IsUseCounted(WebFeature::kPreferredColorSchemeDark));
}

// The preferred color scheme setting used to differ from the preferred color
// scheme when forced dark mode was enabled. Test that it is no longer the case.
TEST_F(StyleEngineTest, PreferredColorSchemeSettingMetric) {
  ColorSchemeHelper color_scheme_helper(GetDocument());
  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kLight);
  GetDocument().GetSettings()->SetForceDarkModeEnabled(false);
  EXPECT_FALSE(IsUseCounted(WebFeature::kPreferredColorSchemeDark));
  EXPECT_FALSE(IsUseCounted(WebFeature::kPreferredColorSchemeDarkSetting));

  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kDark);
  // Clear the UseCounters before they are updated by the
  // |SetForceDarkModeEnabled| call, below.
  ClearUseCounter(WebFeature::kPreferredColorSchemeDark);
  ClearUseCounter(WebFeature::kPreferredColorSchemeDarkSetting);
  GetDocument().GetSettings()->SetForceDarkModeEnabled(true);

  EXPECT_TRUE(IsUseCounted(WebFeature::kPreferredColorSchemeDark));
  EXPECT_TRUE(IsUseCounted(WebFeature::kPreferredColorSchemeDarkSetting));
}

TEST_F(StyleEngineTest, ForcedDarkModeMetric) {
  GetDocument().GetSettings()->SetForceDarkModeEnabled(false);
  EXPECT_FALSE(IsUseCounted(WebFeature::kForcedDarkMode));
  GetDocument().GetSettings()->SetForceDarkModeEnabled(true);
  EXPECT_TRUE(IsUseCounted(WebFeature::kForcedDarkMode));
}

TEST_F(StyleEngineTest, ColorSchemeDarkSupportedOnRootMetricFromMetaDark) {
  EXPECT_FALSE(IsUseCounted(WebFeature::kColorSchemeDarkSupportedOnRoot));
  GetDocument().body()->setInnerHTML(R"HTML(
    <meta name="color-scheme" content="dark">
  )HTML");
  UpdateAllLifecyclePhases();
  EXPECT_TRUE(IsUseCounted(WebFeature::kColorSchemeDarkSupportedOnRoot));
}

TEST_F(StyleEngineTest, ColorSchemeDarkSupportedOnRootMetricFromMetaLightDark) {
  EXPECT_FALSE(IsUseCounted(WebFeature::kColorSchemeDarkSupportedOnRoot));
  GetDocument().body()->setInnerHTML(R"HTML(
    <meta name="color-scheme" content="light dark">
  )HTML");
  UpdateAllLifecyclePhases();
  EXPECT_TRUE(IsUseCounted(WebFeature::kColorSchemeDarkSupportedOnRoot));
}

TEST_F(StyleEngineTest, ColorSchemeDarkSupportedOnRootMetricFromCSSDark) {
  EXPECT_FALSE(IsUseCounted(WebFeature::kColorSchemeDarkSupportedOnRoot));
  GetDocument().body()->setInnerHTML(R"HTML(
    <style> :root { color-scheme: dark; } </style>
  )HTML");
  UpdateAllLifecyclePhases();
  EXPECT_TRUE(IsUseCounted(WebFeature::kColorSchemeDarkSupportedOnRoot));
}

TEST_F(StyleEngineTest, ColorSchemeDarkSupportedOnRootMetricFromCSSLightDark) {
  EXPECT_FALSE(IsUseCounted(WebFeature::kColorSchemeDarkSupportedOnRoot));
  GetDocument().body()->setInnerHTML(R"HTML(
    <style> :root { color-scheme: light dark; } </style>
  )HTML");
  UpdateAllLifecyclePhases();
  EXPECT_TRUE(IsUseCounted(WebFeature::kColorSchemeDarkSupportedOnRoot));
}

TEST_F(StyleEngineTest, ColorSchemeDarkSupportedOnRootMetricFromChildCSSDark) {
  EXPECT_FALSE(IsUseCounted(WebFeature::kColorSchemeDarkSupportedOnRoot));
  GetDocument().body()->setInnerHTML(R"HTML(
    <style> div { color-scheme: dark; } </style>
    <div></div>
  )HTML");
  UpdateAllLifecyclePhases();
  EXPECT_FALSE(IsUseCounted(WebFeature::kColorSchemeDarkSupportedOnRoot));
}

TEST_F(StyleEngineTest, ColorSchemeDarkSupportedOnRootMetricFromLight) {
  EXPECT_FALSE(IsUseCounted(WebFeature::kColorSchemeDarkSupportedOnRoot));
  GetDocument().body()->setInnerHTML(R"HTML(
    <meta name="color-scheme" content="light">
    <style> :root { color-scheme: light; } </style>
  )HTML");
  UpdateAllLifecyclePhases();
  EXPECT_FALSE(IsUseCounted(WebFeature::kColorSchemeDarkSupportedOnRoot));
}

TEST_F(StyleEngineTest, MediaQueriesReducedMotionOverride) {
  EXPECT_FALSE(GetDocument().GetSettings()->GetPrefersReducedMotion());

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      body { color: red }
      @media (prefers-reduced-motion: reduce) {
        body { color: green }
      }
    </style>
    <body></body>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  GetDocument().GetPage()->SetMediaFeatureOverride("prefers-reduced-motion",
                                                   "reduce");
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 128, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  GetDocument().GetPage()->ClearMediaFeatureOverrides();
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, MediaQueriesChangeNavigationControls) {
  ScopedMediaQueryNavigationControlsForTest scoped_feature(true);
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @media (navigation-controls: none) {
        body { color: red }
      }
      @media (navigation-controls: back-button) {
        body { color: green }
      }
    </style>
    <body></body>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(255, 0, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));

  GetDocument().GetSettings()->SetNavigationControls(
      NavigationControls::kBackButton);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(MakeRGB(0, 128, 0),
            GetDocument().body()->GetComputedStyle()->VisitedDependentColor(
                GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, ShadowRootStyleRecalcCrash) {
  GetDocument().body()->setInnerHTML("<div id=host></div>");
  auto* host = To<HTMLElement>(GetDocument().getElementById("host"));
  ASSERT_TRUE(host);

  ShadowRoot& shadow_root =
      host->AttachShadowRootInternal(ShadowRootType::kOpen);

  shadow_root.setInnerHTML(R"HTML(
    <span id=span></span>
    <style>
      :nth-child(odd) { color: green }
    </style>
  )HTML");
  UpdateAllLifecyclePhases();

  // This should not cause DCHECK errors on style recalc flags.
  shadow_root.getElementById("span")->remove();
  host->SetInlineStyleProperty(CSSPropertyID::kDisplay, "inline");
  UpdateAllLifecyclePhases();
}

TEST_F(StyleEngineTest, GetComputedStyleOutsideFlatTreeCrash) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      body, div { display: contents }
      div::before { display: contents; content: "" }
    </style>
    <div id=host>
      <!-- no slots here -->
    </host>
    <div id=non-slotted></div>
  )HTML");

  GetDocument().getElementById("host")->AttachShadowRootInternal(
      ShadowRootType::kOpen);
  UpdateAllLifecyclePhases();
  GetDocument().body()->EnsureComputedStyle();
  GetDocument()
      .getElementById("non-slotted")
      ->SetInlineStyleProperty(CSSPropertyID::kColor, "blue");
  UpdateAllLifecyclePhases();
}

TEST_F(StyleEngineTest, RejectSelectorForPseudoElement) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      div::before { content: "" }
      .not-in-filter div::before { color: red }
    </style>
    <div class='not-in-filter'></div>
  )HTML");
  UpdateAllLifecyclePhases();

  StyleEngine& engine = GetStyleEngine();
  // Even if the Stats() were already enabled, the following resets it to 0.
  engine.SetStatsEnabled(true);

  StyleResolverStats* stats = engine.Stats();
  ASSERT_TRUE(stats);
  EXPECT_EQ(0u, stats->rules_fast_rejected);

  Element* div = GetDocument().QuerySelector("div");
  ASSERT_TRUE(div);
  div->SetInlineStyleProperty(CSSPropertyID::kColor, "green");

  GetDocument().Lifecycle().AdvanceTo(DocumentLifecycle::kInStyleRecalc);
  GetStyleEngine().RecalcStyle();

  // Should fast reject ".not-in-filter div::before {}" for both the div and its
  // ::before pseudo element.
  EXPECT_EQ(2u, stats->rules_fast_rejected);
}

TEST_F(StyleEngineTest, FirstLetterRemoved) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>.fl::first-letter { color: pink }</style>
    <div class=fl id=d1><div><span id=f1>A</span></div></div>
    <div class=fl id=d2><div><span id=f2>BB</span></div></div>
    <div class=fl id=d3><div><span id=f3>C<!---->C</span></div></div>
  )HTML");
  UpdateAllLifecyclePhases();

  Element* d1 = GetDocument().getElementById("d1");
  Element* d2 = GetDocument().getElementById("d2");
  Element* d3 = GetDocument().getElementById("d3");

  FirstLetterPseudoElement* fl1 =
      To<FirstLetterPseudoElement>(d1->GetPseudoElement(kPseudoIdFirstLetter));
  EXPECT_TRUE(fl1);

  GetDocument().getElementById("f1")->firstChild()->remove();

  EXPECT_FALSE(d1->firstChild()->ChildNeedsStyleRecalc());
  EXPECT_FALSE(d1->firstChild()->ChildNeedsReattachLayoutTree());
  EXPECT_FALSE(d1->firstChild()->NeedsReattachLayoutTree());
  EXPECT_TRUE(d1->ChildNeedsStyleRecalc());
  EXPECT_TRUE(fl1->NeedsStyleRecalc());

  UpdateAllLifecyclePhases();
  EXPECT_FALSE(
      To<FirstLetterPseudoElement>(d1->GetPseudoElement(kPseudoIdFirstLetter)));

  FirstLetterPseudoElement* fl2 =
      To<FirstLetterPseudoElement>(d2->GetPseudoElement(kPseudoIdFirstLetter));
  EXPECT_TRUE(fl2);

  GetDocument().getElementById("f2")->firstChild()->remove();

  EXPECT_FALSE(d2->firstChild()->ChildNeedsStyleRecalc());
  EXPECT_FALSE(d2->firstChild()->ChildNeedsReattachLayoutTree());
  EXPECT_FALSE(d2->firstChild()->NeedsReattachLayoutTree());
  EXPECT_TRUE(d2->ChildNeedsStyleRecalc());
  EXPECT_TRUE(fl2->NeedsStyleRecalc());

  UpdateAllLifecyclePhases();
  EXPECT_FALSE(
      To<FirstLetterPseudoElement>(d2->GetPseudoElement(kPseudoIdFirstLetter)));

  FirstLetterPseudoElement* fl3 =
      To<FirstLetterPseudoElement>(d3->GetPseudoElement(kPseudoIdFirstLetter));
  EXPECT_TRUE(fl3);

  Element* f3 = GetDocument().getElementById("f3");
  f3->firstChild()->remove();

  EXPECT_TRUE(d3->firstChild()->ChildNeedsStyleRecalc());
  EXPECT_FALSE(d3->firstChild()->ChildNeedsReattachLayoutTree());
  EXPECT_FALSE(d3->firstChild()->NeedsReattachLayoutTree());
  EXPECT_TRUE(d3->ChildNeedsStyleRecalc());
  EXPECT_TRUE(fl3->NeedsStyleRecalc());

  UpdateAllLifecyclePhases();
  fl3 =
      To<FirstLetterPseudoElement>(d3->GetPseudoElement(kPseudoIdFirstLetter));
  EXPECT_TRUE(fl3);
  EXPECT_EQ(f3->lastChild()->GetLayoutObject(),
            fl3->RemainingTextLayoutObject());
}

TEST_F(StyleEngineTest, InitialDataCreation) {
  UpdateAllLifecyclePhases();

  // There should be no initial data if nothing is registered.
  EXPECT_FALSE(GetStyleEngine().MaybeCreateAndGetInitialData());

  // After registering, there should be initial data.
  css_test_helpers::RegisterProperty(GetDocument(), "--x", "<length>", "10px",
                                     false);
  auto data1 = GetStyleEngine().MaybeCreateAndGetInitialData();
  EXPECT_TRUE(data1);

  // After a full recalc, we should have the same initial data.
  GetDocument().body()->setInnerHTML("<style>* { font-size: 1px; } </style>");
  EXPECT_TRUE(GetDocument().documentElement()->NeedsStyleRecalc());
  EXPECT_TRUE(GetDocument().documentElement()->ChildNeedsStyleRecalc());
  UpdateAllLifecyclePhases();
  auto data2 = GetStyleEngine().MaybeCreateAndGetInitialData();
  EXPECT_TRUE(data2);
  EXPECT_EQ(data1, data2);

  // After registering a new property, initial data should be invalidated,
  // such that the new initial data is different.
  css_test_helpers::RegisterProperty(GetDocument(), "--y", "<color>", "black",
                                     false);
  EXPECT_NE(data1, GetStyleEngine().MaybeCreateAndGetInitialData());
}

TEST_F(StyleEngineTest, CSSSelectorEmptyWhitespaceOnlyFail) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>.match:empty { background-color: red }</style>
    <div></div>
    <div> <span></span></div>
    <div> <!-- -->X</div>
    <div></div>
    <div> <!-- --></div>
  )HTML");
  GetDocument().View()->UpdateAllLifecyclePhasesForTest();

  EXPECT_FALSE(GetDocument().IsUseCounted(
      WebFeature::kCSSSelectorEmptyWhitespaceOnlyFail));

  auto* div_elements = GetDocument().getElementsByTagName("div");
  ASSERT_TRUE(div_elements);
  ASSERT_EQ(5u, div_elements->length());

  auto is_counted = [](Element* element) {
    element->setAttribute(blink::html_names::kClassAttr, "match");
    element->GetDocument().View()->UpdateAllLifecyclePhasesForTest();
    return element->GetDocument().IsUseCounted(
        WebFeature::kCSSSelectorEmptyWhitespaceOnlyFail);
  };

  EXPECT_FALSE(is_counted(div_elements->item(0)));
  EXPECT_FALSE(is_counted(div_elements->item(1)));
  EXPECT_FALSE(is_counted(div_elements->item(2)));
  EXPECT_FALSE(is_counted(div_elements->item(3)));
  EXPECT_TRUE(is_counted(div_elements->item(4)));
}

TEST_F(StyleEngineTest, EnsuredComputedStyleRecalc) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <div style="display:none">
      <div>
        <div id="computed">
          <span id="span"><span>XXX</span></span>
        </div>
      </div>
    </div>
  )HTML");
  UpdateAllLifecyclePhases();

  Element* computed = GetDocument().getElementById("computed");
  Element* span_outer = GetDocument().getElementById("span");
  Node* span_inner = span_outer->firstChild();

  // Initially all null in display:none subtree.
  EXPECT_FALSE(computed->GetComputedStyle());
  EXPECT_FALSE(span_outer->GetComputedStyle());
  EXPECT_FALSE(span_inner->GetComputedStyle());

  // Force computed style down to #computed.
  computed->EnsureComputedStyle();
  UpdateAllLifecyclePhases();
  EXPECT_TRUE(computed->GetComputedStyle());
  EXPECT_FALSE(span_outer->GetComputedStyle());
  EXPECT_FALSE(span_inner->GetComputedStyle());

  // Setting span color should not create ComputedStyles during style recalc.
  span_outer->SetInlineStyleProperty(CSSPropertyID::kColor, "blue");
  EXPECT_TRUE(span_outer->NeedsStyleRecalc());
  GetDocument().Lifecycle().AdvanceTo(DocumentLifecycle::kInStyleRecalc);
  GetStyleEngine().RecalcStyle();
  GetDocument().Lifecycle().AdvanceTo(DocumentLifecycle::kStyleClean);

  EXPECT_FALSE(span_outer->NeedsStyleRecalc());
  EXPECT_FALSE(span_outer->GetComputedStyle());
  EXPECT_FALSE(span_inner->GetComputedStyle());
  // #computed still non-null because #span_outer is the recalc root.
  EXPECT_TRUE(computed->GetComputedStyle());

  // Triggering style recalc which propagates the color down the tree should
  // clear ComputedStyle objects in the display:none subtree.
  GetDocument().body()->SetInlineStyleProperty(CSSPropertyID::kColor, "pink");
  UpdateAllLifecyclePhases();

  EXPECT_FALSE(computed->GetComputedStyle());
  EXPECT_FALSE(span_outer->GetComputedStyle());
  EXPECT_FALSE(span_inner->GetComputedStyle());
}

TEST_F(StyleEngineTest, EnsureCustomComputedStyle) {
  GetDocument().body()->setInnerHTML("");
  GetDocument().body()->setInnerHTML(R"HTML(
    <div id=div>
      <progress id=progress>
    </div>
  )HTML");
  UpdateAllLifecyclePhases();

  // Note: <progress> is chosen because it creates ProgressShadowElement
  // instances, which override CustomStyleForLayoutObject with
  // display:none.
  Element* div = GetDocument().getElementById("div");
  Element* progress = GetDocument().getElementById("progress");
  ASSERT_TRUE(div);
  ASSERT_TRUE(progress);

  // This causes ProgressShadowElements to get ComputedStyles with
  // IsEnsuredInDisplayNone==true.
  for (Node* node = progress; node;
       node = FlatTreeTraversal::Next(*node, progress)) {
    node->EnsureComputedStyle();
  }

  // This triggers layout tree building.
  div->SetInlineStyleProperty(CSSPropertyID::kDisplay, "inline");
  UpdateAllLifecyclePhases();

  // We must not create LayoutObjects for Nodes with
  // IsEnsuredInDisplayNone==true
  for (Node* node = progress; node;
       node = FlatTreeTraversal::Next(*node, progress)) {
    ASSERT_TRUE(!node->GetComputedStyle() ||
                !node->ComputedStyleRef().IsEnsuredInDisplayNone() ||
                !node->GetLayoutObject());
  }
}

// Via HTMLFormControlElement, it's possible to enter
// Node::MarkAncestorsWithChildNeedsStyleRecalc for nodes which have
// isConnected==true, but an ancestor with isConnected==false. This is because
// we mark the ancestor chain for style recalc via HTMLFormElement::
// InvalidateDefaultButtonStyle while the subtree disconnection
// is taking place.
TEST_F(StyleEngineTest, NoCrashWhenMarkingPartiallyRemovedSubtree) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      #foo:default {} /* Needed to enter Element::PseudoStateChanged */
    </style>
    <form id="form">
      <div id="outer">
        <button>
        <div id="inner"></div>
      </div>
    </form>
  )HTML");
  UpdateAllLifecyclePhases();

  Element* form = GetDocument().getElementById("form");
  Element* outer = GetDocument().getElementById("outer");
  Element* inner = GetDocument().getElementById("inner");
  ASSERT_TRUE(form);
  ASSERT_TRUE(outer);
  ASSERT_TRUE(inner);

  // Add some more buttons, to give InvalidateDefaultButtonStyle
  // something to do when the original <button> is removed.
  inner->setInnerHTML("<button><button>");
  UpdateAllLifecyclePhases();

  form->removeChild(outer);
}

TEST_F(StyleEngineTest, ColorSchemeBaseBackgroundChange) {
  ColorSchemeHelper color_scheme_helper(GetDocument());
  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kDark);
  UpdateAllLifecyclePhases();

  EXPECT_EQ(Color::kWhite, GetDocument().View()->BaseBackgroundColor());

  GetDocument().documentElement()->SetInlineStyleProperty(
      CSSPropertyID::kColorScheme, "dark");
  UpdateAllLifecyclePhases();

  EXPECT_EQ(Color(0x12, 0x12, 0x12),
            GetDocument().View()->BaseBackgroundColor());

  color_scheme_helper.SetForcedColors(GetDocument(), ForcedColors::kActive);
  UpdateAllLifecyclePhases();
  Color system_background_color = LayoutTheme::GetTheme().SystemColor(
      CSSValueID::kCanvas, mojom::blink::ColorScheme::kLight);

  EXPECT_EQ(system_background_color,
            GetDocument().View()->BaseBackgroundColor());
}

TEST_F(StyleEngineTest, ColorSchemeOverride) {
  ColorSchemeHelper color_scheme_helper(GetDocument());
  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kLight);

  GetDocument().documentElement()->SetInlineStyleProperty(
      CSSPropertyID::kColorScheme, "light dark");
  UpdateAllLifecyclePhases();

  EXPECT_EQ(
      mojom::blink::ColorScheme::kLight,
      GetDocument().documentElement()->GetComputedStyle()->UsedColorScheme());

  GetDocument().GetPage()->SetMediaFeatureOverride("prefers-color-scheme",
                                                   "dark");

  UpdateAllLifecyclePhases();
  EXPECT_EQ(
      mojom::blink::ColorScheme::kDark,
      GetDocument().documentElement()->GetComputedStyle()->UsedColorScheme());
}

TEST_F(StyleEngineTest, PseudoElementBaseComputedStyle) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @keyframes anim {
        from { background-color: white }
        to { background-color: blue }
      }
      #anim::before {
        content:"";
        animation: anim 1s;
      }
    </style>
    <div id="anim"></div>
  )HTML");

  UpdateAllLifecyclePhases();

  auto* anim_element = GetDocument().getElementById("anim");
  auto* before = anim_element->GetPseudoElement(kPseudoIdBefore);
  auto* animations = before->GetElementAnimations();

  ASSERT_TRUE(animations);

  before->SetNeedsAnimationStyleRecalc();
  UpdateAllLifecyclePhases();

  ASSERT_TRUE(before->GetComputedStyle());
  const ComputedStyle* base_computed_style =
      before->GetComputedStyle()->GetBaseComputedStyle();
  EXPECT_TRUE(base_computed_style);

  before->SetNeedsAnimationStyleRecalc();
  UpdateAllLifecyclePhases();

  ASSERT_TRUE(before->GetComputedStyle());
  EXPECT_TRUE(before->GetComputedStyle()->GetBaseComputedStyle());
#if !DCHECK_IS_ON()
  // When DCHECK is enabled, ShouldComputeBaseComputedStyle always returns true
  // and we repeatedly create new instances which means the pointers will be
  // different here.
  EXPECT_EQ(base_computed_style,
            before->GetComputedStyle()->GetBaseComputedStyle());
#endif
}

TEST_F(StyleEngineTest, NeedsLayoutTreeRebuild) {
  UpdateAllLifecyclePhases();

  EXPECT_FALSE(GetDocument().NeedsLayoutTreeUpdate());
  EXPECT_FALSE(GetStyleEngine().NeedsLayoutTreeRebuild());

  GetDocument().documentElement()->SetInlineStyleProperty(
      CSSPropertyID::kDisplay, "none");

  EXPECT_TRUE(GetDocument().NeedsLayoutTreeUpdate());

  GetDocument().Lifecycle().AdvanceTo(DocumentLifecycle::kInStyleRecalc);
  GetDocument().GetStyleEngine().RecalcStyle();

  EXPECT_TRUE(GetStyleEngine().NeedsLayoutTreeRebuild());
}

TEST_F(StyleEngineTest, ForceReattachLayoutTreeStyleRecalcRoot) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <div id="outer">
      <div id="inner"></div>
    </div>
  )HTML");
  UpdateAllLifecyclePhases();

  Element* outer = GetDocument().getElementById("outer");
  Element* inner = GetDocument().getElementById("inner");

  outer->SetForceReattachLayoutTree();
  inner->SetInlineStyleProperty(CSSPropertyID::kColor, "blue");

  EXPECT_EQ(outer, GetStyleRecalcRoot());
}

TEST_F(StyleEngineTest, ForceReattachNoStyleForElement) {
  GetDocument().body()->setInnerHTML(R"HTML(<div id="reattach"></div>)HTML");

  auto* reattach = GetDocument().getElementById("reattach");

  UpdateAllLifecyclePhases();

  unsigned initial_count = GetStyleEngine().StyleForElementCount();

  reattach->SetForceReattachLayoutTree();
  EXPECT_EQ(reattach, GetStyleRecalcRoot());

  UpdateAllLifecyclePhases();
  EXPECT_EQ(GetStyleEngine().StyleForElementCount(), initial_count);
}

TEST_F(StyleEngineTest, RecalcPropagatedWritingMode) {
  GetDocument().body()->SetInlineStyleProperty(CSSPropertyID::kWritingMode,
                                               "vertical-lr");

  UpdateAllLifecyclePhases();

  // Make sure that recalculating style for the root element does not trigger a
  // visual diff that requires layout. That is, we take the body -> root
  // propagation of writing-mode into account before setting ComputedStyle on
  // the root LayoutObject.
  GetDocument().documentElement()->SetInlineStyleProperty(
      CSSPropertyID::kWritingMode, "horizontal-tb");

  GetDocument().Lifecycle().AdvanceTo(DocumentLifecycle::kInStyleRecalc);
  GetDocument().GetStyleEngine().RecalcStyle();

  EXPECT_FALSE(GetStyleEngine().NeedsLayoutTreeRebuild());
  EXPECT_FALSE(GetDocument().View()->NeedsLayout());
}

TEST_F(StyleEngineTest, GetComputedStyleOutsideFlatTree) {
  GetDocument().body()->setInnerHTML(
      R"HTML(<div id="host"><div id="outer"><div id="inner"><div id="innermost"></div></div></div></div>)HTML");

  auto* host = GetDocument().getElementById("host");
  auto* outer = GetDocument().getElementById("outer");
  auto* inner = GetDocument().getElementById("inner");
  auto* innermost = GetDocument().getElementById("innermost");

  host->AttachShadowRootInternal(ShadowRootType::kOpen);
  UpdateAllLifecyclePhases();

  EXPECT_TRUE(host->GetComputedStyle());
  // ComputedStyle is not generated outside the flat tree.
  EXPECT_FALSE(outer->GetComputedStyle());
  EXPECT_FALSE(inner->GetComputedStyle());
  EXPECT_FALSE(innermost->GetComputedStyle());

  inner->EnsureComputedStyle();
  scoped_refptr<const ComputedStyle> outer_style = outer->GetComputedStyle();
  scoped_refptr<const ComputedStyle> inner_style = inner->GetComputedStyle();

  ASSERT_TRUE(outer_style);
  ASSERT_TRUE(inner_style);
  EXPECT_FALSE(innermost->GetComputedStyle());
  EXPECT_TRUE(outer_style->IsEnsuredOutsideFlatTree());
  EXPECT_TRUE(inner_style->IsEnsuredOutsideFlatTree());
  EXPECT_EQ(Color::kTransparent, inner_style->VisitedDependentColor(
                                     GetCSSPropertyBackgroundColor()));

  inner->SetInlineStyleProperty(CSSPropertyID::kBackgroundColor, "green");
  UpdateAllLifecyclePhases();

  // Old ensured style is not cleared before we re-ensure it.
  EXPECT_TRUE(inner->NeedsStyleRecalc());
  EXPECT_EQ(inner_style, inner->GetComputedStyle());

  inner->EnsureComputedStyle();

  // Outer style was not dirty - we still have the same ComputedStyle object.
  EXPECT_EQ(outer_style, outer->GetComputedStyle());
  EXPECT_NE(inner_style, inner->GetComputedStyle());

  inner_style = inner->GetComputedStyle();
  EXPECT_EQ(Color(0, 128, 0), inner_style->VisitedDependentColor(
                                  GetCSSPropertyBackgroundColor()));

  // Making outer dirty will require that we clear ComputedStyles all the way up
  // ensuring the style for innermost later because of inheritance.
  outer->SetInlineStyleProperty(CSSPropertyID::kColor, "green");
  UpdateAllLifecyclePhases();

  EXPECT_EQ(outer_style, outer->GetComputedStyle());
  EXPECT_EQ(inner_style, inner->GetComputedStyle());
  EXPECT_FALSE(innermost->GetComputedStyle());

  auto* innermost_style = innermost->EnsureComputedStyle();

  EXPECT_NE(outer_style, outer->GetComputedStyle());
  EXPECT_NE(inner_style, inner->GetComputedStyle());
  ASSERT_TRUE(innermost_style);
  EXPECT_EQ(Color(0, 128, 0),
            innermost_style->VisitedDependentColor(GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, MoveSlottedOutsideFlatTree) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <div id="parent">
      <div id="host1"><span style="display:contents"></span></div>
      <div id="host2"></div>
    </div>
  )HTML");

  auto* host1 = GetDocument().getElementById("host1");
  auto* host2 = GetDocument().getElementById("host2");
  auto* span = host1->firstChild();

  ShadowRoot& shadow_root =
      host1->AttachShadowRootInternal(ShadowRootType::kOpen);
  shadow_root.setInnerHTML("<slot></slot>");
  host2->AttachShadowRootInternal(ShadowRootType::kOpen);

  UpdateAllLifecyclePhases();

  host2->appendChild(span);
  EXPECT_FALSE(GetStyleRecalcRoot());

  span->remove();
  EXPECT_FALSE(GetStyleRecalcRoot());
}

TEST_F(StyleEngineTest, StyleRecalcRootInShadowTree) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <div id="host"></div>
  )HTML");
  Element* host = GetDocument().getElementById("host");
  ShadowRoot& shadow_root =
      host->AttachShadowRootInternal(ShadowRootType::kOpen);
  shadow_root.setInnerHTML("<div><span></span></div>");
  UpdateAllLifecyclePhases();

  Element* span = To<Element>(shadow_root.firstChild()->firstChild());
  // Mark style dirty.
  span->SetInlineStyleProperty(CSSPropertyID::kColor, "blue");

  EXPECT_EQ(span, GetStyleRecalcRoot());
}

TEST_F(StyleEngineTest, StyleRecalcRootOutsideFlatTree) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <div id="host"><div id="ensured"><span></span></div></div>
    <div id="dirty"></div>
  )HTML");

  auto* host = GetDocument().getElementById("host");
  auto* dirty = GetDocument().getElementById("dirty");
  auto* ensured = GetDocument().getElementById("ensured");
  auto* span = To<Element>(ensured->firstChild());

  host->AttachShadowRootInternal(ShadowRootType::kOpen);

  UpdateAllLifecyclePhases();

  dirty->SetInlineStyleProperty(CSSPropertyID::kColor, "blue");
  EXPECT_EQ(dirty, GetStyleRecalcRoot());

  // Ensure a computed style for the span parent to try to trick us into
  // incorrectly using the span as a recalc root.
  ensured->EnsureComputedStyle();
  span->SetInlineStyleProperty(CSSPropertyID::kColor, "pink");

  // <span> is outside the flat tree, so it should not affect the style recalc
  // root.
  EXPECT_EQ(dirty, GetStyleRecalcRoot());

  // Should not trigger any DCHECK failures.
  UpdateAllLifecyclePhases();
}

TEST_F(StyleEngineTest, RemoveStyleRecalcRootFromFlatTree) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <div id=host><span style="display:contents"></span></div>
  )HTML");

  auto* host = GetDocument().getElementById("host");
  auto* span = To<Element>(host->firstChild());

  ShadowRoot& shadow_root =
      host->AttachShadowRootInternal(ShadowRootType::kOpen);
  shadow_root.setInnerHTML("<div><slot></slot></div>");

  UpdateAllLifecyclePhases();

  // Make the span style dirty.
  span->setAttribute("style", "color:green");

  EXPECT_TRUE(GetDocument().NeedsLayoutTreeUpdate());
  EXPECT_EQ(span, GetStyleRecalcRoot());

  auto* div = shadow_root.firstChild();
  auto* slot = To<Element>(div->firstChild());

  slot->setAttribute("name", "x");
  GetDocument().GetSlotAssignmentEngine().RecalcSlotAssignments();

  // Make sure shadow tree div and slot have their ChildNeedsStyleRecalc()
  // cleared.
  EXPECT_FALSE(div->ChildNeedsStyleRecalc());
  EXPECT_FALSE(slot->ChildNeedsStyleRecalc());
  EXPECT_FALSE(span->NeedsStyleRecalc());
  EXPECT_FALSE(GetStyleRecalcRoot());
}

TEST_F(StyleEngineTest, SlottedWithEnsuredStyleOutsideFlatTree) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <div id="host"><span></span></div>
  )HTML");

  auto* host = GetDocument().getElementById("host");
  auto* span = To<Element>(host->firstChild());

  ShadowRoot& shadow_root =
      host->AttachShadowRootInternal(ShadowRootType::kOpen);
  shadow_root.setInnerHTML(R"HTML(
    <div><slot name="default"></slot></div>
  )HTML");

  UpdateAllLifecyclePhases();

  // Ensure style outside the flat tree.
  const ComputedStyle* style = span->EnsureComputedStyle();
  ASSERT_TRUE(style);
  EXPECT_TRUE(style->IsEnsuredOutsideFlatTree());

  span->setAttribute("slot", "default");
  GetDocument().GetSlotAssignmentEngine().RecalcSlotAssignments();
  EXPECT_EQ(span, GetStyleRecalcRoot());
  EXPECT_FALSE(span->GetComputedStyle());
}

TEST_F(StyleEngineTest, ForceReattachRecalcRootAttachShadow) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <div id="reattach"></div>
    <div id="host"><span style="display:contents"></span></div>
  )HTML");

  auto* reattach = GetDocument().getElementById("reattach");
  auto* host = GetDocument().getElementById("host");

  UpdateAllLifecyclePhases();

  reattach->SetForceReattachLayoutTree();
  EXPECT_FALSE(reattach->NeedsStyleRecalc());
  EXPECT_EQ(reattach, GetStyleRecalcRoot());

  // Attaching the shadow root will call RemovedFromFlatTree() on the span child
  // of the host. The style recalc root should still be #reattach.
  host->AttachShadowRootInternal(ShadowRootType::kOpen);
  EXPECT_EQ(reattach, GetStyleRecalcRoot());
}

TEST_F(StyleEngineTest, InitialColorChange) {
  // Set color scheme to light.
  ColorSchemeHelper color_scheme_helper(GetDocument());
  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kLight);

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      :root { color-scheme: light dark }
      #initial { color: initial }
    </style>
    <div id="initial"></div>
  )HTML");
  UpdateAllLifecyclePhases();

  Element* initial = GetDocument().getElementById("initial");
  ASSERT_TRUE(initial);
  ASSERT_TRUE(GetDocument().documentElement());
  const ComputedStyle* document_element_style =
      GetDocument().documentElement()->GetComputedStyle();
  ASSERT_TRUE(document_element_style);
  EXPECT_EQ(Color::kBlack, document_element_style->VisitedDependentColor(
                               GetCSSPropertyColor()));

  const ComputedStyle* initial_style = initial->GetComputedStyle();
  ASSERT_TRUE(initial_style);
  EXPECT_EQ(Color::kBlack,
            initial_style->VisitedDependentColor(GetCSSPropertyColor()));

  // Change color scheme to dark.
  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kDark);
  UpdateAllLifecyclePhases();

  document_element_style = GetDocument().documentElement()->GetComputedStyle();
  ASSERT_TRUE(document_element_style);
  EXPECT_EQ(Color::kWhite, document_element_style->VisitedDependentColor(
                               GetCSSPropertyColor()));

  initial_style = initial->GetComputedStyle();
  ASSERT_TRUE(initial_style);
  EXPECT_EQ(Color::kWhite,
            initial_style->VisitedDependentColor(GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest,
       MediaQueryAffectingValueChanged_InvalidateForChangedSizeQueries) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @media (min-width: 1000px) {
        div { color: green }
      }
    </style>
    <style>
      @media (min-width: 1200px) {
        * { color: red }
      }
    </style>
    <style>
      @media print {
        * { color: blue }
      }
    </style>
    <div id="green"></div>
    <span></span>
  )HTML");
  UpdateAllLifecyclePhases();

  Element* div = GetDocument().getElementById("green");
  EXPECT_EQ(Color::kBlack, div->GetComputedStyle()->VisitedDependentColor(
                               GetCSSPropertyColor()));

  unsigned initial_count = GetStyleEngine().StyleForElementCount();

  GetDocument().View()->SetLayoutSizeFixedToFrameSize(false);
  GetDocument().View()->SetLayoutSize(gfx::Size(1100, 800));
  UpdateAllLifecyclePhases();

  // Only the single div element should have its style recomputed.
  EXPECT_EQ(1u, GetStyleEngine().StyleForElementCount() - initial_count);
  EXPECT_EQ(MakeRGB(0, 128, 0), div->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest,
       MediaQueryAffectingValueChanged_InvalidateForChangedTypeQuery) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @media speech {
        div { color: green }
      }
    </style>
    <style>
      @media (max-width: 100px) {
        * { color: red }
      }
    </style>
    <style>
      @media print {
        * { color: blue }
      }
    </style>
    <div id="green"></div>
    <span></span>
  )HTML");
  UpdateAllLifecyclePhases();

  Element* div = GetDocument().getElementById("green");
  EXPECT_EQ(Color::kBlack, div->GetComputedStyle()->VisitedDependentColor(
                               GetCSSPropertyColor()));

  unsigned initial_count = GetStyleEngine().StyleForElementCount();

  GetDocument().GetSettings()->SetMediaTypeOverride("speech");
  UpdateAllLifecyclePhases();

  // Only the single div element should have its style recomputed.
  EXPECT_EQ(1u, GetStyleEngine().StyleForElementCount() - initial_count);
  EXPECT_EQ(MakeRGB(0, 128, 0), div->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest,
       MediaQueryAffectingValueChanged_InvalidateForChangedReducedMotionQuery) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @media (prefers-reduced-motion: reduce) {
        div { color: green }
      }
    </style>
    <style>
      @media (max-width: 100px) {
        * { color: red }
      }
    </style>
    <style>
      @media print {
        * { color: blue }
      }
    </style>
    <div id="green"></div>
    <span></span>
  )HTML");
  UpdateAllLifecyclePhases();

  Element* div = GetDocument().getElementById("green");
  EXPECT_EQ(Color::kBlack, div->GetComputedStyle()->VisitedDependentColor(
                               GetCSSPropertyColor()));

  unsigned initial_count = GetStyleEngine().StyleForElementCount();

  GetDocument().GetSettings()->SetPrefersReducedMotion(true);
  UpdateAllLifecyclePhases();

  // Only the single div element should have its style recomputed.
  EXPECT_EQ(1u, GetStyleEngine().StyleForElementCount() - initial_count);
  EXPECT_EQ(MakeRGB(0, 128, 0), div->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, RevertUseCount) {
  GetDocument().body()->setInnerHTML(
      "<style>div { display: unset; }</style><div></div>");
  UpdateAllLifecyclePhases();
  EXPECT_FALSE(GetDocument().IsUseCounted(WebFeature::kCSSKeywordRevert));

  GetDocument().body()->setInnerHTML(
      "<style>div { display: revert; }</style><div></div>");
  UpdateAllLifecyclePhases();
  EXPECT_TRUE(GetDocument().IsUseCounted(WebFeature::kCSSKeywordRevert));
}

TEST_F(StyleEngineTest, RevertUseCountForCustomProperties) {
  GetDocument().body()->setInnerHTML(
      "<style>div { --x: unset; }</style><div></div>");
  UpdateAllLifecyclePhases();
  EXPECT_FALSE(GetDocument().IsUseCounted(WebFeature::kCSSKeywordRevert));

  GetDocument().body()->setInnerHTML(
      "<style>div { --x: revert; }</style><div></div>");
  UpdateAllLifecyclePhases();
  EXPECT_TRUE(GetDocument().IsUseCounted(WebFeature::kCSSKeywordRevert));
}

TEST_F(StyleEngineTest, NoRevertUseCountForForcedColors) {
  ScopedForcedColorsForTest scoped_feature(true);

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      #elem { color: red; }
    </style>
    <div id=ref></div>
    <div id=elem></div>
  )HTML");
  UpdateAllLifecyclePhases();

  Element* ref = GetDocument().getElementById("ref");
  Element* elem = GetDocument().getElementById("elem");
  ASSERT_TRUE(ref);
  ASSERT_TRUE(elem);

  // This test assumes that the initial color is not 'red'. Verify that
  // assumption.
  ASSERT_NE(ComputedValue(ref, "color")->CssText(),
            ComputedValue(elem, "color")->CssText());

  EXPECT_EQ("rgb(255, 0, 0)", ComputedValue(elem, "color")->CssText());

  ColorSchemeHelper color_scheme_helper(GetDocument());
  color_scheme_helper.SetForcedColors(GetDocument(), ForcedColors::kActive);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(ComputedValue(ref, "color")->CssText(),
            ComputedValue(elem, "color")->CssText());

  EXPECT_FALSE(GetDocument().IsUseCounted(WebFeature::kCSSKeywordRevert));
}

TEST_F(StyleEngineTest, PrintNoDarkColorScheme) {
  ColorSchemeHelper color_scheme_helper(GetDocument());
  color_scheme_helper.SetPreferredColorScheme(
      mojom::blink::PreferredColorScheme::kDark);

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      :root { color-scheme: light dark }
      @media (prefers-color-scheme: light) {
        body { color: green; }
      }
      @media (prefers-color-scheme: dark) {
        body { color: red; }
      }
    </style>
  )HTML");
  UpdateAllLifecyclePhases();
  Element* body = GetDocument().body();
  Element* root = GetDocument().documentElement();

  EXPECT_EQ(Color::kWhite, root->GetComputedStyle()->VisitedDependentColor(
                               GetCSSPropertyColor()));
  EXPECT_EQ(mojom::blink::ColorScheme::kDark,
            root->GetComputedStyle()->UsedColorScheme());
  EXPECT_EQ(MakeRGB(255, 0, 0), body->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));

  gfx::SizeF page_size(400, 400);
  GetDocument().GetFrame()->StartPrinting(page_size, page_size, 1);
  EXPECT_EQ(Color::kBlack, root->GetComputedStyle()->VisitedDependentColor(
                               GetCSSPropertyColor()));
  EXPECT_EQ(mojom::blink::ColorScheme::kLight,
            root->GetComputedStyle()->UsedColorScheme());
  EXPECT_EQ(MakeRGB(0, 128, 0), body->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));

  GetDocument().GetFrame()->EndPrinting();
  EXPECT_EQ(Color::kWhite, root->GetComputedStyle()->VisitedDependentColor(
                               GetCSSPropertyColor()));
  EXPECT_EQ(mojom::blink::ColorScheme::kDark,
            root->GetComputedStyle()->UsedColorScheme());
  EXPECT_EQ(MakeRGB(255, 0, 0), body->GetComputedStyle()->VisitedDependentColor(
                                    GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, AtPropertyUseCount) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      body { --x: No @property rule here; }
    </style>
  )HTML");
  UpdateAllLifecyclePhases();
  EXPECT_FALSE(GetDocument().IsUseCounted(WebFeature::kCSSAtRuleProperty));

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @property --x {
        syntax: "<length>";
        inherits: false;
        initial-value: 0px;
      }
    </style>
  )HTML");
  UpdateAllLifecyclePhases();
  EXPECT_TRUE(GetDocument().IsUseCounted(WebFeature::kCSSAtRuleProperty));
}

TEST_F(StyleEngineTest, AtScrollTimelineUseCount) {
  ScopedCSSScrollTimelineForTest scoped_feature(true);

  GetDocument().body()->setInnerHTML("<div>No @scroll-timline</div>");
  UpdateAllLifecyclePhases();
  EXPECT_FALSE(
      GetDocument().IsUseCounted(WebFeature::kCSSAtRuleScrollTimeline));

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @scroll-timeline foo { }
    </style>
  )HTML");
  UpdateAllLifecyclePhases();
  EXPECT_TRUE(GetDocument().IsUseCounted(WebFeature::kCSSAtRuleScrollTimeline));
}

TEST_F(StyleEngineTest, MediaQueryAffectedByViewportSanityCheck) {
  GetDocument().body()->setInnerHTML("<audio controls>");
  UpdateAllLifecyclePhases();
  EXPECT_FALSE(GetStyleEngine().MediaQueryAffectedByViewportChange());
}

TEST_F(StyleEngineTest, CSSMatchMediaUnknownUseCounter) {
  ScopedCSSMediaQueries4ForTest media_queries_4_flag(false);

  UpdateAllLifecyclePhases();

  {
    MediaQueryList* mql =
        GetDocument().domWindow()->matchMedia("(min-width: 0px)");
    ASSERT_TRUE(mql);
    mql->media();
    EXPECT_FALSE(IsUseCounted(WebFeature::kCSSMatchMediaUnknown));
    ClearUseCounter(WebFeature::kCSSMatchMediaUnknown);
  }

  {
    MediaQueryList* mql =
        GetDocument().domWindow()->matchMedia("(width: 100px) or (unknown)");
    ASSERT_TRUE(mql);
    mql->media();
    // Should not be use-counted, because it's a real parse error without
    // CSSMediaQueries4 enabled.
    EXPECT_FALSE(IsUseCounted(WebFeature::kCSSMatchMediaUnknown));
    ClearUseCounter(WebFeature::kCSSMatchMediaUnknown);
  }

  {
    MediaQueryList* mql =
        GetDocument().domWindow()->matchMedia("(unknown: 0px)");
    ASSERT_TRUE(mql);
    mql->media();
    EXPECT_TRUE(IsUseCounted(WebFeature::kCSSMatchMediaUnknown));
    ClearUseCounter(WebFeature::kCSSMatchMediaUnknown);
  }

  {
    MediaQueryList* mql = GetDocument().domWindow()->matchMedia(
        "not print and (width: 100px) and (unknown)");
    ASSERT_TRUE(mql);
    mql->media();
    EXPECT_TRUE(IsUseCounted(WebFeature::kCSSMatchMediaUnknown));
    ClearUseCounter(WebFeature::kCSSMatchMediaUnknown);
  }
}

TEST_F(StyleEngineTest, CSSMediaListUnknownUseCounter) {
  ScopedCSSMediaQueries4ForTest media_queries_4_flag(false);

  UpdateAllLifecyclePhases();

  {
    GetDocument().body()->setInnerHTML(R"HTML(
      <style media="(min-width: 0px)"></style>
    )HTML");
    auto* style =
        DynamicTo<HTMLStyleElement>(GetDocument().QuerySelector("style"));
    ASSERT_TRUE(style);
    ASSERT_TRUE(style->sheet());
    MediaList* media = style->sheet()->media();
    ASSERT_TRUE(media);
    media->mediaText(GetDocument().GetExecutionContext());
    EXPECT_FALSE(IsUseCounted(WebFeature::kCSSMediaListUnknown));
    ClearUseCounter(WebFeature::kCSSMediaListUnknown);
  }

  {
    GetDocument().body()->setInnerHTML(R"HTML(
      <style media="(width: 100px) or (unknown)"></style>
    )HTML");
    auto* style =
        DynamicTo<HTMLStyleElement>(GetDocument().QuerySelector("style"));
    ASSERT_TRUE(style);
    ASSERT_TRUE(style->sheet());
    MediaList* media = style->sheet()->media();
    ASSERT_TRUE(media);
    media->mediaText(GetDocument().GetExecutionContext());
    // Should not be use-counted, because it's a real parse error without
    // CSSMediaQueries4 enabled.
    EXPECT_FALSE(IsUseCounted(WebFeature::kCSSMediaListUnknown));
    ClearUseCounter(WebFeature::kCSSMediaListUnknown);
  }

  {
    GetDocument().body()->setInnerHTML(R"HTML(
      <style media="(unknown: 0px)"></style>
    )HTML");
    auto* style =
        DynamicTo<HTMLStyleElement>(GetDocument().QuerySelector("style"));
    ASSERT_TRUE(style);
    ASSERT_TRUE(style->sheet());
    MediaList* media = style->sheet()->media();
    ASSERT_TRUE(media);
    media->mediaText(GetDocument().GetExecutionContext());
    EXPECT_TRUE(IsUseCounted(WebFeature::kCSSMediaListUnknown));
    ClearUseCounter(WebFeature::kCSSMediaListUnknown);

    media->MediaTextInternal();
    EXPECT_FALSE(IsUseCounted(WebFeature::kCSSMediaListUnknown));
    ClearUseCounter(WebFeature::kCSSMediaListUnknown);
  }

  {
    GetDocument().body()->setInnerHTML(R"HTML(
      <style media="not print and (width: 100px) and (unknown)"></style>
    )HTML");
    auto* style =
        DynamicTo<HTMLStyleElement>(GetDocument().QuerySelector("style"));
    ASSERT_TRUE(style);
    ASSERT_TRUE(style->sheet());
    MediaList* media = style->sheet()->media();
    ASSERT_TRUE(media);
    media->mediaText(GetDocument().GetExecutionContext());
    EXPECT_TRUE(IsUseCounted(WebFeature::kCSSMediaListUnknown));
    ClearUseCounter(WebFeature::kCSSMediaListUnknown);

    media->MediaTextInternal();
    EXPECT_FALSE(IsUseCounted(WebFeature::kCSSMediaListUnknown));
    ClearUseCounter(WebFeature::kCSSMediaListUnknown);
  }
}

TEST_F(StyleEngineTest, CSSOMMediaConditionUnknownUseCounter) {
  ScopedCSSMediaQueries4ForTest media_queries_4_flag(false);

  GetDocument().body()->setInnerHTML(R"HTML(
    <style id=style>
      @media (min-width: 100px) {}
      @media (width: 100px) or (unknown) {}
      @media (unknown: 0px) {}
      @media not print and (width: 100px) and (unknown) {}
    </style>
  )HTML");
  UpdateAllLifecyclePhases();

  {
    GetDocument().body()->setInnerHTML(R"HTML(
      <style>
        @media (min-width: 100px) {}
      </style>
    )HTML");
    auto* style =
        DynamicTo<HTMLStyleElement>(GetDocument().QuerySelector("style"));
    ASSERT_TRUE(style);
    ASSERT_TRUE(style->sheet());
    ASSERT_EQ(1u, style->sheet()->length());
    auto* rule = DynamicTo<CSSMediaRule>(style->sheet()->item(0));
    ASSERT_TRUE(rule);
    rule->conditionText();
    EXPECT_FALSE(IsUseCounted(WebFeature::kCSSOMMediaConditionUnknown));
    ClearUseCounter(WebFeature::kCSSOMMediaConditionUnknown);
  }

  {
    GetDocument().body()->setInnerHTML(R"HTML(
      <style>
        @media (width: 100px) or (unknown) {}
      </style>
    )HTML");
    auto* style =
        DynamicTo<HTMLStyleElement>(GetDocument().QuerySelector("style"));
    ASSERT_TRUE(style);
    ASSERT_TRUE(style->sheet());
    ASSERT_EQ(1u, style->sheet()->length());
    auto* rule = DynamicTo<CSSMediaRule>(style->sheet()->item(0));
    ASSERT_TRUE(rule);
    rule->conditionText();
    EXPECT_FALSE(IsUseCounted(WebFeature::kCSSOMMediaConditionUnknown));
    ClearUseCounter(WebFeature::kCSSOMMediaConditionUnknown);
  }

  {
    GetDocument().body()->setInnerHTML(R"HTML(
      <style>
        @media (unknown: 0px) {}
      </style>
    )HTML");
    auto* style =
        DynamicTo<HTMLStyleElement>(GetDocument().QuerySelector("style"));
    ASSERT_TRUE(style);
    ASSERT_TRUE(style->sheet());
    ASSERT_EQ(1u, style->sheet()->length());
    auto* rule = DynamicTo<CSSMediaRule>(style->sheet()->item(0));
    ASSERT_TRUE(rule);
    rule->conditionText();
    EXPECT_TRUE(IsUseCounted(WebFeature::kCSSOMMediaConditionUnknown));
    ClearUseCounter(WebFeature::kCSSOMMediaConditionUnknown);

    rule->ConditionTextInternal();
    EXPECT_FALSE(IsUseCounted(WebFeature::kCSSOMMediaConditionUnknown));
    ClearUseCounter(WebFeature::kCSSOMMediaConditionUnknown);
  }

  {
    GetDocument().body()->setInnerHTML(R"HTML(
      <style>
        @media not print and (width: 100px) and (unknown) {}
      </style>
    )HTML");
    auto* style =
        DynamicTo<HTMLStyleElement>(GetDocument().QuerySelector("style"));
    ASSERT_TRUE(style);
    ASSERT_TRUE(style->sheet());
    ASSERT_EQ(1u, style->sheet()->length());
    auto* rule = DynamicTo<CSSMediaRule>(style->sheet()->item(0));
    ASSERT_TRUE(rule);
    rule->conditionText();
    EXPECT_TRUE(IsUseCounted(WebFeature::kCSSOMMediaConditionUnknown));
    ClearUseCounter(WebFeature::kCSSOMMediaConditionUnknown);

    rule->ConditionTextInternal();
    EXPECT_FALSE(IsUseCounted(WebFeature::kCSSOMMediaConditionUnknown));
    ClearUseCounter(WebFeature::kCSSOMMediaConditionUnknown);
  }
}

TEST_F(StyleEngineTest, RemoveDeclaredPropertiesEmptyRegistry) {
  EXPECT_FALSE(GetDocument().GetPropertyRegistry());
  PropertyRegistration::RemoveDeclaredProperties(GetDocument());
  EXPECT_FALSE(GetDocument().GetPropertyRegistry());
}

TEST_F(StyleEngineTest, AtPropertyInUserOrigin) {
  // @property in the user origin:
  InjectSheet("user1", WebDocument::kUserOrigin, R"CSS(
    @property --x {
      syntax: "<length>";
      inherits: false;
      initial-value: 10px;
    }
  )CSS");
  UpdateAllLifecyclePhases();
  ASSERT_TRUE(ComputedValue(GetDocument().body(), "--x"));
  EXPECT_EQ("10px", ComputedValue(GetDocument().body(), "--x")->CssText());

  // @property in the author origin (should win over user origin)
  InjectSheet("author", WebDocument::kAuthorOrigin, R"CSS(
    @property --x {
      syntax: "<length>";
      inherits: false;
      initial-value: 20px;
    }
  )CSS");
  UpdateAllLifecyclePhases();
  ASSERT_TRUE(ComputedValue(GetDocument().body(), "--x"));
  EXPECT_EQ("20px", ComputedValue(GetDocument().body(), "--x")->CssText());

  // An additional @property in the user origin:
  InjectSheet("user2", WebDocument::kUserOrigin, R"CSS(
    @property --y {
      syntax: "<length>";
      inherits: false;
      initial-value: 30px;
    }
  )CSS");
  UpdateAllLifecyclePhases();
  ASSERT_TRUE(ComputedValue(GetDocument().body(), "--x"));
  ASSERT_TRUE(ComputedValue(GetDocument().body(), "--y"));
  EXPECT_EQ("20px", ComputedValue(GetDocument().body(), "--x")->CssText());
  EXPECT_EQ("30px", ComputedValue(GetDocument().body(), "--y")->CssText());
}

TEST_F(StyleEngineTest, AtScrollTimelineInUserOrigin) {
  ScopedCSSScrollTimelineForTest scoped_feature(true);

  // @scroll-timeline in the user origin:
  InjectSheet("user1", WebDocument::kUserOrigin, R"CSS(
    @scroll-timeline timeline1 {
      source: selector(#scroller1);
    }
  )CSS");
  UpdateAllLifecyclePhases();
  StyleRuleScrollTimeline* rule1 = FindScrollTimelineRule("timeline1");
  ASSERT_TRUE(rule1);
  ASSERT_TRUE(rule1->GetSource());
  EXPECT_EQ("selector(#scroller1)", rule1->GetSource()->CssText());

  // @scroll-timeline in the author origin (should win over user origin)
  InjectSheet("author", WebDocument::kAuthorOrigin, R"CSS(
    @scroll-timeline timeline1 {
      source: selector(#scroller2);
    }
  )CSS");
  UpdateAllLifecyclePhases();
  StyleRuleScrollTimeline* rule2 = FindScrollTimelineRule("timeline1");
  ASSERT_TRUE(rule2);
  ASSERT_TRUE(rule2->GetSource());
  EXPECT_EQ("selector(#scroller2)", rule2->GetSource()->CssText());

  // An additional @scroll-timeline in the user origin:
  InjectSheet("user2", WebDocument::kUserOrigin, R"CSS(
    @scroll-timeline timeline2 {
      source: selector(#scroller3);
    }
  )CSS");
  UpdateAllLifecyclePhases();
  StyleRuleScrollTimeline* rule3 = FindScrollTimelineRule("timeline2");
  ASSERT_TRUE(rule3);
  ASSERT_TRUE(rule3->GetSource());
  EXPECT_EQ("selector(#scroller3)", rule3->GetSource()->CssText());
}

TEST_F(StyleEngineTest, SystemColorComputeToSelfUseCount) {
  // Don't count system color use by itself - only in conjunction with
  // color-scheme.
  GetDocument().body()->setInnerHTML(
      "<style>div { color: MenuText; }</style><div></div>");
  UpdateAllLifecyclePhases();
  EXPECT_FALSE(
      GetDocument().IsUseCounted(WebFeature::kCSSSystemColorComputeToSelf));

  // Count system color use when used on an element with a different
  // color-scheme from its parent.
  GetDocument().body()->setInnerHTML(
      "<style>"
      "div { color: MenuText; color-scheme: dark; }"
      "</style><div></div>");
  UpdateAllLifecyclePhases();
  EXPECT_TRUE(
      GetDocument().IsUseCounted(WebFeature::kCSSSystemColorComputeToSelf));
}

// https://crbug.com/1050564
TEST_F(StyleEngineTest, MediaAttributeChangeUpdatesFontCacheVersion) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @font-face { font-family: custom-font; src: url(fake-font.woff); }
    </style>
    <style id=target>
      .display-none { display: none; }
    </style>
    <div style="font-family: custom-font">foo</div>
    <div class="display-none">bar</div>
  )HTML");
  UpdateAllLifecyclePhases();

  Element* target = GetDocument().getElementById("target");
  target->setAttribute(html_names::kMediaAttr, "print");

  // Shouldn't crash.
  UpdateAllLifecyclePhases();
}

// Properties stored for forced colors mode should only be usable by the UA.
TEST_F(StyleEngineTest, InternalForcedProperties) {
  String properties_to_test[] = {
      "-internal-forced-background-color", "-internal-forced-border-color",
      "-internal-forced-color", "-internal-forced-outline-color",
      "-internal-forced-visited-color"};
  for (auto property : properties_to_test) {
    String declaration = property + ":red";
    ASSERT_TRUE(
        css_test_helpers::ParseDeclarationBlock(declaration, kHTMLStandardMode)
            ->IsEmpty());
    ASSERT_TRUE(
        !css_test_helpers::ParseDeclarationBlock(declaration, kUASheetMode)
             ->IsEmpty());
  }
}

class StyleEngineSimTest : public SimTest {};

TEST_F(StyleEngineSimTest, OwnerColorScheme) {
  SimRequest main_resource("https://example.com", "text/html");
  SimRequest frame_resource("https://example.com/frame.html", "text/html");

  LoadURL("https://example.com");

  main_resource.Complete(R"HTML(
    <!doctype html>
    <style>
      iframe { color-scheme: dark }
    </style>
    <iframe id="frame" src="https://example.com/frame.html"></iframe>
  )HTML");

  frame_resource.Complete(R"HTML(
    <!doctype html>
    <p>Frame</p>
  )HTML");

  test::RunPendingTasks();
  Compositor().BeginFrame();

  auto* frame_element =
      To<HTMLIFrameElement>(GetDocument().getElementById("frame"));
  auto* frame_document = frame_element->contentDocument();
  ASSERT_TRUE(frame_document);
  EXPECT_EQ(mojom::blink::ColorScheme::kDark,
            frame_document->GetStyleEngine().GetOwnerColorScheme());

  frame_element->SetInlineStyleProperty(CSSPropertyID::kColorScheme, "light");

  test::RunPendingTasks();
  Compositor().BeginFrame();
  EXPECT_EQ(mojom::blink::ColorScheme::kLight,
            frame_document->GetStyleEngine().GetOwnerColorScheme());
}

TEST_F(StyleEngineSimTest, OwnerColorSchemeBaseBackground) {
  SimRequest main_resource("https://example.com", "text/html");
  SimRequest dark_frame_resource("https://example.com/dark.html", "text/html");
  SimRequest light_frame_resource("https://example.com/light.html",
                                  "text/html");

  LoadURL("https://example.com");

  main_resource.Complete(R"HTML(
    <style>
      .dark { color-scheme: dark }
    </style>
    <iframe id="dark-frame" src="dark.html"></iframe>
    <iframe id="light-frame" src="light.html"></iframe>
  )HTML");

  dark_frame_resource.Complete(R"HTML(
    <!doctype html>
    <meta name=color-scheme content="dark">
    <p>Frame</p>
  )HTML");

  light_frame_resource.Complete(R"HTML(
    <!doctype html>
    <p>Frame</p>
  )HTML");

  test::RunPendingTasks();
  Compositor().BeginFrame();

  auto* dark_document =
      To<HTMLIFrameElement>(GetDocument().getElementById("dark-frame"))
          ->contentDocument();
  auto* light_document =
      To<HTMLIFrameElement>(GetDocument().getElementById("light-frame"))
          ->contentDocument();
  ASSERT_TRUE(dark_document);
  ASSERT_TRUE(light_document);

  EXPECT_TRUE(dark_document->View()->ShouldPaintBaseBackgroundColor());
  EXPECT_EQ(Color(0x12, 0x12, 0x12),
            dark_document->View()->BaseBackgroundColor());
  EXPECT_FALSE(light_document->View()->ShouldPaintBaseBackgroundColor());

  GetDocument().documentElement()->setAttribute(blink::html_names::kClassAttr,
                                                "dark");

  test::RunPendingTasks();
  Compositor().BeginFrame();

  EXPECT_FALSE(dark_document->View()->ShouldPaintBaseBackgroundColor());
  EXPECT_TRUE(light_document->View()->ShouldPaintBaseBackgroundColor());
  EXPECT_EQ(Color::kWhite, light_document->View()->BaseBackgroundColor());
}

TEST_F(StyleEngineSimTest, ColorSchemeBaseBackgroundWhileRenderBlocking) {
  SimRequest main_resource("https://example.com", "text/html");
  SimSubresourceRequest css_resource("https://example.com/slow.css",
                                     "text/css");

  LoadURL("https://example.com");

  main_resource.Write(R"HTML(
    <!doctype html>
    <meta name="color-scheme" content="dark">
    <link rel="stylesheet" href="slow.css">
    Some content
  )HTML");

  css_resource.Start();
  test::RunPendingTasks();

  // No rendering updates should have happened yet.
  ASSERT_TRUE(GetDocument().documentElement());
  ASSERT_FALSE(GetDocument().documentElement()->GetComputedStyle());
  EXPECT_TRUE(Compositor().DeferMainFrameUpdate());

  // The dark color-scheme meta should affect the canvas color.
  EXPECT_EQ(Color(0x12, 0x12, 0x12),
            GetDocument().View()->BaseBackgroundColor());

  main_resource.Finish();
  css_resource.Finish();
}

TEST_F(StyleEngineContainerQueryTest, UpdateStyleAndLayoutTreeForContainer) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      .container {
        container-type: size;
        width: 100px;
        height: 100px;
      }
      @container size(min-width: 200px) {
        .affected { background-color: green; }
      }
    </style>
    <div id="container1" class="container">
      <span class="affected"></span>
      <div id="container2" class="container affected">
        <span class="affected"></span>
        <span></span>
        <span class="affected"></span>
        <span><span class="affected"></span></span>
        <span class="affected"></span>
        <div style="display:none" class="affected">
          <span class="affected"></span>
        </div>
        <div style="display:none">
          <span class="affected"></span>
          <span class="affected"></span>
        </div>
      </div>
      <span></span>
      <div class="container">
        <span class="affected"></span>
        <span class="affected"></span>
      </div>
      <span class="container" style="display:inline-block">
        <span class="affected"></span>
      </span>
    </div>
  )HTML");

  UpdateAllLifecyclePhases();

  auto* container1 = GetDocument().getElementById("container1");
  auto* container2 = GetDocument().getElementById("container2");
  ASSERT_TRUE(container1);
  ASSERT_TRUE(container2);

  unsigned start_count = GetStyleEngine().StyleForElementCount();
  GetStyleEngine().UpdateStyleAndLayoutTreeForContainer(
      *container1, LogicalSize(200, 100), LogicalAxes(kLogicalAxisBoth));

  // The first span.affected child and #container2
  EXPECT_EQ(2u, GetStyleEngine().StyleForElementCount() - start_count);

  start_count = GetStyleEngine().StyleForElementCount();
  GetStyleEngine().UpdateStyleAndLayoutTreeForContainer(
      *container2, LogicalSize(200, 100), LogicalAxes(kLogicalAxisBoth));

  // Three direct span.affected children, and the two display:none elements.
  EXPECT_EQ(6u, GetStyleEngine().StyleForElementCount() - start_count);
}

TEST_F(StyleEngineContainerQueryTest, ContainerQueriesContainmentNotApplying) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      .container {
        container-type: size;
        width: 100px;
        height: 100px;
      }
      @container size(min-width: 200px) {
        .toggle { background-color: green; }
      }
    </style>
    <div id="container" class="container">

      <!-- None of the following should be affected by a change in the
           size of #container. -->
      <div class="container" style="display:contents">
        <span class="toggle"></span>
      </div>
      <span class="container">
        <span class="toggle"></span>
      </span>
      <rt class="container">
        <span class="toggle"></span>
      </rt>
      <div class="container" style="display:table">
        <span class="toggle"></span>
      </div>
      <div class="container" style="display:table-cell">
        <span class="toggle"></span>
      </div>
      <div class="container" style="display:table-row">
        <span class="toggle"></span>
      </div>
      <div class="container" style="display:table-row-group">
        <span class="toggle"></span>
      </div>

      <!-- This should be affected, however. -->
      <div class="toggle">Affected</div>
    </div>
  )HTML");

  UpdateAllLifecyclePhases();

  auto* container = GetDocument().getElementById("container");
  ASSERT_TRUE(container);

  unsigned start_count = GetStyleEngine().StyleForElementCount();

  GetStyleEngine().UpdateStyleAndLayoutTreeForContainer(
      *container, LogicalSize(200, 100), LogicalAxes(kLogicalAxisBoth));

  // Even though none of the inner containers are eligible for containment,
  // they are still containers for the purposes of evaluating container
  // queries. Hence, they should not be affected when the outer container
  // changes its size.
  EXPECT_EQ(1u, GetStyleEngine().StyleForElementCount() - start_count);
}

TEST_F(StyleEngineContainerQueryTest, PseudoElementContainerQueryRecalc) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      #container {
        container-type: size;
        width: 100px;
        height: 100px;
      }
      @container size(min-width: 200px) {
        #container::before { content: " " }
        span::before { content: " " }
      }
    </style>
    <div id="container">
      <span id="span"></span>
    </div>
  )HTML");

  UpdateAllLifecyclePhases();

  auto* container = GetDocument().getElementById("container");
  auto* span = GetDocument().getElementById("span");
  ASSERT_TRUE(container);
  ASSERT_TRUE(span);

  unsigned start_count = GetStyleEngine().StyleForElementCount();
  GetStyleEngine().UpdateStyleAndLayoutTreeForContainer(
      *container, LogicalSize(200, 100), LogicalAxes(kLogicalAxisBoth));

  // The two ::before elements.
  EXPECT_EQ(2u, GetStyleEngine().StyleForElementCount() - start_count);
}

TEST_F(StyleEngineContainerQueryTest, MarkStyleDirtyFromContainerRecalc) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      #container {
        container-type: size;
        width: 100px;
        height: 100px;
      }
      @container size(min-width: 200px) {
        #input { background-color: green; }
      }
    </style>
    <div id="container">
      <input id="input" type="text">
    </div>
  )HTML");

  UpdateAllLifecyclePhases();

  auto* container = GetDocument().getElementById("container");
  auto* input = GetDocument().getElementById("input");
  ASSERT_TRUE(container);
  ASSERT_TRUE(input);
  auto* inner_editor = DynamicTo<HTMLInputElement>(input)->InnerEditorElement();
  ASSERT_TRUE(inner_editor);

  scoped_refptr<const ComputedStyle> old_inner_style =
      inner_editor->GetComputedStyle();
  EXPECT_TRUE(old_inner_style);

  unsigned start_count = GetStyleEngine().StyleForElementCount();
  GetStyleEngine().UpdateStyleAndLayoutTreeForContainer(
      *container, LogicalSize(200, 100), LogicalAxes(kLogicalAxisBoth));

  // Input elements mark their InnerEditorElement() style-dirty when they are
  // recalculated. That means the UpdateStyleAndLayoutTreeForContainer() call
  // above will involve marking ChildNeedsStyleRecalc all the way up to the
  // documentElement. Check that we don't leave anything dirty.
  EXPECT_FALSE(GetDocument().NeedsLayoutTreeUpdate());
  EXPECT_FALSE(GetDocument().documentElement()->ChildNeedsStyleRecalc());

  // The input element is recalculated. The inner editor element isn't counted
  // because we don't do normal style resolution to create the ComputedStyle for
  // it, but check that we have a new ComputedStyle object for it.
  EXPECT_EQ(1u, GetStyleEngine().StyleForElementCount() - start_count);

  const ComputedStyle* new_inner_style = inner_editor->GetComputedStyle();
  EXPECT_TRUE(new_inner_style);
  EXPECT_NE(old_inner_style, new_inner_style);
}

TEST_F(StyleEngineContainerQueryTest, UsesContainerQueries) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
      <style>
        #a { z-index:2; }
      </style>
      <style id=late>
      </style>
      <div id=a></div>
    )HTML");
  UpdateAllLifecyclePhases();
  auto* a = GetDocument().getElementById("a");
  ASSERT_TRUE(a);
  EXPECT_EQ(2, a->ComputedStyleRef().ZIndex());
  EXPECT_FALSE(GetStyleEngine().UsesContainerQueries());

  auto* late_style = GetDocument().getElementById("late");
  ASSERT_TRUE(late_style);

  late_style->setTextContent(R"CSS(
      @container size(min-width: 1px) {
        #a { color: green; }
      }
    )CSS");
  GetStyleEngine().UpdateActiveStyle();
  // Note the @container query does not match anything (it's not inside a
  // container), but UsesContainerQueries should still be true.
  EXPECT_TRUE(GetStyleEngine().UsesContainerQueries());

  late_style->setTextContent("");
  GetStyleEngine().UpdateActiveStyle();
  EXPECT_FALSE(GetStyleEngine().UsesContainerQueries());
}

TEST_F(StyleEngineContainerQueryTest,
       UpdateStyleAndLayoutTreeWithoutLayoutDependency) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      .toggle { width: 200px; }
    </style>
    <div id=a></div>
  )HTML");
  UpdateAllLifecyclePhases();
  EXPECT_FALSE(GetDocument().View()->NeedsLayout());

  Element* a = GetDocument().getElementById("a");
  ASSERT_TRUE(a);
  a->classList().Add("toggle");

  GetDocument().UpdateStyleAndLayoutTree();
  EXPECT_TRUE(GetDocument().View()->NeedsLayout())
      << "No layout if style does not depend on layout";
}

TEST_F(StyleEngineContainerQueryTest,
       UpdateStyleAndLayoutTreeWithLayoutDependency) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      #container {
        container-type: inline-size;
      }
      #container.toggle {
        width: 200px;
      }

      @container size(min-width: 200px) {
        #a { z-index: 2; }
      }
    </style>
    <main id=container>
      <div id=a></div>
    </main>
  )HTML");
  UpdateAllLifecyclePhases();
  EXPECT_FALSE(GetDocument().View()->NeedsLayout());

  Element* container = GetDocument().getElementById("container");
  ASSERT_TRUE(container);
  container->classList().Add("toggle");

  GetDocument().UpdateStyleAndLayoutTree();
  EXPECT_FALSE(GetDocument().View()->NeedsLayout())
      << "Layout should happen as part of UpdateStyleAndLayoutTree";

  Element* a = GetDocument().getElementById("a");
  ASSERT_TRUE(a);
  EXPECT_EQ(2, a->ComputedStyleRef().ZIndex());
}

TEST_F(StyleEngineTest, ContainerRelativeUnitsRuntimeFlag) {
  String css = R"CSS(
    top: 1qw;
    left: 1qh;
    bottom: 1qi;
    right: 1qb;
    padding-top: 1qmin;
    padding-right: 1qmax;
    padding-bottom: calc(1qw);
    margin-left: 1px;
  )CSS";

  {
    ScopedCSSContainerQueriesForTest cq_feature(false);
    ScopedCSSContainerRelativeUnitsForTest feature(false);
    const CSSPropertyValueSet* set =
        css_test_helpers::ParseDeclarationBlock(css);
    ASSERT_TRUE(set);
    EXPECT_EQ(1u, set->PropertyCount());
    EXPECT_TRUE(set->HasProperty(CSSPropertyID::kMarginLeft));
  }

  {
    ScopedCSSContainerQueriesForTest cq_feature(false);
    ScopedCSSContainerRelativeUnitsForTest feature(true);
    const CSSPropertyValueSet* set =
        css_test_helpers::ParseDeclarationBlock(css);
    ASSERT_TRUE(set);
    EXPECT_EQ(8u, set->PropertyCount());
  }
}

TEST_F(StyleEngineTest, ContainerPropertiesRuntimeFlag) {
  Vector<String> declarations = {"container-type:inline-size",
                                 "container-name:foo", "container:inline-size"};

  {
    ScopedCSSContainerQueriesForTest feature(false);

    for (const String& decl : declarations) {
      const auto* set = css_test_helpers::ParseDeclarationBlock(decl);
      ASSERT_TRUE(set);
      EXPECT_EQ(0u, set->PropertyCount());
    }
  }

  {
    ScopedCSSContainerQueriesForTest feature(true);

    for (const String& decl : declarations) {
      const auto* set = css_test_helpers::ParseDeclarationBlock(decl);
      ASSERT_TRUE(set);
      EXPECT_GT(set->PropertyCount(), 0u);
    }
  }
}

TEST_F(StyleEngineTest, VideoControlsReject) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <video controls></video>
    <div id="target"></div>
  )HTML");
  UpdateAllLifecyclePhases();

  StyleEngine& engine = GetStyleEngine();
  // Even if the Stats() were already enabled, the following resets it to 0.
  engine.SetStatsEnabled(true);

  StyleResolverStats* stats = engine.Stats();
  ASSERT_TRUE(stats);
  EXPECT_EQ(0u, stats->rules_fast_rejected);
  EXPECT_EQ(0u, stats->rules_rejected);

  Element* target = GetDocument().getElementById("target");
  ASSERT_TRUE(target);
  target->SetInlineStyleProperty(CSSPropertyID::kColor, "green");

  GetDocument().Lifecycle().AdvanceTo(DocumentLifecycle::kInStyleRecalc);
  GetStyleEngine().RecalcStyle();

  // There should be no UA rules for a div to reject
  EXPECT_EQ(0u, stats->rules_fast_rejected);
  EXPECT_EQ(0u, stats->rules_rejected);
}

TEST_F(StyleEngineTest, FastRejectForHostChild) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      .notfound span {
        color: pink;
      }
    </style>
    <div id="host">
      <span id="slotted"></span>
    </div>
  )HTML");

  Element* host = GetDocument().getElementById("host");
  ASSERT_TRUE(host);
  ShadowRoot& shadow_root =
      host->AttachShadowRootInternal(ShadowRootType::kOpen);
  shadow_root.setInnerHTML(R"HTML(
    <slot></slot>
  )HTML");
  UpdateAllLifecyclePhases();

  StyleEngine& engine = GetStyleEngine();
  // Even if the Stats() were already enabled, the following resets it to 0.
  engine.SetStatsEnabled(true);

  StyleResolverStats* stats = engine.Stats();
  ASSERT_TRUE(stats);
  EXPECT_EQ(0u, stats->rules_fast_rejected);

  Element* span = GetDocument().getElementById("slotted");
  ASSERT_TRUE(span);
  span->SetInlineStyleProperty(CSSPropertyID::kColor, "green");

  GetDocument().Lifecycle().AdvanceTo(DocumentLifecycle::kInStyleRecalc);
  GetStyleEngine().RecalcStyle();

  // Should fast reject ".notfound span"
  EXPECT_EQ(1u, stats->rules_fast_rejected);
}

TEST_F(StyleEngineTest, RejectSlottedSelector) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <div id="host">
      <span id="slotted"></span>
    </div>
  )HTML");

  Element* host = GetDocument().getElementById("host");
  ASSERT_TRUE(host);
  ShadowRoot& shadow_root =
      host->AttachShadowRootInternal(ShadowRootType::kOpen);
  shadow_root.setInnerHTML(R"HTML(
    <style>
      .notfound ::slotted(span) {
        color: pink;
      }
    </style>
    <slot></slot>
  )HTML");
  UpdateAllLifecyclePhases();

  StyleEngine& engine = GetStyleEngine();
  // Even if the Stats() were already enabled, the following resets it to 0.
  engine.SetStatsEnabled(true);

  StyleResolverStats* stats = engine.Stats();
  ASSERT_TRUE(stats);
  EXPECT_EQ(0u, stats->rules_fast_rejected);

  Element* span = GetDocument().getElementById("slotted");
  ASSERT_TRUE(span);
  span->SetInlineStyleProperty(CSSPropertyID::kColor, "green");

  GetDocument().Lifecycle().AdvanceTo(DocumentLifecycle::kInStyleRecalc);
  GetStyleEngine().RecalcStyle();

  // Should fast reject ".notfound ::slotted(span)"
  EXPECT_EQ(1u, stats->rules_fast_rejected);
}

TEST_F(StyleEngineTest, AudioUAStyleNameSpace) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <audio id="html-audio"></audio>
  )HTML");
  Element* html_audio = GetDocument().getElementById("html-audio");
  Element* audio = GetDocument().createElementNS("http://dummyns", "audio",
                                                 ASSERT_NO_EXCEPTION);
  GetDocument().body()->appendChild(audio);
  UpdateAllLifecyclePhases();

  // display:none UA rule for audio element should not apply outside html.
  EXPECT_TRUE(audio->GetComputedStyle());
  EXPECT_FALSE(html_audio->GetComputedStyle());

  gfx::SizeF page_size(400, 400);
  GetDocument().GetFrame()->StartPrinting(page_size, page_size, 1);

  // Also for printing.
  EXPECT_TRUE(audio->GetComputedStyle());
  EXPECT_FALSE(html_audio->GetComputedStyle());
}

TEST_F(StyleEngineTest, TargetTextUseCount) {
  ClearUseCounter(WebFeature::kCSSSelectorTargetText);
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      #nevermatch::target-text { background-color: pink }
    </style>
  )HTML");
  UpdateAllLifecyclePhases();
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSSelectorTargetText));
  ClearUseCounter(WebFeature::kCSSSelectorTargetText);

  // Count ::target-text if we would have matched if the page was loaded with a
  // text fragment url.
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      div::target-text { background-color: pink }
    </style>
    <div></div>
  )HTML");
  UpdateAllLifecyclePhases();
  EXPECT_TRUE(IsUseCounted(WebFeature::kCSSSelectorTargetText));
  ClearUseCounter(WebFeature::kCSSSelectorTargetText);
}

TEST_F(StyleEngineTest, NonDirtyStyleRecalcRoot) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <div id="host">
      <span id="slotted"></span>
    </div>
  )HTML");

  auto* host = GetDocument().getElementById("host");
  auto* slotted = GetDocument().getElementById("slotted");

  ShadowRoot& shadow_root =
      host->AttachShadowRootInternal(ShadowRootType::kOpen);
  shadow_root.setInnerHTML("<slot></slot>");
  UpdateAllLifecyclePhases();

  slotted->remove();
  GetDocument().body()->appendChild(slotted);
  host->remove();
  auto* recalc_root = GetStyleRecalcRoot();
  EXPECT_EQ(recalc_root, &GetDocument());
  EXPECT_TRUE(GetDocument().documentElement()->ChildNeedsStyleRecalc());
}

TEST_F(StyleEngineTest, AtCounterStyleUseCounter) {
  GetDocument().View()->UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSAtRuleCounterStyle));

  GetDocument().body()->setInnerHTML("<style>@counter-style foo {}</style>");
  GetDocument().View()->UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(IsUseCounted(WebFeature::kCSSAtRuleCounterStyle));
}

TEST_F(StyleEngineTest, CounterStyleDisabledInShadowDOM) {
  ScopedCSSAtRuleCounterStyleInShadowDOMForTest
      counter_style_in_shadow_dom_disabled(false);

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @counter-style foo { symbols: A; }
    </style>
    <ol id="foo" style="list-style-type: foo"><li></li></ol>
    <div id="host"></div>
  )HTML");

  Element* host = GetDocument().getElementById("host");
  ShadowRoot& shadow_root =
      host->AttachShadowRootInternal(ShadowRootType::kOpen);
  shadow_root.setInnerHTML(R"HTML(
    <style>
      @counter-style bar { symbols: B; }
    </style>
    <ol id="foo" style="list-style-type: foo"><li></li></ol>
    <ol id="bar" style="list-style-type: bar"><li></li></ol>
  )HTML");

  UpdateAllLifecyclePhases();

  // Only @counter-style rules defined in the document scope are effective,
  // matching the spec status as of Feb 2021.

  LayoutObject* document_foo =
      GetDocument().getElementById("foo")->firstChild()->GetLayoutObject();
  EXPECT_EQ("A. ", GetListMarkerText(document_foo));

  LayoutObject* shadow_foo =
      shadow_root.getElementById("foo")->firstChild()->GetLayoutObject();
  EXPECT_EQ("A. ", GetListMarkerText(shadow_foo));

  LayoutObject* shadow_bar =
      shadow_root.getElementById("bar")->firstChild()->GetLayoutObject();
  EXPECT_EQ("1. ", GetListMarkerText(shadow_bar));
}

TEST_F(StyleEngineTest, SystemFontsObeyDefaultFontSize) {
  // <input> get assigned "font: -webkit-small-control" in the UA sheet.
  Element* body = GetDocument().body();
  body->setInnerHTML("<input>");
  Element* input = GetDocument().QuerySelector("input");

  // Test the standard font sizes that can be chosen in chrome://settings/
  for (int fontSize : {9, 12, 16, 20, 24}) {
    GetDocument().GetSettings()->SetDefaultFontSize(fontSize);
    UpdateAllLifecyclePhases();
    EXPECT_EQ(fontSize, body->GetComputedStyle()->FontSize());
    EXPECT_EQ(fontSize - 3, input->GetComputedStyle()->FontSize());
  }

  // Now test degenerate cases
  GetDocument().GetSettings()->SetDefaultFontSize(-1);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(1, body->GetComputedStyle()->FontSize());
  EXPECT_EQ(1, input->GetComputedStyle()->FontSize());

  GetDocument().GetSettings()->SetDefaultFontSize(0);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(1, body->GetComputedStyle()->FontSize());
  EXPECT_EQ(13, input->GetComputedStyle()->FontSize());

  GetDocument().GetSettings()->SetDefaultFontSize(1);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(1, body->GetComputedStyle()->FontSize());
  EXPECT_EQ(1, input->GetComputedStyle()->FontSize());

  GetDocument().GetSettings()->SetDefaultFontSize(2);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(2, body->GetComputedStyle()->FontSize());
  EXPECT_EQ(2, input->GetComputedStyle()->FontSize());

  GetDocument().GetSettings()->SetDefaultFontSize(3);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(3, body->GetComputedStyle()->FontSize());
  EXPECT_EQ(0, input->GetComputedStyle()->FontSize());

  GetDocument().GetSettings()->SetDefaultFontSize(12345);
  UpdateAllLifecyclePhases();
  EXPECT_EQ(10000, body->GetComputedStyle()->FontSize());
  EXPECT_EQ(10000, input->GetComputedStyle()->FontSize());
}

TEST_F(StyleEngineTest, CascadeLayersInOriginsAndTreeScopes) {
  ScopedCSSCascadeLayersForTest enabled_scope(true);

  // Verifies that user layers and author layers in each tree scope are managed
  // separately. Each have their own layer ordering.

  auto* user_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  user_sheet->ParseString("@layer foo, bar;");
  StyleSheetKey user_key("user_layers");
  GetStyleEngine().InjectSheet(user_key, user_sheet, WebDocument::kUserOrigin);

  GetDocument().body()->setInnerHTMLWithDeclarativeShadowDOMForTesting(R"HTML(
    <style>
      @layer bar, foo;
    </style>
    <div id="host">
      <template shadowroot="open">
        <style>
          @layer foo, bar, foo.baz;
        </style>
      </template>
    </div>
  )HTML");

  UpdateAllLifecyclePhases();

  // User layer order: foo, bar, (implicit outer layer)
  auto* user_layer_map = GetStyleEngine().GetUserCascadeLayerMap();
  ASSERT_TRUE(user_layer_map);

  const CascadeLayer& user_outer_layer =
      user_sheet->GetRuleSet().CascadeLayers();
  EXPECT_EQ("", user_outer_layer.GetName());
  EXPECT_EQ(CascadeLayerMap::kImplicitOuterLayerOrder,
            user_layer_map->GetLayerOrder(user_outer_layer));

  const CascadeLayer& user_foo = *user_outer_layer.GetDirectSubLayers()[0];
  EXPECT_EQ("foo", user_foo.GetName());
  EXPECT_EQ(0u, user_layer_map->GetLayerOrder(user_foo));

  const CascadeLayer& user_bar = *user_outer_layer.GetDirectSubLayers()[1];
  EXPECT_EQ("bar", user_bar.GetName());
  EXPECT_EQ(1u, user_layer_map->GetLayerOrder(user_bar));

  // Document scope author layer order: bar, foo, (implicit outer layer)
  auto* document_layer_map =
      GetDocument().GetScopedStyleResolver()->GetCascadeLayerMap();
  ASSERT_TRUE(document_layer_map);

  const CascadeLayer& document_outer_layer =
      To<HTMLStyleElement>(GetDocument().QuerySelector("style"))
          ->sheet()
          ->Contents()
          ->GetRuleSet()
          .CascadeLayers();
  EXPECT_EQ("", document_outer_layer.GetName());
  EXPECT_EQ(CascadeLayerMap::kImplicitOuterLayerOrder,
            document_layer_map->GetLayerOrder(document_outer_layer));

  const CascadeLayer& document_bar =
      *document_outer_layer.GetDirectSubLayers()[0];
  EXPECT_EQ("bar", document_bar.GetName());
  EXPECT_EQ(0u, document_layer_map->GetLayerOrder(document_bar));

  const CascadeLayer& document_foo =
      *document_outer_layer.GetDirectSubLayers()[1];
  EXPECT_EQ("foo", document_foo.GetName());
  EXPECT_EQ(1u, document_layer_map->GetLayerOrder(document_foo));

  // Shadow scope author layer order: foo.baz, foo, bar, (implicit outer layer)
  ShadowRoot* shadow = GetDocument().getElementById("host")->GetShadowRoot();
  auto* shadow_layer_map =
      shadow->GetScopedStyleResolver()->GetCascadeLayerMap();
  ASSERT_TRUE(shadow_layer_map);

  const CascadeLayer& shadow_outer_layer =
      To<HTMLStyleElement>(shadow->QuerySelector("style"))
          ->sheet()
          ->Contents()
          ->GetRuleSet()
          .CascadeLayers();
  EXPECT_EQ("", shadow_outer_layer.GetName());
  EXPECT_EQ(CascadeLayerMap::kImplicitOuterLayerOrder,
            shadow_layer_map->GetLayerOrder(shadow_outer_layer));

  const CascadeLayer& shadow_foo = *shadow_outer_layer.GetDirectSubLayers()[0];
  EXPECT_EQ("foo", shadow_foo.GetName());
  EXPECT_EQ(1u, shadow_layer_map->GetLayerOrder(shadow_foo));

  const CascadeLayer& shadow_foo_baz = *shadow_foo.GetDirectSubLayers()[0];
  EXPECT_EQ("baz", shadow_foo_baz.GetName());
  EXPECT_EQ(0u, shadow_layer_map->GetLayerOrder(shadow_foo_baz));

  const CascadeLayer& shadow_bar = *shadow_outer_layer.GetDirectSubLayers()[1];
  EXPECT_EQ("bar", shadow_bar.GetName());
  EXPECT_EQ(2u, shadow_layer_map->GetLayerOrder(shadow_bar));
}

TEST_F(StyleEngineTest, CascadeLayersFromMultipleSheets) {
  ScopedCSSCascadeLayersForTest enabled_scope(true);

  // The layer ordering in sheet2 is different from the final ordering.
  GetDocument().body()->setInnerHTML(R"HTML(
    <style id="sheet1">
      @layer foo, bar;
    </style>
    <style id="sheet2">
      @layer baz, bar.qux, foo.quux;
    </style>
  )HTML");

  UpdateAllLifecyclePhases();

  // Final layer ordering:
  // foo.quux, foo, bar.qux, bar, baz, (implicit outer layer)
  auto* layer_map =
      GetDocument().GetScopedStyleResolver()->GetCascadeLayerMap();
  ASSERT_TRUE(layer_map);

  const CascadeLayer& sheet1_outer_layer =
      To<HTMLStyleElement>(GetDocument().getElementById("sheet1"))
          ->sheet()
          ->Contents()
          ->GetRuleSet()
          .CascadeLayers();
  EXPECT_EQ("", sheet1_outer_layer.GetName());
  EXPECT_EQ(CascadeLayerMap::kImplicitOuterLayerOrder,
            layer_map->GetLayerOrder(sheet1_outer_layer));

  const CascadeLayer& sheet1_foo = *sheet1_outer_layer.GetDirectSubLayers()[0];
  EXPECT_EQ("foo", sheet1_foo.GetName());
  EXPECT_EQ(1u, layer_map->GetLayerOrder(sheet1_foo));

  const CascadeLayer& sheet1_bar = *sheet1_outer_layer.GetDirectSubLayers()[1];
  EXPECT_EQ("bar", sheet1_bar.GetName());
  EXPECT_EQ(3u, layer_map->GetLayerOrder(sheet1_bar));

  const CascadeLayer& sheet2_outer_layer =
      To<HTMLStyleElement>(GetDocument().getElementById("sheet2"))
          ->sheet()
          ->Contents()
          ->GetRuleSet()
          .CascadeLayers();
  EXPECT_EQ("", sheet2_outer_layer.GetName());
  EXPECT_EQ(CascadeLayerMap::kImplicitOuterLayerOrder,
            layer_map->GetLayerOrder(sheet2_outer_layer));

  const CascadeLayer& sheet2_baz = *sheet2_outer_layer.GetDirectSubLayers()[0];
  EXPECT_EQ("baz", sheet2_baz.GetName());
  EXPECT_EQ(4u, layer_map->GetLayerOrder(sheet2_baz));

  const CascadeLayer& sheet2_bar = *sheet2_outer_layer.GetDirectSubLayers()[1];
  EXPECT_EQ("bar", sheet2_bar.GetName());
  EXPECT_EQ(3u, layer_map->GetLayerOrder(sheet2_bar));

  const CascadeLayer& sheet2_bar_qux = *sheet2_bar.GetDirectSubLayers()[0];
  EXPECT_EQ("qux", sheet2_bar_qux.GetName());
  EXPECT_EQ(2u, layer_map->GetLayerOrder(sheet2_bar_qux));

  const CascadeLayer& sheet2_foo = *sheet2_outer_layer.GetDirectSubLayers()[2];
  EXPECT_EQ("foo", sheet2_foo.GetName());
  EXPECT_EQ(1u, layer_map->GetLayerOrder(sheet2_foo));

  const CascadeLayer& sheet2_foo_quux = *sheet2_foo.GetDirectSubLayers()[0];
  EXPECT_EQ("quux", sheet2_foo_quux.GetName());
  EXPECT_EQ(0u, layer_map->GetLayerOrder(sheet2_foo_quux));
}

TEST_F(StyleEngineTest, CascadeLayersNotExplicitlyDeclared) {
  ScopedCSSCascadeLayersForTest enabled_scope(true);

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      #no-layers { }
    </style>
  )HTML");

  UpdateAllLifecyclePhases();

  // We don't create CascadeLayerMap if no layers are explicitly declared.
  ASSERT_TRUE(GetDocument().GetScopedStyleResolver());
  ASSERT_FALSE(GetDocument().GetScopedStyleResolver()->GetCascadeLayerMap());
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSCascadeLayers));
}

TEST_F(StyleEngineTest, CascadeLayersSheetsRemoved) {
  ScopedCSSCascadeLayersForTest enabled_scope(true);

  GetDocument().body()->setInnerHTMLWithDeclarativeShadowDOMForTesting(R"HTML(
    <style>
      @layer bar, foo;
    </style>
    <div id="host">
      <template shadowroot="open">
        <style>
          @layer foo, bar, foo.baz;
        </style>
      </template>
    </div>
  )HTML");

  UpdateAllLifecyclePhases();

  ASSERT_TRUE(GetDocument().GetScopedStyleResolver());
  ASSERT_TRUE(GetDocument().GetScopedStyleResolver()->GetCascadeLayerMap());

  ShadowRoot* shadow = GetDocument().getElementById("host")->GetShadowRoot();
  ASSERT_TRUE(shadow->GetScopedStyleResolver());
  ASSERT_TRUE(shadow->GetScopedStyleResolver()->GetCascadeLayerMap());

  GetDocument().QuerySelector("style")->remove();
  shadow->QuerySelector("style")->remove();
  UpdateAllLifecyclePhases();

  // When all sheets are removed, document ScopedStyleResolver is not cleared
  // but the CascadeLayerMap should be cleared.
  ASSERT_TRUE(GetDocument().GetScopedStyleResolver());
  ASSERT_FALSE(GetDocument().GetScopedStyleResolver()->GetCascadeLayerMap());

  // When all sheets are removed, shadow tree ScopedStyleResolver is cleared.
  ASSERT_FALSE(shadow->GetScopedStyleResolver());
}

TEST_F(StyleEngineTest, NonSlottedStyleDirty) {
  GetDocument().body()->setInnerHTML("<div id=host></div>");
  auto* host = GetDocument().getElementById("host");
  ASSERT_TRUE(host);
  host->AttachShadowRootInternal(ShadowRootType::kOpen);
  UpdateAllLifecyclePhases();

  // Add a child element to a shadow host with no slots. The inserted element is
  // not marked for style recalc because the GetStyleRecalcParent() returns
  // nullptr.
  auto* span = MakeGarbageCollected<HTMLSpanElement>(GetDocument());
  host->appendChild(span);
  EXPECT_FALSE(host->ChildNeedsStyleRecalc());
  EXPECT_FALSE(span->NeedsStyleRecalc());

  UpdateAllLifecyclePhases();

  // Set a style on the inserted child outside the flat tree.
  // GetStyleRecalcParent() still returns nullptr, and the ComputedStyle of the
  // child outside the flat tree is still null. No need to mark dirty.
  span->SetInlineStyleProperty(CSSPropertyID::kColor, "red");
  EXPECT_FALSE(host->ChildNeedsStyleRecalc());
  EXPECT_FALSE(span->NeedsStyleRecalc());

  // Ensure the ComputedStyle for the child and then change the style.
  // GetStyleRecalcParent() is still null, which means the host is not marked
  // with ChildNeedsStyleRecalc(), but the child needs to be marked dirty to
  // make sure the next EnsureComputedStyle updates the style to reflect the
  // changes.
  scoped_refptr<const ComputedStyle> old_style = span->EnsureComputedStyle();
  span->SetInlineStyleProperty(CSSPropertyID::kColor, "green");
  EXPECT_FALSE(host->ChildNeedsStyleRecalc());
  EXPECT_TRUE(span->NeedsStyleRecalc());
  UpdateAllLifecyclePhases();

  EXPECT_EQ(span->GetComputedStyle(), old_style);
  scoped_refptr<const ComputedStyle> new_style = span->EnsureComputedStyle();
  EXPECT_NE(new_style, old_style);

  EXPECT_EQ(MakeRGB(255, 0, 0),
            old_style->VisitedDependentColor(GetCSSPropertyColor()));
  EXPECT_EQ(MakeRGB(0, 128, 0),
            new_style->VisitedDependentColor(GetCSSPropertyColor()));
}

TEST_F(StyleEngineTest, CascadeLayerUseCount) {
  ScopedCSSCascadeLayersForTest enabled_scope(true);

  {
    ASSERT_FALSE(IsUseCounted(WebFeature::kCSSCascadeLayers));
    GetDocument().body()->setInnerHTML("<style>@layer foo;</style>");
    EXPECT_TRUE(IsUseCounted(WebFeature::kCSSCascadeLayers));
    ClearUseCounter(WebFeature::kCSSCascadeLayers);
  }

  {
    ASSERT_FALSE(IsUseCounted(WebFeature::kCSSCascadeLayers));
    GetDocument().body()->setInnerHTML("<style>@layer foo { }</style>");
    EXPECT_TRUE(IsUseCounted(WebFeature::kCSSCascadeLayers));
    ClearUseCounter(WebFeature::kCSSCascadeLayers);
  }

  {
    ASSERT_FALSE(IsUseCounted(WebFeature::kCSSCascadeLayers));
    GetDocument().body()->setInnerHTML(
        "<style>@import url(foo.css) layer(foo);</style>");
    EXPECT_TRUE(IsUseCounted(WebFeature::kCSSCascadeLayers));
    ClearUseCounter(WebFeature::kCSSCascadeLayers);
  }
}

TEST_F(StyleEngineTest, UserKeyframesOverrideWithCascadeLayers) {
  ScopedCSSCascadeLayersForTest enabled_scope(true);

  auto* user_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  user_sheet->ParseString(R"CSS(
    @layer base, override;

    #target {
      animation: anim 1s paused;
    }

    @layer override {
      @keyframes anim {
        from { width: 100px; }
      }
    }

    @layer base {
      @keyframes anim {
        from { width: 50px; }
      }
    }
  )CSS");
  StyleSheetKey key("user");
  GetStyleEngine().InjectSheet(key, user_sheet, WebDocument::kUserOrigin);

  GetDocument().body()->setInnerHTML(
      "<div id=target style='height: 100px'></div>");

  UpdateAllLifecyclePhases();

  Element* target = GetDocument().getElementById("target");
  EXPECT_EQ(100, target->OffsetWidth());
}

TEST_F(StyleEngineTest, UserCounterStyleOverrideWithCascadeLayers) {
  ScopedCSSCascadeLayersForTest enabled_scope(true);

  PageTestBase::LoadAhem(*GetDocument().GetFrame());

  auto* user_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  user_sheet->ParseString(R"CSS(
    @layer base, override;

    #target {
      width: min-content;
      font: 10px/1 Ahem;
    }

    #target::before {
      content: counter(dont-care, cnt-style);
    }

    @layer override {
      @counter-style cnt-style {
        system: cyclic;
        symbols: '0000';
      }
    }

    @layer base {
      @counter-style cnt-style {
        system: cyclic;
        symbols: '000';
      }
    }
  )CSS");
  StyleSheetKey key("user");
  GetStyleEngine().InjectSheet(key, user_sheet, WebDocument::kUserOrigin);

  GetDocument().body()->setInnerHTML("<div id=target></div>");

  UpdateAllLifecyclePhases();

  Element* target = GetDocument().getElementById("target");
  EXPECT_EQ(40, target->OffsetWidth());
}

TEST_F(StyleEngineTest, UserPropertyOverrideWithCascadeLayers) {
  ScopedCSSCascadeLayersForTest enabled_scope(true);

  auto* user_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  user_sheet->ParseString(R"CSS(
    @layer base, override;

    #target {
      width: var(--foo);
    }

    @layer override {
      @property --foo {
        syntax: '<length>';
        initial-value: 100px;
        inherits: false;
      }
    }

    @layer base {
      @property --foo {
        syntax: '<length>';
        initial-value: 50px;
        inherits: false;
      }
    }
  )CSS");
  StyleSheetKey key("user");
  GetStyleEngine().InjectSheet(key, user_sheet, WebDocument::kUserOrigin);

  GetDocument().body()->setInnerHTML(
      "<div id=target style='height: 100px'></div>");

  UpdateAllLifecyclePhases();

  Element* target = GetDocument().getElementById("target");
  EXPECT_EQ(100, target->OffsetWidth());
}

TEST_F(StyleEngineTest, UserAndAuthorPropertyOverrideWithCascadeLayers) {
  ScopedCSSCascadeLayersForTest enabled_scope(true);

  auto* user_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  user_sheet->ParseString(R"CSS(
    @layer base, override;

    @layer override {
      @property --foo {
        syntax: '<length>';
        initial-value: 50px;
        inherits: false;
      }
    }
  )CSS");
  StyleSheetKey key("user");
  GetStyleEngine().InjectSheet(key, user_sheet, WebDocument::kUserOrigin);

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @property --foo {
        syntax: '<length>';
        initial-value: 100px;
        inherits: false;
      }

      #target {
        width: var(--foo);
      }
    </style>
    <div id=target style='height: 100px'></div>
  )HTML");

  UpdateAllLifecyclePhases();

  // User-defined custom properties should not override author-defined
  // properties regardless of cascade layers.
  Element* target = GetDocument().getElementById("target");
  EXPECT_EQ(100, target->OffsetWidth());
}

TEST_F(StyleEngineTest, UserScrollTimelineOverrideWithCascadeLayers) {
  ScopedCSSCascadeLayersForTest layer_enabled(true);
  ScopedCSSScrollTimelineForTest scroll_timeline_enabled(true);

  auto* user_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  user_sheet->ParseString(R"CSS(
    @layer base, override;

    #scroller {
      overflow: scroll;
      width: 100px;
      height: 100px;
    }

    #scroll-contents {
      height: 200px;
    }

    @keyframes expand {
      from { width: 100px; }
      to { width: 200px; }
    }

    #target {
      animation: expand 10s linear;
      animation-timeline: timeline;
      height: 100px;
    }

    @layer override {
      @scroll-timeline timeline {
        source: selector(#scroller);
        start: 0px;
        end: 50px;
      }
    }

    @layer base {
      @scroll-timeline timeline {
        source: selector(#scroller);
        start: 0px;
        end: 100px;
      }
    }
  )CSS");
  StyleSheetKey key("user");
  GetStyleEngine().InjectSheet(key, user_sheet, WebDocument::kUserOrigin);

  GetDocument().body()->setInnerHTML(
      "<div id=scroller><div id=scroll-contents></div></div>"
      "<div id=target></div>");

  Element* scroller = GetDocument().getElementById("scroller");
  scroller->setScrollTop(25);
  UpdateAllLifecyclePhases();

  Element* target = GetDocument().getElementById("target");
  EXPECT_EQ(150, target->OffsetWidth());
}

TEST_F(StyleEngineTest, UserAndAuthorScrollTimelineOverrideWithCascadeLayers) {
  ScopedCSSCascadeLayersForTest layer_enabled(true);
  ScopedCSSScrollTimelineForTest scroll_timeline_enabled(true);

  auto* user_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  user_sheet->ParseString(R"CSS(
    @layer base, override;

    #scroller {
      overflow: scroll;
      width: 100px;
      height: 100px;
    }

    #scroll-contents {
      height: 200px;
    }

    @keyframes expand {
      from { width: 100px; }
      to { width: 200px; }
    }

    @layer override {
      @scroll-timeline timeline {
        source: selector(#scroller);
        start: 0px;
        end: 100px;
      }
    }
  )CSS");
  StyleSheetKey key("user");
  GetStyleEngine().InjectSheet(key, user_sheet, WebDocument::kUserOrigin);

  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @scroll-timeline timeline {
        source: selector(#scroller);
        start: 0px;
        end: 50px;
      }

      #target {
        animation: expand 10s linear;
        animation-timeline: timeline;
        height: 100px;
      }
    </style>
    <div id=scroller><div id=scroll-contents></div></div>
    <div id=target></div>
  )HTML");

  Element* scroller = GetDocument().getElementById("scroller");
  scroller->setScrollTop(25);
  UpdateAllLifecyclePhases();

  // User-defined scroll timelines should not override author-defined
  // scroll timelines regardless of cascade layers.
  Element* target = GetDocument().getElementById("target");
  EXPECT_EQ(150, target->OffsetWidth());
}

TEST_F(StyleEngineSimTest, UserFontFaceOverrideWithCascadeLayers) {
  ScopedCSSCascadeLayersForTest layer_enabled_scope(true);
  ScopedCSSFontFaceSizeAdjustForTest size_adjust_enabled_scope(true);

  SimRequest main_resource("https://example.com", "text/html");
  SimSubresourceRequest ahem_resource("https://example.com/ahem.woff2",
                                      "font/woff2");

  LoadURL("https://example.com");

  main_resource.Complete(R"HTML(
    <!doctype html>
    <div id=target>Test</div>
  )HTML");

  auto* user_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  user_sheet->ParseString(R"CSS(
    @layer base, override;

    @layer override {
      @font-face {
        font-family: custom-font;
        src: url('ahem.woff2') format('woff2');
      }
    }

    @layer base {
      @font-face {
        font-family: custom-font;
        src: url('ahem.woff2') format('woff2');
        size-adjust: 200%; /* To distinguish with the other @font-face */
      }
    }

    #target {
      font: 20px/1 custom-font;
      width: min-content;
    }
  )CSS");
  StyleSheetKey key("user");
  GetDocument().GetStyleEngine().InjectSheet(key, user_sheet,
                                             WebDocument::kUserOrigin);

  Compositor().BeginFrame();

  ahem_resource.Complete(
      test::ReadFromFile(test::CoreTestDataPath("Ahem.woff2"))
          ->CopyAs<Vector<char>>());

  test::RunPendingTasks();
  Compositor().BeginFrame();

  Element* target = GetDocument().getElementById("target");
  EXPECT_EQ(80, target->OffsetWidth());
}

TEST_F(StyleEngineSimTest, UserAndAuthorFontFaceOverrideWithCascadeLayers) {
  ScopedCSSCascadeLayersForTest layer_enabled_scope(true);
  ScopedCSSFontFaceSizeAdjustForTest size_adjust_enabled_scope(true);

  SimRequest main_resource("https://example.com", "text/html");
  SimSubresourceRequest ahem_resource("https://example.com/ahem.woff2",
                                      "font/woff2");

  LoadURL("https://example.com");

  main_resource.Complete(R"HTML(
    <!doctype html>
    <style>
      @font-face {
        font-family: custom-font;
        src: url('ahem.woff2') format('woff2');
      }

      #target {
        font: 20px/1 custom-font;
        width: min-content;
      }
    </style>
    <div id=target>Test</div>
  )HTML");

  auto* user_sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(GetDocument()));
  user_sheet->ParseString(R"CSS(
    @layer base, override;

    @layer override {
      @font-face {
        font-family: custom-font;
        src: url('ahem.woff2') format('woff2');
        size-adjust: 200%; /* To distinguish with the other @font-face */
      }
    }

  )CSS");
  StyleSheetKey key("user");
  GetDocument().GetStyleEngine().InjectSheet(key, user_sheet,
                                             WebDocument::kUserOrigin);

  Compositor().BeginFrame();

  ahem_resource.Complete(
      test::ReadFromFile(test::CoreTestDataPath("Ahem.woff2"))
          ->CopyAs<Vector<char>>());

  test::RunPendingTasks();
  Compositor().BeginFrame();

  // User-defined font faces should not override author-defined font faces
  // regardless of cascade layers.
  Element* target = GetDocument().getElementById("target");
  EXPECT_EQ(80, target->OffsetWidth());
}

TEST_F(StyleEngineTest, CascadeLayerActiveStyleSheetVectorNullRuleSetCrash) {
  ScopedCSSCascadeLayersForTest enabled_scope(true);

  // This creates an ActiveStyleSheetVector where the first entry has no
  // RuleSet, and the second entry has a layer rule difference.
  GetDocument().documentElement()->setInnerHTML(
      "<style media=invalid></style>"
      "<style>@layer {}</style>");

  // Should not crash
  UpdateAllLifecyclePhases();
}

TEST_F(StyleEngineTest, ChangeRenderingForHTMLSelect_DetachParent) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <select id="select"></select>
  )HTML");
  UpdateAllLifecyclePhases();
  EXPECT_FALSE(GetParentForDetachedSubtree());
  GetStyleEngine().ChangeRenderingForHTMLSelect(
      To<HTMLSelectElement>(*GetDocument().getElementById("select")));
  EXPECT_FALSE(GetParentForDetachedSubtree());
}

TEST_F(StyleEngineTest, EmptyDetachParent) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <span id="parent"><b>A</b> <i>B</i></span>
  )HTML");
  UpdateAllLifecyclePhases();

  auto* parent = GetDocument().getElementById("parent");
  parent->setInnerHTML("");

  ASSERT_TRUE(parent->GetLayoutObject());
  EXPECT_FALSE(parent->GetLayoutObject()->WhitespaceChildrenMayChange());
  EXPECT_FALSE(GetDocument().NeedsLayoutTreeUpdate());
}

TEST_F(StyleEngineTest, LegacyListItemRebuildRootCrash) {
  UpdateAllLifecyclePhases();

  auto* doc_elm = GetDocument().documentElement();
  ASSERT_TRUE(doc_elm);

  doc_elm->SetInlineStyleProperty(CSSPropertyID::kDisplay, "list-item");
  doc_elm->SetInlineStyleProperty(CSSPropertyID::kColumnCount, "1");
  UpdateAllLifecyclePhases();

  doc_elm->SetInlineStyleProperty(CSSPropertyID::kBackgroundColor, "green");
  // Should not crash
  UpdateAllLifecyclePhases();
}

// Regression test for https://crbug.com/1270190
TEST_F(StyleEngineTest, ScrollbarStyleNoExcessiveCaching) {
  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
    .a {
      width: 50px;
      height: 50px;
      background-color: magenta;
      overflow-y: scroll;
      margin: 5px;
      float: left;
    }

    .b {
      height: 100px;
    }

    ::-webkit-scrollbar {
      width: 10px;
    }

    ::-webkit-scrollbar-thumb {
      background: green;
    }

    ::-webkit-scrollbar-thumb:hover {
      background: red;
    }
    </style>
    <div class="a" id="container">
      <div class="b">
      </div>
    </div>
  )HTML");
  UpdateAllLifecyclePhases();

  // We currently don't cache ::-webkit-scrollbar-* pseudo element styles, so
  // the cache is always empty. If we decide to cache them, we should make sure
  // that the cache size remains bounded.

  Element* container = GetDocument().getElementById("container");
  EXPECT_FALSE(container->GetComputedStyle()->GetPseudoElementStyleCache());

  PaintLayerScrollableArea* area =
      container->GetLayoutBox()->GetScrollableArea();
  Scrollbar* scrollbar = area->VerticalScrollbar();
  CustomScrollbar* custom_scrollbar = To<CustomScrollbar>(scrollbar);

  scrollbar->SetHoveredPart(kThumbPart);
  UpdateAllLifecyclePhases();
  EXPECT_FALSE(container->GetComputedStyle()->GetPseudoElementStyleCache());
  EXPECT_EQ("#ff0000", custom_scrollbar->GetPart(kThumbPart)
                           ->Style()
                           ->BackgroundColor()
                           .GetColor()
                           .Serialized());

  scrollbar->SetHoveredPart(kNoPart);
  UpdateAllLifecyclePhases();
  EXPECT_FALSE(container->GetComputedStyle()->GetPseudoElementStyleCache());
  EXPECT_EQ("#008000", custom_scrollbar->GetPart(kThumbPart)
                           ->Style()
                           ->BackgroundColor()
                           .GetColor()
                           .Serialized());
}

TEST_F(StyleEngineTest, HasPseudoClassInvalidationSkipIrrelevantClassChange) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>.a:has(.b) { background-color: lime; }</style>
    <div id=div1>
      <div id=div2 class='a'>
        <div id=div3>
          <div id=div4></div>
        </div>
      </div>
    </div>
  )HTML");

  UpdateAllLifecyclePhases();

  unsigned start_count = GetStyleEngine().StyleForElementCount();
  GetDocument().getElementById("div4")->setAttribute(html_names::kClassAttr,
                                                     "c");
  UpdateAllLifecyclePhases();
  unsigned element_count =
      GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(0U, element_count);

  start_count = GetStyleEngine().StyleForElementCount();
  GetDocument().getElementById("div4")->setAttribute(html_names::kClassAttr,
                                                     "b");
  UpdateAllLifecyclePhases();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);
}

TEST_F(StyleEngineTest, HasPseudoClassInvalidationSkipIrrelevantIdChange) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>.a:has(#b) { background-color: lime; }</style>
    <div id=div1>
      <div id=div2 class='a'>
        <div id=div3>
          <div id=div4></div>
        </div>
      </div>
    </div>
  )HTML");

  UpdateAllLifecyclePhases();

  unsigned start_count = GetStyleEngine().StyleForElementCount();
  GetDocument().getElementById("div4")->setAttribute(html_names::kIdAttr, "c");
  UpdateAllLifecyclePhases();
  unsigned element_count =
      GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(0U, element_count);

  start_count = GetStyleEngine().StyleForElementCount();
  GetDocument().getElementById("c")->setAttribute(html_names::kIdAttr, "b");
  UpdateAllLifecyclePhases();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);
}

TEST_F(StyleEngineTest,
       HasPseudoClassInvalidationSkipIrrelevantAttributeChange) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>.a:has([b]) { background-color: lime; }</style>
    <div id=div1>
      <div id=div2 class='a'>
        <div id=div3>
          <div id=div4></div>
        </div>
      </div>
    </div>
  )HTML");

  UpdateAllLifecyclePhases();

  unsigned start_count = GetStyleEngine().StyleForElementCount();
  GetDocument().getElementById("div4")->setAttribute(QualifiedName("", "c", ""),
                                                     "C");
  UpdateAllLifecyclePhases();
  unsigned element_count =
      GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(0U, element_count);

  start_count = GetStyleEngine().StyleForElementCount();
  GetDocument().getElementById("div4")->setAttribute(QualifiedName("", "b", ""),
                                                     "B");
  UpdateAllLifecyclePhases();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);
}

TEST_F(StyleEngineTest,
       HasPseudoClassInvalidationSkipIrrelevantInsertionRemoval) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>.a:has(.b) { background-color: lime; }</style>
    <div id=div1>
      <div id=div2 class='a'>
        <div id=div3></div>
        <div id=div4></div>
      </div>
    </div>
  )HTML");

  UpdateAllLifecyclePhases();

  unsigned start_count = GetStyleEngine().StyleForElementCount();
  auto* div5 = MakeGarbageCollected<HTMLDivElement>(GetDocument());
  div5->setAttribute(html_names::kIdAttr, "div5");
  div5->setInnerHTML(R"HTML(<div class='c'></div>)HTML");
  GetDocument().getElementById("div3")->AppendChild(div5);
  UpdateAllLifecyclePhases();
  unsigned element_count =
      GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(2U, element_count);

  start_count = GetStyleEngine().StyleForElementCount();
  auto* div6 = MakeGarbageCollected<HTMLDivElement>(GetDocument());
  div6->setAttribute(html_names::kIdAttr, "div6");
  div6->setInnerHTML(R"HTML(<div class='b'></div>)HTML");
  GetDocument().getElementById("div4")->AppendChild(div6);
  UpdateAllLifecyclePhases();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(3U, element_count);

  start_count = GetStyleEngine().StyleForElementCount();
  GetDocument().getElementById("div3")->RemoveChild(
      GetDocument().getElementById("div5"));
  UpdateAllLifecyclePhases();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(0U, element_count);

  start_count = GetStyleEngine().StyleForElementCount();
  GetDocument().getElementById("div4")->RemoveChild(
      GetDocument().getElementById("div6"));
  UpdateAllLifecyclePhases();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);
}

TEST_F(StyleEngineTest, HasPseudoClassInvalidationUniversalInArgument) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>.a:has(*) { background-color: lime; }</style>
    <div id=div1>
      <div id=div2 class='a'>
      </div>
    </div>
  )HTML");

  UpdateAllLifecyclePhases();

  unsigned start_count = GetStyleEngine().StyleForElementCount();
  auto* div3 = MakeGarbageCollected<HTMLDivElement>(GetDocument());
  div3->setAttribute(html_names::kIdAttr, "div3");
  GetDocument().getElementById("div2")->AppendChild(div3);
  UpdateAllLifecyclePhases();
  unsigned element_count =
      GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(2U, element_count);

  start_count = GetStyleEngine().StyleForElementCount();
  GetDocument().getElementById("div2")->RemoveChild(
      GetDocument().getElementById("div3"));
  UpdateAllLifecyclePhases();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);
}

TEST_F(StyleEngineTest,
       HasPseudoClassInvalidationInsertionRemovalWithPseudoInHas) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      .a:has(.b:focus) { background-color: lime; }
      .c:has(.d) { background-color: green; }
    </style>
    <div id=div1>
      <div id=div2 class='a'></div>
      <div id=div3 class='c'></div>
    </div>
  )HTML");

  UpdateAllLifecyclePhases();

  unsigned start_count = GetStyleEngine().StyleForElementCount();
  auto* div4 = MakeGarbageCollected<HTMLDivElement>(GetDocument());
  div4->setAttribute(html_names::kIdAttr, "div4");
  GetDocument().getElementById("div2")->AppendChild(div4);
  UpdateAllLifecyclePhases();
  unsigned element_count =
      GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(2U, element_count);

  start_count = GetStyleEngine().StyleForElementCount();
  auto* div5 = MakeGarbageCollected<HTMLDivElement>(GetDocument());
  div5->setAttribute(html_names::kIdAttr, "div5");
  GetDocument().getElementById("div3")->AppendChild(div5);
  UpdateAllLifecyclePhases();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);

  start_count = GetStyleEngine().StyleForElementCount();
  GetDocument().getElementById("div2")->RemoveChild(
      GetDocument().getElementById("div4"));
  UpdateAllLifecyclePhases();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);

  start_count = GetStyleEngine().StyleForElementCount();
  GetDocument().getElementById("div3")->RemoveChild(
      GetDocument().getElementById("div5"));
  UpdateAllLifecyclePhases();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(0U, element_count);
}

}  // namespace blink