ARCH := x86_64
BUILD_DIR := build
ISO_ROOT := $(BUILD_DIR)/iso_root
KERNEL := $(BUILD_DIR)/kernel.elf
ISO := $(BUILD_DIR)/voidOS.iso

BREW_PREFIX := $(shell brew --prefix 2>/dev/null)
LLVM_PREFIX := $(shell brew --prefix llvm 2>/dev/null)
LIMINE_PREFIX := $(shell brew --prefix limine 2>/dev/null)
QEMU_PREFIX := $(shell brew --prefix qemu 2>/dev/null)

CC := $(if $(LLVM_PREFIX),$(LLVM_PREFIX)/bin/clang,clang)
LD := $(if $(shell command -v ld.lld 2>/dev/null),ld.lld,$(if $(LLVM_PREFIX),$(LLVM_PREFIX)/bin/ld.lld,ld.lld))
QEMU := qemu-system-x86_64
XORRISO := xorriso
LIMINE := limine

LIMINE_BIOS_SYS := $(firstword \
	$(wildcard $(LIMINE_PREFIX)/share/limine/limine-bios.sys) \
	$(wildcard $(LIMINE_PREFIX)/share/limine/limine-bios-cd.bin))
LIMINE_BIOS_CD := $(firstword \
	$(wildcard $(LIMINE_PREFIX)/share/limine/limine-bios-cd.bin))
LIMINE_UEFI_CD := $(firstword \
	$(wildcard $(LIMINE_PREFIX)/share/limine/limine-uefi-cd.bin) \
	$(wildcard $(LIMINE_PREFIX)/share/limine/BOOTX64.EFI))
LIMINE_BOOTX64 := $(firstword \
	$(wildcard $(LIMINE_PREFIX)/share/limine/BOOTX64.EFI) \
	$(wildcard $(LIMINE_PREFIX)/share/limine/BOOTX64.efi) \
	$(wildcard $(LIMINE_PREFIX)/share/limine/limine_x64.efi))
LIMINE_BOOTIA32 := $(firstword \
	$(wildcard $(LIMINE_PREFIX)/share/limine/BOOTIA32.EFI) \
	$(wildcard $(LIMINE_PREFIX)/share/limine/BOOTIA32.efi) \
	$(wildcard $(LIMINE_PREFIX)/share/limine/limine_ia32.efi))

OVMF_CODE := $(firstword \
	$(wildcard $(QEMU_PREFIX)/share/qemu/edk2-x86_64-code.fd) \
	$(wildcard $(QEMU_PREFIX)/share/qemu/edk2-x86_64-code.fd.bz2) \
	$(wildcard $(BREW_PREFIX)/share/qemu/edk2-x86_64-code.fd) \
	$(wildcard $(BREW_PREFIX)/share/qemu/edk2-x86_64-code.fd.bz2) \
	$(wildcard /opt/homebrew/share/qemu/edk2-x86_64-code.fd) \
	$(wildcard /usr/local/share/qemu/edk2-x86_64-code.fd))

CFLAGS := \
	-target x86_64-elf \
	-std=c11 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-stack-check \
	-fno-lto \
	-fno-pic \
	-fno-pie \
	-mcmodel=kernel \
	-m64 \
	-mabi=sysv \
	-mno-red-zone \
	-mno-mmx \
	-mno-sse \
	-mno-sse2 \
	-O2 \
	-Wall \
	-Wextra \
	-Ikernel/include \
	$(EXTRA_CFLAGS)

LDFLAGS := \
	-m elf_x86_64 \
	-nostdlib \
	-static \
	-z max-page-size=0x1000 \
	-T linker/kernel.ld

KERNEL_SOURCES := $(shell find kernel -name '*.c')
KERNEL_ASM_SOURCES := $(shell find kernel -name '*.S')
KERNEL_C_OBJECTS := $(patsubst kernel/%.c,$(BUILD_DIR)/kernel/%.o,$(KERNEL_SOURCES))
KERNEL_ASM_OBJECTS := $(patsubst kernel/%.S,$(BUILD_DIR)/kernel/%.o,$(KERNEL_ASM_SOURCES))
KERNEL_OBJECTS := $(KERNEL_C_OBJECTS) $(KERNEL_ASM_OBJECTS)

