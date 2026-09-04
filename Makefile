#! /usr/bin/make -f
# Makefile                                                       -*-makefile-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

NO_COLOR=1

PREFIX?=.install/
BUILD_DIR?=.build
CMAKE_FLAGS?=

# Where to find LLVM/Clang's CMake package. specgen requires the front end,
# and find_package asks for BEMAN_SPECGEN_LLVM_VERSION (22.1), so it
# picks that LLVM even next to a newer one. This is a search hint, not an
# override: an LLVM whose version does not match the request is rejected, so
# building against a different one means moving the pin too, e.g.
# `make CMAKE_FLAGS=-DBEMAN_SPECGEN_LLVM_VERSION=23.0 CLANG_DIR=/usr/lib/llvm-23/lib/cmake/clang`.
CLANG_DIR?=/usr/lib/llvm-22/lib/cmake/clang


PYEXECPATH ?= $(shell which python3.13 || which python3.12 || which python3.11 || which python3.10 || which python3.9 || which python3.8 || which python3)
PYTHON ?= $(notdir $(PYEXECPATH))
VENV := .venv
UV := $(shell command -v uv 2> /dev/null)
ACTIVATE := $(UV) run
PYEXEC := $(UV) run python
MARKER=.initialized.venv.stamp

PRE_COMMIT := $(UV) run pre-commit
EMACS ?= $(shell command -v emacs 2> /dev/null)

TARGETS := test clean all ctest

export

.update-submodules:
	git submodule update --init --recursive
	touch .update-submodules

.gitmodules: .update-submodules

CONFIG?=Asan

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

_configuration_types?="RelWithDebInfo;Debug;Tsan;Asan;Gcov"

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

define run_cmake =
	$(CMAKE) \
	-G "Ninja Multi-Config" \
	-DCMAKE_CONFIGURATION_TYPES=$(_configuration_types) \
	-DCMAKE_INSTALL_PREFIX=$(abspath $(PREFIX)) \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=1 \
	-DCMAKE_PREFIX_PATH=$(CURDIR)/infra/cmake \
	-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=$(_cmake_top_level) \
	-DCMAKE_C_COMPILER_LAUNCHER=ccache \
	-DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
	-DCMAKE_TOOLCHAIN_FILE=$(_toolchain) \
	-DClang_DIR=$(CLANG_DIR) \
    $(_args) \
	$(_cmake_args) \
	$(CMAKE_FLAGS) \
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

# Which target `compile` builds. Honoured, not decorative: the build line
# below passes it through, so `make TARGET=goldens compile` builds only the
# goldens rather than everything. Note that `make goldens` needs no TARGET=
# at all -- the .DEFAULT rule at the bottom passes any unknown goal straight
# through to cmake.
TARGET?=all

.PHONY: compile
compile: $(_build_path)/CMakeCache.txt
compile: compile_commands.json
compile:  ## Compile the project ($(TARGET) by default; TARGET= to pick one)
	$(CMAKE) --build $(_build_path)  --config $(CONFIG) --target $(TARGET) -- -k 0

.PHONY: compile-headers
compile-headers: $(_build_path)/CMakeCache.txt ## Compile the headers
	 $(CMAKE) --build $(_build_path)  --config $(CONFIG) --target all_verify_interface_header_sets -- -k 0

# No --component: the components this project actually creates are
# `specgen_Runtime` (the driver) and `specgen_Development` (the library,
# headers and CMake package), both derived by beman-install-library.cmake
# stripping `beman.` from `beman.specgen`. A `--component beman.specgen` here
# would name no component at all, and `cmake --install` treats an unknown
# component as an empty one, so that target would exit 0 and install nothing.
#
# CONFIG defaults to Asan for day-to-day work, but Asan is the wrong thing to
# install -- the sanitizer writes to stderr, which would corrupt captured
# example output, and a sanitized static library imposes Asan on every
# consumer that links it. So `install` pins its own CONFIG to RelWithDebInfo
# rather than inheriting the global default; that override also reaches the
# `compile` prerequisite, so the two stay in sync without a separate release
# build step.
install: CONFIG=$(RELEASE_CONFIG)
.PHONY: install
install: $(_build_path)/CMakeCache.txt compile ## Install the project (RelWithDebInfo; CONFIG= to override)
	$(CMAKE) --install $(_build_path) --config $(CONFIG) --verbose

# The configuration to ship, and the one `install` and `testinstall` run
# against. RelWithDebInfo rather than Release: optimized, but it keeps the
# debug info that makes a failure in an installed binary diagnosable.
RELEASE_CONFIG?=RelWithDebInfo

