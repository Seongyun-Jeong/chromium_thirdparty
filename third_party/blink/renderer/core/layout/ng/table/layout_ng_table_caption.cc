// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/ng/table/layout_ng_table_caption.h"

#include "third_party/blink/renderer/core/layout/layout_table.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/layout/ng/ng_block_node.h"
#include "third_party/blink/renderer/core/layout/ng/ng_box_fragment.h"
#include "third_party/blink/renderer/core/layout/ng/ng_constraint_space.h"
#include "third_party/blink/renderer/core/layout/ng/ng_layout_result.h"
#include "third_party/blink/renderer/core/layout/ng/ng_length_utils.h"
#include "third_party/blink/renderer/core/layout/ng/ng_out_of_flow_positioned_node.h"
#include "third_party/blink/renderer/core/layout/ng/ng_physical_box_fragment.h"

namespace blink {

LayoutNGTableCaption::LayoutNGTableCaption(Element* element)
    : LayoutNGBlockFlowMixin<LayoutTableCaption>(element) {}

// Legacy method.
void LayoutNGTableCaption::CalculateAndSetMargins(
    const NGConstraintSpace& constraint_space,
    const NGPhysicalFragment& physical_fragment) {
  NOT_DESTROYED();
  const ComputedStyle& containing_block_style = ContainingBlock()->StyleRef();

  NGBoxFragment box_fragment(containing_block_style.GetWritingDirection(),
                             To<NGPhysicalBoxFragment>(physical_fragment));

  NGPhysicalBoxStrut physical_margins =
      ComputePhysicalMargins(constraint_space, StyleRef());

  NGBoxStrut logical_margins = physical_margins.ConvertToLogical(
      containing_block_style.GetWritingDirection());

  LayoutUnit caption_inline_size_in_cb_writing_mode = box_fragment.InlineSize();

  LayoutUnit available_inline_size_in_cb_writing_mode =
      ToPhysicalSize(constraint_space.AvailableSize(),
                     constraint_space.GetWritingMode())
          .ConvertToLogical(containing_block_style.GetWritingMode())
          .inline_size;

  ResolveInlineMargins(StyleRef(), containing_block_style,
                       available_inline_size_in_cb_writing_mode,
                       caption_inline_size_in_cb_writing_mode,
                       &logical_margins);
  SetMargin(logical_margins.ConvertToPhysical(
      containing_block_style.GetWritingDirection()));
}

void LayoutNGTableCaption::InsertedIntoTree() {
  NOT_DESTROYED();
  LayoutBlockFlow::InsertedIntoTree();

  LayoutNGTableInterface* table_interface = TableInterface();
  if (!table_interface->ToLayoutObject()->IsLayoutNGObject())
    To<LayoutTable>(table_interface->ToMutableLayoutObject())->AddCaption(this);
}

void LayoutNGTableCaption::WillBeRemovedFromTree() {
  NOT_DESTROYED();
  LayoutBlockFlow::WillBeRemovedFromTree();

  LayoutNGTableInterface* table_interface = TableInterface();
  if (!table_interface->ToLayoutObject()->IsLayoutNGObject()) {
    To<LayoutTable>(table_interface->ToMutableLayoutObject())
        ->RemoveCaption(this);
  }
}

void LayoutNGTableCaption::UpdateBlockLayout(bool relayout_children) {
  NOT_DESTROYED();

  DCHECK(!IsOutOfFlowPositioned()) << "Out of flow captions are blockified.";

  scoped_refptr<const NGLayoutResult> result = UpdateInFlowBlockLayout();
  CalculateAndSetMargins(result->GetConstraintSpaceForCaching(),
                         result->PhysicalFragment());
}

LayoutNGTableInterface* LayoutNGTableCaption::TableInterface() const {
  NOT_DESTROYED();
  return ToInterface<LayoutNGTableInterface>(Parent());
}

}  // namespace blink
