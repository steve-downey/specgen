export module beman.specgen;

import std;

#define BEMAN_SPECGEN_INCLUDED_FROM_INTERFACE_UNIT
export {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winclude-angled-in-module-purview"
#include <beman/specgen/config.hpp>

#include <beman/specgen/backend/code_spans.hpp>
#include <beman/specgen/backend/draft_spans.hpp>
#include <beman/specgen/backend/element_label.hpp>
#include <beman/specgen/backend/render_fold.hpp>
#include <beman/specgen/backend/latex.hpp>
#include <beman/specgen/backend/mpark.hpp>
#include <beman/specgen/backend/org.hpp>
#include <beman/specgen/conjuncts.hpp>
#include <beman/specgen/depfile.hpp>
#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/docblock.hpp>
#include <beman/specgen/document_build.hpp>
#include <beman/specgen/foundation/fold_left_short.hpp>
#include <beman/specgen/json/descriptor.hpp>
#include <beman/specgen/json/reader.hpp>
#include <beman/specgen/json/writer.hpp>
#include <beman/specgen/foundation/monoid.hpp>
#include <beman/specgen/foundation/overloaded.hpp>
#include <beman/specgen/foundation/parse/cursor.hpp>
#include <beman/specgen/foundation/parse/parser.hpp>
#include <beman/specgen/foundation/traverse.hpp>
#include <beman/specgen/fragments.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/ir_fold.hpp>
#include <beman/specgen/ir_json.hpp>
#include <beman/specgen/lower.hpp>
#include <beman/specgen/markers.hpp>
#include <beman/specgen/rsec.hpp>
#include <beman/specgen/validate/validate.hpp>
#pragma clang diagnostic pop
}
