/* Apply the existing independent fault oracles after native FX/state
 * integration, using the same checked skeletal submission engine. */
#define PF_RECOVERY_CANDIDATE
#define PF_NATIVE_FX_MAIN group_gpu_main
#define PF_NATIVE_GPU_GROUP_SOURCE "bench_native_fx_gpu.c"
#define PF_FAULT_ARGUMENTS 11
#define PF_FAULT_EXTRA_STATE nx_snapshot
#define PF_FAULT_STOCK_REFERENCE
#include "test_native_gpu_faults.c"
