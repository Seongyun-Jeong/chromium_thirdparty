// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/ng/layout_ng_fieldset.h"

#include "third_party/blink/renderer/core/dom/layout_tree_builder_traversal.h"
#include "third_party/blink/renderer/core/layout/layout_fieldset.h"
#include "third_party/blink/renderer/core/layout/layout_object_factory.h"

namespace blink {

LayoutNGFieldset::LayoutNGFieldset(Element* element)
    : LayoutNGBlockFlow(element) {
  SetChildrenInline(false);
}

LayoutBlock* LayoutNGFieldset::FindAnonymousFieldsetContentBox() const {
  LayoutObject* first_child = FirstChild();
  if (!first_child)
    return nullptr;
  if (first_child->IsAnonymous())
    return To<LayoutBlock>(first_child);
  LayoutObject* last_child = first_child->NextSibling();
  DCHECK(!last_child || !last_child->NextSibling());
  if (last_child && last_child->IsAnonymous())
    return To<LayoutBlock>(last_child);
  return nullptr;
}

void LayoutNGFieldset::AddChild(LayoutObject* new_child,
                                LayoutObject* before_child) {
  if (!new_child->IsText() && !new_child->IsAnonymous()) {
    // Adding a child LayoutObject always causes reattach of <fieldset>. So
    // |before_child| is always nullptr.
    // See HTMLFieldSetElement::DidRecalcStyle().
    DCHECK(!before_child);
  } else if (before_child && before_child->IsRenderedLegend()) {
    // Whitespace changes resulting from removed nodes are handled in
    // MarkForWhitespaceReattachment(), and don't trigger
    // HTMLFieldSetElement::DidRecalcStyle(). So the fieldset is not
    // reattached. We adjust |before_child| instead.
    Node* before_node =
        LayoutTreeBuilderTraversal::NextLayoutSibling(*before_child->GetNode());
    before_child = before_node ? before_node->GetLayoutObject() : nullptr;
  }

  // https://html.spec.whatwg.org/C/#the-fieldset-and-legend-elements
  // > * If the element has a rendered legend, then that element is expected
  // >   to be the first child box.
  // > * The anonymous fieldset content box is expected to appear after the
  // >   rendered legend and is expected to contain the content (including
  // >   the '::before' and '::after' pseudo-elements) of the fieldset
  // >   element except for the rendered legend, if there is one.

  if (new_child->IsRenderedLegendCandidate() &&
      !LayoutFieldset::FindInFlowLegend(*this)) {
    LayoutNGBlockFlow::AddChild(new_child, FirstChild());
    return;
  }
  LayoutBlock* fieldset_content = FindAnonymousFieldsetContentBox();
  if (!fieldset_content) {
    // We wrap everything inside an anonymous child, which will take care of the
    // fieldset contents. This parent will only be responsible for the fieldset
    // border and the rendered legend, if there is one. Everything else will be
    // done by the anonymous child. This includes display type, multicol,
    // scrollbars, and even padding. Note that the rendered legend (if any) will
    // also be a child of the anonymous object, although it'd be more natural to
    // have it as the first child of this object. The reason is that our layout
    // object tree builder cannot handle such discrepancies between DOM tree and
    // layout tree. Inserting anonymous wrappers is one thing (that is
    // supported). Removing it from its actual DOM siblings and putting it
    // elsewhere, on the other hand, does not work well.

    // TODO(crbug.com/875235): Consider other display types not mentioned in the
    // spec (ex. EDisplay::kLayoutCustom).
    EDisplay display = EDisplay::kFlowRoot;
    switch (StyleRef().Display()) {
      case EDisplay::kFlex:
      case EDisplay::kInlineFlex:
        display = EDisplay::kFlex;
        break;
      case EDisplay::kGrid:
      case EDisplay::kInlineGrid:
        display = EDisplay::kGrid;
        break;
      default:
        break;
    }

    fieldset_content =
        LayoutBlock::CreateAnonymousWithParentAndDisplay(this, display);
    LayoutBox::AddChild(fieldset_content);
  }
  fieldset_content->AddChild(new_child, before_child);
}

// TODO(mstensho): Should probably remove the anonymous child if it becomes
// childless. While an empty anonymous child should have no effect, it doesn't
// seem right to leave it around.

void LayoutNGFieldset::UpdateAnonymousChildStyle(
    const LayoutObject*,
    ComputedStyle& child_style) const {
  // Inherit all properties listed here:
  // https://html.spec.whatwg.org/C/#anonymous-fieldset-content-box

  child_style.SetAlignContent(StyleRef().AlignContent());
  child_style.SetAlignItems(StyleRef().AlignItems());

  child_style.SetBorderBottomLeftRadius(StyleRef().BorderBottomLeftRadius());
  child_style.SetBorderBottomRightRadius(StyleRef().BorderBottomRightRadius());
  child_style.SetBorderTopLeftRadius(StyleRef().BorderTopLeftRadius());
  child_style.SetBorderTopRightRadius(StyleRef().BorderTopRightRadius());

  child_style.SetPaddingTop(StyleRef().PaddingTop());
  child_style.SetPaddingRight(StyleRef().PaddingRight());
  child_style.SetPaddingBottom(StyleRef().PaddingBottom());
  child_style.SetPaddingLeft(StyleRef().PaddingLeft());

  if (StyleRef().SpecifiesColumns() && AllowsColumns()) {
    child_style.SetColumnCount(StyleRef().ColumnCount());
    child_style.SetColumnWidth(StyleRef().ColumnWidth());
  } else {
    child_style.SetHasAutoColumnCount();
    child_style.SetHasAutoColumnWidth();
  }
  child_style.SetColumnGap(StyleRef().ColumnGap());
  child_style.SetColumnFill(StyleRef().GetColumnFill());
  child_style.SetColumnRuleColor(StyleColor(
      LayoutObject::ResolveColor(StyleRef(), GetCSSPropertyColumnRuleColor())));
  child_style.SetColumnRuleStyle(StyleRef().ColumnRuleStyle());
  child_style.SetColumnRuleWidth(StyleRef().ColumnRuleWidth());

  child_style.SetFlexDirection(StyleRef().FlexDirection());
  child_style.SetFlexWrap(StyleRef().FlexWrap());

  child_style.SetGridAutoColumns(StyleRef().GridAutoColumns());
  child_style.SetGridAutoFlow(StyleRef().GetGridAutoFlow());
  child_style.SetGridAutoRows(StyleRef().GridAutoRows());
  child_style.SetGridColumnEnd(StyleRef().GridColumnEnd());
  child_style.SetGridColumnStart(StyleRef().GridColumnStart());
  child_style.SetGridRowEnd(StyleRef().GridRowEnd());
  child_style.SetGridRowStart(StyleRef().GridRowStart());

  // grid-template-columns, grid-template-rows
  child_style.SetGridTemplateColumns(StyleRef().GridTemplateColumns());
  child_style.SetGridTemplateRows(StyleRef().GridTemplateRows());
  child_style.SetNamedGridArea(StyleRef().NamedGridArea());
  child_style.SetNamedGridAreaColumnCount(
      StyleRef().NamedGridAreaColumnCount());
  child_style.SetNamedGridAreaRowCount(StyleRef().NamedGridAreaRowCount());
  child_style.SetImplicitNamedGridColumnLines(
      StyleRef().ImplicitNamedGridColumnLines());
  child_style.SetImplicitNamedGridRowLines(
      StyleRef().ImplicitNamedGridRowLines());

  child_style.SetRowGap(StyleRef().RowGap());

  child_style.SetJustifyContent(StyleRef().JustifyContent());
  child_style.SetJustifyItems(StyleRef().JustifyItems());
  child_style.SetOverflowX(StyleRef().OverflowX());
  child_style.SetOverflowY(StyleRef().OverflowY());
  child_style.SetUnicodeBidi(StyleRef().GetUnicodeBidi());

  // If the FIELDSET is an OOF container, the anonymous content box should be
  // an OOF container to steal OOF objects under the FIELDSET.
  if (CanContainFixedPositionObjects())
    child_style.SetContain(kContainsPaint);
  else if (StyleRef().CanContainAbsolutePositionObjects())
    child_style.SetPosition(EPosition::kRelative);
}

bool LayoutNGFieldset::IsOfType(LayoutObjectType type) const {
  return type == kLayoutObjectNGFieldset || LayoutNGBlockFlow::IsOfType(type);
}

void LayoutNGFieldset::InvalidatePaint(
    const PaintInvalidatorContext& context) const {
  // Fieldset's box decoration painting depends on the legend geometry.
  const LayoutBox* legend_box = LayoutFieldset::FindInFlowLegend(*this);
  if (legend_box && legend_box->ShouldCheckGeometryForPaintInvalidation()) {
    GetMutableForPainting().SetShouldDoFullPaintInvalidation(
        PaintInvalidationReason::kGeometry);
  }
  LayoutNGBlockFlow::InvalidatePaint(context);
}

bool LayoutNGFieldset::BackgroundIsKnownToBeOpaqueInRect(
    const PhysicalRect& local_rect) const {
  // If the field set has a legend, then it probably does not completely fill
  // its background.
  if (LayoutFieldset::FindInFlowLegend(*this))
    return false;

  return LayoutBlockFlow::BackgroundIsKnownToBeOpaqueInRect(local_rect);
}

bool LayoutNGFieldset::HitTestChildren(HitTestResult& result,
                                       const HitTestLocation& hit_test_location,
                                       const PhysicalOffset& accumulated_offset,
                                       HitTestAction hit_test_action) {
  if (LayoutNGBlockFlow::HitTestChildren(result, hit_test_location,
                                         accumulated_offset, hit_test_action))
    return true;

  DCHECK(!RuntimeEnabledFeatures::LayoutNGFragmentTraversalEnabled());
  LayoutBox* legend = LayoutFieldset::FindInFlowLegend(*this);
  if (!legend || legend->HasSelfPaintingLayer() || legend->IsColumnSpanAll())
    return false;
  if (legend->NodeAtPoint(result, hit_test_location,
                          accumulated_offset + legend->PhysicalLocation(this),
                          hit_test_action == kHitTestChildBlockBackgrounds
                              ? kHitTestChildBlockBackground
                              : hit_test_action)) {
    UpdateHitTestResult(result, hit_test_location.Point() - accumulated_offset);
    return true;
  }
  return false;
}

LayoutUnit LayoutNGFieldset::ScrollWidth() const {
  if (const auto* content = FindAnonymousFieldsetContentBox())
    return content->ScrollWidth();
  return LayoutNGBlockFlow::ScrollWidth();
}

LayoutUnit LayoutNGFieldset::ScrollHeight() const {
  if (const auto* content = FindAnonymousFieldsetContentBox())
    return content->ScrollHeight();
  return LayoutNGBlockFlow::ScrollHeight();
}

}  // namespace blink
