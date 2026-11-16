<!-- markdownlint-disable MD013 -->

# Explicit constraint coverage

Status: accepted, 2026-11-07. Issue #234.

An implementation predicate's spelling cannot prove that arbitrary authored
prose describes it. A normative Constraints or Mandates element may therefore
carry `\covers predicate` or `\covers !predicate`: an explicit authored
claim that its wording covers that derived conjunct with that polarity.

Claims are validator metadata. Each must match exactly one derived conjunct
of the same element kind after the validator's code normalization and local
binding substitution. A typo, duplicate claim, ambiguous target, or wrong
polarity is an Error. Recognizable prose that contradicts the derived evidence
continues to warn even when a coverage claim exists. One matched claim cannot
cover another conjunct.

The annotation is never rendered or treated as a wording leakage site. Grammar,
lowering, and saved IR preserve it; the item node tag advances to `item-v4`
so older readers reject the item instead of silently losing a claim. Current
readers continue to accept earlier item tags with no claims. One descriptor
table drives both JSON directions.

This acknowledges correspondence asserted by the author. It does not establish
an equivalence between implementation traits and arbitrary prose, and it does
not change the authored output.

## Log

2026-11-16, issue #283. A class definition's docblock takes `\covers mandates
P` without `\asdraft`: the class's leading prose stands for the paragraph
derived from its `static_assert`s, which is then not rendered. The claims and
the derived conjuncts are carried on a Mandates element with no body, so the
"normative Constraints or Mandates element" a claim belongs to may be one
whose wording is the description's leading prose. Nothing else above changes:
a claim still matches exactly one derived conjunct, with its polarity, and is
never rendered.
