# BFME2 faction unlocker: dinput8.dll proxy.
# Run from a POSIX shell (Git Bash, MSYS2, Linux) with a 32-bit MinGW-w64 GCC:
#   make                  build/dinput8.dll
#   make test             site verification + engine and proxy tests
#   make dist             dist/ folder ready to copy into the game directory
#   make CROSS=i686-w64-mingw32-   when the compiler is prefixed (Linux cross)

CROSS    ?=
CC       := $(CROSS)gcc
WINDRES  := $(CROSS)windres
PYTHON   ?= python
GAME_DAT ?= C:/repositories/Open-BFME-2/baselines/bfme2/workshop-vanilla-1.06/files/game.dat
WB_EXE   ?= $(dir $(GAME_DAT))worldbuilder.exe
# ROTWK 2.01 baseline (the unprotected exe Age of the Ring ships) and Age of
# the Ring's own patched game.dat. Tests for them are skipped if absent.
ROTWK_DAT ?= C:/Jogos/AgeoftheRing/rotwk/game820.dat
AOTR_DAT  ?= C:/Jogos/AgeoftheRing/rotwk/game.dat
ENGINE_TEST_ARGS := --bfme2 "$(GAME_DAT)" --wb "$(WB_EXE)" \
    $(if $(wildcard $(ROTWK_DAT)),--rotwk "$(ROTWK_DAT)") \
    $(if $(wildcard $(AOTR_DAT)),--aotr "$(AOTR_DAT)")

BUILD := build
SRCS  := src/main.c src/unlocker.c src/engine.c src/patch_table.c src/gameversion.c \
         src/config.c src/log.c src/hooks.c src/sides.c src/skirmish.c src/network.c src/rt.c
OBJS  := $(SRCS:src/%.c=$(BUILD)/%.o) $(BUILD)/version.o

COMMON_FLAGS := -std=gnu11 -Wall -Wextra -Werror -DWIN32_LEAN_AND_MEAN -DUNICODE -D_UNICODE
# No C runtime in the DLL: nothing runs before or around DllMain but our code.
CFLAGS  := -O2 $(COMMON_FLAGS) -ffreestanding -fno-stack-protector \
           -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns
LDFLAGS := -shared -nostdlib -s -Wl,-e,_DllMain@12 -Wl,--enable-stdcall-fixup \
           -Wl,--dynamicbase -Wl,--nxcompat
LIBS    := -lkernel32 -lgcc

TEST_CFLAGS := -O1 $(COMMON_FLAGS) -Isrc -Itests

.PHONY: all test dist clean check-arch

all: check-arch $(BUILD)/dinput8.dll

check-arch:
	@case "$$($(CC) -dumpmachine)" in \
	  i686-*|i586-*|i386-*) ;; \
	  *) echo "error: need a 32-bit (i686) MinGW-w64 gcc, '$(CC)' targets $$($(CC) -dumpmachine)"; exit 1;; \
	esac

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: src/%.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/version.o: src/version.rc | $(BUILD)
	$(WINDRES) -O coff -o $@ $<

$(BUILD)/dinput8.dll: $(OBJS) src/dinput8.def
	$(CC) $(LDFLAGS) -o $@ $(OBJS) src/dinput8.def $(LIBS)

ENGINE_TEST_SRCS := tests/test_engine.c src/unlocker.c src/engine.c src/patch_table.c \
                    src/gameversion.c src/config.c src/log.c src/hooks.c src/sides.c \
                    src/skirmish.c src/network.c

$(BUILD)/test_engine.exe: $(ENGINE_TEST_SRCS) tests/baseline_expect.h src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -DFU_TESTING -static -o $@ $(ENGINE_TEST_SRCS)

HOOK_TEST_SRCS := tests/test_hooks.c src/engine.c src/patch_table.c src/log.c src/hooks.c \
                  src/sides.c src/skirmish.c src/network.c

$(BUILD)/test_hooks.exe: $(HOOK_TEST_SRCS) src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(HOOK_TEST_SRCS)

$(BUILD)/test_proxy.exe: tests/test_proxy.c | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $<

$(BUILD)/test_network.exe: tests/test_network.c src/engine.c src/patch_table.c src/hooks.c src/network.c src/log.c src/sides.c src/skirmish.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

test: all $(BUILD)/test_engine.exe $(BUILD)/test_hooks.exe $(BUILD)/test_proxy.exe $(BUILD)/test_network.exe
	$(PYTHON) ../tools/verify_sites.py
	./$(BUILD)/test_engine.exe $(ENGINE_TEST_ARGS)
	./$(BUILD)/test_hooks.exe "$(GAME_DAT)"
	./$(BUILD)/test_proxy.exe "$(BUILD)/dinput8.dll"
	./$(BUILD)/test_network.exe $(ENGINE_TEST_ARGS)

dist: all
	mkdir -p dist
	cp $(BUILD)/dinput8.dll dinput8.ini ../LICENSE dist/

clean:
	rm -rf $(BUILD) dist
