// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/text/mathml_operator_dictionary.h"

#include "base/notreached.h"
#include "third_party/blink/renderer/platform/wtf/text/character_names.h"

namespace blink {

namespace {

// https://w3c.github.io/mathml-core/#operator-dictionary-compact-special-tables
const char* operators_2_ascii_chars[] = {
    "!!", "!=", "&&", "**", "*=", "++", "+=", "--", "-=", "->",
    "..", "//", "/=", ":=", "<=", "<>", "==", ">=", "||"};

// https://w3c.github.io/mathml-core/#operator-dictionary-categories-hexa-table
struct EntryRange {
  uint16_t entry;
  unsigned range_bounds_delta : 4;
};
static inline uint16_t ExtractKey(const EntryRange& range) {
  return range.entry & 0x3FFF;
}
static inline uint16_t ExtractCategory(const EntryRange& range) {
  return range.entry >> 12;
}

// The following representation is taken from the spec, and reduces storage
// requirements by mapping codepoints and category to better make use of the
// available bytes. For details see
// https://w3c.github.io/mathml-core/#operator-dictionary.
static const EntryRange compact_dictionary[] = {
    {0x8025, 0},  {0x802A, 0},  {0x402B, 0},  {0x402D, 0},  {0x802E, 0},
    {0x402F, 0},  {0x803F, 1},  {0xC05C, 0},  {0x805E, 1},  {0x807C, 0},
    {0x40B1, 0},  {0x80B7, 0},  {0x80D7, 0},  {0x40F7, 0},  {0x4322, 0},
    {0x8323, 0},  {0x832B, 0},  {0x832F, 0},  {0x8332, 0},  {0x8422, 0},
    {0x8443, 0},  {0x4444, 0},  {0xC461, 3},  {0x0590, 9},  {0x059C, 15},
    {0x05AC, 1},  {0x05AF, 6},  {0x05B9, 0},  {0x05BC, 15}, {0x05CC, 0},
    {0x05D0, 13}, {0x05E0, 15}, {0x05F0, 0},  {0x05F3, 0},  {0x05F5, 1},
    {0x05FD, 2},  {0x8606, 0},  {0x860E, 0},  {0x4612, 4},  {0x8617, 0},
    {0x4618, 0},  {0x4624, 0},  {0x4627, 3},  {0x4636, 0},  {0x4638, 0},
    {0x863F, 1},  {0x468C, 3},  {0x4693, 3},  {0x8697, 0},  {0x4698, 0},
    {0x8699, 0},  {0x469D, 2},  {0x86A0, 1},  {0x46BB, 2},  {0x46C4, 0},
    {0x86C5, 0},  {0x46C6, 0},  {0x86C7, 0},  {0x86C9, 3},  {0x46CE, 1},
    {0x46D2, 1},  {0x8705, 1},  {0x89A0, 1},  {0x89AA, 1},  {0x89AD, 4},
    {0x4B95, 2},  {0x8BCB, 0},  {0x8BCD, 0},  {0x0BF0, 1},  {0x4BF4, 0},
    {0x0BF5, 10}, {0x0D0A, 6},  {0x0D12, 1},  {0x0D21, 1},  {0x0D4E, 15},
    {0x0D5E, 3},  {0x0D6E, 1},  {0x8D81, 1},  {0x8D99, 1},  {0x8DB5, 0},
    {0x4DBC, 0},  {0x8DC2, 1},  {0x8DC9, 4},  {0x8DD8, 1},  {0x8DDB, 0},
    {0x8DDF, 1},  {0x8DE2, 0},  {0x8DE7, 6},  {0x4DF6, 0},  {0x8DF8, 3},
    {0x8E1D, 4},  {0x4E22, 12}, {0x8E2F, 8},  {0x4E38, 2},  {0x8E3B, 2},
    {0x8E3F, 0},  {0x4E40, 15}, {0x8E50, 0},  {0x4E51, 15}, {0x4E61, 2},
    {0x4EDA, 1},  {0x8EDC, 1},  {0x4EFB, 0},  {0x4EFD, 0},  {0x8EFE, 0},
    {0x4F32, 0},  {0x0F45, 1},  {0x1021, 0},  {0x5028, 0},  {0x102B, 0},
    {0x102D, 0},  {0x505B, 0},  {0x507B, 1},  {0x10AC, 0},  {0x10B1, 0},
    {0x1332, 0},  {0x5416, 0},  {0x1418, 0},  {0x141C, 0},  {0x1600, 1},
    {0x1603, 1},  {0x1607, 0},  {0xD60F, 2},  {0x1612, 1},  {0x161F, 3},
    {0x962B, 8},  {0x163C, 0},  {0x16BE, 1},  {0xD6C0, 3},  {0x5708, 0},
    {0x570A, 0},  {0x1710, 0},  {0x1719, 0},  {0x5729, 0},  {0x5B72, 0},
    {0x1B95, 1},  {0x1BC0, 0},  {0x5BE6, 0},  {0x5BE8, 0},  {0x5BEA, 0},
    {0x5BEC, 0},  {0x5BEE, 0},  {0x5D80, 0},  {0x5D83, 0},  {0x5D85, 0},
    {0x5D87, 0},  {0x5D89, 0},  {0x5D8B, 0},  {0x5D8D, 0},  {0x5D8F, 0},
    {0x5D91, 0},  {0x5D93, 0},  {0x5D95, 0},  {0x5D97, 0},  {0x1D9B, 15},
    {0x1DAB, 4},  {0x5DFC, 0},  {0xDE00, 10}, {0x9E0B, 15}, {0x9E1B, 1},
    {0x1EEC, 1},  {0xDEFC, 0},  {0xDEFF, 0},  {0x2021, 1},  {0x2026, 1},
    {0x6029, 0},  {0x605D, 0},  {0xA05E, 1},  {0x2060, 0},  {0x607C, 1},
    {0xA07E, 0},  {0x20A8, 0},  {0xA0AF, 0},  {0x20B0, 0},  {0x20B2, 2},
    {0x20B8, 1},  {0xA2C6, 1},  {0xA2C9, 0},  {0x22CA, 1},  {0xA2CD, 0},
    {0x22D8, 2},  {0xA2DC, 0},  {0x22DD, 0},  {0xA2F7, 0},  {0xA302, 0},
    {0x2311, 0},  {0x2320, 0},  {0x2325, 0},  {0x2327, 0},  {0x232A, 0},
    {0x2332, 0},  {0x6416, 0},  {0x2419, 2},  {0x241D, 2},  {0x2432, 5},
    {0xA43E, 0},  {0x2457, 0},  {0x24DB, 1},  {0x6709, 0},  {0x670B, 0},
    {0xA722, 1},  {0x672A, 0},  {0xA7B4, 1},  {0x27CD, 0},  {0xA7DC, 5},
    {0x6B73, 0},  {0x6BE7, 0},  {0x6BE9, 0},  {0x6BEB, 0},  {0x6BED, 0},
    {0x6BEF, 0},  {0x6D80, 0},  {0x6D84, 0},  {0x6D86, 0},  {0x6D88, 0},
    {0x6D8A, 0},  {0x6D8C, 0},  {0x6D8E, 0},  {0x6D90, 0},  {0x6D92, 0},
    {0x6D94, 0},  {0x6D96, 0},  {0x6D98, 0},  {0x6DFD, 0}};

}  // namespace

MathMLOperatorDictionaryCategory FindCategory(
    const String& content,
    MathMLOperatorDictionaryForm form) {
  DCHECK(!content.Is8Bit());
  // Handle special cases and calculate a BMP code point used for the key.
  uint16_t key{0};
  if (content.length() == 1) {
    UChar32 character = content[0];
    if (character < kCombiningMinusSignBelow ||
        character > kGreekCapitalReversedDottedLunateSigmaSymbol) {
      // Accept BMP characters that are not in the ranges where 2-ASCII-chars
      // operators are mapped below.
      key = character;
    }
  } else if (content.length() == 2) {
    UChar32 character = content.CharacterStartingAt(0);
    if (character == kArabicMathematicalOperatorMeemWithHahWithTatweel ||
        character == kArabicMathematicalOperatorHahWithDal) {
      // Special handling of non-BMP Arabic operators.
      if (form == MathMLOperatorDictionaryForm::kPostfix)
        return MathMLOperatorDictionaryCategory::kI;
      return MathMLOperatorDictionaryCategory::kNone;
    } else if (content[1] == kCombiningLongSolidusOverlay ||
               content[1] == kCombiningLongVerticalLineOverlay) {
      // If the second character is COMBINING LONG SOLIDUS OVERLAY or
      // COMBINING LONG VERTICAL LINE OVERLAY, then use the property of the
      // first character.
      key = content[0];
    } else {
      // Perform a binary search for 2-ASCII-chars operators.
      const char** last =
          operators_2_ascii_chars + base::size(operators_2_ascii_chars);
      const char** entry = std::lower_bound(
          operators_2_ascii_chars, last, content,
          [](const char* lhs, const String& rhs) -> bool {
            return lhs[0] < rhs[0] || (lhs[0] == rhs[0] && lhs[1] < rhs[1]);
          });
      if (entry != last && content == *entry)
        key = kCombiningMinusSignBelow + (entry - operators_2_ascii_chars);
    }
  }

  if (!key)
    return MathMLOperatorDictionaryCategory::kNone;

  // Handle special categories that are not encoded in the compact dictionary.
  // https://w3c.github.io/mathml-core/#operator-dictionary-categories-values
  if (form == MathMLOperatorDictionaryForm::kPrefix &&
      ((kDoubleStruckItalicCapitalDCharacter <= key &&
        key <= kDoubleStruckItalicSmallDCharacter) ||
       key == kPartialDifferential ||
       (kSquareRootCharacter <= key && key <= kFourthRootCharacter))) {
    return MathMLOperatorDictionaryCategory::kK;
  }
  if (form == MathMLOperatorDictionaryForm::kInfix &&
      (key == kComma || key == kColon || key == kSemiColon)) {
    return MathMLOperatorDictionaryCategory::kM;
  }
  // Calculate the key for the compact dictionary.
  if (kEnQuadCharacter <= key && key <= kHellschreiberPauseSymbol) {
    // Map above range (U+2000–U+2BFF) to (U+0400-0x0FFF) to fit into
    // 12 bits by decrementing with (U+2000 - U+0400) == 0x1C00.
    key -= 0x1C00;
  } else if (key > kGreekCapitalReversedDottedLunateSigmaSymbol) {
    return MathMLOperatorDictionaryCategory::kNone;
  }
  // Bitmasks used to set form 2-bits (infix=00, prefix=01, postfix=10).
  if (form == MathMLOperatorDictionaryForm::kPrefix)
    key |= 0x1000;
  else if (form == MathMLOperatorDictionaryForm::kPostfix)
    key |= 0x2000;
  DCHECK_LE(key, 0x2FFF);

  // Perform a binary search on the compact dictionary.
  const EntryRange* entry_range = std::upper_bound(
      compact_dictionary, compact_dictionary + base::size(compact_dictionary),
      key, [](uint16_t lhs, EntryRange rhs) -> bool {
        return lhs < ExtractKey(rhs);
      });

  if (entry_range == compact_dictionary)
    return MathMLOperatorDictionaryCategory::kNone;
  entry_range--;

  DCHECK_LE(ExtractKey(*entry_range), key);
  if (key > (ExtractKey(*entry_range) + entry_range->range_bounds_delta))
    return MathMLOperatorDictionaryCategory::kNone;

  // An entry is found: set the properties according the category.
  // https://w3c.github.io/mathml-core/#operator-dictionary-categories-values
  switch (ExtractCategory(*entry_range)) {
    case 0x0:
      return MathMLOperatorDictionaryCategory::kA;
    case 0x4:
      return MathMLOperatorDictionaryCategory::kB;
    case 0x8:
      return MathMLOperatorDictionaryCategory::kC;
    case 0x1:
    case 0x2:
    case 0xC:
      return MathMLOperatorDictionaryCategory::kDorEorL;
    case 0x5:
    case 0x6:
      return MathMLOperatorDictionaryCategory::kForG;
    case 0x9:
      return MathMLOperatorDictionaryCategory::kH;
    case 0xA:
      return MathMLOperatorDictionaryCategory::kI;
    case 0xD:
      return MathMLOperatorDictionaryCategory::kJ;
  }

  NOTREACHED();
  return MathMLOperatorDictionaryCategory::kNone;
}

}  // namespace blink
