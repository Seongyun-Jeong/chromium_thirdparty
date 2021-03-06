// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/element_traversal.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/html/html_element.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/testing/page_test_base.h"

namespace blink {

class AffectedByPseudoTest : public PageTestBase {
 protected:
  struct ElementResult {
    const blink::HTMLQualifiedName tag;
    bool children_or_siblings_affected_by;
  };

  void SetHtmlInnerHTML(const char* html_content);
  void CheckElementsForFocus(ElementResult expected[],
                             unsigned expected_count) const;
};

void AffectedByPseudoTest::SetHtmlInnerHTML(const char* html_content) {
  GetDocument().documentElement()->setInnerHTML(String::FromUTF8(html_content));
  UpdateAllLifecyclePhasesForTest();
}

void AffectedByPseudoTest::CheckElementsForFocus(
    ElementResult expected[],
    unsigned expected_count) const {
  unsigned i = 0;
  HTMLElement* element = GetDocument().body();

  for (; element && i < expected_count;
       element = Traversal<HTMLElement>::Next(*element), ++i) {
    ASSERT_TRUE(element->HasTagName(expected[i].tag));
    DCHECK(element->GetComputedStyle());
    ASSERT_EQ(expected[i].children_or_siblings_affected_by,
              element->ChildrenOrSiblingsAffectedByFocus());
  }

  DCHECK(!element);
  DCHECK_EQ(i, expected_count);
}

// ":focus div" will mark ascendants of all divs with
// childrenOrSiblingsAffectedByFocus.
TEST_F(AffectedByPseudoTest, FocusedAscendant) {
  ElementResult expected[] = {{html_names::kBodyTag, true},
                              {html_names::kDivTag, true},
                              {html_names::kDivTag, false},
                              {html_names::kDivTag, false},
                              {html_names::kSpanTag, false}};

  SetHtmlInnerHTML(R"HTML(
    <head>
    <style>:focus div { background-color: pink }</style>
    </head>
    <body>
    <div><div></div></div>
    <div><span></span></div>
    </body>
  )HTML");

  CheckElementsForFocus(expected, sizeof(expected) / sizeof(ElementResult));
}

// "body:focus div" will mark the body element with
// childrenOrSiblingsAffectedByFocus.
TEST_F(AffectedByPseudoTest, FocusedAscendantWithType) {
  ElementResult expected[] = {{html_names::kBodyTag, true},
                              {html_names::kDivTag, false},
                              {html_names::kDivTag, false},
                              {html_names::kDivTag, false},
                              {html_names::kSpanTag, false}};

  SetHtmlInnerHTML(R"HTML(
    <head>
    <style>body:focus div { background-color: pink }</style>
    </head>
    <body>
    <div><div></div></div>
    <div><span></span></div>
    </body>
  )HTML");

  CheckElementsForFocus(expected, sizeof(expected) / sizeof(ElementResult));
}

// ":not(body):focus div" should not mark the body element with
// childrenOrSiblingsAffectedByFocus.
// Note that currently ":focus:not(body)" does not do the same. Then the :focus
// is checked and the childrenOrSiblingsAffectedByFocus flag set before the
// negated type selector is found.
TEST_F(AffectedByPseudoTest, FocusedAscendantWithNegatedType) {
  ElementResult expected[] = {{html_names::kBodyTag, false},
                              {html_names::kDivTag, true},
                              {html_names::kDivTag, false},
                              {html_names::kDivTag, false},
                              {html_names::kSpanTag, false}};

  SetHtmlInnerHTML(R"HTML(
    <head>
    <style>:not(body):focus div { background-color: pink }</style>
    </head>
    <body>
    <div><div></div></div>
    <div><span></span></div>
    </body>
  )HTML");

  CheckElementsForFocus(expected, sizeof(expected) / sizeof(ElementResult));
}

// Checking current behavior for ":focus + div", but this is a BUG or at best
// sub-optimal. The focused element will also in this case get
// childrenOrSiblingsAffectedByFocus even if it's really a sibling. Effectively,
// the whole sub-tree of the focused element will have styles recalculated even
// though none of the children are affected. There are other mechanisms that
// makes sure the sibling also gets its styles recalculated.
TEST_F(AffectedByPseudoTest, FocusedSibling) {
  ElementResult expected[] = {{html_names::kBodyTag, false},
                              {html_names::kDivTag, true},
                              {html_names::kSpanTag, false},
                              {html_names::kDivTag, false}};

  SetHtmlInnerHTML(R"HTML(
    <head>
    <style>:focus + div { background-color: pink }</style>
    </head>
    <body>
    <div>
      <span></span>
    </div>
    <div></div>
    </body>
  )HTML");

  CheckElementsForFocus(expected, sizeof(expected) / sizeof(ElementResult));
}

TEST_F(AffectedByPseudoTest, AffectedByFocusUpdate) {
  // Check that when focussing the outer div in the document below, you only
  // get a single element style recalc.

  SetHtmlInnerHTML(R"HTML(
    <style>:focus { border: 1px solid lime; }</style>
    <div id=d tabIndex=1>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  unsigned start_count = GetStyleEngine().StyleForElementCount();

  GetElementById("d")->focus();
  UpdateAllLifecyclePhasesForTest();

  unsigned element_count =
      GetStyleEngine().StyleForElementCount() - start_count;

  ASSERT_EQ(1U, element_count);
}

TEST_F(AffectedByPseudoTest, ChildrenOrSiblingsAffectedByFocusUpdate) {
  // Check that when focussing the outer div in the document below, you get a
  // style recalc for the whole subtree.

  SetHtmlInnerHTML(R"HTML(
    <style>:focus div { border: 1px solid lime; }</style>
    <div id=d tabIndex=1>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  unsigned start_count = GetStyleEngine().StyleForElementCount();

  GetElementById("d")->focus();
  UpdateAllLifecyclePhasesForTest();

  unsigned element_count =
      GetStyleEngine().StyleForElementCount() - start_count;

  ASSERT_EQ(11U, element_count);
}

TEST_F(AffectedByPseudoTest, InvalidationSetFocusUpdate) {
  // Check that when focussing the outer div in the document below, you get a
  // style recalc for the outer div and the class=a div only.

  SetHtmlInnerHTML(R"HTML(
    <style>:focus .a { border: 1px solid lime; }</style>
    <div id=d tabIndex=1>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div class='a'></div>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  unsigned start_count = GetStyleEngine().StyleForElementCount();

  GetElementById("d")->focus();
  UpdateAllLifecyclePhasesForTest();

  unsigned element_count =
      GetStyleEngine().StyleForElementCount() - start_count;

  ASSERT_EQ(2U, element_count);
}

TEST_F(AffectedByPseudoTest, NoInvalidationSetFocusUpdate) {
  // Check that when focussing the outer div in the document below, you get a
  // style recalc for the outer div only. The invalidation set for :focus will
  // include 'a', but the id=d div should be affectedByFocus, not
  // childrenOrSiblingsAffectedByFocus.

  SetHtmlInnerHTML(R"HTML(
    <style>#nomatch:focus .a { border: 1px solid lime; }</style>
    <div id=d tabIndex=1>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div class='a'></div>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  unsigned start_count = GetStyleEngine().StyleForElementCount();

  GetElementById("d")->focus();
  UpdateAllLifecyclePhasesForTest();

  unsigned element_count =
      GetStyleEngine().StyleForElementCount() - start_count;

  ASSERT_EQ(1U, element_count);
}

TEST_F(AffectedByPseudoTest, FocusWithinCommonAncestor) {
  // Check that when changing the focus between 2 elements we don't need a style
  // recalc for all the ancestors affected by ":focus-within".

  SetHtmlInnerHTML(R"HTML(
    <style>div:focus-within { background-color: lime; }</style>
    <div>
      <div>
        <div id=focusme1 tabIndex=1></div>
        <div id=focusme2 tabIndex=2></div>
      <div>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();

  unsigned start_count = GetStyleEngine().StyleForElementCount();

  GetElementById("focusme1")->focus();
  UpdateAllLifecyclePhasesForTest();

  unsigned element_count =
      GetStyleEngine().StyleForElementCount() - start_count;

  EXPECT_EQ(3U, element_count);

  start_count += element_count;

  GetElementById("focusme2")->focus();
  UpdateAllLifecyclePhasesForTest();

  element_count = GetStyleEngine().StyleForElementCount() - start_count;

  // Only "focusme1" & "focusme2" elements need a recalc thanks to the common
  // ancestor strategy.
  EXPECT_EQ(2U, element_count);
}

TEST_F(AffectedByPseudoTest, HoverScrollbar) {
  SetHtmlInnerHTML(
      "<style>div::-webkit-scrollbar:hover { color: pink; }</style>"
      "<div id=div1></div>");

  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(GetElementById("div1")->GetComputedStyle()->AffectedByHover());
}

TEST_F(AffectedByPseudoTest, AffectedByHasAndAncestorsAffectedByHas) {
  SetHtmlInnerHTML(R"HTML(
    <style>.a:has(.b) { background-color: lime; }</style>
    <div id=div1>
      <div id=div2 class='a'>
        <div id=div3>
          <div id=div4></div>
        </div>
      </div>
      <div id=div5 class='a'>
        <div id=div6 style='display: none'>
          <div id=div7></div>
        </div>
      </div>
      <div id=div8>
        <div id=div9>
          <div id=div10></div>
        </div>
      </div>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(GetElementById("div1")->GetComputedStyle()->AffectedByHas());
  EXPECT_FALSE(
      GetElementById("div1")->GetComputedStyle()->AncestorsAffectedByHas());
  EXPECT_TRUE(GetElementById("div2")->GetComputedStyle()->AffectedByHas());
  EXPECT_TRUE(
      GetElementById("div2")->GetComputedStyle()->AncestorsAffectedByHas());
  EXPECT_FALSE(GetElementById("div3")->GetComputedStyle()->AffectedByHas());
  EXPECT_TRUE(
      GetElementById("div3")->GetComputedStyle()->AncestorsAffectedByHas());
  EXPECT_FALSE(GetElementById("div4")->GetComputedStyle()->AffectedByHas());
  EXPECT_TRUE(
      GetElementById("div4")->GetComputedStyle()->AncestorsAffectedByHas());
  EXPECT_TRUE(GetElementById("div5")->GetComputedStyle()->AffectedByHas());
  EXPECT_TRUE(
      GetElementById("div5")->GetComputedStyle()->AncestorsAffectedByHas());
  EXPECT_EQ(GetElementById("div6")->GetComputedStyle(), nullptr);
  EXPECT_EQ(GetElementById("div7")->GetComputedStyle(), nullptr);
  EXPECT_FALSE(GetElementById("div8")->GetComputedStyle()->AffectedByHas());
  EXPECT_FALSE(
      GetElementById("div8")->GetComputedStyle()->AncestorsAffectedByHas());
  EXPECT_FALSE(GetElementById("div9")->GetComputedStyle()->AffectedByHas());
  EXPECT_FALSE(
      GetElementById("div9")->GetComputedStyle()->AncestorsAffectedByHas());
  EXPECT_FALSE(GetElementById("div10")->GetComputedStyle()->AffectedByHas());
  EXPECT_FALSE(
      GetElementById("div10")->GetComputedStyle()->AncestorsAffectedByHas());

  unsigned start_count = GetStyleEngine().StyleForElementCount();
  GetElementById("div10")->setAttribute(html_names::kClassAttr, "b");
  UpdateAllLifecyclePhasesForTest();
  unsigned element_count =
      GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(0U, element_count);

  start_count = GetStyleEngine().StyleForElementCount();
  GetElementById("div4")->setAttribute(html_names::kClassAttr, "b");
  UpdateAllLifecyclePhasesForTest();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);

  start_count = GetStyleEngine().StyleForElementCount();
  GetElementById("div7")->setAttribute(html_names::kClassAttr, "b");
  UpdateAllLifecyclePhasesForTest();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);
}

TEST_F(AffectedByPseudoTest, AncestorsAffectedByHasCheckFalseRestore) {
  SetHtmlInnerHTML(R"HTML(
    <style>.a:has(.b) { background-color: lime; }</style>
    <main id=div1>
      <div id=div2 class='a'>
        <div id=div3>
          <div id=div4></div>
        </div>
      </div>
      <div id=div5>
        <div id=div6>
          <div id=div7></div>
        </div>
      </div>
    </main>
  )HTML");

  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(GetElementById("div1")->GetComputedStyle()->AffectedByHas());
  EXPECT_FALSE(
      GetElementById("div1")->GetComputedStyle()->AncestorsAffectedByHas());
  EXPECT_TRUE(GetElementById("div2")->GetComputedStyle()->AffectedByHas());
  EXPECT_TRUE(
      GetElementById("div2")->GetComputedStyle()->AncestorsAffectedByHas());
  EXPECT_FALSE(GetElementById("div3")->GetComputedStyle()->AffectedByHas());
  EXPECT_TRUE(
      GetElementById("div3")->GetComputedStyle()->AncestorsAffectedByHas());
  EXPECT_FALSE(GetElementById("div4")->GetComputedStyle()->AffectedByHas());
  EXPECT_TRUE(
      GetElementById("div4")->GetComputedStyle()->AncestorsAffectedByHas());
  EXPECT_FALSE(GetElementById("div5")->GetComputedStyle()->AffectedByHas());
  EXPECT_FALSE(
      GetElementById("div5")->GetComputedStyle()->AncestorsAffectedByHas());
  EXPECT_FALSE(GetElementById("div6")->GetComputedStyle()->AffectedByHas());
  EXPECT_FALSE(
      GetElementById("div6")->GetComputedStyle()->AncestorsAffectedByHas());
  EXPECT_FALSE(GetElementById("div7")->GetComputedStyle()->AffectedByHas());
  EXPECT_FALSE(
      GetElementById("div7")->GetComputedStyle()->AncestorsAffectedByHas());

  unsigned start_count = GetStyleEngine().StyleForElementCount();
  GetElementById("div7")->setAttribute(html_names::kClassAttr, "b");
  UpdateAllLifecyclePhasesForTest();
  unsigned element_count =
      GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(0U, element_count);

  start_count = GetStyleEngine().StyleForElementCount();
  GetElementById("div4")->setAttribute(html_names::kClassAttr, "b");
  UpdateAllLifecyclePhasesForTest();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);
}

TEST_F(AffectedByPseudoTest,
       AffectedByDescendantPseudoStateAndAncestorsAffectedByHover) {
  SetHtmlInnerHTML(R"HTML(
    <style>
      .a:has(.b:hover) { background-color: lime; }
      .c:has(:hover) { background-color: green; }
      .d:has(.e) { background-color: blue }
    </style>
    <div id=div1>
      <div id=div2 class='a'>
        <div id=div3></div>
        <div id=div4></div>
      </div>
      <div id=div5 class='a'>
        <div id=div6></div>
        <div id=div7 class='b'></div>
      </div>
      <div id=div8 class='c'>
        <div id=div9></div>
        <div id=div10></div>
      </div>
      <div id=div11 class='d'>
        <div id=div12></div>
        <div id=div13></div>
      </div>
    </div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(
      GetElementById("div2")->GetComputedStyle()->AffectedByPseudoInHas());
  EXPECT_TRUE(GetElementById("div2")
                  ->GetComputedStyle()
                  ->AncestorsAffectedByHoverInHas());
  EXPECT_FALSE(
      GetElementById("div3")->GetComputedStyle()->AffectedByPseudoInHas());
  EXPECT_TRUE(GetElementById("div3")
                  ->GetComputedStyle()
                  ->AncestorsAffectedByHoverInHas());
  EXPECT_FALSE(
      GetElementById("div4")->GetComputedStyle()->AffectedByPseudoInHas());
  EXPECT_TRUE(GetElementById("div4")
                  ->GetComputedStyle()
                  ->AncestorsAffectedByHoverInHas());
  EXPECT_TRUE(
      GetElementById("div5")->GetComputedStyle()->AffectedByPseudoInHas());
  EXPECT_TRUE(GetElementById("div5")
                  ->GetComputedStyle()
                  ->AncestorsAffectedByHoverInHas());
  EXPECT_FALSE(
      GetElementById("div6")->GetComputedStyle()->AffectedByPseudoInHas());
  EXPECT_TRUE(GetElementById("div6")
                  ->GetComputedStyle()
                  ->AncestorsAffectedByHoverInHas());
  EXPECT_FALSE(
      GetElementById("div7")->GetComputedStyle()->AffectedByPseudoInHas());
  EXPECT_TRUE(GetElementById("div7")
                  ->GetComputedStyle()
                  ->AncestorsAffectedByHoverInHas());
  EXPECT_TRUE(
      GetElementById("div8")->GetComputedStyle()->AffectedByPseudoInHas());
  EXPECT_TRUE(GetElementById("div8")
                  ->GetComputedStyle()
                  ->AncestorsAffectedByHoverInHas());
  EXPECT_FALSE(
      GetElementById("div9")->GetComputedStyle()->AffectedByPseudoInHas());
  EXPECT_TRUE(GetElementById("div9")
                  ->GetComputedStyle()
                  ->AncestorsAffectedByHoverInHas());
  EXPECT_FALSE(
      GetElementById("div10")->GetComputedStyle()->AffectedByPseudoInHas());
  EXPECT_TRUE(GetElementById("div10")
                  ->GetComputedStyle()
                  ->AncestorsAffectedByHoverInHas());
  EXPECT_FALSE(
      GetElementById("div11")->GetComputedStyle()->AffectedByPseudoInHas());
  EXPECT_FALSE(GetElementById("div11")
                   ->GetComputedStyle()
                   ->AncestorsAffectedByHoverInHas());
  EXPECT_FALSE(
      GetElementById("div12")->GetComputedStyle()->AffectedByPseudoInHas());
  EXPECT_FALSE(GetElementById("div12")
                   ->GetComputedStyle()
                   ->AncestorsAffectedByHoverInHas());
  EXPECT_FALSE(
      GetElementById("div13")->GetComputedStyle()->AffectedByPseudoInHas());
  EXPECT_FALSE(GetElementById("div13")
                   ->GetComputedStyle()
                   ->AncestorsAffectedByHoverInHas());

  unsigned start_count = GetStyleEngine().StyleForElementCount();
  GetElementById("div3")->SetHovered(true);
  UpdateAllLifecyclePhasesForTest();
  unsigned element_count =
      GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);
  GetElementById("div3")->SetHovered(false);
  UpdateAllLifecyclePhasesForTest();

  start_count = GetStyleEngine().StyleForElementCount();
  GetElementById("div4")->SetHovered(true);
  UpdateAllLifecyclePhasesForTest();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);
  GetElementById("div4")->SetHovered(false);
  UpdateAllLifecyclePhasesForTest();

  start_count = GetStyleEngine().StyleForElementCount();
  GetElementById("div4")->setAttribute(html_names::kClassAttr, "b");
  UpdateAllLifecyclePhasesForTest();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);

  EXPECT_TRUE(
      GetElementById("div2")->GetComputedStyle()->AffectedByPseudoInHas());
  EXPECT_TRUE(GetElementById("div2")
                  ->GetComputedStyle()
                  ->AncestorsAffectedByHoverInHas());
  EXPECT_FALSE(
      GetElementById("div3")->GetComputedStyle()->AffectedByPseudoInHas());
  EXPECT_TRUE(GetElementById("div3")
                  ->GetComputedStyle()
                  ->AncestorsAffectedByHoverInHas());
  EXPECT_FALSE(
      GetElementById("div4")->GetComputedStyle()->AffectedByPseudoInHas());
  EXPECT_TRUE(GetElementById("div4")
                  ->GetComputedStyle()
                  ->AncestorsAffectedByHoverInHas());

  start_count = GetStyleEngine().StyleForElementCount();
  GetElementById("div6")->SetHovered(true);
  UpdateAllLifecyclePhasesForTest();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);
  GetElementById("div6")->SetHovered(false);
  UpdateAllLifecyclePhasesForTest();

  start_count = GetStyleEngine().StyleForElementCount();
  GetElementById("div7")->SetHovered(true);
  UpdateAllLifecyclePhasesForTest();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);
  GetElementById("div7")->SetHovered(false);
  UpdateAllLifecyclePhasesForTest();

  start_count = GetStyleEngine().StyleForElementCount();
  GetElementById("div9")->SetHovered(true);
  UpdateAllLifecyclePhasesForTest();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);
  GetElementById("div9")->SetHovered(false);
  UpdateAllLifecyclePhasesForTest();

  start_count = GetStyleEngine().StyleForElementCount();
  GetElementById("div10")->SetHovered(true);
  UpdateAllLifecyclePhasesForTest();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(1U, element_count);
  GetElementById("div10")->SetHovered(false);
  UpdateAllLifecyclePhasesForTest();

  start_count = GetStyleEngine().StyleForElementCount();
  GetElementById("div12")->SetHovered(true);
  UpdateAllLifecyclePhasesForTest();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(0U, element_count);
  GetElementById("div12")->SetHovered(false);
  UpdateAllLifecyclePhasesForTest();

  start_count = GetStyleEngine().StyleForElementCount();
  GetElementById("div13")->SetHovered(true);
  UpdateAllLifecyclePhasesForTest();
  element_count = GetStyleEngine().StyleForElementCount() - start_count;
  ASSERT_EQ(0U, element_count);
  GetElementById("div13")->SetHovered(false);
  UpdateAllLifecyclePhasesForTest();
}

}  // namespace blink