.PHONY: release
release: ## Build the release (RelWithDebInfo) binaries
	$(MAKE) CONFIG=$(RELEASE_CONFIG) compile

.PHONY: install-release
install-release: ## Install the release (RelWithDebInfo) build
	$(MAKE) CONFIG=$(RELEASE_CONFIG) install

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

.PHONY: docs
docs: ## Build the docs with Doxygen
	doxygen docs/Doxyfile

.PHONY: mrdocs
mrdocs: ## Build the docs with Doxygen
	-rm -rf docs/adoc
	cd docs && NO_COLOR=1 mrdocs mrdocs.yml 2>&1 | sed 's/\x1b\[[0-9;]*m//g'
	find docs/adoc -name '*.adoc' | xargs asciidoctor

ORGFILES := $(wildcard docs/*.org)
EXAMPLE_ORGFILES := docs/examples.org

docs/%.html : docs/%.org
	@test -n "$(EMACS)" || { echo "emacs not found; install emacs or set EMACS"; exit 1; }
	$(EMACS) --init-directory=.emacs.d/ \
		--batch --load .emacs.d/init.el \
		--eval "(setq enable-local-variables :all)" \
		--visit $< \
		--eval "(org-transclusion-mode t)" \
		--eval "(org-export-to-file 'html \"$(abspath $@)\")"
	echo $@ : \\ > $@.deps
	echo "  $<" \\ >> $@.deps
	sed -n "s#^.*\[\[file:\([^]:]*\)\(::[^]]*\)\?\]\].*\$$#$(dir $<)\1#p" < $< | sort -u | xargs printf "  %s \\\\\\n" >> $@.deps

$(ORGFILES:%.org=%.html.deps):

-include $(ORGFILES:%.org=%.html.deps)

docs/examples.md : docs/examples.org
	@test -n "$(EMACS)" || { echo "emacs not found; install emacs or set EMACS"; exit 1; }
	$(EMACS) --init-directory=.emacs.d/ \
		--batch --load .emacs.d/init.el \
		-f package-initialize \
		--eval "(setq enable-local-variables :all)" \
		--visit $< \
		--eval "(org-transclusion-mode t)" \
		--eval "(require 'ox-gfm)" \
		--eval "(org-export-to-file 'gfm \"$(abspath $@)\")"
	echo $@ : \\ > $@.deps
	echo "  $<" \\ >> $@.deps
	sed -n "s#^.*\[\[file:\([^]:]*\)\(::[^]]*\)\?\]\].*\$$#$(dir $<)\1#p" < $< | sort -u | xargs printf "  %s \\\\\\n" >> $@.deps

$(EXAMPLE_ORGFILES:.org=.md.deps):

-include $(EXAMPLE_ORGFILES:.org=.md.deps)

.PHONY: examples-md
examples-md: $(EXAMPLE_ORGFILES:.org=.md) ## Convert docs/examples.org to GFM markdown

.PHONY: examples-html
examples-html: docs/examples.html ## Export docs/examples.org to HTML

# The live document runs its own commands during the export, so the tool has to
# be installed before the export starts. An order-only prerequisite: the page
# depends on the tool existing, not on its timestamp, so a rebuilt binary does
# not by itself make the page stale.
docs/examples-live.html: | install-release

.PHONY: examples-live-html
examples-live-html: docs/examples-live.html ## Export the live document, running its commands

.PHONY: clean-org-deps
clean-org-deps:
	-rm -f $(ORGFILES:%.org=%.html.deps) $(EXAMPLE_ORGFILES:.org=.md.deps)
clean: clean-org-deps

.PHONY: clean-org-html
clean-org-html:
	-rm -f $(ORGFILES:%.org=%.html)
clean: clean-org-html

.PHONY: clean-emacs.d
clean-emacs.d:
	-rm -rf .emacs.d/elpa* .emacs.d/eln-cache
realclean: clean-emacs.d

# Runs against the release install rather than the default Asan one, so the
# thing tested is the thing shipped. installtest/ is a standalone consumer
# project: it is configured against the install prefix and knows nothing about
# this build tree.
.PHONY: testinstall
testinstall: install-release
testinstall: ## Test the installed package with a standalone consumer
	$(CMAKE) -S installtest -B installtest/.build \
		-DCMAKE_TOOLCHAIN_FILE=$(_toolchain) \
		-DCMAKE_PREFIX_PATH=$(abspath $(PREFIX))
	$(CMAKE) --build installtest/.build
	$(CTEST) --test-dir installtest/.build --output-on-failure

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
