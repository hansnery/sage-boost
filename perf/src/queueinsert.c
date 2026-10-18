#include "queueinsert.h"
#include "queueinsert_table.h"
#include "patch.h"

/* This path changes only the rendering-task queue. The by-value mesh holder
 * is consumed exactly once, including when the original entry is called. */
typedef struct { unsigned word[8]; } qi_record;
typedef struct { qi_record *first, *last, *limit; } qi_queue;
typedef struct { void **first, **last, **limit; } qi_methods;
typedef struct { void *vtable; unsigned refs; } qi_object;
static pf_queue_add_fn qi_original;

void pf_queue_insert_original(pf_queue_add_fn original) { qi_original = original; }

static int intersects(unsigned begin, unsigned end, unsigned slot)
{ return begin < slot + 32 && slot < end; }

void __attribute__((thiscall)) pf_queue_insert_add(void *queues, void *mesh, const void *input)
{
    const qi_methods *methods = input;
    unsigned begin = (unsigned)methods->first, end = (unsigned)methods->last;
    if (end < begin || ((end - begin) & 3) || end - begin > 24 || !mesh)
        goto fallback;
    unsigned char *model = *(unsigned char **)((char *)mesh + 0xc4);
    if (!model) goto fallback;
    unsigned which = 0;
    if (model[0x19] & 4) {
        const unsigned char *geometry = *(unsigned char **)(model + 0xbc);
        if (!geometry) goto fallback;
        which = *geometry ? 1 : 2;
    }
    qi_queue *q = (qi_queue *)queues + which;
    unsigned first = (unsigned)q->first, last = (unsigned)q->last, limit = (unsigned)q->limit;
    if (!first || last < first || limit < last || ((last - first) & 31) ||
        ((limit - first) & 31) || limit - last < 32) goto fallback;
    /* Native construction zeros the holders before reading the input stack.
     * Preserve that order by delegating any destination/input alias. */
    if (intersects(begin, end, last) ||
        intersects((unsigned)methods, (unsigned)(methods + 1), last)) goto fallback;
    qi_record *record = q->last;
    unsigned count = (end - begin) / 4;
    record->word[0] = (unsigned)mesh;
    record->word[1] = count;
    for (unsigned i = 0; i < 6; i++) record->word[2 + i] = 0;
    q->last++;
    ((qi_object *)mesh)->refs++;
    for (unsigned i = 0; i < count; i++) {
        qi_object *method = methods->first[i];
        if (method) method->refs++;
        record->word[2 + i] = (unsigned)method;
    }
    ((qi_object *)mesh)->refs--;
    return;
fallback:
    qi_original(queues, mesh, input);
}

const char *pf_queue_insert_find(const pf_image *image, unsigned *entry)
{
    unsigned locations[QI_SYMBOL_COUNT];
    if (entry) *entry = 0;
    const char *why = pf_contract_find(image, &qi_contract, locations);
    if (!why && entry) *entry = locations[0];
    return why;
}

static void set_original(unsigned index, void *original)
{ (void)index; pf_queue_insert_original((pf_queue_add_fn)original); }

int pf_queue_insert_install(const pf_image *image, const char **why)
{
    unsigned entry;
    const char *error = pf_queue_insert_find(image, &entry);
    if (error) { if (why) *why = error; return 0; }
    /* mov eax,imm32 is position independent. Its original SEH setup remains
     * in place after the trampoline returns to the following instruction. */
    const pf_site site = {"queue-insert", "AddTask", QI_SIGNATURE, 0, 5, 8, 1, 1, 0, 0};
    const int enabled = 1;
    void *stub = (void *)pf_queue_insert_add;
    pf_result result;
    pf_install(image, &site, 1, &enabled, 1, &stub, set_original, &result);
    if (why) *why = pf_status_name(result.status);
    return result.status == PF_ST_APPLIED;
}
