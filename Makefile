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

# --------------------------------------------------------------- userspace --

# User programs are freestanding like the kernel but run in ring 3:
# no kernel code model, and no SSE because the kernel does not save
# FPU/SSE state across context switches yet.
USER_CFLAGS := --target=x86_64-elf -std=c11 -ffreestanding -fno-builtin \
    -fno-stack-protector -fno-pic -mno-red-zone \
    -mno-mmx -mno-sse -mno-sse2 -mno-avx \
    -Wall -Wextra -Werror -O2 -g -MMD -MP

USER_C_SRCS  := $(shell find user -name '*.c' | sort)
USER_ELFS    := $(BUILD)/user/init.elf $(BUILD)/user/hello.elf
USER_LDFLAGS := -T user/user.ld -nostdlib -static -z max-page-size=0x1000 \
    --strip-debug

# Explicit rules, not patterns: the kernel's %.o: %.c pattern also
# matches these paths, and GNU make 3.81 (macOS) resolves pattern
# conflicts by order, not specificity.
$(BUILD)/user/init.o: user/init.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/user/hello.o: user/hello.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/user/crt0.o: user/crt0.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 -g -F dwarf $< -o $@

# --strip-debug keeps the embedded images lean; the objects keep
# their debug info for future symbolization.
$(BUILD)/user/init.elf: $(BUILD)/user/crt0.o $(BUILD)/user/init.o user/user.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(BUILD)/user/crt0.o $(BUILD)/user/init.o

$(BUILD)/user/hello.elf: $(BUILD)/user/crt0.o $(BUILD)/user/hello.o user/user.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(BUILD)/user/crt0.o $(BUILD)/user/hello.o

# The program images are embedded in kernel .rodata; the explicit
# rule overrides the kernel pattern rule so the object also depends
# on the images it incbins.
$(BUILD)/kernel/user_blob.o: kernel/user_blob.asm $(USER_ELFS)
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

# The filesystem disk, attached as a modern virtio-blk device (the
# boot disk stays on the BIOS/INT13 path). Scratch zeros until
# mkgraphfs.py lands in slice 5b.
FS_IMG_MIB := 16
$(BUILD)/fs.img:
	@mkdir -p $(BUILD)
	$(PY) -c "open('$@','wb').write(bytes($(FS_IMG_MIB)*1024*1024))"

QEMU_FLAGS := -m 256M -drive file=$(BUILD)/disk.img,format=raw \
    -drive file=$(BUILD)/fs.img,format=raw,if=none,id=fsdisk \
    -device virtio-blk-pci,drive=fsdisk,disable-legacy=on

.PHONY: run
run: $(BUILD)/disk.img $(BUILD)/fs.img
	$(QEMU) $(QEMU_FLAGS) -serial stdio

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
    tests/host/test_heap.c tests/host/test_sched.c tests/host/test_elf64.c \
    tests/host/test_proc.c tests/host/test_virtq.c \
    kernel/lib/string.c kernel/lib/fmt.c kernel/drivers/kbd_map.c \
    kernel/mm/pmm_core.c kernel/mm/heap_core.c kernel/sched/sched_core.c \
    kernel/lib/elf64.c kernel/proc/proc_core.c kernel/drivers/virtq_core.c

$(BUILD)/host_tests: $(HOST_TEST_SRCS) tests/host/test.h \
                     kernel/include/string.h kernel/include/fmt.h \
                     kernel/include/kbd_map.h kernel/include/pmm_core.h \
                     kernel/include/heap_core.h kernel/include/sched_core.h \
                     kernel/include/thread.h kernel/include/elf64.h \
                     kernel/include/proc_core.h kernel/include/virtq_core.h
	@mkdir -p $(BUILD)
	$(CC) $(HOST_CFLAGS) $(HOST_TEST_SRCS) -o $@

# ------------------------------------------------------------------ checks --

.PHONY: check check-host check-boot
check: check-host check-boot
	@echo "make check: all suites passed"

check-host: $(BUILD)/host_tests
	$(BUILD)/host_tests

check-boot: $(BUILD)/disk.img $(BUILD)/fs.img tests/run_qemu.py
	$(PY) tests/run_qemu.py --image $(BUILD)/disk.img --fsimg $(BUILD)/fs.img \
	    --qemu $(QEMU)

# ------------------------------------------------------------ code quality --

ALL_C_FILES := $(shell find kernel tests user -name '*.c' -o -name '*.h' | sort)

.PHONY: format format-check tidy
format:
	$(LLVM_BIN)/clang-format -i $(ALL_C_FILES)

format-check:
	$(LLVM_BIN)/clang-format --dry-run --Werror $(ALL_C_FILES)

tidy:
	$(LLVM_BIN)/clang-tidy --quiet $(KERNEL_C_SRCS) -- $(KERNEL_CFLAGS)
	$(LLVM_BIN)/clang-tidy --quiet $(USER_C_SRCS) -- $(USER_CFLAGS)

# ----------------------------------------------------------------- cleanup --

.PHONY: clean
clean:
	rm -rf $(BUILD)

-include $(KERNEL_OBJS:.o=.d) $(BUILD)/user/init.d $(BUILD)/user/hello.d
