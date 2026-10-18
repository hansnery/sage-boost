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
         src/inst.c src/declcache.c src/fpsites.c src/fps.c src/extraprofile.c src/fpsmath.c src/btnclock.c src/passflag.c \
         src/shadowpass.c src/spsites.c src/fxguard.c src/catchup.c src/cusites.c src/notice.c \
         src/lights.c src/ltsites.c
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
                         src/presentgap.c src/csvrow.c src/clock.c src/reflect.c src/uistate.c src/fps.c src/extraprofile.c src/log.c \
                         src/fpsmath.c src/btnclock.c src/shadowpass.c src/catchup.c src/notice.c \
                         src/lights.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_fps.exe: tests/test_fps.c src/fps.c src/extraprofile.c src/stubs.c src/log.c src/fpsmath.c \
                       src/btnclock.c src/scan.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_extraprofile.exe: tests/test_extraprofile.c src/extraprofile.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_declcache.exe: tests/test_declcache.c src/declcache.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_inst.exe: tests/test_inst.c src/inst.c src/declcache.c src/log.c src/scan.c src/patch.c src/stubs.c \
                        src/timing.c src/presentgap.c src/csvrow.c src/clock.c src/reflect.c src/uistate.c \
                        src/fps.c src/extraprofile.c src/fpsmath.c src/btnclock.c src/shadowpass.c src/catchup.c \
                        src/notice.c src/lights.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_catchup.exe: tests/test_catchup.c src/catchup.c src/notice.c src/log.c src/uistate.c \
                           src/scan.c src/patch.c src/stubs.c src/timing.c src/presentgap.c src/csvrow.c src/clock.c \
                           src/reflect.c src/fps.c src/extraprofile.c src/fpsmath.c src/btnclock.c src/shadowpass.c \
                           src/lights.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_shadowpass.exe: tests/test_shadowpass.c src/shadowpass.c src/fxguard.c src/spsites.c \
                              src/scan.c src/fps.c src/extraprofile.c src/stubs.c src/log.c src/fpsmath.c \
                              src/btnclock.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

$(BUILD)/test_lights.exe: tests/test_lights.c src/lights.c src/shadowpass.c src/stubs.c src/log.c \
                          src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -static -o $@ $(filter %.c,$^)

test: all $(BUILD)/test_row.exe $(BUILD)/test_reflect.exe $(BUILD)/test_config.exe \
      $(BUILD)/test_sites.exe $(BUILD)/test_hooks.exe $(BUILD)/test_fps.exe $(BUILD)/test_load.exe \
      $(BUILD)/test_inst.exe $(BUILD)/test_declcache.exe $(BUILD)/test_btn.exe $(BUILD)/test_shadowpass.exe \
      $(BUILD)/test_catchup.exe $(BUILD)/test_lights.exe $(BUILD)/test_extraprofile.exe $(BUILD)/test_presentgap.exe
	./$(BUILD)/test_row.exe
	./$(BUILD)/test_reflect.exe
	./$(BUILD)/test_btn.exe
	./$(BUILD)/test_config.exe
	./$(BUILD)/test_sites.exe $(SITE_TEST_ARGS)
	./$(BUILD)/test_hooks.exe
	./$(BUILD)/test_fps.exe
	./$(BUILD)/test_load.exe "$(BUILD)/bfme2perf.asi"
	./$(BUILD)/test_presentgap.exe
	./$(BUILD)/test_extraprofile.exe
	./$(BUILD)/test_declcache.exe
	./$(BUILD)/test_inst.exe
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

DISPATCH_OBJS := $(filter-out $(BUILD)/main.o $(BUILD)/rt.o $(BUILD)/inst.o $(BUILD)/version.o,$(OBJS))
$(BUILD)/bench_dispatch.exe: tests/bench_dispatch.c tests/test_inst.c $(BUILD)/inst_dispatch_engine.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -static -o $@ $< $(BUILD)/inst_dispatch_engine.o $(DISPATCH_OBJS)

.PHONY: bench-dispatch bench-ambient bench-retained bench-material-queue test-fx-lifecycle bench-scopes

.PHONY: test-native-events
.PHONY: test-native-group
.PHONY: test-native-mesh
$(BUILD)/test_native_mesh.exe: tests/test_native_mesh.c src/stubs.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -mgeneral-regs-only -static -o $@ $(filter %.c,$^)

test-native-mesh: $(BUILD)/test_native_mesh.exe
	python ../tools/test_mesh_draw_contract.py "$(AOTR_DAT)" --self-test --execute
	python ../tools/test_mesh_draw_contract.py "$(ROTWK_DAT)" --self-test --execute

$(BUILD)/test_native_group.exe: tests/test_native_group.c | $(BUILD)
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

$(BUILD)/bench_effect.exe: tests/bench_effect.c tests/shadow_fx/shadow_fx.c src/shadowfx_table.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -static -o $@ $<

$(BUILD)/inst_retained_engine.o: tests/inst_retained_engine.c tests/inst_dispatch_engine.c src/inst.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/fps_extra_bench.o: tests/fps_extra_bench.c src/fps.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/bench_scopes.exe: tests/bench_scopes.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/inst_dispatch_engine.o $(BUILD)/fps_extra_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/inst_dispatch_engine.o $(BUILD)/fps_extra_bench.o $(filter-out $(BUILD)/fps.o,$(DISPATCH_OBJS))

bench-scopes: $(BUILD)/bench_scopes.exe
	./$(BUILD)/bench_scopes.exe "$(DISPATCH_ASSETS)/Shaders.big" "$(DISPATCH_ASSETS)/msvcr71.dll"

$(BUILD)/bench_retained.exe: tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(filter-out $(BUILD)/fps.o,$(DISPATCH_OBJS))

$(BUILD)/material_fixture.o: tests/material_fixture.c tests/effect_filter_bench.h tests/shadow_fx/shadow_fx.c src/shadowfx_table.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -c -o $@ $<

$(BUILD)/effect_filter_bench.o: tests/effect_filter_bench.c tests/effect_filter_bench.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/timing_bulk_bench.o: tests/timing_bulk_bench.c src/timing.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/bench_material_queue.exe: tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -static -o $@ $< $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_retained_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

RETAINED_MASK ?= narrow
MATERIAL_CHURN ?=
MATERIAL_PROTOCOL ?=
$(BUILD)/inst_leader_engine.o: tests/inst_leader_engine.c tests/inst_retained_engine.c tests/inst_dispatch_engine.c src/inst.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

$(BUILD)/bench_leader.exe: tests/bench_material_queue.c tests/bench_retained.c tests/bench_dispatch.c tests/test_inst.c tests/bench_guard.h $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_leader_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(DISPATCH_OBJS) src/*.h
	$(CC) $(TEST_CFLAGS) -O2 -Wno-unused-function -DPF_BENCH_LEADER -static -o $@ $< $(BUILD)/material_fixture.o $(BUILD)/effect_filter_bench.o $(BUILD)/inst_leader_engine.o $(BUILD)/fps_extra_bench.o $(BUILD)/timing_bulk_bench.o $(filter-out $(BUILD)/fps.o $(BUILD)/timing.o,$(DISPATCH_OBJS))

LEADER_PROTOCOL ?= leader
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
