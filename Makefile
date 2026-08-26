# intro-os: a "Hello, World!" kernel for aarch64, booted by Limine, run on QEMU.
#
# Everything this needs (cross-compiler, QEMU, Limine, OVMF firmware) is
# fetched into ./toolchain by `make deps` without requiring root: package
# archives are downloaded with `apt-get download` (no install step) and
# unpacked locally with `dpkg-deb -x`, so nothing here touches the system.

SHELL := /bin/bash
.SUFFIXES:

ROOT_DIR      := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
TOOLCHAIN_DIR := $(ROOT_DIR)/toolchain
BUILD_DIR     := $(ROOT_DIR)/build

IMAGE_NAME := intro-os
KERNEL_ELF := $(BUILD_DIR)/kernel
ISO        := $(BUILD_DIR)/$(IMAGE_NAME).iso

# ---- Vendored, no-root toolchain ------------------------------------------

CROSS_TRIPLE := aarch64-linux-gnu
CROSS_BIN    := $(TOOLCHAIN_DIR)/usr/bin
CXX          := $(CROSS_BIN)/$(CROSS_TRIPLE)-g++
LD           := $(CROSS_BIN)/$(CROSS_TRIPLE)-ld
QEMU         := $(TOOLCHAIN_DIR)/usr/bin/qemu-system-aarch64
OVMF_CODE    := $(TOOLCHAIN_DIR)/share/ovmf/ovmf-code-aarch64.fd
LIMINE_SHARE := $(TOOLCHAIN_DIR)/share/limine

# The vendored cross-binutils link against a handful of host shared
# libraries that live next to them rather than in the system search path.
export LD_LIBRARY_PATH := $(TOOLCHAIN_DIR)/usr/lib/x86_64-linux-gnu

DEB_PACKAGES := \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    cpp-13-aarch64-linux-gnu \
    gcc-13-aarch64-linux-gnu \
    g++-13-aarch64-linux-gnu \
    libgcc-13-dev-arm64-cross \
    libc6-dev-arm64-cross \
    linux-libc-dev-arm64-cross \
    libstdc++-13-dev-arm64-cross \
    binutils-aarch64-linux-gnu \
    binutils-common \
    qemu-system-arm

LIMINE_RELEASE_URL := https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz
OVMF_RELEASE_URL    := https://github.com/osdev0/edk2-ovmf-stable-bins/releases/latest/download/edk2-ovmf-bins.tar.gz

# ---- QEMU run configuration ------------------------------------------------

SMP ?= 4
MEM ?= 256M
QEMUFLAGS ?= -M virt -cpu cortex-a72 -smp $(SMP) -m $(MEM) -nographic

# ---- Compiler / linker flags ------------------------------------------------

CXXFLAGS := \
    -std=gnu++20 \
    -Wall -Wextra \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fno-pic -fno-pie \
    -fno-exceptions \
    -fno-rtti \
    -fno-threadsafe-statics \
    -mgeneral-regs-only \
    -march=armv8-a+nofp+nosimd \
    -mno-outline-atomics \
    -Ikernel/include \
    -O2 -g

LDFLAGS := \
    -m aarch64elf \
    -nostdlib -static \
    -z max-page-size=0x1000 \
    --gc-sections \
    -T kernel/linker/aarch64.ld

