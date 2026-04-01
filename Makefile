ROOT_DIR := $(abspath .)
APP_DIR := $(ROOT_DIR)/apple/Apps/AutomixMac
APP_PROJECT := AutomixMac.xcodeproj
APP_SCHEME := AutomixMac
APP_CONFIG ?= Debug
APP_DEST ?= platform=macOS,arch=arm64
APP_DERIVED_DATA := $(APP_DIR)/build
APP_BUNDLE := $(APP_DERIVED_DATA)/Build/Products/$(APP_CONFIG)/AutomixMac.app

BUILD_TYPE ?= Debug
ARCH ?= arm64
PKG_CONFIG_BIN ?= /opt/homebrew/bin/pkg-config
JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 8)
RELEASE_ARGS ?=

.PHONY: help doctor dev cmake-configure build-lib build-demo build build-app run-app run app test clean clean-app clean-all release

help: ; @echo "AutoMix shortcuts:"; \
	echo "  make dev           - switch to arm64 toolchain and rebuild libautomix (Essentia required)"; \
	echo "  make build         - build libautomix + SwiftPM AutomixDemo"; \
	echo "  make build-app     - generate Xcode project and build AutomixMac app"; \
	echo "  make run-app       - build and launch AutomixMac app"; \
	echo "  make run           - alias of make run-app"; \
	echo "  make app           - alias of make run-app"; \
	echo "  make test          - run C++ tests (ctest)"; \
	echo "  make clean         - clean cmake/.build/app derived data"; \
	echo "  make release VERSION=X.Y.Z [RELEASE_ARGS='--no-push -y']"

doctor: ; @echo "arch=$$(uname -m)"; \
	echo "pkg-config=$$(command -v pkg-config || true)"; \
	echo "pkg-config(libdir essentia)=$$($(PKG_CONFIG_BIN) --variable=libdir essentia 2>/dev/null || echo '<missing essentia>')"; \
	echo "libautomix=$$( [ -f cmake-build/libautomix.a ] && lipo -info cmake-build/libautomix.a || echo '<missing cmake-build/libautomix.a>' )"; \
	echo "libessentia=$$( [ -f /opt/homebrew/lib/libessentia.a ] && lipo -info /opt/homebrew/lib/libessentia.a || echo '<missing /opt/homebrew/lib/libessentia.a>' )"

dev: ; ./scripts/switch_arm64.sh

cmake-configure: ; cmake -S . -B cmake-build \
  -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" \
  -DCMAKE_OSX_ARCHITECTURES="$(ARCH)" \
  -DENABLE_ESSENTIA=ON \
  -DPKG_CONFIG_EXECUTABLE="$(PKG_CONFIG_BIN)"

build-lib: cmake-configure
	@cmake --build cmake-build -j"$(JOBS)"

build-demo: build-lib ; swift build --target AutomixDemo

build: build-lib build-demo

build-app: build-lib
	@cd "$(APP_DIR)" && xcodegen generate
	@cd "$(APP_DIR)" && xcodebuild \
	  -project "$(APP_PROJECT)" \
	  -scheme "$(APP_SCHEME)" \
	  -configuration "$(APP_CONFIG)" \
	  -destination '$(APP_DEST)' \
	  -derivedDataPath "$(APP_DERIVED_DATA)" \
	  build

run-app: build-app
	@open "$(APP_BUNDLE)"

run: run-app

app: run-app

test: build-lib
	@ctest --test-dir cmake-build --output-on-failure

clean: ; rm -rf cmake-build .build "$(APP_DERIVED_DATA)"

clean-app: ; rm -rf "$(APP_DERIVED_DATA)"

clean-all: clean
	@rm -rf "$(APP_DIR)/$(APP_PROJECT)"

release:
	@if [ -z "$(VERSION)" ]; then \
	  echo "ERROR: VERSION is required. Example: make release VERSION=1.2.3"; \
	  exit 1; \
	fi
	@./scripts/release.sh --version "$(VERSION)" $(RELEASE_ARGS)
