// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>
#include "base/bind.h"
#include "base/cxx17_backports.h"
#include "base/test/scoped_feature_list.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/origin_trials/scoped_test_origin_trial_policy.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/html/html_head_element.h"
#include "third_party/blink/renderer/core/html/html_meta_element.h"
#include "third_party/blink/renderer/core/html/html_script_element.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/speculation_rules/document_speculation_rules.h"
#include "third_party/blink/renderer/core/testing/dummy_page_holder.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"

namespace blink {
namespace {

// Generated by:
//  tools/origin_trials/generate_token.py --version 3 --expire-days 3650 \
//      https://speculationrules.test SpeculationRulesPrefetch
// Token details:
//  Version: 3
//  Origin: https://speculationrules.test:443
//  Is Subdomain: None
//  Is Third Party: None
//  Usage Restriction: None
//  Feature: SpeculationRulesPrefetch
//  Expiry: 1936881669 (2031-05-18 14:41:09 UTC)
//  Signature (Base64):
//  dLwu1RhLf1iAH+NzRrTitAhWF9oFZFtDt7CjwaQENvBK7m/RECTJuFe2wj+5WTB7HIUkgbgtzhp50pelkGG4BA==
constexpr char kSpeculationRulesPrefetchToken[] =
    "A3S8LtUYS39YgB/jc0a04rQIVhfaBWRbQ7ewo8GkBDbwSu5v0RAkybhXtsI/uVkwex"
    "yFJIG4Lc4aedKXpZBhuAQAAABseyJvcmlnaW4iOiAiaHR0cHM6Ly9zcGVjdWxhdGlv"
    "bnJ1bGVzLnRlc3Q6NDQzIiwgImZlYXR1cmUiOiAiU3BlY3VsYXRpb25SdWxlc1ByZW"
    "ZldGNoIiwgImV4cGlyeSI6IDE5MzY4ODE2Njl9";

constexpr char kSimplePrefetchProxyRuleSet[] =
    R"({
        "prefetch": [{
          "source": "list",
          "urls": ["//example.com/index2.html"],
          "requires": ["anonymous-client-ip-when-cross-origin"]
        }]
      })";

// Similar to SpeculationRuleSettest.PropagatesToDocument.
::testing::AssertionResult DocumentAcceptsRuleSet(const char* trial_token,
                                                  const char* json) {
  DummyPageHolder page_holder;
  Document& document = page_holder.GetDocument();

  // Clear the security origin and set a secure one, recomputing the security
  // state.
  SecurityContext& security_context =
      page_holder.GetFrame().DomWindow()->GetSecurityContext();
  security_context.SetSecurityOriginForTesting(nullptr);
  security_context.SetSecurityOrigin(
      SecurityOrigin::CreateFromString("https://speculationrules.test"));
  EXPECT_EQ(security_context.GetSecureContextMode(),
            SecureContextMode::kSecureContext);

  // Enable scripts so that <script> is not ignored.
  page_holder.GetFrame().GetSettings()->SetScriptEnabled(true);

  HTMLMetaElement* meta =
      MakeGarbageCollected<HTMLMetaElement>(document, CreateElementFlags());
  meta->setAttribute(html_names::kHttpEquivAttr, "Origin-Trial");
  meta->setAttribute(html_names::kContentAttr, trial_token);
  document.head()->appendChild(meta);

  HTMLScriptElement* script =
      MakeGarbageCollected<HTMLScriptElement>(document, CreateElementFlags());
  script->setAttribute(html_names::kTypeAttr, "speculationrules");
  script->setText(json);
  document.head()->appendChild(script);

  auto* supplement = DocumentSpeculationRules::FromIfExists(document);
  return (supplement && !supplement->rule_sets().IsEmpty())
             ? ::testing::AssertionSuccess() << "a rule set was found"
             : ::testing::AssertionFailure() << "no rule set was found";
}

// Without the corresponding base::Feature, this trial token should not be
// accepted.
TEST(SpeculationRulesOriginTrialTest, RequiresBaseFeature) {
  base::test::ScopedFeatureList scoped_features;
  scoped_features.InitAndDisableFeature(
      features::kSpeculationRulesPrefetchProxy);
  ScopedTestOriginTrialPolicy using_test_keys;

  EXPECT_FALSE(DocumentAcceptsRuleSet(kSpeculationRulesPrefetchToken,
                                      kSimplePrefetchProxyRuleSet));
}

// Without a valid origin trial token, this feature should not be exposed.
TEST(SpeculationRulesOriginTrialTest, RequiresValidToken) {
  base::test::ScopedFeatureList scoped_features;
  scoped_features.InitAndEnableFeature(
      features::kSpeculationRulesPrefetchProxy);
  ScopedTestOriginTrialPolicy using_test_keys;

  EXPECT_FALSE(
      DocumentAcceptsRuleSet("invalid token", kSimplePrefetchProxyRuleSet));
}

// With the feature and a matching token, speculation rules should be turned on.
TEST(SpeculationRulesOriginTrialTest, BaseFeatureAndValidTokenSuffice) {
  base::test::ScopedFeatureList scoped_features;
  scoped_features.InitAndEnableFeature(
      features::kSpeculationRulesPrefetchProxy);
  ScopedTestOriginTrialPolicy using_test_keys;

  EXPECT_TRUE(DocumentAcceptsRuleSet(kSpeculationRulesPrefetchToken,
                                     kSimplePrefetchProxyRuleSet));
}

}  // namespace
}  // namespace blink
