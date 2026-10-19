# bfme2perf: performance plugin for BFME2 / ROTWK, loaded by Miles from the
# game's mss\ folder.
# Run from a POSIX shell (Git Bash, MSYS2, Linux) with a 32-bit MinGW-w64 GCC:
#   make                  build/bfme2perf.asi
#   make test             row, config, site, hook and load tests
#   make dist             dist/ folder: copy its contents into the game's mss\
#   make CROSS=i686-w64-mingw32-   when the compiler is prefixed (Linux cross)

CROSS    ?=
CC       := $(CROSS)gcc
WINDRES  := $(CROSS)windres

BUILD := build
SRCS  := src/main.c src/log.c src/rt.c src/scan.c src/sites.c src/patch.c src/stubs.c \
         src/timing.c src/presentgap.c src/csvrow.c src/clock.c src/reflect.c src/config.c src/uistate.c \
         src/inst.c src/declcache.c src/fpsites.c src/fps.c src/pairpace.c src/extraprofile.c src/fpsmath.c src/btnclock.c src/passflag.c \
         src/shadowpass.c src/spsites.c src/fxguard.c src/catchup.c src/cusites.c src/notice.c \
         src/lights.c src/ltsites.c src/queueinsert.c src/nativecontract.c src/queuesort.c src/instsubmit.c src/bulk.c src/bulkcontract.c src/bulkpatch.c
OBJS  := $(SRCS:src/%.c=$(BUILD)/%.o) $(BUILD)/version.o

COMMON_FLAGS := -std=gnu11 -Wall -Wextra -Werror -DWIN32_LEAN_AND_MEAN -DUNICODE -D_UNICODE
# No C runtime in the DLL: nothing runs before or around DllMain but our code.
# The hook stubs run between a game function's return and its caller, so no
# x87 or SSE register may change: general registers only.
CFLAGS  := -O2 $(COMMON_FLAGS) -ffreestanding -fno-stack-protector \
           -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns \
           -mgeneral-regs-only
LDFLAGS := -shared -nostdlib -s -Wl,-e,_DllMain@12 -Wl,--enable-stdcall-fixup \
           -Wl,--dynamicbase -Wl,--nxcompat
LIBS    := -lkernel32 -lgcc

TEST_CFLAGS := -O1 $(COMMON_FLAGS) -Isrc -Itests

AOTR_DAT  ?= C:/Jogos/AgeoftheRing/rotwk/game.dat
ROTWK_DAT ?= C:/Jogos/AgeoftheRing/rotwk/game820.dat
SITE_TEST_ARGS := $(if $(wildcard $(AOTR_DAT)),--aotr "$(AOTR_DAT)") \
                  $(if $(wildcard $(ROTWK_DAT)),--rotwk "$(ROTWK_DAT)")

.PHONY: all test dist clean check-arch inst-gpu shadow-fx

all: check-arch $(BUILD)/bfme2perf.asi

check-arch:
	@case "$$($(CC) -dumpmachine)" in \
	  i686-*|i586-*|i386-*) ;; \
	  *) echo "error: need a 32-bit (i686) MinGW-w64 gcc, '$(CC)' targets $$($(CC) -dumpmachine)"; exit 1;; \
	esac

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: src/%.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# The one unit with floating point (see src/fpsmath.h): SSE scalar only, no
# vectorizing, no x87.
FPSMATH_CFLAGS := -O2 $(COMMON_FLAGS) -ffreestanding -fno-stack-protector \
                  -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns \
                  -msse2 -mfpmath=sse -fno-math-errno -fno-tree-vectorize -fno-tree-slp-vectorize

