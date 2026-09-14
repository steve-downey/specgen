# Plan: eliminating enumerations by algebra, and the no-raw-switch gate

**Status:** PROPOSED (2026-09-13). Occasioned by the `Declared` omission in
`validate.cpp`'s `invisibility_reason`: the switch had missed an enumerator, `-Wswitch` said
so, nothing failed, and the trailing `return "visible"` answered in the missing case's place.
PR #111 added the case; PR #112 replaced the fallback with `std::unreachable()`. That the
omission and its silencer were closed by two unrelated PRs, neither of which set out to look
for it, is itself the argument for this plan: those two closed one switch. This plan closes the
*shape*.

## Goal

Extend [visitation-rules](../decisions/visitation-rules.md) from `std::variant` to the other
sum type in this tree — the enumeration — so that a new enumerator is a **compile error at
every site that eliminates it**, naming the omission, exactly as a new variant alternative
already is. Then make the doctrine a gate: **`specgen-no-raw-switch`**, a second check in the
existing `tools/tidy/` module, sibling to `specgen-no-raw-loops`.

The rule this plan proposes:

> A `switch` over an **enumeration** is a defect. A `switch` over a scalar is not.

That line is the whole doctrine, and it is drawn where it is because it is exactly the line
between "this is a sum type with a known set of summands" and "this is an integer". It is also
precisely matchable in the AST, which is what makes the gate cheap (§5).

## 1. Why the compiler warning is not the gate

Three independent reasons, each sufficient:

- **No `-Werror`.** Grepping every toolchain file (`cmake/gcc-toolchain.cmake`,
  `cmake/gcc-flags.cmake`, `cmake/clang-flags.cmake`, the four `llvm-*-toolchain.cmake`) finds
  `-Wall -Wextra` and no `-Werror` anywhere. `-Wswitch` is advisory. That is how the `Declared`
  omission reached `main` — and the comment above `names_a_visible_entity` asserting that a new
  disposition "should stop compiling here" was, until this plan's stage 0, simply false.
- **A `default:` disables it.** `-Wswitch` does not fire on a switch that has a `default:`
  label. Any future author who adds one silences the check permanently and silently. The tree
  is clean of this today (§2) but nothing holds it that way.
- **A fallback answers in the missing case's place.** `-Wswitch` catches the *omission*;
  `-Wreturn-type` then demands the function return something, and the natural way to satisfy it
  is a plausible value after the switch. That value is the actual harm — see §3.

## 2. The measurement

Taken 2026-09-13 against `.build/build-gcc-16`'s compile database (GCC 16, 186 production TUs,
`vendor/` `_deps/` `tests/` excluded), with the `Declared` fix applied:

| flag | findings |
|---|---|
| `-Wswitch` | **0** |
| `-Wswitch-enum` | **0** |
| `-Wswitch-default` | 56 (wants the opposite of this doctrine; not adopted) |

`-Wswitch-enum` is the stronger form: it fires on an unhandled enumerator *even when a
`default:` is present*, so it is the one that cannot be evaded. It is clean today. **Stage 0 is
therefore free** — a flag change with no code churn behind it — and it should not wait on the
facility.

## 3. What the fourteen switches actually are

Fourteen `switch` statements in `src/`, `include/`, `tools/`, `examples/`. They are three
different things, and they want three different remedies:

**Shape A — enumeration to a constant (8 sites).** A total function from enumerator to string
or tag. No control flow, no state; the switch is a lookup table written as code.