.PHONY: all kernel iso run test-exception test-page-fault clean check-tools check-uefi

all: iso

kernel: $(KERNEL)

iso: check-tools $(ISO)

run: iso check-uefi
	$(QEMU) \
		-M q35 \
		-m 512M \
		-cdrom $(ISO) \
		-serial stdio \
		-no-reboot \
		-no-shutdown \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE)

test-exception:
	$(MAKE) clean
	$(MAKE) iso EXTRA_CFLAGS=-DVOIDOS_TEST_EXCEPTION

test-page-fault:
	$(MAKE) clean
	$(MAKE) iso EXTRA_CFLAGS=-DVOIDOS_TEST_PAGE_FAULT

$(KERNEL): $(KERNEL_OBJECTS) linker/kernel.ld | $(BUILD_DIR)
	$(LD) $(LDFLAGS) $(KERNEL_OBJECTS) -o $@

$(BUILD_DIR)/kernel/%.o: kernel/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/%.o: kernel/%.S | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) -target x86_64-elf -m64 -c $< -o $@

$(ISO): $(KERNEL) boot/limine.conf
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL) $(ISO_ROOT)/kernel.elf
	cp boot/limine.conf $(ISO_ROOT)/limine.conf
	cp $(LIMINE_BIOS_SYS) $(ISO_ROOT)/limine-bios.sys
	cp $(LIMINE_BIOS_CD) $(ISO_ROOT)/limine-bios-cd.bin
	cp $(LIMINE_UEFI_CD) $(ISO_ROOT)/limine-uefi-cd.bin
	@if [ -n "$(LIMINE_BOOTX64)" ]; then cp "$(LIMINE_BOOTX64)" $(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI; fi
	@if [ -n "$(LIMINE_BOOTIA32)" ]; then cp "$(LIMINE_BOOTIA32)" $(ISO_ROOT)/EFI/BOOT/BOOTIA32.EFI; fi
	$(XORRISO) -as mkisofs \
		-R \
		-r \
		-J \
		-b limine-bios-cd.bin \
		-no-emul-boot \
		-boot-load-size 4 \
		-boot-info-table \
		-hfsplus \
		-apm-block-size 2048 \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part \
		--efi-boot-image \
		--protective-msdos-label \
		$(ISO_ROOT) \
		-o $(ISO)
	$(LIMINE) bios-install $(ISO)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

check-tools:
	@command -v $(CC) >/dev/null || { echo "Missing clang. Install with: brew install llvm"; exit 1; }
	@command -v $(LD) >/dev/null || { echo "Missing ld.lld. Install with: brew install lld"; exit 1; }
	@command -v $(QEMU) >/dev/null || { echo "Missing qemu-system-x86_64. Install with: brew install qemu"; exit 1; }
	@command -v $(XORRISO) >/dev/null || { echo "Missing xorriso. Install with: brew install xorriso"; exit 1; }
	@command -v $(LIMINE) >/dev/null || { echo "Missing limine. Install with: brew install limine"; exit 1; }
	@[ -f kernel/include/limine.h ] || { echo "Missing kernel/include/limine.h"; exit 1; }
	@[ -n "$(LIMINE_BIOS_SYS)" ] || { echo "Missing limine-bios.sys. Install with: brew install limine"; exit 1; }
	@[ -n "$(LIMINE_BIOS_CD)" ] || { echo "Missing limine-bios-cd.bin. Install with: brew install limine"; exit 1; }
	@[ -n "$(LIMINE_UEFI_CD)" ] || { echo "Missing limine-uefi-cd.bin. Install with: brew install limine"; exit 1; }
	@[ -n "$(LIMINE_BOOTX64)" ] || { echo "Missing Limine BOOTX64.EFI. Install with: brew install limine"; exit 1; }

check-uefi:
	@[ -n "$(OVMF_CODE)" ] || { \
		echo "Missing x86_64 UEFI firmware."; \
		echo "Expected one of:"; \
		echo "  $(QEMU_PREFIX)/share/qemu/edk2-x86_64-code.fd"; \
		echo "  $(BREW_PREFIX)/share/qemu/edk2-x86_64-code.fd"; \
		echo "Install or reinstall QEMU with: brew install qemu"; \
		exit 1; \
	}

clean:
	rm -rf $(BUILD_DIR)