$(BUILD)/fpsmath.o: src/fpsmath.c src/*.h | $(BUILD)
	$(CC) $(FPSMATH_CFLAGS) -c -o $@ $<

$(BUILD)/version.o: src/version.rc | $(BUILD)
	$(WINDRES) -O coff -o $@ $<

$(BUILD)/bfme2perf.asi: $(OBJS) src/bfme2perf.def
	$(CC) $(LDFLAGS) -o $@ $(OBJS) src/bfme2perf.def $(LIBS)

$(BUILD)/test_load.exe: tests/test_load.c | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $<

# Native timing research only: no game entry, runtime hooks or GPU work.
$(BUILD)/test_native_rates.exe: tests/test_native_rates.c | $(BUILD)
	$(CC) $(TEST_CFLAGS) -msse2 -mfpmath=sse -static -o $@ $<

.PHONY: test-native-rates
test-native-rates: $(BUILD)/test_native_rates.exe
	python ../tools/test_native_rates.py --image "$(AOTR_DAT)" --image "$(ROTWK_DAT)" --executable $< --output $(BUILD)/native-rate-screen.json

$(BUILD)/test_native_durations.exe: tests/test_native_durations.c | $(BUILD)
	$(CC) $(TEST_CFLAGS) -msse2 -mfpmath=sse -static -o $@ $<

.PHONY: test-native-durations
test-native-durations: $(BUILD)/test_native_durations.exe
	python ../tools/test_native_durations.py --image "$(AOTR_DAT)" --image "$(ROTWK_DAT)" --executable $< --output $(BUILD)/native-duration-screen.json

# Offline CPU screen only; native bodies are read from local images at run time.
$(BUILD)/bench_native_decode.exe: tests/bench_native_decode.c tests/bench_guard.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -mgeneral-regs-only -fno-tree-loop-distribute-patterns -static -o $@ $<

.PHONY: test-native-decode bench-native-decode
test-native-decode: $(BUILD)/bench_native_decode.exe
	python ../tools/bench_native_decode.py "$(AOTR_DAT)" $< --self-test --samples 0
	python ../tools/bench_native_decode.py "$(ROTWK_DAT)" $< --self-test --samples 0

bench-native-decode: $(BUILD)/bench_native_decode.exe
	python ../tools/bench_native_decode.py "$(AOTR_DAT)" $< --self-test

CONTRACT_OBJS := $(BUILD)/nativecontract.o $(BUILD)/scan.o $(BUILD)/patch.o
$(BUILD)/test_instsubmit.exe: tests/test_instsubmit.c $(BUILD)/instsubmit.o src/instsubmit.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -static -o $@ $< $(BUILD)/instsubmit.o

.PHONY: test-instsubmit
test-instsubmit: $(BUILD)/test_instsubmit.exe
	./$(BUILD)/test_instsubmit.exe

$(BUILD)/test_inst_ownership.exe: tests/test_inst_ownership.c src/inst.c src/declcache.c $(BUILD)/instsubmit.o src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -mgeneral-regs-only -fno-tree-loop-distribute-patterns -static -o $@ $< src/declcache.c $(BUILD)/instsubmit.o

$(BUILD)/test_inst_verify_policy.exe: tests/test_inst_verify_policy.c tests/test_inst_ownership.c src/inst.c src/declcache.c $(BUILD)/instsubmit.o src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -mgeneral-regs-only -fno-tree-loop-distribute-patterns -static -o $@ $< src/declcache.c $(BUILD)/instsubmit.o

$(BUILD)/test_bulk_lifetime.exe: tests/test_bulk_lifetime.c tests/test_inst_ownership.c src/inst.c src/declcache.c $(BUILD)/instsubmit.o $(BUILD)/bulk.o src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -mgeneral-regs-only -fno-tree-loop-distribute-patterns -static -o $@ $< src/declcache.c $(BUILD)/instsubmit.o $(BUILD)/bulk.o

.PHONY: test-bulk-lifetime
test-bulk-lifetime: $(BUILD)/test_bulk_lifetime.exe
	./$(BUILD)/test_bulk_lifetime.exe

.PHONY: test-inst-ownership
test-inst-ownership: $(BUILD)/test_inst_ownership.exe
	./$(BUILD)/test_inst_ownership.exe

BULK_CONTRACT_OBJS := $(BUILD)/bulkcontract.o $(BUILD)/queueinsert.o $(BUILD)/queuesort.o $(CONTRACT_OBJS)
$(BUILD)/test_bulkcontract.exe: tests/test_bulkcontract.c tests/native_contract_image.h tests/bench_guard.h $(BULK_CONTRACT_OBJS) | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -mgeneral-regs-only -fno-tree-loop-distribute-patterns -static -o $@ $< $(BULK_CONTRACT_OBJS)

$(BUILD)/test_bulk_install.exe: tests/test_bulkcontract.c tests/native_contract_image.h tests/bench_guard.h $(filter-out $(BUILD)/main.o $(BUILD)/rt.o $(BUILD)/version.o,$(OBJS))
	$(CC) $(TEST_CFLAGS) -O2 -DPF_TEST_BULK_INSTALL -static -o $@ $< $(filter-out $(BUILD)/main.o $(BUILD)/rt.o $(BUILD)/version.o,$(OBJS))

.PHONY: test-bulk-install
test-bulk-install: $(BUILD)/test_bulk_install.exe
	python ../tools/test_bulkcontract.py "$(AOTR_DAT)" $<
	python ../tools/test_bulkcontract.py "$(ROTWK_DAT)" $<

.PHONY: test-bulkcontract
test-bulkcontract: $(BUILD)/test_bulkcontract.exe
	python ../tools/verify_sites.py --perf-bulk
	python ../tools/test_bulkcontract.py "$(AOTR_DAT)" $<
	python ../tools/test_bulkcontract.py "$(ROTWK_DAT)" $<

SORT_OBJS := $(BUILD)/queuesort.o $(CONTRACT_OBJS)
$(BUILD)/bench_native_sort.exe: tests/bench_native_sort.c tests/bench_guard.h $(SORT_OBJS) | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -mgeneral-regs-only -fno-tree-loop-distribute-patterns -static -o $@ $< $(SORT_OBJS)

.PHONY: test-native-sort bench-native-sort
test-native-sort: $(BUILD)/bench_native_sort.exe
	python ../tools/bench_native_sort.py "$(AOTR_DAT)" $< --self-test --samples 0
	python ../tools/bench_native_sort.py "$(ROTWK_DAT)" $< --self-test --samples 0

bench-native-sort: $(BUILD)/bench_native_sort.exe
	python ../tools/bench_native_sort.py "$(AOTR_DAT)" $< --self-test

$(BUILD)/test_queuesort.exe: tests/test_queuesort.c tests/bench_native_sort.c tests/bench_guard.h tests/native_contract_image.h $(SORT_OBJS) $(BUILD)/queueinsert.o | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -mgeneral-regs-only -fno-tree-loop-distribute-patterns -static -o $@ $< $(SORT_OBJS) $(BUILD)/queueinsert.o

.PHONY: test-queuesort
test-queuesort: $(BUILD)/test_queuesort.exe
	python ../tools/verify_sites.py --perf-sort
	python ../tools/test_queuesort.py "$(AOTR_DAT)" $<
	python ../tools/test_queuesort.py "$(ROTWK_DAT)" $<

QUEUE_OBJS := $(BUILD)/queueinsert.o $(CONTRACT_OBJS)
$(BUILD)/bench_native_insert.exe: tests/bench_native_insert.c tests/bench_guard.h $(QUEUE_OBJS) | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -mgeneral-regs-only -fno-tree-loop-distribute-patterns -static -o $@ $< $(QUEUE_OBJS)

.PHONY: test-native-insert bench-native-insert
test-native-insert: $(BUILD)/bench_native_insert.exe
	python ../tools/bench_native_insert.py "$(AOTR_DAT)" $< --self-test --samples 0
	python ../tools/bench_native_insert.py "$(ROTWK_DAT)" $< --self-test --samples 0

bench-native-insert: $(BUILD)/bench_native_insert.exe
	python ../tools/bench_native_insert.py "$(AOTR_DAT)" $< --self-test

$(BUILD)/test_queueinsert.exe: tests/test_queueinsert.c tests/bench_native_insert.c tests/bench_guard.h $(QUEUE_OBJS) | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -mgeneral-regs-only -fno-tree-loop-distribute-patterns -static -o $@ $< $(QUEUE_OBJS)

.PHONY: test-queueinsert
test-queueinsert: $(BUILD)/test_queueinsert.exe
	python ../tools/verify_sites.py --perf-queue
	python ../tools/test_queueinsert.py "$(AOTR_DAT)" $<
	python ../tools/test_queueinsert.py "$(ROTWK_DAT)" $<

$(BUILD)/test_sites.exe: tests/test_sites.c src/scan.c src/sites.c src/uistate.c src/fpsites.c \
                         src/btnclock.c src/passflag.c src/spsites.c src/cusites.c src/ltsites.c \
                         src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_config.exe: tests/test_config.c src/config.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_row.exe: tests/test_row.c src/csvrow.c src/clock.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_reflect.exe: tests/test_reflect.c src/reflect.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_btn.exe: tests/test_btn.c src/btnclock.c src/scan.c src/sites.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_hooks.exe: tests/test_hooks.c src/scan.c src/patch.c src/stubs.c src/timing.c \
                         src/presentgap.c src/csvrow.c src/clock.c src/reflect.c src/uistate.c src/fps.c src/pairpace.c src/extraprofile.c src/log.c \
                         src/fpsmath.c src/btnclock.c src/shadowpass.c src/catchup.c src/notice.c \
                         src/lights.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_fps.exe: tests/test_fps.c src/fps.c src/pairpace.c src/uistate.c src/extraprofile.c src/stubs.c src/log.c src/fpsmath.c \
                       src/btnclock.c src/scan.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

# Controlled-cost policy screen; it does not execute the runtime adapter.
$(BUILD)/test_pairpace.exe: tests/test_pairpace.c $(BUILD)/pairpace.o src/fps.c src/uistate.c src/extraprofile.c src/stubs.c src/log.c src/fpsmath.c \
                            src/btnclock.c src/scan.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c %.o,$^)

.PHONY: test-pairpace
test-pairpace: $(BUILD)/test_pairpace.exe
	python ../tools/test_pairpace.py --executable $< --output $(BUILD)/pairpace-screen.json

$(BUILD)/test_pair_sequential.exe: tests/test_pair_sequential.c $(BUILD)/pairpace.o src/pairpace.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c %.o,$^)

.PHONY: test-pair-sequential
test-pair-sequential: $(BUILD)/test_pair_sequential.exe
	./$(BUILD)/test_pair_sequential.exe

PAIR_RUNTIME_OBJS := $(addprefix $(BUILD)/,fps.o pairpace.o uistate.o extraprofile.o stubs.o log.o \
                                         fpsmath.o btnclock.o scan.o catchup.o notice.o)
$(BUILD)/test_pair_runtime.exe: tests/test_pair_runtime.c tests/test_fps.c $(PAIR_RUNTIME_OBJS) src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $< $(PAIR_RUNTIME_OBJS)

.PHONY: test-pair-runtime
test-pair-runtime: $(BUILD)/test_pair_runtime.exe
	./$(BUILD)/test_pair_runtime.exe

$(BUILD)/test_extraprofile.exe: tests/test_extraprofile.c src/extraprofile.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_declcache.exe: tests/test_declcache.c src/declcache.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_inst.exe: tests/test_inst.c src/inst.c src/instsubmit.c src/declcache.c src/log.c src/scan.c src/patch.c src/stubs.c \
                        src/timing.c src/presentgap.c src/csvrow.c src/clock.c src/reflect.c src/uistate.c \
                        src/fps.c src/pairpace.c src/extraprofile.c src/fpsmath.c src/btnclock.c src/shadowpass.c src/catchup.c \
                        src/notice.c src/lights.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_catchup.exe: tests/test_catchup.c src/catchup.c src/notice.c src/log.c src/uistate.c \
                           src/scan.c src/patch.c src/stubs.c src/timing.c src/presentgap.c src/csvrow.c src/clock.c \
                           src/reflect.c src/fps.c src/pairpace.c src/extraprofile.c src/fpsmath.c src/btnclock.c src/shadowpass.c \
                           src/lights.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_shadowpass.exe: tests/test_shadowpass.c src/shadowpass.c src/fxguard.c src/spsites.c \
                              src/scan.c src/fps.c src/pairpace.c src/uistate.c src/extraprofile.c src/stubs.c src/log.c src/fpsmath.c \
                              src/btnclock.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_lights.exe: tests/test_lights.c src/lights.c src/shadowpass.c src/stubs.c src/log.c \
                          src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

test: all test-bulk-lifetime test-bulk-install $(BUILD)/test_row.exe $(BUILD)/test_reflect.exe $(BUILD)/test_config.exe \
      $(BUILD)/test_sites.exe $(BUILD)/test_hooks.exe $(BUILD)/test_fps.exe $(BUILD)/test_load.exe \
      $(BUILD)/test_inst.exe $(BUILD)/test_declcache.exe $(BUILD)/test_btn.exe $(BUILD)/test_shadowpass.exe \
      $(BUILD)/test_catchup.exe $(BUILD)/test_lights.exe $(BUILD)/test_extraprofile.exe $(BUILD)/test_presentgap.exe \
      $(BUILD)/test_queueinsert.exe $(BUILD)/test_queuesort.exe $(BUILD)/test_inst_ownership.exe $(BUILD)/test_instsubmit.exe \
      $(BUILD)/test_inst_verify_policy.exe $(BUILD)/test_pair_runtime.exe $(BUILD)/test_pair_sequential.exe
	./$(BUILD)/test_row.exe
	./$(BUILD)/test_reflect.exe
	./$(BUILD)/test_btn.exe
	./$(BUILD)/test_config.exe
	@if [ -f "$(AOTR_DAT)" ] && [ -f "$(ROTWK_DAT)" ]; then \
	  AOTR_DAT="$(AOTR_DAT)" ROTWK_DAT="$(ROTWK_DAT)" python ../tools/verify_sites.py --perf-queue; fi
	@if [ -f "$(AOTR_DAT)" ]; then python ../tools/test_queueinsert.py "$(AOTR_DAT)" $(BUILD)/test_queueinsert.exe; fi
	@if [ -f "$(ROTWK_DAT)" ]; then python ../tools/test_queueinsert.py "$(ROTWK_DAT)" $(BUILD)/test_queueinsert.exe; fi
	@if [ -f "$(AOTR_DAT)" ] && [ -f "$(ROTWK_DAT)" ]; then \
	  AOTR_DAT="$(AOTR_DAT)" ROTWK_DAT="$(ROTWK_DAT)" python ../tools/verify_sites.py --perf-sort; fi
	@if [ -f "$(AOTR_DAT)" ]; then python ../tools/test_queuesort.py "$(AOTR_DAT)" $(BUILD)/test_queuesort.exe; fi
	@if [ -f "$(ROTWK_DAT)" ]; then python ../tools/test_queuesort.py "$(ROTWK_DAT)" $(BUILD)/test_queuesort.exe; fi
	./$(BUILD)/test_sites.exe $(SITE_TEST_ARGS)
	./$(BUILD)/test_hooks.exe
	./$(BUILD)/test_fps.exe
	./$(BUILD)/test_pair_runtime.exe
	./$(BUILD)/test_load.exe "$(BUILD)/bfme2perf.asi"
	./$(BUILD)/test_pair_sequential.exe
	./$(BUILD)/test_presentgap.exe
	./$(BUILD)/test_extraprofile.exe
	./$(BUILD)/test_declcache.exe
	./$(BUILD)/test_inst.exe
	./$(BUILD)/test_inst_ownership.exe
	./$(BUILD)/test_instsubmit.exe
	./$(BUILD)/test_inst_verify_policy.exe
	./$(BUILD)/test_shadowpass.exe
	./$(BUILD)/test_catchup.exe
	./$(BUILD)/test_lights.exe

# Off-line GPU check of the instanced shaders against the game's (needs a GPU and
# the game's Shaders.big; DXVK's d3d9.dll is used for the second run when present).
DXVK_D3D9 ?= C:/Jogos/AgeoftheRing/rotwk/d3d9.dll

$(BUILD)/inst_gpu/inst_gpu.exe: tests/inst_gpu/harness.c src/inst_shaders.h src/depthcmp.h \
                                | $(BUILD)
	mkdir -p $(BUILD)/inst_gpu
	$(CC) -O1 -std=gnu11 -Wall -Wextra -Werror -Isrc -static -o $@ $<

inst-gpu: $(BUILD)/inst_gpu/inst_gpu.exe
	./$(BUILD)/inst_gpu/inst_gpu.exe
	if [ -f "$(DXVK_D3D9)" ]; then \
	  mkdir -p $(BUILD)/inst_gpu_dxvk && cp $(BUILD)/inst_gpu/inst_gpu.exe "$(DXVK_D3D9)" $(BUILD)/inst_gpu_dxvk/ && \
	  (cd $(BUILD)/inst_gpu_dxvk && DXVK_LOG_LEVEL=none ./inst_gpu.exe); fi

# Off-line proof that no effect's _CreateShadowMap technique reads the light
# environment or the fog colour (ShadowBatchIgnoreLight); needs a GPU, the
# game's Shaders.big and d3dx9_43.dll.
$(BUILD)/shadow_fx/shadow_fx.exe: tests/shadow_fx/shadow_fx.c src/shadowfx_table.h | $(BUILD)
	mkdir -p $(BUILD)/shadow_fx
	$(CC) -O1 -std=gnu11 -Wall -Wextra -Werror -Isrc -static -o $@ $<

shadow-fx: $(BUILD)/shadow_fx/shadow_fx.exe
	./$(BUILD)/shadow_fx/shadow_fx.exe
	if [ -f "$(DXVK_D3D9)" ]; then \
	  mkdir -p $(BUILD)/shadow_fx_dxvk && cp $(BUILD)/shadow_fx/shadow_fx.exe "$(DXVK_D3D9)" $(BUILD)/shadow_fx_dxvk/ && \
	  (cd $(BUILD)/shadow_fx_dxvk && DXVK_LOG_LEVEL=none ./shadow_fx.exe); fi

dist: all
	mkdir -p dist
	cp $(BUILD)/bfme2perf.asi bfme2perf.ini dist/

clean:
	rm -rf $(BUILD) dist

$(BUILD)/test_presentgap.exe: tests/test_presentgap.c src/presentgap.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

# Offline dispatch experiment. Production code uses the ASI's optimization
# and general-register flags; the GPU fixture needs floats and uses -O2.
$(BUILD)/inst_dispatch_engine.o: tests/inst_dispatch_engine.c src/inst.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

CHECKED_WRAP := -Wl,--wrap=pf_submit_batch -Wl,--wrap=pf_submit_pending
DISPATCH_OBJS := $(filter-out $(BUILD)/main.o $(BUILD)/rt.o $(BUILD)/inst.o $(BUILD)/version.o,$(OBJS))
$(BUILD)/test_inst_ownership_gpu.exe: tests/test_inst_ownership_gpu.c tests/test_inst.c tests/bench_guard.h $(BUILD)/inst.o $(DISPATCH_OBJS) | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -static -o $@ $< $(BUILD)/inst.o $(DISPATCH_OBJS)

.PHONY: test-inst-ownership-gpu
test-inst-ownership-gpu: $(BUILD)/test_inst_ownership_gpu.exe
	./$(BUILD)/test_inst_ownership_gpu.exe "$(DISPATCH_ASSETS)/Shaders.big"
	@test -f "$(DXVK_D3D9)" || { echo "error: test-inst-ownership-gpu requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/inst_ownership_dxvk
	cp $(BUILD)/test_inst_ownership_gpu.exe "$(DXVK_D3D9)" $(BUILD)/inst_ownership_dxvk/
	cd $(BUILD)/inst_ownership_dxvk && DXVK_LOG_LEVEL=none ./test_inst_ownership_gpu.exe "$(DISPATCH_ASSETS)/Shaders.big"

$(BUILD)/bench_dispatch.exe: tests/bench_dispatch.c tests/test_inst.c $(BUILD)/inst_dispatch_engine.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -static -o $@ $< $(BUILD)/inst_dispatch_engine.o $(DISPATCH_OBJS)

.PHONY: bench-dispatch bench-ambient bench-retained bench-material-queue test-fx-lifecycle bench-scopes

.PHONY: test-native-events
.PHONY: test-native-group
.PHONY: test-native-mesh
$(BUILD)/test_native_mesh.exe: tests/test_native_mesh.c tests/native_mesh_image.h src/stubs.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -mgeneral-regs-only -static -o $@ $(filter %.c,$^)

test-native-mesh: $(BUILD)/test_native_mesh.exe
	python ../tools/test_mesh_draw_contract.py "$(AOTR_DAT)" --self-test --execute
	python ../tools/test_mesh_draw_contract.py "$(ROTWK_DAT)" --self-test --execute

$(BUILD)/test_native_group.exe: tests/test_native_group.c tests/native_group_image.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -mgeneral-regs-only -static -o $@ $<

test-native-group: $(BUILD)/test_native_group.exe
	python ../tools/test_group_contract.py "$(AOTR_DAT)" --self-test --execute
	python ../tools/test_group_contract.py "$(ROTWK_DAT)" --self-test --execute

$(BUILD)/test_native_events.exe: tests/test_native_events.c | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $<

test-native-events: $(BUILD)/test_native_events.exe
	python ../tools/test_event_contract.py "$(AOTR_DAT)" --self-test --execute
	python ../tools/test_event_contract.py "$(ROTWK_DAT)" --self-test --execute

# Native wrapper correctness only: no GPU, scene, startup or timing claims.
$(BUILD)/test_fx_lifecycle.exe: tests/test_fx_lifecycle.c | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $<

test-fx-lifecycle: $(BUILD)/test_fx_lifecycle.exe
	python ../tools/test_fx_contract.py
	python ../tools/test_fx_lifecycle.py "$(AOTR_DAT)"
	python ../tools/test_fx_lifecycle.py "$(ROTWK_DAT)"
DISPATCH_ASSETS ?= C:/Jogos/AgeoftheRing/rotwk

$(BUILD)/bench_effect.exe: tests/bench_effect.c tests/effect_source_audit.h tests/bench_guard.h tests/shadow_fx/shadow_fx.c src/shadowfx_table.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -static -o $@ $<

$(BUILD)/bench_typed_effect.exe: tests/bench_typed_effect.c tests/bench_effect.c tests/effect_source_audit.h tests/bench_guard.h tests/shadow_fx/shadow_fx.c src/shadowfx_table.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -static -o $@ $<

$(BUILD)/bench_external_rt.exe: tests/bench_external_rt.c tests/bench_effect.c tests/effect_source_audit.h tests/bench_guard.h tests/shadow_fx/shadow_fx.c src/shadowfx_table.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $<

$(BUILD)/inst_retained_engine.o: tests/inst_retained_engine.c tests/inst_dispatch_engine.c src/inst.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/fps_extra_bench.o: tests/fps_extra_bench.c src/fps.c src/pairpace.c src/uistate.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/bench_scopes.exe: tests/bench_scopes.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/inst_dispatch_engine.o $(BUILD)/fps_extra_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/inst_dispatch_engine.o $(BUILD)/fps_extra_bench.o $(filter-out $(BUILD)/fps.o,$(DISPATCH_OBJS))

bench-scopes: $(BUILD)/bench_scopes.exe
	./$(BUILD)/bench_scopes.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll"

$(BUILD)/bench_retained.exe: tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(filter-out $(BUILD)/fps.o,$(DISPATCH_OBJS))

$(BUILD)/bench_retained_multi.exe: tests/bench_retained_multi.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(filter-out $(BUILD)/fps.o,$(DISPATCH_OBJS))

.PHONY: bench-retained-multi
bench-retained-multi: $(BUILD)/bench_retained_multi.exe
	./$(BUILD)/bench_retained_multi.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit
	./$(BUILD)/bench_retained_multi.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow
	@test -f "$(DXVK_D3D9)" || { echo "error: bench-retained-multi requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/retained_multi_dxvk
	cp $(BUILD)/bench_retained_multi.exe "$(DXVK_D3D9)" $(BUILD)/retained_multi_dxvk/
	cd $(BUILD)/retained_multi_dxvk && \
	  DXVK_LOG_LEVEL=none ./bench_retained_multi.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit && \
	  DXVK_LOG_LEVEL=none ./bench_retained_multi.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow

$(BUILD)/bench_ui_upload.exe: tests/bench_ui_upload.c tests/bench_guard.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -msse2 -mfpmath=sse -static -o $@ $< -luser32 -lgdi32

.PHONY: bench-ui-upload
bench-ui-upload: $(BUILD)/bench_ui_upload.exe
	./$(BUILD)/bench_ui_upload.exe native
	@test -f "$(DXVK_D3D9)" || { echo "error: bench-ui-upload requires DXVK_D3D9"; exit 1; }
	DXVK_LOG_LEVEL=none ./$(BUILD)/bench_ui_upload.exe "$(DXVK_D3D9)"

$(BUILD)/material_fixture.o: tests/material_fixture.c tests/material_packet.h tests/source_packet.h tests/effect_filter_bench.h tests/shadow_fx/shadow_fx.c src/shadowfx_table.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -c -o $@ $<

$(BUILD)/effect_filter_bench.o: tests/effect_filter_bench.c tests/effect_filter_bench.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/timing_bulk_bench.o: tests/timing_bulk_bench.c src/timing.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/bench_material_queue.exe: tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

$(BUILD)/bench_material_multi.exe: tests/bench_material_multi.c tests/bench_retained_multi.c tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

$(BUILD)/palette_texture_shaders.h: ../tools/palette_texture_shaders.py ../tools/inst_shaders.py shaders/inst.hlsl | $(BUILD)
	python ../tools/palette_texture_shaders.py --big "$(DISPATCH_ASSETS)/Shaders.big" --output $@

$(BUILD)/bench_palette_texture_6.exe $(BUILD)/bench_palette_texture_20.exe: tests/bench_palette_texture.c $(BUILD)/palette_texture_shaders.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -I$(BUILD) -O2 -Wno-unused-function -DPF_FIXTURE_BONES=$(if $(findstring _20.exe,$@),20,6) -static -o $@ $< $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

.PHONY: bench-palette-texture
bench-palette-texture: $(BUILD)/bench_palette_texture_6.exe $(BUILD)/bench_palette_texture_20.exe
	@for bones in 6 20; do for mode in lit shadow; do \
	  ./$(BUILD)/bench_palette_texture_$$bones.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" $$mode || exit $$?; \
	done; done
	@test -f "$(DXVK_D3D9)" || { echo "error: bench-palette-texture requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/palette_texture_dxvk
	cp $(BUILD)/bench_palette_texture_6.exe $(BUILD)/bench_palette_texture_20.exe "$(DXVK_D3D9)" $(BUILD)/palette_texture_dxvk/
	@cd $(BUILD)/palette_texture_dxvk && for bones in 6 20; do for mode in lit shadow; do \
	  DXVK_LOG_LEVEL=none ./bench_palette_texture_$$bones.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" $$mode || exit $$?; \
	done; done

.PHONY: bench-material-multi
bench-material-multi: $(BUILD)/bench_material_multi.exe
	./$(BUILD)/bench_material_multi.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit
	./$(BUILD)/bench_material_multi.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow
	@test -f "$(DXVK_D3D9)" || { echo "error: bench-material-multi requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/material_multi_dxvk
	cp $(BUILD)/bench_material_multi.exe "$(DXVK_D3D9)" $(BUILD)/material_multi_dxvk/
	cd $(BUILD)/material_multi_dxvk && \
	  DXVK_LOG_LEVEL=none ./bench_material_multi.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit && \
	  DXVK_LOG_LEVEL=none ./bench_material_multi.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow

$(BUILD)/bench_external_draw.exe: tests/bench_external_draw.c tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

$(BUILD)/bench_external_barriers.exe $(BUILD)/bench_external_readbacks.exe: $(BUILD)/%.exe: tests/%.c tests/bench_external_draw.c tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

$(BUILD)/bench_batch_worker.exe: tests/bench_batch_worker.c tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

$(BUILD)/native_source_capture.o: tests/native_source_capture.c tests/source_packet.h tests/bench_native_matrix_sources.c | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -c -o $@ $<

$(BUILD)/native_pose_capture.o: tests/native_pose_capture.c tests/pose_packet.h tests/bench_native_decode.c | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -mgeneral-regs-only -c -o $@ $<

$(BUILD)/bench_native_pose_worker.exe: tests/bench_batch_worker.c tests/source_packet.h tests/pose_packet.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_pose_capture.o $(BUILD)/native_source_capture.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_NATIVE_SOURCE_WORKER -DPF_NATIVE_POSE_WORKER -static -o $@ $< $(BUILD)/native_pose_capture.o $(BUILD)/native_source_capture.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

$(BUILD)/bench_window_pose_worker.exe: tests/bench_batch_worker.c tests/material_packet.h tests/source_packet.h tests/pose_packet.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_pose_capture.o $(BUILD)/native_source_capture.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_NATIVE_SOURCE_WORKER -DPF_NATIVE_POSE_WORKER -DPF_WINDOW_DEVICE_OWNER -static -o $@ $< $(BUILD)/native_pose_capture.o $(BUILD)/native_source_capture.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

$(BUILD)/bench_window_reset_worker.exe: tests/bench_worker_reset.c tests/bench_batch_worker.c tests/material_packet.h tests/source_packet.h tests/pose_packet.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_pose_capture.o $(BUILD)/native_source_capture.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_NATIVE_SOURCE_WORKER -DPF_NATIVE_POSE_WORKER -DPF_WINDOW_DEVICE_OWNER -static -o $@ $< $(BUILD)/native_pose_capture.o $(BUILD)/native_source_capture.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

$(BUILD)/bench_packet_consumer.exe: tests/packet_consumer.h tests/bench_worker_reset.c tests/bench_batch_worker.c tests/material_packet.h tests/source_packet.h tests/pose_packet.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_pose_capture.o $(BUILD)/native_source_capture.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_NATIVE_SOURCE_WORKER -DPF_NATIVE_POSE_WORKER -DPF_WINDOW_DEVICE_OWNER -DPF_PACKET_CONSUMER -static -o $@ tests/bench_worker_reset.c $(BUILD)/native_pose_capture.o $(BUILD)/native_source_capture.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

$(BUILD)/bench_native_source_worker.exe: tests/bench_batch_worker.c tests/source_packet.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_source_capture.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_NATIVE_SOURCE_WORKER -static -o $@ $< $(BUILD)/native_source_capture.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

RETAINED_MASK ?= narrow
MATERIAL_CHURN ?=
MATERIAL_PROTOCOL ?=
$(BUILD)/inst_leader_engine.o: tests/inst_leader_engine.c tests/inst_retained_engine.c tests/inst_dispatch_engine.c src/inst.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/bench_leader.exe: tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_leader_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_BENCH_LEADER -static -o $@ $< $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_leader_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

LEADER_PROTOCOL ?= leader
.PHONY: bench-native-leader
.PHONY: bench-native-group-gpu
.PHONY: test-native-gpu-faults
.PHONY: test-native-gpu-recovery bench-native-gpu-recovery
.PHONY: bench-native-fx-gpu test-native-fx-state test-native-fx-recovery
test-native-fx-state:
	python ../tools/native_fx_state_contract.py "$(AOTR_DAT)" --self-test
	python ../tools/native_fx_state_contract.py "$(ROTWK_DAT)" --self-test

$(BUILD)/test_native_fx_recovery.exe: tests/test_native_fx_recovery.c tests/test_native_gpu_faults.c tests/bench_native_fx_gpu.c tests/native_fx_image.h tests/bench_native_group_gpu.c tests/bench_native_leader.c tests/native_group_image.h tests/native_mesh_image.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_recovery_fault_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_recovery_fault_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS)) $(CHECKED_WRAP)

test-native-fx-recovery: $(BUILD)/test_native_fx_recovery.exe
	python ../tools/bench_native_fx_gpu.py --self-test "$(AOTR_DAT)" $(BUILD)/test_native_fx_recovery.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first
	python ../tools/bench_native_fx_gpu.py --self-test "$(AOTR_DAT)" $(BUILD)/test_native_fx_recovery.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first
	@test -f "$(DXVK_D3D9)" || { echo "error: test-native-fx-recovery requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/native_fx_recovery_dxvk
	cp $(BUILD)/test_native_fx_recovery.exe "$(DXVK_D3D9)" $(BUILD)/native_fx_recovery_dxvk/
	cd $(BUILD)/native_fx_recovery_dxvk && \
	  DXVK_LOG_LEVEL=none python ../../../tools/bench_native_fx_gpu.py --self-test "$(AOTR_DAT)" test_native_fx_recovery.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first && \
	  DXVK_LOG_LEVEL=none python ../../../tools/bench_native_fx_gpu.py --self-test "$(AOTR_DAT)" test_native_fx_recovery.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first

$(BUILD)/inst_native_fallback_engine.o: tests/inst_native_fallback_engine.c tests/inst_recovery_fault_engine.c tests/inst_recovery_engine.c tests/inst_gpu_fault_engine.c tests/inst_native_leader_engine.c tests/inst_retained_engine.c tests/inst_dispatch_engine.c $(BUILD)/inst_recovery_base.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -Itests -Wno-unused-function -c -o $@ $<

.PHONY: test-native-group-fallback
$(BUILD)/test_native_group_fallback.exe: tests/test_native_group_fallback.c tests/test_native_gpu_faults.c tests/bench_native_fx_gpu.c tests/native_fx_image.h tests/bench_native_group_gpu.c tests/bench_native_leader.c tests/native_group_image.h tests/native_mesh_image.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_native_fallback_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_native_fallback_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS)) $(CHECKED_WRAP)

test-native-group-fallback: $(BUILD)/test_native_group_fallback.exe
	python ../tools/test_native_group_fallback.py --self-test "$(AOTR_DAT)" $(BUILD)/test_native_group_fallback.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first
	python ../tools/test_native_group_fallback.py --self-test "$(AOTR_DAT)" $(BUILD)/test_native_group_fallback.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first
	@test -f "$(DXVK_D3D9)" || { echo "error: test-native-group-fallback requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/native_group_fallback_dxvk
	cp $(BUILD)/test_native_group_fallback.exe "$(DXVK_D3D9)" $(BUILD)/native_group_fallback_dxvk/
	cd $(BUILD)/native_group_fallback_dxvk && \
	  DXVK_LOG_LEVEL=none python ../../../tools/test_native_group_fallback.py --self-test "$(AOTR_DAT)" test_native_group_fallback.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first && \
	  DXVK_LOG_LEVEL=none python ../../../tools/test_native_group_fallback.py --self-test "$(AOTR_DAT)" test_native_group_fallback.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first

$(BUILD)/inst_runtime_fallback_engine.o: tests/inst_native_fallback_engine.c tests/inst_recovery_fault_engine.c tests/inst_recovery_engine.c tests/inst_gpu_fault_engine.c tests/inst_native_leader_engine.c tests/inst_retained_engine.c tests/inst_dispatch_engine.c $(BUILD)/inst_recovery_base.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -DPF_RUNTIME_CANCEL_WRAP -Isrc -Itests -Wno-unused-function -c -o $@ $<

.PHONY: test-runtime-group-fallback
$(BUILD)/test_runtime_group_fallback.exe: tests/test_native_group_fallback.c tests/test_native_gpu_faults.c tests/bench_native_fx_gpu.c tests/native_fx_image.h tests/bench_native_group_gpu.c tests/bench_native_leader.c tests/native_group_image.h tests/native_mesh_image.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_runtime_fallback_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_RUNTIME_BULK -static -o $@ $< $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_runtime_fallback_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS)) $(CHECKED_WRAP) -Wl,--wrap=pf_inst_bulk_cancel

test-runtime-group-fallback: $(BUILD)/test_runtime_group_fallback.exe
	python ../tools/test_native_group_fallback.py --self-test "$(AOTR_DAT)" $(BUILD)/test_runtime_group_fallback.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first
	python ../tools/test_native_group_fallback.py --self-test "$(AOTR_DAT)" $(BUILD)/test_runtime_group_fallback.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first
	@test -f "$(DXVK_D3D9)" || { echo "error: test-runtime-group-fallback requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/runtime_group_fallback_dxvk
	cp $(BUILD)/test_runtime_group_fallback.exe "$(DXVK_D3D9)" $(BUILD)/runtime_group_fallback_dxvk/
	cd $(BUILD)/runtime_group_fallback_dxvk && \
	  DXVK_LOG_LEVEL=none python ../../../tools/test_native_group_fallback.py --self-test "$(AOTR_DAT)" test_runtime_group_fallback.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first && \
	  DXVK_LOG_LEVEL=none python ../../../tools/test_native_group_fallback.py --self-test "$(AOTR_DAT)" test_runtime_group_fallback.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first

$(BUILD)/bench_native_fx_gpu.exe: tests/bench_native_fx_gpu.c tests/native_fx_image.h tests/bench_native_group_gpu.c tests/bench_native_leader.c tests/native_group_image.h tests/native_mesh_image.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_recovery_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_recovery_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS)) $(CHECKED_WRAP)

$(BUILD)/native_sort_fixture.o: tests/bench_native_sort.c tests/bench_guard.h src/queuesort.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -mgeneral-regs-only -fno-tree-loop-distribute-patterns -Wno-unused-function -DPF_NATIVE_SORT_EMBED -c -o $@ $<

$(BUILD)/bench_native_queue_gpu.exe: tests/bench_native_queue_gpu.c tests/native_shared_arena.h tests/bench_native_fx_gpu.c tests/native_fx_image.h tests/bench_native_group_gpu.c tests/bench_native_leader.c tests/native_group_image.h tests/native_mesh_image.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_sort_fixture.o $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_recovery_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/native_sort_fixture.o $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_recovery_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS)) $(CHECKED_WRAP)

QUEUE_MODE ?= combined
QUEUE_ORDER ?= random
QUEUE_OPTIONS ?=
.PHONY: test-native-queue-gpu bench-native-queue-gpu
test-native-queue-gpu: $(BUILD)/bench_native_queue_gpu.exe
	$(MAKE) bench-native-queue-gpu QUEUE_OPTIONS="--controls-only --self-test"

bench-native-queue-gpu: $(BUILD)/bench_native_queue_gpu.exe
	python ../tools/bench_native_queue_gpu.py $(QUEUE_OPTIONS) --mode $(QUEUE_MODE) --order $(QUEUE_ORDER) "$(AOTR_DAT)" $< "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first
	python ../tools/bench_native_queue_gpu.py $(QUEUE_OPTIONS) --mode $(QUEUE_MODE) --order $(QUEUE_ORDER) "$(AOTR_DAT)" $< "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first
	@test -f "$(DXVK_D3D9)" || { echo "error: bench-native-queue-gpu requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/native_queue_gpu_dxvk
	cp $< "$(DXVK_D3D9)" $(BUILD)/native_queue_gpu_dxvk/
	cd $(BUILD)/native_queue_gpu_dxvk && \
	  DXVK_LOG_LEVEL=none python ../../../tools/bench_native_queue_gpu.py $(QUEUE_OPTIONS) --mode $(QUEUE_MODE) --order $(QUEUE_ORDER) "$(AOTR_DAT)" bench_native_queue_gpu.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first && \
	  DXVK_LOG_LEVEL=none python ../../../tools/bench_native_queue_gpu.py $(QUEUE_OPTIONS) --mode $(QUEUE_MODE) --order $(QUEUE_ORDER) "$(AOTR_DAT)" bench_native_queue_gpu.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first

$(BUILD)/native_insert_fixture.o: tests/bench_native_insert.c tests/bench_guard.h src/queueinsert.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -mgeneral-regs-only -fno-tree-loop-distribute-patterns -Wno-unused-function -DPF_NATIVE_INSERT_EMBED -c -o $@ $<

$(BUILD)/bench_queue_cleanup.exe: tests/bench_queue_cleanup.c tests/bench_native_insert.c tests/bench_guard.h $(BUILD)/queueinsert.o $(BUILD)/nativecontract.o $(BUILD)/scan.o $(BUILD)/patch.o $(BUILD)/sites.o
	$(CC) $(TEST_CFLAGS) -O2 -mgeneral-regs-only -Wno-unused-function -static -o $@ $< $(BUILD)/queueinsert.o $(BUILD)/nativecontract.o $(BUILD)/scan.o $(BUILD)/patch.o $(BUILD)/sites.o

.PHONY: bench-queue-cleanup
bench-queue-cleanup: $(BUILD)/bench_queue_cleanup.exe
	python ../tools/bench_queue_cleanup.py "$(AOTR_DAT)" $<

$(BUILD)/bench_native_insert_gpu.exe: tests/bench_native_insert_gpu.c tests/bench_native_queue_gpu.c tests/native_shared_arena.h tests/bench_native_fx_gpu.c tests/native_fx_image.h tests/bench_native_group_gpu.c tests/bench_native_leader.c tests/native_group_image.h tests/native_mesh_image.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_insert_fixture.o $(BUILD)/native_sort_fixture.o $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_recovery_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/native_insert_fixture.o $(BUILD)/native_sort_fixture.o $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_recovery_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS)) $(CHECKED_WRAP)

INSERT_MODE ?= combined
INSERT_ORDER ?= random
INSERT_OPTIONS ?=
.PHONY: test-native-insert-gpu bench-native-insert-gpu
test-native-insert-gpu: $(BUILD)/bench_native_insert_gpu.exe
	$(MAKE) bench-native-insert-gpu INSERT_OPTIONS="--controls-only --self-test"

bench-native-insert-gpu: $(BUILD)/bench_native_insert_gpu.exe
	python ../tools/bench_native_insert_gpu.py $(INSERT_OPTIONS) --mode $(INSERT_MODE) --order $(INSERT_ORDER) "$(AOTR_DAT)" $< "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first
	python ../tools/bench_native_insert_gpu.py $(INSERT_OPTIONS) --mode $(INSERT_MODE) --order $(INSERT_ORDER) "$(AOTR_DAT)" $< "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first
	@test -f "$(DXVK_D3D9)" || { echo "error: bench-native-insert-gpu requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/native_insert_gpu_dxvk
	cp $< "$(DXVK_D3D9)" $(BUILD)/native_insert_gpu_dxvk/
	cd $(BUILD)/native_insert_gpu_dxvk && \
	  DXVK_LOG_LEVEL=none python ../../../tools/bench_native_insert_gpu.py $(INSERT_OPTIONS) --mode $(INSERT_MODE) --order $(INSERT_ORDER) "$(AOTR_DAT)" bench_native_insert_gpu.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first && \
	  DXVK_LOG_LEVEL=none python ../../../tools/bench_native_insert_gpu.py $(INSERT_OPTIONS) --mode $(INSERT_MODE) --order $(INSERT_ORDER) "$(AOTR_DAT)" bench_native_insert_gpu.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first

$(BUILD)/bench_runtime_bulk_gpu.exe: tests/bench_native_insert_gpu.c tests/bench_native_queue_gpu.c tests/native_shared_arena.h tests/bench_native_fx_gpu.c tests/native_fx_image.h tests/bench_native_group_gpu.c tests/bench_native_leader.c tests/native_group_image.h tests/native_mesh_image.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_insert_fixture.o $(BUILD)/native_sort_fixture.o $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_recovery_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_RUNTIME_BULK -static -o $@ $< $(BUILD)/native_insert_fixture.o $(BUILD)/native_sort_fixture.o $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_recovery_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS)) $(CHECKED_WRAP)

$(BUILD)/bench_runtime_bulk_20_gpu.exe: tests/bench_native_insert_gpu.c $(BUILD)/bench_runtime_bulk_gpu.exe
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_RUNTIME_BULK -DPF_FIXTURE_BONES=20 -static -o $@ $< $(BUILD)/native_insert_fixture.o $(BUILD)/native_sort_fixture.o $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_recovery_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS)) $(CHECKED_WRAP)

$(BUILD)/bulk_chain_screen.c: src/bulk.c ../tools/prepare_chain_screen.py | $(BUILD)
	python ../tools/prepare_chain_screen.py $< $@

$(BUILD)/bulk_chain_screen.o: $(BUILD)/bulk_chain_screen.c src/*.h
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

CHAIN_SCREEN_OBJS = $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_recovery_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(BUILD)/bulk_chain_screen.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o $(BUILD)/bulk.o,$(DISPATCH_OBJS))
$(BUILD)/bench_chain_screen.exe: tests/bench_chain_screen.c tests/native_chain_fixture.h $(BUILD)/bench_runtime_bulk_gpu.exe $(BUILD)/bulk_chain_screen.o
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_RUNTIME_BULK -static -o $@ $< $(CHAIN_SCREEN_OBJS) $(CHECKED_WRAP)

$(BUILD)/bench_chain_screen_20.exe: tests/bench_chain_screen.c tests/native_chain_fixture.h $(BUILD)/bench_runtime_bulk_gpu.exe $(BUILD)/bulk_chain_screen.o
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_RUNTIME_BULK -DPF_FIXTURE_BONES=20 -static -o $@ $< $(CHAIN_SCREEN_OBJS) $(CHECKED_WRAP)

CHAIN_RECOVERY_OBJS = $(filter-out $(BUILD)/inst_recovery_engine.o,$(CHAIN_SCREEN_OBJS)) $(BUILD)/inst_runtime_fallback_engine.o
$(BUILD)/test_chain_recovery.exe: tests/test_chain_recovery.c tests/native_chain_fixture.h $(BUILD)/test_runtime_group_fallback.exe $(BUILD)/bulk_chain_screen.o
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_RUNTIME_BULK -static -o $@ $< $(CHAIN_RECOVERY_OBJS) $(CHECKED_WRAP) -Wl,--wrap=pf_inst_bulk_cancel

$(BUILD)/test_chain_recovery_20.exe: tests/test_chain_recovery.c tests/native_chain_fixture.h $(BUILD)/test_runtime_group_fallback.exe $(BUILD)/bulk_chain_screen.o
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_RUNTIME_BULK -DPF_FIXTURE_BONES=20 -static -o $@ $< $(CHAIN_RECOVERY_OBJS) $(CHECKED_WRAP) -Wl,--wrap=pf_inst_bulk_cancel

CHAIN_RUNTIME_OBJS = $(filter-out $(BUILD)/bulk_chain_screen.o,$(CHAIN_RECOVERY_OBJS)) $(BUILD)/bulk.o
$(BUILD)/test_chain_runtime.exe: tests/test_chain_recovery.c tests/native_chain_fixture.h $(BUILD)/test_runtime_group_fallback.exe
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_RUNTIME_BULK -static -o $@ $< $(CHAIN_RUNTIME_OBJS) $(CHECKED_WRAP) -Wl,--wrap=pf_inst_bulk_cancel

$(BUILD)/test_chain_runtime_20.exe: tests/test_chain_recovery.c tests/native_chain_fixture.h $(BUILD)/test_runtime_group_fallback.exe
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_RUNTIME_BULK -DPF_FIXTURE_BONES=20 -static -o $@ $< $(CHAIN_RUNTIME_OBJS) $(CHECKED_WRAP) -Wl,--wrap=pf_inst_bulk_cancel

$(BUILD)/inst_verify_old.c: | $(BUILD)
	git show 35ac82b:perf/src/inst.c > $@

$(BUILD)/inst_verify_old_engine.o: tests/inst_verify_old_engine.c tests/inst_recovery_engine.c $(BUILD)/inst_verify_old.c src/*.h
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

VERIFY_GPU_OBJS = $(BUILD)/native_insert_fixture.o $(BUILD)/native_sort_fixture.o $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))
$(BUILD)/bench_verify_window_gpu.exe: tests/bench_verify_window_gpu.c $(BUILD)/bench_runtime_bulk_gpu.exe
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_RUNTIME_BULK -static -o $@ $< $(BUILD)/inst_recovery_engine.o $(VERIFY_GPU_OBJS) $(CHECKED_WRAP)

$(BUILD)/bench_verify_window_old_gpu.exe: tests/bench_verify_window_gpu.c $(BUILD)/bench_runtime_bulk_gpu.exe $(BUILD)/inst_verify_old_engine.o
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_RUNTIME_BULK -DPF_VERIFY_WINDOW_OLD -static -o $@ $< $(BUILD)/inst_verify_old_engine.o $(VERIFY_GPU_OBJS) $(CHECKED_WRAP)

$(BUILD)/bench_verify_window_20_gpu.exe: tests/bench_verify_window_gpu.c $(BUILD)/bench_runtime_bulk_gpu.exe
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_RUNTIME_BULK -DPF_FIXTURE_BONES=20 -static -o $@ $< $(BUILD)/inst_recovery_engine.o $(VERIFY_GPU_OBJS) $(CHECKED_WRAP)

$(BUILD)/bench_verify_window_old_20_gpu.exe: tests/bench_verify_window_gpu.c $(BUILD)/bench_runtime_bulk_gpu.exe $(BUILD)/inst_verify_old_engine.o
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_RUNTIME_BULK -DPF_VERIFY_WINDOW_OLD -DPF_FIXTURE_BONES=20 -static -o $@ $< $(BUILD)/inst_verify_old_engine.o $(VERIFY_GPU_OBJS) $(CHECKED_WRAP)

.PHONY: test-runtime-bulk-gpu bench-runtime-bulk-gpu
test-runtime-bulk-gpu: $(BUILD)/bench_runtime_bulk_gpu.exe
	$(MAKE) bench-runtime-bulk-gpu INSERT_OPTIONS="--controls-only --self-test"

bench-runtime-bulk-gpu: $(BUILD)/bench_runtime_bulk_gpu.exe
	python ../tools/bench_native_insert_gpu.py $(INSERT_OPTIONS) --mode $(INSERT_MODE) --order $(INSERT_ORDER) "$(AOTR_DAT)" $< "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first
	python ../tools/bench_native_insert_gpu.py $(INSERT_OPTIONS) --mode $(INSERT_MODE) --order $(INSERT_ORDER) "$(AOTR_DAT)" $< "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first
	@test -f "$(DXVK_D3D9)" || { echo "error: bench-runtime-bulk-gpu requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/runtime_bulk_gpu_dxvk
	cp $< "$(DXVK_D3D9)" $(BUILD)/runtime_bulk_gpu_dxvk/
	cd $(BUILD)/runtime_bulk_gpu_dxvk && \
	  DXVK_LOG_LEVEL=none python ../../../tools/bench_native_insert_gpu.py $(INSERT_OPTIONS) --mode $(INSERT_MODE) --order $(INSERT_ORDER) "$(AOTR_DAT)" bench_runtime_bulk_gpu.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first && \
	  DXVK_LOG_LEVEL=none python ../../../tools/bench_native_insert_gpu.py $(INSERT_OPTIONS) --mode $(INSERT_MODE) --order $(INSERT_ORDER) "$(AOTR_DAT)" bench_runtime_bulk_gpu.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first

bench-native-fx-gpu: $(BUILD)/bench_native_fx_gpu.exe
	python ../tools/bench_native_fx_gpu.py "$(AOTR_DAT)" $(BUILD)/bench_native_fx_gpu.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first
	python ../tools/bench_native_fx_gpu.py "$(AOTR_DAT)" $(BUILD)/bench_native_fx_gpu.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first
	@test -f "$(DXVK_D3D9)" || { echo "error: bench-native-fx-gpu requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/native_fx_gpu_dxvk
	cp $(BUILD)/bench_native_fx_gpu.exe "$(DXVK_D3D9)" $(BUILD)/native_fx_gpu_dxvk/
	cd $(BUILD)/native_fx_gpu_dxvk && \
	  DXVK_LOG_LEVEL=none python ../../../tools/bench_native_fx_gpu.py "$(AOTR_DAT)" bench_native_fx_gpu.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first && \
	  DXVK_LOG_LEVEL=none python ../../../tools/bench_native_fx_gpu.py "$(AOTR_DAT)" bench_native_fx_gpu.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first

$(BUILD)/inst_recovery_base.c: src/inst.c ../tools/make_inst_recovery_source.py | $(BUILD)
	python ../tools/make_inst_recovery_source.py

$(BUILD)/inst_recovery_engine.o: tests/inst_recovery_engine.c tests/inst_native_leader_engine.c tests/inst_retained_engine.c tests/inst_dispatch_engine.c src/inst.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/inst_recovery_fault_engine.o: tests/inst_recovery_fault_engine.c tests/inst_recovery_engine.c tests/inst_gpu_fault_engine.c tests/inst_native_leader_engine.c tests/inst_retained_engine.c tests/inst_dispatch_engine.c src/inst.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/test_native_gpu_recovery.exe: tests/test_native_gpu_recovery.c tests/test_native_gpu_faults.c tests/bench_native_group_gpu.c tests/bench_native_leader.c tests/native_group_image.h tests/native_mesh_image.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_recovery_fault_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_recovery_fault_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS)) $(CHECKED_WRAP)

test-native-gpu-recovery: $(BUILD)/test_native_gpu_recovery.exe
	python ../tools/test_native_gpu_faults.py --self-test --recovery "$(AOTR_DAT)" $(BUILD)/test_native_gpu_recovery.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first
	python ../tools/test_native_gpu_faults.py --self-test --recovery "$(AOTR_DAT)" $(BUILD)/test_native_gpu_recovery.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first
	@test -f "$(DXVK_D3D9)" || { echo "error: test-native-gpu-recovery requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/native_gpu_recovery_dxvk
	cp $(BUILD)/test_native_gpu_recovery.exe "$(DXVK_D3D9)" $(BUILD)/native_gpu_recovery_dxvk/
	cd $(BUILD)/native_gpu_recovery_dxvk && \
	  DXVK_LOG_LEVEL=none python ../../../tools/test_native_gpu_faults.py --self-test --recovery "$(AOTR_DAT)" test_native_gpu_recovery.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first && \
	  DXVK_LOG_LEVEL=none python ../../../tools/test_native_gpu_faults.py --self-test --recovery "$(AOTR_DAT)" test_native_gpu_recovery.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first

$(BUILD)/bench_native_gpu_recovery.exe: tests/bench_native_group_gpu.c tests/bench_native_leader.c tests/native_group_image.h tests/native_mesh_image.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_recovery_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_recovery_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS)) $(CHECKED_WRAP)

bench-native-gpu-recovery: $(BUILD)/bench_native_gpu_recovery.exe
	python ../tools/bench_native_group_gpu.py "$(AOTR_DAT)" $(BUILD)/bench_native_gpu_recovery.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first
	python ../tools/bench_native_group_gpu.py "$(AOTR_DAT)" $(BUILD)/bench_native_gpu_recovery.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first
	@test -f "$(DXVK_D3D9)" || { echo "error: bench-native-gpu-recovery requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/native_gpu_recovery_bench_dxvk
	cp $(BUILD)/bench_native_gpu_recovery.exe "$(DXVK_D3D9)" $(BUILD)/native_gpu_recovery_bench_dxvk/
	cd $(BUILD)/native_gpu_recovery_bench_dxvk && \
	  DXVK_LOG_LEVEL=none python ../../../tools/bench_native_group_gpu.py "$(AOTR_DAT)" bench_native_gpu_recovery.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first && \
	  DXVK_LOG_LEVEL=none python ../../../tools/bench_native_group_gpu.py "$(AOTR_DAT)" bench_native_gpu_recovery.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first

$(BUILD)/inst_gpu_fault_engine.o: tests/inst_gpu_fault_engine.c tests/inst_native_leader_engine.c tests/inst_retained_engine.c tests/inst_dispatch_engine.c src/inst.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/test_native_gpu_faults.exe: tests/test_native_gpu_faults.c tests/bench_native_group_gpu.c tests/bench_native_leader.c tests/native_group_image.h tests/native_mesh_image.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_gpu_fault_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_gpu_fault_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

test-native-gpu-faults: $(BUILD)/test_native_gpu_faults.exe
	python ../tools/test_native_gpu_faults.py --self-test "$(AOTR_DAT)" $(BUILD)/test_native_gpu_faults.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first
	python ../tools/test_native_gpu_faults.py --self-test "$(AOTR_DAT)" $(BUILD)/test_native_gpu_faults.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first
	@test -f "$(DXVK_D3D9)" || { echo "error: test-native-gpu-faults requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/native_gpu_fault_dxvk
	cp $(BUILD)/test_native_gpu_faults.exe "$(DXVK_D3D9)" $(BUILD)/native_gpu_fault_dxvk/
	cd $(BUILD)/native_gpu_fault_dxvk && \
	  DXVK_LOG_LEVEL=none python ../../../tools/test_native_gpu_faults.py --self-test "$(AOTR_DAT)" test_native_gpu_faults.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first && \
	  DXVK_LOG_LEVEL=none python ../../../tools/test_native_gpu_faults.py --self-test "$(AOTR_DAT)" test_native_gpu_faults.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first

$(BUILD)/native_group_gpu_bridge.o: tests/native_group_gpu_bridge.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/bench_native_group_gpu.exe: tests/bench_native_group_gpu.c tests/bench_native_leader.c tests/native_group_image.h tests/native_mesh_image.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_native_leader_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_native_leader_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

$(BUILD)/bench_group_consumer.exe: tests/bench_group_consumer.c tests/packet_consumer.h tests/material_packet.h tests/bench_native_group_gpu.c tests/bench_native_leader.c tests/native_group_image.h tests/native_mesh_image.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_native_leader_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_native_leader_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

$(BUILD)/bench_group_commands.exe: tests/bench_group_consumer.c tests/group_commands.h $(BUILD)/bench_group_consumer.exe
	$(CC) $(TEST_CFLAGS) -O2 -DPF_GROUP_COMMANDS -Wno-unused-function -static -o $@ $< $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_native_leader_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

$(BUILD)/bench_group_profile.exe: tests/bench_group_consumer.c tests/group_commands.h $(BUILD)/bench_group_commands.exe
	$(CC) $(TEST_CFLAGS) -O2 -DPF_GROUP_COMMANDS -DPF_GROUP_PROFILE -Wno-unused-function -static -o $@ $< $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_native_leader_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

$(BUILD)/bench_group_capture.exe: tests/bench_group_consumer.c tests/group_capture.h $(BUILD)/bench_group_profile.exe
	$(CC) $(TEST_CFLAGS) -O2 -DPF_GROUP_COMMANDS -DPF_GROUP_PROFILE -DPF_GROUP_CAPTURE -Wno-unused-function -static -o $@ $< $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_native_leader_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

$(BUILD)/bench_group_first.exe: tests/bench_group_consumer.c tests/group_first_batch.h $(BUILD)/bench_group_capture.exe
	$(CC) $(TEST_CFLAGS) -O2 -DPF_GROUP_COMMANDS -DPF_GROUP_PROFILE -DPF_GROUP_CAPTURE -DPF_GROUP_BATCH_FIRST -Wno-unused-function -static -o $@ $< $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_native_leader_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

$(BUILD)/bench_group_start.exe: tests/bench_group_consumer.c tests/group_frame_start.h $(BUILD)/bench_group_first.exe
	$(CC) $(TEST_CFLAGS) -O2 -DPF_GROUP_COMMANDS -DPF_GROUP_PROFILE -DPF_GROUP_CAPTURE -DPF_GROUP_BATCH_FIRST -DPF_GROUP_FRAME_START -Wno-unused-function -static -o $@ $< $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_native_leader_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

$(BUILD)/bench_group_completion.exe: tests/bench_group_consumer.c tests/group_completion.h tests/group_completion_cases.h $(BUILD)/bench_group_start.exe
	$(CC) $(TEST_CFLAGS) -O2 -DPF_GROUP_COMMANDS -DPF_GROUP_PROFILE -DPF_GROUP_CAPTURE -DPF_GROUP_BATCH_FIRST -DPF_GROUP_FRAME_START -DPF_GROUP_COMPLETION -Wno-unused-function -static -o $@ $< $(BUILD)/native_group_gpu_bridge.o $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_native_leader_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

bench-native-group-gpu: $(BUILD)/bench_native_group_gpu.exe
	python ../tools/bench_native_group_gpu.py "$(AOTR_DAT)" $(BUILD)/bench_native_group_gpu.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first
	python ../tools/bench_native_group_gpu.py "$(AOTR_DAT)" $(BUILD)/bench_native_group_gpu.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first
	@test -f "$(DXVK_D3D9)" || { echo "error: bench-native-group-gpu requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/native_group_gpu_dxvk
	cp $(BUILD)/bench_native_group_gpu.exe "$(DXVK_D3D9)" $(BUILD)/native_group_gpu_dxvk/
	cd $(BUILD)/native_group_gpu_dxvk && \
	  DXVK_LOG_LEVEL=none python ../../../tools/bench_native_group_gpu.py "$(AOTR_DAT)" bench_native_group_gpu.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first && \
	  DXVK_LOG_LEVEL=none python ../../../tools/bench_native_group_gpu.py "$(AOTR_DAT)" bench_native_group_gpu.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first

$(BUILD)/inst_native_leader_engine.o: tests/inst_native_leader_engine.c tests/inst_retained_engine.c tests/inst_dispatch_engine.c src/inst.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/bench_native_leader.exe: tests/bench_native_leader.c tests/native_mesh_image.h tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_native_leader_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_native_leader_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

bench-native-leader: $(BUILD)/bench_native_leader.exe
	python ../tools/bench_native_leader.py "$(AOTR_DAT)" $(BUILD)/bench_native_leader.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first
	python ../tools/bench_native_leader.py "$(AOTR_DAT)" $(BUILD)/bench_native_leader.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first
	@test -f "$(DXVK_D3D9)" || { echo "error: bench-native-leader requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/native_leader_dxvk
	cp $(BUILD)/bench_native_leader.exe "$(DXVK_D3D9)" $(BUILD)/native_leader_dxvk/
	cd $(BUILD)/native_leader_dxvk && \
	  DXVK_LOG_LEVEL=none python ../../../tools/bench_native_leader.py "$(AOTR_DAT)" bench_native_leader.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 leader-bulk-first && \
	  DXVK_LOG_LEVEL=none python ../../../tools/bench_native_leader.py "$(AOTR_DAT)" bench_native_leader.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 leader-bulk-first

.PHONY: bench-leader
bench-leader: $(BUILD)/bench_leader.exe
	./$(BUILD)/bench_leader.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 $(LEADER_PROTOCOL)
	./$(BUILD)/bench_leader.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 $(LEADER_PROTOCOL)
	@test -f "$(DXVK_D3D9)" || { echo "error: bench-leader requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/leader_dxvk
	cp $(BUILD)/bench_leader.exe "$(DXVK_D3D9)" $(BUILD)/leader_dxvk/
	cd $(BUILD)/leader_dxvk && \
	  DXVK_LOG_LEVEL=none ./bench_leader.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit narrow 10 $(LEADER_PROTOCOL) && \
	  DXVK_LOG_LEVEL=none ./bench_leader.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow narrow 10 $(LEADER_PROTOCOL)

bench-material-queue: $(BUILD)/bench_material_queue.exe
	./$(BUILD)/bench_material_queue.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit $(RETAINED_MASK) $(MATERIAL_CHURN) $(MATERIAL_PROTOCOL)
	./$(BUILD)/bench_material_queue.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow $(RETAINED_MASK) $(MATERIAL_CHURN) $(MATERIAL_PROTOCOL)
	@test -f "$(DXVK_D3D9)" || { echo "error: bench-material-queue requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/material_dxvk
	cp $(BUILD)/bench_material_queue.exe "$(DXVK_D3D9)" $(BUILD)/material_dxvk/
	cd $(BUILD)/material_dxvk && \
	  DXVK_LOG_LEVEL=none ./bench_material_queue.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit $(RETAINED_MASK) $(MATERIAL_CHURN) $(MATERIAL_PROTOCOL) && \
	  DXVK_LOG_LEVEL=none ./bench_material_queue.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow $(RETAINED_MASK) $(MATERIAL_CHURN) $(MATERIAL_PROTOCOL)

bench-retained: $(BUILD)/bench_retained.exe
	./$(BUILD)/bench_retained.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit $(RETAINED_MASK)
	./$(BUILD)/bench_retained.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow $(RETAINED_MASK)
	@test -f "$(DXVK_D3D9)" || { echo "error: bench-retained requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/retained_dxvk
	cp $(BUILD)/bench_retained.exe "$(DXVK_D3D9)" $(BUILD)/retained_dxvk/
	cd $(BUILD)/retained_dxvk && \
	  DXVK_LOG_LEVEL=none ./bench_retained.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" lit $(RETAINED_MASK) && \
	  DXVK_LOG_LEVEL=none ./bench_retained.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow $(RETAINED_MASK)

$(BUILD)/bench_ambient.exe: tests/bench_ambient.c | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -static -o $@ $<

$(BUILD)/bench_native_matrix_sources.exe: tests/bench_native_matrix_sources.c tests/bench_guard.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -static -o $@ $<

bench-ambient: $(BUILD)/bench_ambient.exe
	python ../tools/bench_ambient.py "$(AOTR_DAT)"
	python ../tools/bench_ambient.py "$(ROTWK_DAT)"

bench-dispatch: $(BUILD)/bench_dispatch.exe
	./$(BUILD)/bench_dispatch.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll"
	./$(BUILD)/bench_dispatch.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow
	@test -f "$(DXVK_D3D9)" || { echo "error: bench-dispatch requires DXVK_D3D9"; exit 1; }
	mkdir -p $(BUILD)/dispatch_dxvk
	cp $(BUILD)/bench_dispatch.exe "$(DXVK_D3D9)" $(BUILD)/dispatch_dxvk/
	cd $(BUILD)/dispatch_dxvk && \
	  DXVK_LOG_LEVEL=none ./bench_dispatch.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" && \
	  DXVK_LOG_LEVEL=none ./bench_dispatch.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll" shadow