SRCS     := $(wildcard kernel/src/*.cpp)
ASM_SRCS := $(wildcard kernel/src/*.S)
OBJS     := $(patsubst kernel/src/%.cpp,$(BUILD_DIR)/obj/%.o,$(SRCS)) \
            $(patsubst kernel/src/%.S,$(BUILD_DIR)/obj/%.o,$(ASM_SRCS))
DEPS     := $(patsubst kernel/src/%.cpp,$(BUILD_DIR)/obj/%.d,$(SRCS))

# ---- Top-level targets ------------------------------------------------------

.PHONY: all
all: $(ISO)

.PHONY: run
run: $(ISO)
	$(QEMU) $(QEMUFLAGS) \
	    -drive if=pflash,unit=0,format=raw,file=$(OVMF_CODE),readonly=on \
	    -cdrom $(ISO)

.PHONY: deps
deps: $(TOOLCHAIN_DIR)/.deps-obtained

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

.PHONY: distclean
distclean: clean
	rm -rf $(TOOLCHAIN_DIR)

# ---- Dependency fetching (no root required) ---------------------------------

$(TOOLCHAIN_DIR)/.deps-obtained:
	@mkdir -p $(TOOLCHAIN_DIR)/_debs
	cd $(TOOLCHAIN_DIR)/_debs && apt-get download $(DEB_PACKAGES)
	for deb in $(TOOLCHAIN_DIR)/_debs/*.deb; do dpkg-deb -x "$$deb" $(TOOLCHAIN_DIR); done
	rm -rf $(TOOLCHAIN_DIR)/_debs
	mkdir -p $(LIMINE_SHARE) $(TOOLCHAIN_DIR)/share/ovmf
	curl -fL $(LIMINE_RELEASE_URL) | tar xz -C $(LIMINE_SHARE) --strip-components=1 \
	    limine-binary/BOOTAA64.EFI limine-binary/limine-uefi-cd.bin
	curl -fL $(OVMF_RELEASE_URL) | tar xz -C $(TOOLCHAIN_DIR)/share/ovmf --strip-components=1 \
	    edk2-ovmf-bins/ovmf-code-aarch64.fd
	touch $@

# ---- Kernel build -------------------------------------------------------------

$(BUILD_DIR)/obj/%.o: kernel/src/%.cpp $(TOOLCHAIN_DIR)/.deps-obtained Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

# Hand-written assembly (e.g. context_switch.S): no CXXFLAGS -- those tune
# C++ codegen (-mgeneral-regs-only and friends) and don't apply to .S files,
# which the compiler driver just preprocesses and assembles as-is.
$(BUILD_DIR)/obj/%.o: kernel/src/%.S $(TOOLCHAIN_DIR)/.deps-obtained Makefile
	@mkdir -p $(dir $@)
	$(CXX) -c $< -o $@

$(KERNEL_ELF): $(OBJS) kernel/linker/aarch64.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJS) -o $@.tmp
	mv $@.tmp $@

-include $(DEPS)

# ---- Bootable ISO ---------------------------------------------------------------

# Packs a kernel ELF into a bootable Limine ISO. $(1) is the kernel ELF to
# boot, $(2) the ISO path to produce, $(3) a scratch iso_root directory to
# stage it in (must be unique per concurrent build -- the main kernel and
# every test each get their own, so `make test` can build/run tests
# without stomping on `make`'s own build/iso_root).
#
# Builds into $(2).tmp and only renames it to $(2) once xorriso has fully
# succeeded: `mv` on the same filesystem is atomic, so an interrupted or
# failed build (e.g. Ctrl-C mid-run) never leaves a corrupt-but-newer-than-
# its-dependency $(2) behind for make to mistake as already up to date on
# the next invocation.
define MAKE_ISO
	rm -rf $(3)
	mkdir -p $(3)/boot/limine $(3)/EFI/BOOT
	cp $(1) $(3)/boot/kernel
	cp limine.conf $(3)/boot/limine/
	cp $(LIMINE_SHARE)/limine-uefi-cd.bin $(3)/boot/limine/
	cp $(LIMINE_SHARE)/BOOTAA64.EFI $(3)/EFI/BOOT/
	rm -f $(2).tmp
	xorriso -as mkisofs -R -r -J \
	    -hfsplus -apm-block-size 2048 \
	    --efi-boot boot/limine/limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image --protective-msdos-label \
	    $(3) -o $(2).tmp
	mv $(2).tmp $(2)
	rm -rf $(3)
endef

$(ISO): $(KERNEL_ELF) limine.conf $(TOOLCHAIN_DIR)/.deps-obtained
	$(call MAKE_ISO,$(KERNEL_ELF),$@,$(BUILD_DIR)/iso_root)

# ---- Test harness -----------------------------------------------------------
#
# Each kernel/tests/*.cpp is a standalone testcase: it defines its own
# extern "C" void main() (see tests/test_util.hpp), which kmain() schedules
# and runs in place of the kernel's own weak default `main` (see
# kernel/src/main.cpp). Building a test links that one test object
# together with the exact same kernel/src objects ($(OBJS)) the normal
# kernel uses -- nothing in kernel/src changes per test.
#
# `make test` runs every test; `make test <name>...` runs just those --
# e.g. a test in tests/switch.cpp is run with `make test switch`. This
# works via a standard make trick: any word after `test` on the command
# line is registered as a no-op phony target instead of something make
# would otherwise try (and fail) to build as a file.

TESTS_DIR      := tests
TEST_SRCS      := $(wildcard $(TESTS_DIR)/*.cpp)
TEST_NAMES     := $(basename $(notdir $(TEST_SRCS)))
TEST_BUILD_DIR := $(BUILD_DIR)/tests

# How long a single test gets in QEMU before it's declared a failure (e.g.
# a bug that hangs a thread instead of reaching test::pass()/fail()).
TEST_TIMEOUT ?= 10

# The fixed line kmain() (kernel/src/main.cpp) always prints first, before
# anything a test or the (possibly-overridden) boot banner produces. Used
# below to discard the UEFI firmware/Limine boot log a tests/*.ok file has
# no business needing to match -- keep this in sync with main.cpp's string
# if it ever changes.
TEST_BOOT_MARKER := --- kernel output begins ---

$(TEST_BUILD_DIR)/obj/%.o: $(TESTS_DIR)/%.cpp $(TOOLCHAIN_DIR)/.deps-obtained Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(patsubst $(TESTS_DIR)/%.cpp,$(TEST_BUILD_DIR)/obj/%.d,$(TEST_SRCS))

# Builds one test's kernel+ISO, runs it under QEMU, and diffs its console
# output -- everything after TEST_BOOT_MARKER, with \r\n normalized to \n
# -- against tests/<name>.ok, byte for byte. $(1) is the test's name (its
# .cpp file's basename, e.g. "heap").
define TEST_TEMPLATE

$(TEST_BUILD_DIR)/$(1)/kernel: $(OBJS) $(TEST_BUILD_DIR)/obj/$(1).o kernel/linker/aarch64.ld
	@mkdir -p $$(dir $$@)
	$(LD) $(LDFLAGS) $(OBJS) $(TEST_BUILD_DIR)/obj/$(1).o -o $$@.tmp
	mv $$@.tmp $$@

$(TEST_BUILD_DIR)/$(1)/test.iso: $(TEST_BUILD_DIR)/$(1)/kernel limine.conf $(TOOLCHAIN_DIR)/.deps-obtained
	$$(call MAKE_ISO,$(TEST_BUILD_DIR)/$(1)/kernel,$$@,$(TEST_BUILD_DIR)/$(1)/iso_root)

.PHONY: test-$(1)
test-$(1): $(TEST_BUILD_DIR)/$(1)/test.iso $(TESTS_DIR)/$(1).ok
	@echo "--- $(1) ---"
	@rm -f $(TEST_BUILD_DIR)/$(1)/output.log $(TEST_BUILD_DIR)/$(1)/qemu.log \
	    $(TEST_BUILD_DIR)/$(1)/actual.log $(TEST_BUILD_DIR)/$(1)/diff.log
	@timeout $(TEST_TIMEOUT) $(QEMU) $(QEMUFLAGS) \
	    -drive if=pflash,unit=0,format=raw,file=$(OVMF_CODE),readonly=on \
	    -serial file:$(TEST_BUILD_DIR)/$(1)/output.log \
	    -monitor none \
	    -cdrom $$< > $(TEST_BUILD_DIR)/$(1)/qemu.log 2>&1; \
	tr -d '\r' < $(TEST_BUILD_DIR)/$(1)/output.log 2>/dev/null \
	    | awk -v marker='$(TEST_BOOT_MARKER)' 'index($$$$0, marker) > 0 { f = 1; next } f' \
	    > $(TEST_BUILD_DIR)/$(1)/actual.log; \
	if diff -u $(TESTS_DIR)/$(1).ok $(TEST_BUILD_DIR)/$(1)/actual.log > $(TEST_BUILD_DIR)/$(1)/diff.log 2>&1; then \
	    echo "PASS: $(1)"; \
	else \
	    echo "FAIL: $(1)"; \
	    echo "--- diff (expected: $(TESTS_DIR)/$(1).ok, actual: kernel output after boot) ---"; \
	    cat $(TEST_BUILD_DIR)/$(1)/diff.log; \
	    echo "--- full guest console, including firmware boot (output.log) ---"; \
	    cat $(TEST_BUILD_DIR)/$(1)/output.log 2>/dev/null; \
	    echo "--- qemu process output (qemu.log) ---"; \
	    cat $(TEST_BUILD_DIR)/$(1)/qemu.log 2>/dev/null; \
	    exit 1; \
	fi

endef

$(foreach t,$(TEST_NAMES),$(eval $(call TEST_TEMPLATE,$(t))))

ifeq (test,$(firstword $(MAKECMDGOALS)))
  TEST_ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  $(eval $(TEST_ARGS):;@:)
endif

.PHONY: test
test:
	@$(MAKE) --no-print-directory $(addprefix test-,$(if $(TEST_ARGS),$(TEST_ARGS),$(TEST_NAMES)))
