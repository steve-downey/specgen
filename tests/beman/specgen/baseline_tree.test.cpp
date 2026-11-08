// tests/beman/specgen/baseline_tree.test.cpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/baseline_tree.hpp>
#include <beman/specgen/baseline_tree.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace baseline_tree = beman::specgen::baseline_tree;
namespace fs            = std::filesystem;

namespace {

// A throwaway repository with two commits: `include/g.hpp` reads "one" at
// the first and "two" at the second, and `include/new.hpp` exists only at
// the second. Removed when the test ends.
struct Repo {
    fs::path root;

    Repo() {
        std::string pattern = (fs::temp_directory_path() / "specgen-baseline-test-XXXXXX").string();
        REQUIRE(mkdtemp(pattern.data()) != nullptr);
        root = pattern;
        fs::create_directories(root / "include");
        git("init -q");
        git("config user.email t@example.com");
        git("config user.name t");
        write("include/g.hpp", "one\n");
        git("add -A");
        git("commit -q -m first");
        write("include/g.hpp", "two\n");
        write("include/new.hpp", "new\n");
        git("add -A");
        git("commit -q -m second");
    }
    ~Repo() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void write(const std::string& relative, const std::string& text) const { std::ofstream(root / relative) << text; }
    void git(const std::string& args) const {
        REQUIRE(std::system(("git -C '" + root.string() + "' " + args).c_str()) == 0);
    }
    std::string read(const fs::path& path) const {
        std::ifstream in(path);
        std::string   text;
        std::getline(in, text);
        return text;
    }
};

} // namespace

TEST_CASE("baseline_tree - HeaderIsIdempotent") { SUCCEED(); }

TEST_CASE("baseline_tree - a revision is checked out into a scratch tree and the header found there") {
    const Repo repo;
    const auto header = (repo.root / "include" / "g.hpp").string();
    auto       tree   = baseline_tree::Tree::materialize("HEAD~1", header);
    REQUIRE(tree.has_value());
    CHECK(fs::equivalent(tree->top_level(), repo.root));
    CHECK(tree->has(header));
    CHECK(repo.read(tree->remap(header)) == "one");
    CHECK(repo.read(header) == "two");
    // A header the revision does not have is a new document.
    CHECK_FALSE(tree->has((repo.root / "include" / "new.hpp").string()));
    const fs::path root = tree->root();
    CHECK(fs::exists(root));
    // The scratch tree goes with its last owner.
    {
        const auto moved = std::move(*tree);
    }
    CHECK_FALSE(fs::exists(root));
}

TEST_CASE("baseline_tree - paths outside the repository are left alone, inside it remapped") {
    const Repo repo;
    const auto header = (repo.root / "include" / "g.hpp").string();
    const auto tree   = baseline_tree::Tree::materialize("HEAD", header);
    REQUIRE(tree.has_value());
    CHECK(tree->remap("/usr/include") == "/usr/include");
    CHECK_FALSE(tree->has("/usr/include/stdio.h"));
    const std::string include = (repo.root / "include").string();
    CHECK(tree->remap(include) == (tree->root() / "tree" / "include").string());
    CHECK(tree->remap_args({"-std=c++2c",
                            "-I",
                            include,
                            "-I" + include,
                            "-isystem",
                            "/opt/x",
                            "-Dfoo=1",
                            "-include",
                            include + "/g.hpp"}) ==
          std::vector<std::string>{"-std=c++2c",
                                   "-I",
                                   (tree->root() / "tree" / "include").string(),
                                   "-I" + (tree->root() / "tree" / "include").string(),
                                   "-isystem",
                                   "/opt/x",
                                   "-Dfoo=1",
                                   "-include",
                                   (tree->root() / "tree" / "include" / "g.hpp").string()});
}

TEST_CASE("baseline_tree - a revision that names no commit, and a path outside any repository, are reported") {
    const Repo repo;
    const auto header  = (repo.root / "include" / "g.hpp").string();
    const auto missing = baseline_tree::Tree::materialize("no-such-revision", header);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().find("does not name a commit") != std::string::npos);
    const auto outside = baseline_tree::top_level_of("/");
    CHECK_FALSE(outside.has_value());
}
