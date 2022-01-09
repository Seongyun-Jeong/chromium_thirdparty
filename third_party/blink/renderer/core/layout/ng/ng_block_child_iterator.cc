// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/ng/ng_block_child_iterator.h"

#include "third_party/blink/renderer/core/layout/ng/inline/ng_inline_break_token.h"
#include "third_party/blink/renderer/core/layout/ng/ng_block_break_token.h"
#include "third_party/blink/renderer/core/layout/ng/ng_block_node.h"
#include "third_party/blink/renderer/core/layout/ng/ng_layout_input_node.h"

namespace blink {

NGBlockChildIterator::NGBlockChildIterator(NGLayoutInputNode first_child,
                                           const NGBlockBreakToken* break_token)
    : next_unstarted_child_(first_child),
      break_token_(break_token),
      child_token_idx_(0) {
  if (break_token_) {
    const auto& child_break_tokens = break_token_->ChildBreakTokens();
    // If there are child break tokens, we don't yet know which one is the the
    // next unstarted child (need to get past the child break tokens first). If
    // we've already seen all children, there will be no unstarted children.
    if (!child_break_tokens.empty() || break_token_->HasSeenAllChildren())
      next_unstarted_child_ = nullptr;
    // We're already done with this parent break token if there are no child
    // break tokens, so just forget it right away.
    if (child_break_tokens.empty())
      break_token_ = nullptr;
  }
}

NGBlockChildIterator::Entry NGBlockChildIterator::NextChild(
    const NGInlineBreakToken* previous_inline_break_token) {
  if (previous_inline_break_token) {
    return Entry(previous_inline_break_token->InputNode(),
                 previous_inline_break_token);
  }

  const NGBreakToken* current_child_break_token = nullptr;
  NGLayoutInputNode current_child = next_unstarted_child_;
  if (break_token_) {
    // If we're resuming layout after a fragmentainer break, we'll first resume
    // the children that fragmented earlier (represented by one break token
    // each).
    DCHECK(!next_unstarted_child_);
    const auto& child_break_tokens = break_token_->ChildBreakTokens();
    if (child_token_idx_ < child_break_tokens.size()) {
      current_child_break_token = child_break_tokens[child_token_idx_++];
      current_child = current_child_break_token->InputNode();

      if (child_token_idx_ == child_break_tokens.size()) {
        // We reached the last child break token. Prepare for the next unstarted
        // sibling, and forget the parent break token.
        if (!break_token_->HasSeenAllChildren())
          next_unstarted_child_ = current_child.NextSibling();
        break_token_ = nullptr;
      }
    }
  } else if (next_unstarted_child_) {
    next_unstarted_child_ = next_unstarted_child_.NextSibling();
  }

  return Entry(current_child, current_child_break_token);
}

}  // namespace blink
