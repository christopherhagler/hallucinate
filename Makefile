# Hallucinate OS — top-level build.
#
# Requirements (macOS): clang (Xcode CLT), nasm, ld.lld (brew install lld),
# qemu, python3. clang-format/clang-tidy come from `brew install llvm`.
#
# Primary targets:
#   all          build the bootable disk image (default)
#   run          boot the image in QEMU with a window + serial on stdio
#   check        host unit tests (ASan/UBSan) + QEMU boot integration test
#   format       apply clang-format to all C sources and headers
#   format-check fail if any file is not clang-format clean
#   tidy         run clang-tidy static analysis over the kernel
#   clean        remove the build tree

BUILD    := build
CC       := clang
NASM     := nasm
LD       := ld.lld
PY       := python3
QEMU     := qemu-system-x86_64
LLVM_BIN ?= /opt/homebrew/opt/llvm/bin

# ---------------------------------------------------------------- kernel --

KERNEL_C_SRCS   := $(shell find kernel -name '*.c' | sort)
KERNEL_ASM_SRCS := $(shell find kernel -name '*.asm' | sort)
KERNEL_OBJS     := $(patsubst %.c,$(BUILD)/%.o,$(KERNEL_C_SRCS)) \
                   $(patsubst %.asm,$(BUILD)/%.o,$(KERNEL_ASM_SRCS))

# Freestanding kernel: no host headers, no FP/SIMD register use, higher-half
# code model, red zone off (required once interrupts arrive).
KERNEL_CFLAGS := --target=x86_64-elf -std=c11 -ffreestanding -fno-builtin \
    -fno-stack-protector -fno-pic -mcmodel=kernel -mno-red-zone \
    -mno-mmx -mno-sse -mno-sse2 -mno-avx \
    -Wall -Wextra -Werror -O2 -g -MMD -MP \
    -Ikernel/include -Ikernel

.PHONY: all
all: $(BUILD)/disk.img

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD)/kernel/%.o: kernel/%.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 -g -F dwarf $< -o $@

# The embedded userspace init: user/init.asm -> flat binary -> kernel
# .rodata blob. The explicit rule overrides the pattern rule above so
# the object also depends on the binary it incbins.
$(BUILD)/user/init.bin: user/init.asm
	@mkdir -p $(dir $@)
	$(NASM) -f bin $< -o $@

$(BUILD)/kernel/user_blob.o: kernel/user_blob.asm $(BUILD)/user/init.bin
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 -g -F dwarf -i $(BUILD)/user/ $< -o $@

$(BUILD)/kernel.elf: $(KERNEL_OBJS) kernel/linker.ld
	$(LD) -T kernel/linker.ld -nostdlib -static -z max-page-size=0x1000 \
	    -o $@ $(KERNEL_OBJS)

# ------------------------------------------------------------- bootloader --

$(BUILD)/boot/%.bin: boot/%.asm
	@mkdir -p $(dir $@)
	$(NASM) -f bin $< -o $@

# ------------------------------------------------------------- disk image --

$(BUILD)/disk.img: $(BUILD)/boot/stage1.bin $(BUILD)/boot/stage2.bin \
                   $(BUILD)/kernel.elf tools/mkimage.py
	$(PY) tools/mkimage.py \
	    --stage1 $(BUILD)/boot/stage1.bin \
	    --stage2 $(BUILD)/boot/stage2.bin \
	    --kernel $(BUILD)/kernel.elf \
	    --out $@

.PHONY: run
run: $(BUILD)/disk.img
	$(QEMU) -m 256M -drive file=$<,format=raw -serial stdio

# -------------------------------------------------------------- host tests --

# The freestanding lib is compiled for the host with its public symbols
# renamed (memcpy -> hl_memcpy, ...) so they can never collide with libc or
# the sanitizer runtime; test sources get the same renames, so plain calls
# in test code resolve to the kernel implementations under test.
HOST_RENAMES := -Dmemcpy=hl_memcpy -Dmemmove=hl_memmove -Dmemset=hl_memset \
    -Dmemcmp=hl_memcmp -Dstrlen=hl_strlen -Dstrnlen=hl_strnlen \
    -Dstrcmp=hl_strcmp -Dstrncmp=hl_strncmp -Dvsnprintf=hl_vsnprintf \
    -Dsnprintf=hl_snprintf

HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -g -O1 \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    -fno-builtin -D_FORTIFY_SOURCE=0 \
    -Ikernel/include $(HOST_RENAMES)

HOST_TEST_SRCS := tests/host/test_main.c tests/host/test_string.c \
    tests/host/test_fmt.c tests/host/test_kbd.c tests/host/test_pmm.c \
    tests/host/test_heap.c tests/host/test_sched.c \
    kernel/lib/string.c kernel/lib/fmt.c kernel/drivers/kbd_map.c \
    kernel/mm/pmm_core.c kernel/mm/heap_core.c kernel/sched/sched_core.c

$(BUILD)/host_tests: $(HOST_TEST_SRCS) tests/host/test.h \
                     kernel/include/string.h kernel/include/fmt.h \
                     kernel/include/kbd_map.h kernel/include/pmm_core.h \
                     kernel/include/heap_core.h kernel/include/sched_core.h \
                     kernel/include/thread.h
	@mkdir -p $(BUILD)
	$(CC) $(HOST_CFLAGS) $(HOST_TEST_SRCS) -o $@

# ------------------------------------------------------------------ checks --

.PHONY: check check-host check-boot
check: check-host check-boot
	@echo "make check: all suites passed"

check-host: $(BUILD)/host_tests
	$(BUILD)/host_tests

check-boot: $(BUILD)/disk.img tests/run_qemu.py
	$(PY) tests/run_qemu.py --image $(BUILD)/disk.img --qemu $(QEMU)

# ------------------------------------------------------------ code quality --

ALL_C_FILES := $(shell find kernel tests -name '*.c' -o -name '*.h' | sort)

.PHONY: format format-check tidy
format:
	$(LLVM_BIN)/clang-format -i $(ALL_C_FILES)

format-check:
	$(LLVM_BIN)/clang-format --dry-run --Werror $(ALL_C_FILES)

tidy:
	$(LLVM_BIN)/clang-tidy --quiet $(KERNEL_C_SRCS) -- $(KERNEL_CFLAGS)

# ----------------------------------------------------------------- cleanup --

.PHONY: clean
clean:
	rm -rf $(BUILD)

-include $(KERNEL_OBJS:.o=.d)
