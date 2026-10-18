#ifndef PF_NATIVECONTRACT_H
#define PF_NATIVECONTRACT_H
#include "scan.h"

typedef struct { unsigned size, first, count, fnv, crc; } pf_contract_body;
typedef struct { unsigned offset, absolute, symbol; } pf_contract_patch;
typedef struct { unsigned symbol; const char *signature; } pf_contract_root;
typedef struct {
    unsigned body_count, symbol_count, patch_count, root_count;
    const pf_contract_body *bodies;
    const pf_contract_patch *patches;
    const unsigned char *code;
    const pf_contract_root *roots;
} pf_native_contract;

/* locations has symbol_count entries; it is filled only on success.
 * Contracts are generated metadata, not an untrusted data-file format. */
const char *pf_contract_find(const pf_image *image, const pf_native_contract *contract,
                             unsigned *locations);
#endif
