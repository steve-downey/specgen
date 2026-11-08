// include/beman/specgen/baseline_tree.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — a git revision of the repository a header lives in, checked out
// into a scratch tree for the run's lifetime, for the wording diff's
// `--baseline-rev` (docs/plans/wording-diff.md). A document is the main file
// and the includes inside its `.syn` region (decision document-extent) and
// `\expos` reaches included headers, so a baseline's includes must be the
// baseline's: the revision is materialized as a tree, not a file.
//
// Clang-free (Tier A): git is driven as a subprocess, and the remapping of a
// path or an argument list into the tree is string work. The parse of the
// remapped header is the front end's, as for any header.

#ifndef BEMAN_SPECGEN_BASELINE_TREE_HPP
#define BEMAN_SPECGEN_BASELINE_TREE_HPP

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::baseline_tree {

// The revision checked out. Movable, not copyable: the scratch tree is
// removed when the last owner goes.
class Tree {
  public:
    // Check `revision` of the repository holding `path_in_repo` (a header of
    // the current checkout, whose directory git is asked about) out into a
    // fresh scratch directory. The plumbing pair `read-tree` into a temporary
    // index and `checkout-index` touches neither the real index nor the
    // worktree list, needs no `tar`, and reads only. Fails, with git's own
    // words, when the path is not inside a repository, the revision does not
    // name a commit, or the checkout itself fails.
    static std::expected<Tree, std::string> materialize(std::string_view revision, std::string_view path_in_repo);

    Tree(Tree&&) noexcept;
    Tree& operator=(Tree&&) noexcept;
    Tree(const Tree&)            = delete;
    Tree& operator=(const Tree&) = delete;
    ~Tree();

    // The repository's top level in the current checkout, and the scratch
    // tree the revision was checked out into.
    const std::filesystem::path& top_level() const { return top_level_; }
    const std::filesystem::path& root() const { return root_; }

    // A path under the repository's top level, remapped into the tree; a path
    // outside it, unchanged -- an `-I` into an installed library is the same
    // directory at either revision.
    std::string remap(std::string_view path) const;

    // Compile arguments with every path clang reads as a directory or a file
    // remapped: `-I`, `-isystem`, `-iquote`, `-idirafter`, `-include` and
    // `-imacros`, attached or as the next argument. Everything else passes
    // through untouched.
    std::vector<std::string> remap_args(const std::vector<std::string>& args) const;

    // Whether `path_in_repo` (a path of the current checkout) exists at the
    // revision: the header is there to parse, or the document is new.
    bool has(std::string_view path_in_repo) const;

  private:
    Tree(std::filesystem::path top_level, std::filesystem::path root);

    std::filesystem::path top_level_;
    std::filesystem::path root_;
};

// The repository's top level for a path, by asking git; empty when the path
// is not inside one. Exposed for the driver's "every header of a paper is in
// one repository" check.
std::expected<std::filesystem::path, std::string> top_level_of(std::string_view path);

} // namespace beman::specgen::baseline_tree

#endif // BEMAN_SPECGEN_BASELINE_TREE_HPP
