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
	$(LD) $(LDFLAGS) $(OBJS) -o $@

-include $(DEPS)

# ---- Bootable ISO ---------------------------------------------------------------

$(ISO): $(KERNEL_ELF) limine.conf $(TOOLCHAIN_DIR)/.deps-obtained
	rm -rf $(BUILD_DIR)/iso_root
	mkdir -p $(BUILD_DIR)/iso_root/boot/limine $(BUILD_DIR)/iso_root/EFI/BOOT
	cp $(KERNEL_ELF) $(BUILD_DIR)/iso_root/boot/kernel
	cp limine.conf $(BUILD_DIR)/iso_root/boot/limine/
	cp $(LIMINE_SHARE)/limine-uefi-cd.bin $(BUILD_DIR)/iso_root/boot/limine/
	cp $(LIMINE_SHARE)/BOOTAA64.EFI $(BUILD_DIR)/iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
	    -hfsplus -apm-block-size 2048 \
	    --efi-boot boot/limine/limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image --protective-msdos-label \
	    $(BUILD_DIR)/iso_root -o $@
	rm -rf $(BUILD_DIR)/iso_root
