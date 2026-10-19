// tests/corpus/spec_source_markup_diagnostics.cc                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Source-file markup that produces no wording, each with its finding
// (issue #149):
//
// - `close` misspells its element, a grammar finding positioned here.
// - `audit` is a namespace-scope function, and only a class member's
//   definition supplies markup from a source file.
// - `helper` is declared nowhere but here.
// - `entry` is a class, not a member definition.

#include "spec_source_markup_diagnostics.hpp"

namespace demo {

//! \effects Posts `amount` to the ledger.
void ledger::post(int amount) { (void)amount; }

//! \effect Closes the ledger.
void ledger::close() {}

//! \effects Checks `book`.
void audit(const ledger& book) { (void)book; }

//! \effects Does nothing.
static void helper() {}

//! \effects Records one posting.
struct entry {};

} // namespace demo
