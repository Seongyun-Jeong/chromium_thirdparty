// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/properties/css_parsing_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_context.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_token_stream.h"
#include "third_party/blink/renderer/core/css/parser/css_tokenizer.h"
#include "third_party/blink/renderer/core/html/html_html_element.h"
#include "third_party/blink/renderer/core/testing/dummy_page_holder.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"

namespace blink {
namespace {

using css_parsing_utils::AtDelimiter;
using css_parsing_utils::AtIdent;
using css_parsing_utils::ConsumeAngle;
using css_parsing_utils::ConsumeIdSelector;
using css_parsing_utils::ConsumeIfDelimiter;
using css_parsing_utils::ConsumeIfIdent;

CSSParserContext* MakeContext() {
  return MakeGarbageCollected<CSSParserContext>(
      kHTMLStandardMode, SecureContextMode::kInsecureContext);
}

TEST(CSSParsingUtilsTest, BasicShapeUseCount) {
  auto dummy_page_holder =
      std::make_unique<DummyPageHolder>(gfx::Size(800, 600));
  Page::InsertOrdinaryPageForTesting(&dummy_page_holder->GetPage());
  Document& document = dummy_page_holder->GetDocument();
  WebFeature feature = WebFeature::kCSSBasicShape;
  EXPECT_FALSE(document.IsUseCounted(feature));
  document.documentElement()->setInnerHTML(
      "<style>span { shape-outside: circle(); }</style>");
  EXPECT_TRUE(document.IsUseCounted(feature));
}

TEST(CSSParsingUtilsTest, Revert) {
  EXPECT_TRUE(css_parsing_utils::IsCSSWideKeyword(CSSValueID::kRevert));
  EXPECT_TRUE(css_parsing_utils::IsCSSWideKeyword("revert"));
}

TEST(CSSParsingUtilsTest, ConsumeIdSelector) {
  {
    String text = "#foo";
    auto tokens = CSSTokenizer(text).TokenizeToEOF();
    CSSParserTokenRange range(tokens);
    EXPECT_EQ("#foo", ConsumeIdSelector(range)->CssText());
  }
  {
    String text = "#bar  ";
    auto tokens = CSSTokenizer(text).TokenizeToEOF();
    CSSParserTokenRange range(tokens);
    EXPECT_EQ("#bar", ConsumeIdSelector(range)->CssText());
    EXPECT_TRUE(range.AtEnd())
        << "ConsumeIdSelector cleans up trailing whitespace";
  }

  {
    String text = "#123";
    auto tokens = CSSTokenizer(text).TokenizeToEOF();
    CSSParserTokenRange range(tokens);
    ASSERT_TRUE(range.Peek().GetType() == kHashToken &&
                range.Peek().GetHashTokenType() == kHashTokenUnrestricted);
    EXPECT_FALSE(ConsumeIdSelector(range))
        << "kHashTokenUnrestricted is not a valid <id-selector>";
  }
  {
    String text = "#";
    auto tokens = CSSTokenizer(text).TokenizeToEOF();
    CSSParserTokenRange range(tokens);
    EXPECT_FALSE(ConsumeIdSelector(range));
  }
  {
    String text = " #foo";
    auto tokens = CSSTokenizer(text).TokenizeToEOF();
    CSSParserTokenRange range(tokens);
    EXPECT_FALSE(ConsumeIdSelector(range))
        << "ConsumeIdSelector does not accept preceding whitespace";
    EXPECT_EQ(kWhitespaceToken, range.Peek().GetType());
  }
  {
    String text = "foo";
    auto tokens = CSSTokenizer(text).TokenizeToEOF();
    CSSParserTokenRange range(tokens);
    EXPECT_FALSE(ConsumeIdSelector(range));
  }
  {
    String text = "##";
    auto tokens = CSSTokenizer(text).TokenizeToEOF();
    CSSParserTokenRange range(tokens);
    EXPECT_FALSE(ConsumeIdSelector(range));
  }
  {
    String text = "10px";
    auto tokens = CSSTokenizer(text).TokenizeToEOF();
    CSSParserTokenRange range(tokens);
    EXPECT_FALSE(ConsumeIdSelector(range));
  }
}

double ConsumeAngleValue(String target) {
  auto tokens = CSSTokenizer(target).TokenizeToEOF();
  CSSParserTokenRange range(tokens);
  return ConsumeAngle(range, *MakeContext(), absl::nullopt)->ComputeDegrees();
}

double ConsumeAngleValue(String target, double min, double max) {
  auto tokens = CSSTokenizer(target).TokenizeToEOF();
  CSSParserTokenRange range(tokens);
  return ConsumeAngle(range, *MakeContext(), absl::nullopt, min, max)
      ->ComputeDegrees();
}

TEST(CSSParsingUtilsTest, ConsumeAngles) {
  const double kMaxDegreeValue = 2867080569122160;

  EXPECT_EQ(10.0, ConsumeAngleValue("10deg"));
  EXPECT_EQ(-kMaxDegreeValue, ConsumeAngleValue("-3.40282e+38deg"));
  EXPECT_EQ(kMaxDegreeValue, ConsumeAngleValue("3.40282e+38deg"));

  EXPECT_EQ(kMaxDegreeValue, ConsumeAngleValue("calc(infinity * 1deg)"));
  EXPECT_EQ(-kMaxDegreeValue, ConsumeAngleValue("calc(-infinity * 1deg)"));
  EXPECT_EQ(kMaxDegreeValue, ConsumeAngleValue("calc(NaN * 1deg)"));

  // Math function with min and max ranges

  EXPECT_EQ(-100, ConsumeAngleValue("calc(-3.40282e+38deg)", -100, 100));
  EXPECT_EQ(100, ConsumeAngleValue("calc(3.40282e+38deg)", -100, 100));
}

TEST(CSSParsingUtilsTest, AtIdent_Range) {
  String text = "foo,bar,10px";
  auto tokens = CSSTokenizer(text).TokenizeToEOF();
  CSSParserTokenRange range(tokens);
  EXPECT_FALSE(AtIdent(range.Consume(), "bar"));  // foo
  EXPECT_FALSE(AtIdent(range.Consume(), "bar"));  // ,
  EXPECT_TRUE(AtIdent(range.Consume(), "bar"));   // bar
  EXPECT_FALSE(AtIdent(range.Consume(), "bar"));  // ,
  EXPECT_FALSE(AtIdent(range.Consume(), "bar"));  // 10px
  EXPECT_FALSE(AtIdent(range.Consume(), "bar"));  // EOF
}

TEST(CSSParsingUtilsTest, AtIdent_Stream) {
  String text = "foo,bar,10px";
  CSSTokenizer tokenizer(text);
  CSSParserTokenStream stream(tokenizer);
  EXPECT_FALSE(AtIdent(stream.Consume(), "bar"));  // foo
  EXPECT_FALSE(AtIdent(stream.Consume(), "bar"));  // ,
  EXPECT_TRUE(AtIdent(stream.Consume(), "bar"));   // bar
  EXPECT_FALSE(AtIdent(stream.Consume(), "bar"));  // ,
  EXPECT_FALSE(AtIdent(stream.Consume(), "bar"));  // 10px
  EXPECT_FALSE(AtIdent(stream.Consume(), "bar"));  // EOF
}

TEST(CSSParsingUtilsTest, ConsumeIfIdent_Range) {
  String text = "foo,bar,10px";
  auto tokens = CSSTokenizer(text).TokenizeToEOF();
  CSSParserTokenRange range(tokens);
  EXPECT_TRUE(AtIdent(range.Peek(), "foo"));
  EXPECT_FALSE(ConsumeIfIdent(range, "bar"));
  EXPECT_TRUE(AtIdent(range.Peek(), "foo"));
  EXPECT_TRUE(ConsumeIfIdent(range, "foo"));
  EXPECT_EQ(kCommaToken, range.Peek().GetType());
}

TEST(CSSParsingUtilsTest, ConsumeIfIdent_Stream) {
  String text = "foo,bar,10px";
  CSSTokenizer tokenizer(text);
  CSSParserTokenStream stream(tokenizer);
  EXPECT_TRUE(AtIdent(stream.Peek(), "foo"));
  EXPECT_FALSE(ConsumeIfIdent(stream, "bar"));
  EXPECT_TRUE(AtIdent(stream.Peek(), "foo"));
  EXPECT_TRUE(ConsumeIfIdent(stream, "foo"));
  EXPECT_EQ(kCommaToken, stream.Peek().GetType());
}

TEST(CSSParsingUtilsTest, AtDelimiter_Range) {
  String text = "foo,<,10px";
  auto tokens = CSSTokenizer(text).TokenizeToEOF();
  CSSParserTokenRange range(tokens);
  EXPECT_FALSE(AtDelimiter(range.Consume(), '<'));  // foo
  EXPECT_FALSE(AtDelimiter(range.Consume(), '<'));  // ,
  EXPECT_TRUE(AtDelimiter(range.Consume(), '<'));   // <
  EXPECT_FALSE(AtDelimiter(range.Consume(), '<'));  // ,
  EXPECT_FALSE(AtDelimiter(range.Consume(), '<'));  // 10px
  EXPECT_FALSE(AtDelimiter(range.Consume(), '<'));  // EOF
}

TEST(CSSParsingUtilsTest, AtDelimiter_Stream) {
  String text = "foo,<,10px";
  CSSTokenizer tokenizer(text);
  CSSParserTokenStream stream(tokenizer);
  EXPECT_FALSE(AtDelimiter(stream.Consume(), '<'));  // foo
  EXPECT_FALSE(AtDelimiter(stream.Consume(), '<'));  // ,
  EXPECT_TRUE(AtDelimiter(stream.Consume(), '<'));   // <
  EXPECT_FALSE(AtDelimiter(stream.Consume(), '<'));  // ,
  EXPECT_FALSE(AtDelimiter(stream.Consume(), '<'));  // 10px
  EXPECT_FALSE(AtDelimiter(stream.Consume(), '<'));  // EOF
}

TEST(CSSParsingUtilsTest, ConsumeIfDelimiter_Range) {
  String text = "<,=,10px";
  auto tokens = CSSTokenizer(text).TokenizeToEOF();
  CSSParserTokenRange range(tokens);
  EXPECT_TRUE(AtDelimiter(range.Peek(), '<'));
  EXPECT_FALSE(ConsumeIfDelimiter(range, '='));
  EXPECT_TRUE(AtDelimiter(range.Peek(), '<'));
  EXPECT_TRUE(ConsumeIfDelimiter(range, '<'));
  EXPECT_EQ(kCommaToken, range.Peek().GetType());
}

TEST(CSSParsingUtilsTest, ConsumeIfDelimiter_Stream) {
  String text = "<,=,10px";
  CSSTokenizer tokenizer(text);
  CSSParserTokenStream stream(tokenizer);
  EXPECT_TRUE(AtDelimiter(stream.Peek(), '<'));
  EXPECT_FALSE(ConsumeIfDelimiter(stream, '='));
  EXPECT_TRUE(AtDelimiter(stream.Peek(), '<'));
  EXPECT_TRUE(ConsumeIfDelimiter(stream, '<'));
  EXPECT_EQ(kCommaToken, stream.Peek().GetType());
}

TEST(CSSParsingUtilsTest, ConsumeAnyValue) {
  struct {
    // The input string to parse as <any-value>.
    const char* input;
    // The expected result from ConsumeAnyValue.
    bool expected;
    // The serialization of the tokens remaining in the range.
    const char* remainder;
  } tests[] = {
      {"1", true, ""},
      {"1px", true, ""},
      {"1px ", true, ""},
      {"ident", true, ""},
      {"(([ident]))", true, ""},
      {" ( ( 1 ) ) ", true, ""},
      {"rgb(1, 2, 3)", true, ""},
      {"rgb(1, 2, 3", true, ""},
      {"!!!;;;", true, ""},
      {"asdf)", false, ")"},
      {")asdf", false, ")asdf"},
      {"(ab)cd) e", false, ") e"},
      {"(as]df) e", false, " e"},
      {"(a b [ c { d ) e } f ] g h) i", false, " i"},
      {"a url(() b", false, "url(() b"},
  };

  for (const auto& test : tests) {
    String input(test.input);
    SCOPED_TRACE(input);
    auto tokens = CSSTokenizer(input).TokenizeToEOF();
    CSSParserTokenRange range(tokens);
    EXPECT_EQ(test.expected, css_parsing_utils::ConsumeAnyValue(range));
    EXPECT_EQ(String(test.remainder), range.Serialize());
  }
}

}  // namespace
}  // namespace blink