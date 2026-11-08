// src/beman/specgen/baseline_tree.cpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/baseline_tree.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// POSIX: the subprocess seam and the scratch directory. `<cstdlib>` would
// spell `mkdtemp` too, but it is POSIX's, not the standard's, so its own
// header names it; the wait-status macros come through <sys/wait.h>.
#include <stdlib.h>    // NOLINT(misc-include-cleaner): mkdtemp is POSIX, from here
#include <sys/types.h> // pid_t, ssize_t
#include <sys/wait.h>  // NOLINT(misc-include-cleaner): WIFEXITED, WEXITSTATUS
#include <unistd.h>

namespace beman::specgen::baseline_tree {

namespace {

namespace fs = std::filesystem;

// The text without its trailing line ends: git's one-line answers.
std::string trimmed_line(std::string text) {
    text.erase(text.find_last_not_of("\r\n") + 1);
    return text;
}

// Run a command, its output (both streams) captured, in `cwd`. The one
// subprocess seam in the tool: git, and `env` to hand it a variable. POSIX
// fork and exec, with no shell in between, so an argument is never
// re-interpreted.
std::expected<std::string, std::string> run(const std::vector<std::string>& argv, const fs::path& cwd) {
    std::array<int, 2> pipe_fds{};
    if (::pipe(pipe_fds.data()) != 0)
        return std::unexpected(std::format("cannot create a pipe: {}", std::strerror(errno)));

    const pid_t pid = ::fork();
    if (pid < 0)
        return std::unexpected(std::format("cannot fork: {}", std::strerror(errno)));
    if (pid == 0) {
        ::close(pipe_fds[0]);
        ::dup2(pipe_fds[1], STDOUT_FILENO);
        ::dup2(pipe_fds[1], STDERR_FILENO);
        ::close(pipe_fds[1]);
        if (::chdir(cwd.c_str()) != 0)
            ::_exit(127);
        std::vector<char*> args =
            argv | std::views::transform([](const std::string& s) { return const_cast<char*>(s.c_str()); }) |
            std::ranges::to<std::vector>();
        args.push_back(nullptr);
        ::execvp(args.front(), args.data());
        ::_exit(127);
    }
    ::close(pipe_fds[1]);

    std::string            output;
    std::array<char, 4096> buffer{};
    // substrate generic algorithm: reading a pipe to its end is the read
    // loop itself, each read's length deciding the next.
    for (ssize_t n = ::read(pipe_fds[0], buffer.data(), buffer.size()); n > 0;
         n         = ::read(pipe_fds[0], buffer.data(), buffer.size()))
        output.append(buffer.data(), static_cast<std::size_t>(n));
    ::close(pipe_fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) { // NOLINT(misc-include-cleaner)
        const std::string trimmed = trimmed_line(output);
        return std::unexpected(trimmed.empty() ? std::format("'{}' failed", argv.front()) : trimmed);
    }
    return output;
}

// The directory git is asked about: the path's own directory when it is one,
// its parent otherwise, and the current directory for a bare file name.
fs::path directory_of(std::string_view path) {
    const fs::path  p(path);
    std::error_code ec;
    if (fs::is_directory(p, ec))
        return p;
    return p.has_parent_path() ? p.parent_path() : fs::path(".");
}

// A fresh scratch directory under the system's temporary directory.
std::expected<fs::path, std::string> scratch_directory() {
    std::string pattern = (fs::temp_directory_path() / "specgen-baseline-XXXXXX").string();
    if (::mkdtemp(pattern.data()) == nullptr) // NOLINT(misc-include-cleaner)
        return std::unexpected(std::format("cannot create a scratch directory: {}", std::strerror(errno)));
    return fs::path(pattern);
}

} // namespace

std::expected<fs::path, std::string> top_level_of(std::string_view path) {
    return run({"git", "rev-parse", "--show-toplevel"}, directory_of(path))
        .transform([](std::string out) { return fs::path(trimmed_line(std::move(out))); })
        .transform_error(
            [&](const std::string& why) { return std::format("'{}' is not inside a git repository: {}", path, why); });
}

Tree::Tree(fs::path top_level, fs::path root) : top_level_(std::move(top_level)), root_(std::move(root)) {}

Tree::Tree(Tree&& other) noexcept : top_level_(std::move(other.top_level_)), root_(std::move(other.root_)) {
    other.root_.clear();
}

Tree& Tree::operator=(Tree&& other) noexcept {
    if (this != &other) {
        std::error_code ec;
        if (!root_.empty())
            fs::remove_all(root_, ec);
        top_level_ = std::move(other.top_level_);
        root_      = std::move(other.root_);
        other.root_.clear();
    }
    return *this;
}

Tree::~Tree() {
    std::error_code ec;
    if (!root_.empty())
        fs::remove_all(root_, ec);
}

std::expected<Tree, std::string> Tree::materialize(std::string_view revision, std::string_view path_in_repo) {
    return top_level_of(path_in_repo).and_then([&](fs::path top_level) -> std::expected<Tree, std::string> {
        // A clear word for a revision that names no commit, before any
        // scratch directory exists to clean up.
        const auto verified =
            run({"git", "rev-parse", "--verify", "--quiet", std::string(revision) + "^{commit}"}, top_level);
        if (!verified)
            return std::unexpected(std::format("'{}' does not name a commit in {}", revision, top_level.string()));
        return scratch_directory().and_then([&](fs::path scratch) -> std::expected<Tree, std::string> {
            Tree            tree(top_level, scratch);
            const fs::path  index = scratch / "index";
            const fs::path  root  = scratch / "tree";
            std::error_code ec;
            fs::create_directories(root, ec);
            const std::string index_var = "GIT_INDEX_FILE=" + index.string();
            return run({"env", index_var, "git", "read-tree", std::string(revision)}, top_level)
                .and_then([&](const std::string&) {
                    return run({"env", index_var, "git", "--work-tree=" + root.string(), "checkout-index", "-a"},
                               top_level);
                })
                .transform([&](const std::string&) {
                    tree.root_ = scratch; // the whole scratch directory is the tree's to remove
                    return std::move(tree);
                })
                .transform_error(
                    [&](const std::string& why) { return std::format("cannot check out '{}': {}", revision, why); });
        });
    });
}

std::string Tree::remap(std::string_view path) const {
    std::error_code ec;
    const fs::path  absolute = fs::absolute(fs::path(path), ec).lexically_normal();
    const fs::path  top      = top_level_.lexically_normal();
    const fs::path  relative = absolute.lexically_relative(top);
    if (relative.empty() || relative.native().starts_with(".."))
        return std::string(path);
    return (root_ / "tree" / relative).string();
}

std::vector<std::string> Tree::remap_args(const std::vector<std::string>& args) const {
    static constexpr std::array<std::string_view, 6> path_flags{
        "-I", "-isystem", "-iquote", "-idirafter", "-include", "-imacros"};
    // A fold with one bit of state: whether the previous argument was a
    // path-taking flag on its own, so this one is its path.
    struct State {
        std::vector<std::string> out;
        bool                     path_next = false;
    };
    State state = std::ranges::fold_left(args, State{}, [&](State s, const std::string& arg) {
        if (s.path_next) {
            s.out.push_back(remap(arg));
            s.path_next = false;
            return s;
        }
        const auto flag = std::ranges::find_if(path_flags, [&](std::string_view f) { return arg.starts_with(f); });
        if (flag == path_flags.end()) {
            s.out.push_back(arg);
            return s;
        }
        if (arg.size() == flag->size()) {
            s.out.push_back(arg);
            s.path_next = true;
            return s;
        }
        s.out.push_back(std::string(*flag) + remap(std::string_view(arg).substr(flag->size())));
        return s;
    });
    return std::move(state.out);
}

bool Tree::has(std::string_view path_in_repo) const {
    const std::string remapped = remap(path_in_repo);
    if (remapped == path_in_repo)
        return false; // outside the repository: nothing of it is at any revision
    std::error_code ec;
    return fs::exists(remapped, ec);
}

} // namespace beman::specgen::baseline_tree
