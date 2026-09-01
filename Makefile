#! /usr/bin/make -f
# Makefile                                                       -*-makefile-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

NO_COLOR=1

INSTALL_PREFIX?=.install/
BUILD_DIR?=.build
DEST?=$(INSTALL_PREFIX)
CMAKE_FLAGS?=


PYEXECPATH ?= $(shell which python3.13 || which python3.12 || which python3.11 || which python3.10 || which python3.9 || which python3.8 || which python3)
PYTHON ?= $(notdir $(PYEXECPATH))
VENV := .venv
UV := $(shell command -v uv 2> /dev/null)
ACTIVATE := $(UV) run
PYEXEC := $(UV) run python
MARKER=.initialized.venv.stamp

PRE_COMMIT := $(UV) run pre-commit

TARGETS := test clean all ctest

export

.update-submodules:
	git submodule update --init --recursive
	touch .update-submodules

.gitmodules: .update-submodules

CONFIG?=Asan

# Benchmarks are opt-in and driven by the bench* targets below. BENCHMARKS
# gates whether the benchmark targets are configured at all; BENCH_CONFIG is
# the build config used to *measure* — RelWithDebInfo (optimized, symbols),
# never a sanitizer build, which would wreck the timings.
# The build config used to *measure* benchmarks: Bench is optimized and
# unsanitized (defined in the toolchain flags files, like Gcov/Asan). Never
# measure under a sanitizer.
BENCH_CONFIG?=Bench

export

ifeq ($(strip $(TOOLCHAIN)),)
	_build_name?=build-system/
	_build_dir?=.build/
	_local_toolchain?=$(CURDIR)/cmake/toolchain.cmake
else
	_build_name?=build-$(TOOLCHAIN)
	_build_dir?=.build/
	_local_toolchain?=$(CURDIR)/cmake/$(TOOLCHAIN)-toolchain.cmake
endif

_configuration_types?="RelWithDebInfo;Debug;Tsan;Asan;Gcov;Bench"