| site | over | tail |
|---|---|---|
| `ir.cpp:59` `span_kind_name` | `SpanKind` | `return "?"` ← fallback |
| `ir.cpp:93` `index_kind_name` | `IndexKind` | fallback |
| `diagnostic.hpp:24` | `Severity` | `return "note"` ← **fallback** |
| `backend/common.hpp:123` | `SpanKind` | `std::unreachable()` |
| `backend/mpark.cpp:66` | `SpanKind` | `std::unreachable()` |
| `backend/latex.cpp:146` | `IndexKind` | `std::unreachable()` |
| `validate.cpp:343` `invisibility_reason` | `Disposition` | `std::unreachable()` (this plan's occasion) |
| `validate.cpp:325` `names_a_visible_entity` | `Disposition` | `std::unreachable()` |

`diagnostic.hpp:24` deserves its name in bold. Its fallback is `return "note"`: an unhandled
`Severity` would be *printed as a Note*. The failure mode is not a cosmetic string — it is an
Error reported to the user as a Note. It is the same defect as the one this plan was occasioned
by, sitting in the diagnostic path, and it is live today only in the sense that `Severity` has
not grown a fourth enumerator yet.

Note also that ir.cpp already writes this shape **both ways**: `kDispositionNames` and
`kMemberKindNames` are `std::array` tables, while `span_kind_name` and `index_kind_name` are
switches doing the identical job twenty lines away. Neither form is checked — the arrays carry
a hand-written extent (`std::array<..., 9>`) with no `static_assert` tying it to the
enumerator count, so a tenth `Disposition` silently fails to reach the table exactly as it
silently failed to reach the switch. **The table idiom has the same hole, more quietly.**

**Shape B — enumeration to behaviour (4 sites).** `docblock.cpp:459`, `docblock.cpp:483`,
`docblock.cpp:536` (`MarkerArity`), `frontend.cpp:1380` (`SeeBelowTarget`). Cases run statements
rather than yielding a constant. These want visitation.

**Shape C — not a sum type (2 sites).** `json_writer.hpp:56` and `json_descriptor.hpp:126`
switch over a `char`. These are the two `default:` labels in the tree, correctly so: the
summands of `char` are not an interface anyone maintains. **These stay switches and the gate
must not touch them** — which the enum-typed-condition matcher achieves by construction, with
no marker needed.

## 4. The facility

### 4.1 What is wanted

An eliminator over an enumeration that is a **hard compile error** when a case is missing, that
names the missing enumerator, and that has no syntactic hole an author can fall through.
`foundation::overloaded` already does exactly this for `std::variant`, via a `consteval`
catch-all whose `static_assert(false)` fires on any alternative the explicit cases miss. The
question is only how to reach an enumeration with it.

### 4.2 Option B — reify the enumerators, reuse `overloaded` verbatim

Give each enumeration a companion variant of unit types and a reifier:

```cpp
template <Disposition D> using disp_c = std::integral_constant<Disposition, D>;
using DispositionAlt = std::variant<disp_c<Disposition::Described>, /* ... */>;
DispositionAlt reify(Disposition);
```

Every site becomes `std::visit(overloaded{...}, reify(d))` and inherits the proven tripwire
unchanged. One switch survives per enumeration, inside `reify`, next to the enumeration it
reifies; that is the gate's entire whitelist.

*For:* no new metaprogramming — the tripwire is already written, already documented, already
trusted. *Against:* the call site reads `[](disp_c<Disposition::Merged>)` where it read
`case ir::Disposition::Merged:`, and this tree's wording code is read as prose. The variant
also has to be spelled out per enumeration, which is the very duplication the gate is meant
to make unnecessary.

### 4.3 Option C — an enumerator list, and `match` over it (recommended)

Declare each enumeration's summands **once, as data**, and check coverage against that:

```cpp
template <> inline constexpr auto foundation::enumerators<ir::Disposition> = std::array{
    ir::Disposition::Described, /* ... */ ir::Disposition::Undocumented,
};

std::string_view invisibility_reason(ir::Disposition d) {
    return foundation::match(d,
        when<Disposition::Merged>([] { return "a `\\merge`d twin"; }),
        /* ... */);
}
```

`match` `static_assert`s that the handled set equals `enumerators<E>`, naming what is missing.

This is the recommendation, for a reason specific to this tree rather than general taste: **the
enumerator list already half-exists**, as `kDispositionNames` and `kMemberKindNames`. Option C
gives those tables a home with a completeness check, lets `span_kind_name` and
`index_kind_name` become lookups over the same table rather than switches, and collapses ir.cpp's
two competing idioms into one. It fixes §3's quiet hole and the loud one with a single
facility. Option B fixes only the loud one and leaves the arrays as they are.

For Shape A specifically, `match` is not even needed at the call site: a **total table** keyed
by the enumeration — `std::array` sized from `enumerators<E>`, indexed by the enumerator, with
the completeness check at construction — removes the fallback by removing the code path that
could reach it. There is nothing to fall back *from* in a table lookup. That is the strongest
version of the fix and it covers 8 of the 12 sites.

*Against:* it is a facility to write and to live with, and it puts a `foundation/` header
between an author and a `switch`. That is the trade this plan is asking to be signed off.

### 4.4 What remains a switch

The reifier or table constructor per enumeration (Option B: one per enum; Option C: none, if
the table is data), plus Shape C's scalar switches. Everything else in the tree stops
switching over an enumeration.

## 5. The check

`specgen-no-raw-switch`, registered alongside `NoRawLoopsCheck` in
`tools/tidy/specgen_tidy_module.cpp`. This is materially *easier* than no-raw-loops was,
because the doctrine's boundary is a type:

```cpp
finder->addMatcher(
    switchStmt(unless(isInTemplateInstantiation()),
               hasCondition(expr(hasType(hasUnqualifiedDesugaredType(enumType())))))
        .bind("sw"),
    this);
```

`hasUnqualifiedDesugaredType(enumType())` sees through the typedef and the qualifier and *does
not* match the `char` switches, so Shape C needs no marker and no exemption list — it is
outside the rule, not excused from it.

Escape hatch: the same mechanism as no-raw-loops, with a different phrase — the exact marker
`// enumeration eliminator` plus a one-line reason, on the `switch` line or in the contiguous
comment block directly above it. The buffer-reading logic (`is_comment_line`, the line slice,
the upward walk) is **identical** to `NoRawLoopsCheck`'s and should be factored into a shared
`marker_above_or_on_line(sm, site, marker)` helper as the first commit of stage 3, not
copy-pasted — the module would otherwise contain two copies of a walk whose own loop is marked
`// substrate generic algorithm`.

Wiring rides the existing rails: `tools/tidy/run-no-raw-loops.cmake` already runs
`run-clang-tidy` over the scrubbed database with specgen findings promoted to errors. Either
generalise it to take a check name (preferred — one script, two ctest cases) or add a sibling.
The ctest case is `style.no-raw-switch`; `ctest -R style` then selects both. Probes go under
`tests/tidy/probes/` next to the loop probes, as `ctest -R tidy.` cases.

## 6. Stages

| # | stage | touches | size |
|---|---|---|---|
| 0 | `-Werror=switch -Werror=switch-enum` in every toolchain/flags file; delete the false "should stop compiling here" claim above `names_a_visible_entity`, or make it true and keep it | 7 cmake files, 1 comment | **measured clean (§2)** — no code churn |
| 1 | `foundation/enumeration.hpp`: `enumerators<E>`, the completeness `static_assert`, the total table, `match`/`when`. Unit tests including a negative probe that a missing enumerator fails to compile | 1 header, 1 test | the design commit; wants sign-off before it is written |
| 2 | Migrate Shape A (8 sites). `diagnostic.hpp:24` **first** — it is the one whose fallback corrupts a diagnostic. Fold `kDispositionNames`/`kMemberKindNames` onto the same table | ir.cpp, diagnostic.hpp, common.hpp, mpark.cpp, latex.cpp, validate.cpp | mechanical once stage 1 lands; no golden movement expected (same strings) |
| 3 | Factor the marker reader out of `NoRawLoopsCheck`; add `NoRawSwitchCheck`; probes under `tests/tidy/` | tools/tidy/, tests/tidy/ | the sibling-check commit |
| 4 | Migrate Shape B (4 sites) to `match` | docblock.cpp, frontend.cpp | judgement per site; `docblock.cpp:536` has control flow in its cases |
| 5 | Wire `style.no-raw-switch`; `docs/CODING_RULES.md` gets the rule next to "No raw loops"; extend `decisions/visitation-rules.md` to name enumerations (a new decision record only if §4's choice diverges from what that record already says); `AGENTS.md` and `docs/building.md` get the gate and the new test count | cmake, 4 docs | closes it |

Stages 0 and 3 are independently landable and independently valuable: stage 0 is the hardening
that would have caught the occasioning bug, and stage 3 is the gate that catches the *next*
author who writes the shape. Neither depends on §4's outcome. If the facility is not wanted,
stages 0, 3, and 5 still leave the tree strictly better defended than it is today.

## 7. Open questions

1. **Option B or Option C** (§4.2 / §4.3). C is recommended and is the larger commitment. B is
   a day's work and reuses a tripwire that is already trusted.
2. **Does the marked-switch escape hatch exist at all?** Under Option C with total tables, there
   may be no legitimate enum switch left in the tree, in which case the check needs no marker
   and the rule is absolute. Absolute rules are easier to hold. Recommendation: implement the
   marker (stage 3 needs the helper factored regardless), then see whether any site claims it.
3. **`-Wswitch-default`'s 56 findings** are noise from a flag that wants a `default:` on every
   switch — the exact opposite of this doctrine, since a `default:` is what makes an enumeration
   switch unfalsifiable. Recommendation: do not adopt it, and say so here so the next person who
   runs the flag does not re-litigate it.
4. **Scope.** no-raw-loops covers `src/`, `tools/`, `examples/`, and every `FILE_SET` header as
   its own TU, and excludes `vendor/`, `tests/`, `_deps/`. Recommendation: identical scope, for
   the same reasons, with no separate argument.