_build_path?=$(_build_dir)/$(_build_name)
_build_path:=$(subst //,/,$(_build_path))
_build_path:=$(patsubst %/,%,$(_build_path))

VCPKG ?= $(shell command -v vcpkg 2> /dev/null)

ifeq ($(VCPKG),)
	_cmake_top_level?="./cmake/use-fetch-content.cmake"
	_toolchain:=$(_local_toolchain)
	# To use a local git mirror for faster builds, uncomment and adjust:
	# _args=-DBEMANINFRA_googletest_REPO=file:///path/to/local/googletest.git
	_args=
else
	_vcpkg_toolchain:=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
	_cmake_top_level?=$(_vcpkg_toolchain)
	export PROJECT_VCPKG_TOOLCHAIN=$(_local_toolchain)
	_toolchain:=$(_local_toolchain)
	_args=-DVCPKG_OVERLAY_TRIPLETS=$(CURDIR)/cmake -DVCPKG_TARGET_TRIPLET=x64-linux-custom
	# for debugging add 	-DVCPKG_INSTALL_OPTIONS="--debug"
endif

CMAKE ?= $(UV) run cmake
CTEST ?= $(UV) run ctest

# Build telemetry (CMake instrumentation) is turned on through a top-level
# include: infra/cmake/BuildTelemetry.cmake runs configure_build_telemetry()
# on every configure, which is what keeps the instrumentation query alive.
# It is appended to the dependency-provider/vcpkg top-level include as a
# semicolon-separated list rather than replacing it.
_telemetry_include:=$(CURDIR)/infra/cmake/BuildTelemetry.cmake
_top_level_includes:=$(_cmake_top_level);$(_telemetry_include)

define run_cmake =
	$(CMAKE) \
	-G "Ninja Multi-Config" \
	-DCMAKE_CONFIGURATION_TYPES=$(_configuration_types) \
	-DCMAKE_INSTALL_PREFIX=$(abspath $(INSTALL_PREFIX)) \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=1 \
	-DCMAKE_PREFIX_PATH=$(CURDIR)/infra/cmake \
	-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$(_top_level_includes)" \
	-DCMAKE_C_COMPILER_LAUNCHER=ccache \
	-DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
	-DCMAKE_TOOLCHAIN_FILE=$(_toolchain) \
    $(_args) \
	$(_cmake_args) \
	$(CURDIR)
endef

default: test
.PHONY: default

$(_build_path):
	mkdir -p $(_build_path)

$(_build_path)/CMakeCache.txt: | $(_build_path) .gitmodules $(VENV)
	cd $(_build_path) && $(run_cmake)

$(_build_path)/compile_commands.json : $(_build_path)/CMakeCache.txt

.PHONY: compile_commands.json
compile_commands.json: $(_build_path)/compile_commands.json
compile_commands.json: ## symlink the current compile commands db
	if [ "$(shell readlink compile_commands.json)" != "$(_build_path)/compile_commands.json" ] ; then \
		ln -sf $(_build_path)/compile_commands.json ; \
	fi

TARGET:=all
.PHONY: TARGET

.PHONY: compile
compile: $(_build_path)/CMakeCache.txt
compile: compile_commands.json
compile:  ## Compile the project
	$(CMAKE) --build $(_build_path)  --config $(CONFIG) --target all -- -k 0

.PHONY: compile-headers
compile-headers: $(_build_path)/CMakeCache.txt ## Compile the headers
	 $(CMAKE) --build $(_build_path)  --config $(CONFIG) --target all_verify_interface_header_sets -- -k 0

.PHONY: install
install: $(_build_path)/CMakeCache.txt compile ## Install the project
	$(CMAKE) --install $(_build_path) --config $(CONFIG) --component beman.tree_algorithms --verbose

.PHONY: clean-install
clean-install:
	-rm -rf .install

.PHONY: realclean
realclean: clean-install

.PHONY: ctest
ctest: $(_build_path)/CMakeCache.txt ## Run CTest on current build
	$(CTEST) --test-dir $(_build_path) --output-on-failure -C $(CONFIG)

.PHONY: ctest_
ctest_ : compile
	$(CTEST) --test-dir $(_build_path) --output-on-failure -C $(CONFIG)

.PHONY: test
test: ctest_ ## Rebuild and run tests

.PHONY: cmake
cmake: |  $(_build_path)
	cd $(_build_path) && ${run_cmake}

.PHONY: clean
clean: $(_build_path)/CMakeCache.txt ## Clean the build artifacts
	$(CMAKE) --build $(_build_path)  --config $(CONFIG) --target clean

.PHONY: realclean
realclean: ## Delete the build directory
	rm -rf $(_build_path)

.PHONY: env
env:
	$(foreach v, $(.VARIABLES), $(info $(v) = $($(v))))

.DEFAULT: $(_build_path)/CMakeCache.txt ## Other targets passed through to cmake
	$(CMAKE) --build $(_build_path)  --config $(CONFIG) --target $@ -- -k 0

.PHONY: all
all: compile


.PHONY: venv
venv: ## Create python virtual env
venv: $(VENV)/$(MARKER)

.PHONY: clean-venv
clean-venv:
clean-venv: ## Delete python virtual env
	-rm -rf $(VENV)

realclean: clean-venv

.PHONY: show-venv
show-venv: venv
show-venv: ## Debugging target - show venv details
	$(PYEXEC) -c "import sys; print('Python ' + sys.version.replace('\n',''))"
	@echo venv: $(VENV)

uv.lock: pyproject.toml
	$(UV) lock

$(VENV):
	$(UV) venv --python $(PYTHON)

$(VENV)/$(MARKER): uv.lock | $(VENV)
	$(UV) sync
	touch $(VENV)/$(MARKER)

.PHONY: dev-shell
dev-shell: venv
dev-shell: ## Shell with the venv activated
	$(ACTIVATE) $(notdir $(SHELL))

.PHONY: bash zsh
bash zsh: venv
bash zsh: ## Run bash or zsh with the venv activated
	$(ACTIVATE) $@

.PHONY: lint
lint: venv
lint: ## Run all configured tools in pre-commit
	$(PRE_COMMIT) run -a

.PHONY: lint-manual
lint-manual: venv
lint-manual: ## Run all manual tools in pre-commit
	$(PRE_COMMIT) run --hook-stage manual -a

.PHONY: coverage
coverage: ## Build and run the tests with the GCOV profile and process the results
coverage: venv $(_build_path)/CMakeCache.txt
	$(CMAKE) --build $(_build_path) --config Gcov
	$(ACTIVATE) ctest --build-config Gcov --output-on-failure --test-dir $(_build_path)
	$(CMAKE) --build $(_build_path) --config Gcov --target process_coverage

.PHONY: view-coverage
view-coverage: ## View the coverage report
	sensible-browser $(_build_path)/coverage/coverage.html

# ---------------------------------------------------------------------
# Benchmarks.
#
# The benchmark targets are ordinary targets in the default build, so they
# always compile and can never rot silently. What the bench* targets add is
# *measurement*: they build and run in the Bench configuration (optimized,
# unsanitized — see the toolchain flags files), and read compile timings out
# of the CMake build telemetry, which drops one per-translation-unit
# `compile` event into $(_build_path)/.trace on every build.
#
# CCACHE_RECACHE forces a real (measurable) recompile while keeping the
# compiler cache coherent — it recompiles and refreshes the cache entry
# rather than serving an old object; the produced objects are identical, so
# the build stays coherent. Touching the generated sources is what makes
# ninja consider them dirty.
# ---------------------------------------------------------------------

_bench_bin_dir:=$(_build_path)/benchmarks/beman/tree_algorithms/$(BENCH_CONFIG)
_bench_ct_src:=$(_build_path)/benchmarks/beman/tree_algorithms/ct_*.cpp

.PHONY: bench
bench: $(_build_path)/CMakeCache.txt ## Build and run the runtime microbenchmarks (Bench config)
	$(CMAKE) --build $(_build_path) --config $(BENCH_CONFIG) --target bench_runtime_all
	@echo "==== fold: consuming a very large tree ===="
	$(_bench_bin_dir)/beman.tree_algorithms.benchmarks.fold "[!benchmark]"
	@echo "==== build: producing / copying / refolding ===="
	$(_bench_bin_dir)/beman.tree_algorithms.benchmarks.build "[!benchmark]"

.PHONY: bench-compile
bench-compile: $(_build_path)/CMakeCache.txt ## Recompile the compile-time benchmark matrix and report per-TU compile time
	-touch $(_bench_ct_src)
	CCACHE_RECACHE=1 $(CMAKE) --build $(_build_path) --config $(BENCH_CONFIG) --target bench_ct_all
	$(PYEXEC) benchmarks/compile_trace_report.py --build-dir $(_build_path) --config $(BENCH_CONFIG)

.PHONY: bench-report
bench-report: ## Re-print the compile-time report from the existing .trace, without rebuilding
	$(PYEXEC) benchmarks/compile_trace_report.py --build-dir $(_build_path) --config $(BENCH_CONFIG)

.PHONY: docs
docs: ## Build the docs with Doxygen
	doxygen docs/Doxyfile

.PHONY: mrdocs
mrdocs: ## Build the docs with Doxygen
	-rm -rf docs/adoc
	cd docs && NO_COLOR=1 mrdocs mrdocs.yml 2>&1 | sed 's/\x1b\[[0-9;]*m//g'
	find docs/adoc -name '*.adoc' | xargs asciidoctor

.PHONY: testinstall
testinstall: install
testinstall: ## Test the installed package
	mkdir -p installtest
	$(CMAKE) -S installtest -B installtest/.build
	$(CMAKE) --build  installtest/.build --target test

.PHONY: clean-testinstall
clean-testinstall:
	-rm -rf installtest/.build

realclean: clean-testinstall

# Git subtree management for infra/
INFRA_REMOTE ?= https://github.com/bemanproject/infra.git
INFRA_BRANCH ?= main

.PHONY: subtree-pull
subtree-pull: ## Pull latest infra changes via git subtree (squash)
	git subtree pull --squash --prefix=infra $(INFRA_REMOTE) $(INFRA_BRANCH) \
		-m "Merge infra subtree from $(INFRA_REMOTE) $(INFRA_BRANCH)"

.PHONY: subtree-pull-full
subtree-pull-full: ## Pull infra with full history (for upstreaming)
	git subtree pull --prefix=infra $(INFRA_REMOTE) $(INFRA_BRANCH)

.PHONY: subtree-split
subtree-split: ## Split infra changes into a branch for upstreaming as a PR
	git subtree split --prefix=infra -b infra-upstream

.PHONY: subtree-add
subtree-add: ## Initial import of infra as git subtree (squash)
	git subtree add --squash --prefix=infra $(INFRA_REMOTE) $(INFRA_BRANCH)

ifeq ($(UV),)
define install_uv_cmd
pipx install uv
endef

define uv_error_message

'uv' command not found.
Please install uv or set the UV variable to the path of the uv binary.
The makefile target "install-uv" will run ``$(install_uv_cmd)''
endef

$(error "$(uv_error_message)")
endif

.PHONY: install-uv
install-uv: ## install uv via `pipx install uv`
	$(install_uv_cmd)

# Help target
.PHONY: help
help: ## Show this help.
	@awk 'BEGIN {FS = ":.*?## "} /^[.a-zA-Z_-]+:.*?## / {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}'  $(MAKEFILE_LIST) | sort
