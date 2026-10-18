#include "queuesort.h"
#include "queuesort_table.h"
#include "patch.h"

/* Preserve the native permutation, including ties, while the queue retains
 * ownership of every record. Only verified pure comparisons qualify: no
 * callback can observe temporary moves or reference counts during this sort. */
typedef struct { unsigned word[8]; } qs_record;
static pf_queue_sort_fn qs_original;
static pf_queue_compare_fn qs_compare[2];

void pf_queue_sort_configure(pf_queue_sort_fn original, pf_queue_compare_fn mesh,
                             pf_queue_compare_fn mesh_state)
{
    qs_original = original;
    qs_compare[0] = mesh;
    qs_compare[1] = mesh_state;
}

static void move_record(qs_record *to, const qs_record *from)
{ for (unsigned i = 0; i < 8; i++) to->word[i] = from->word[i]; }

static int less(const qs_record *a, const qs_record *b, unsigned kind)
{
    const unsigned *am = (const unsigned *)a->word[0], *bm = (const unsigned *)b->word[0];
    unsigned left = am[0xc4 / 4], right = bm[0xc4 / 4];
    if (kind && left == right) { left = am[0x310 / 4]; right = bm[0x310 / 4]; }
    return left < right;
}

static const qs_record *median_record(const qs_record *a, const qs_record *b,
                                      const qs_record *c, unsigned kind)
{
    if (less(a, b, kind)) {
        if (less(b, c, kind)) return b;
        return less(a, c, kind) ? c : a;
    }
    if (less(a, c, kind)) return a;
    return less(b, c, kind) ? c : b;
}

static qs_record *partition(qs_record *first, qs_record *last, const qs_record *pivot,
                            unsigned kind)
{
    for (;;) {
        while (less(first, pivot, kind)) first++;
        do { last--; } while (less(pivot, last, kind));
        if (first >= last) return first;
        qs_record saved; move_record(&saved, first);
        move_record(first, last); move_record(last, &saved);
        first++;
    }
}

static void repair_heap(qs_record *records, unsigned count, unsigned hole,
                        const qs_record *value, unsigned kind)
{
    unsigned top = hole, child = 2 * hole + 2;
    while (child < count) {
        if (less(records + child, records + child - 1, kind)) child--;
        move_record(records + hole, records + child);
        hole = child;
        child = 2 * hole + 2;
    }
    if (child == count) {
        move_record(records + hole, records + child - 1);
        hole = child - 1;
    }
    while (hole > top) {
        unsigned parent = (hole - 1) / 2;
        if (!less(records + parent, value, kind)) break;
        move_record(records + hole, records + parent);
        hole = parent;
    }
    move_record(records + hole, value);
}

static void heap_sort(qs_record *records, unsigned count, unsigned kind)
{
    if (count < 2) return;
    unsigned parent = (count - 2) / 2;
    for (;;) {
        qs_record value; move_record(&value, records + parent);
        repair_heap(records, count, parent, &value, kind);
        if (!parent) break;
        parent--;
    }
    while (count > 1) {
        qs_record value; move_record(&value, records + --count);
        move_record(records + count, records);
        repair_heap(records, count, 0, &value, kind);
    }
}

static void partition_sort(qs_record *first, qs_record *last, unsigned kind, unsigned depth)
{
    while (last - first > 16) {
        if (!depth) { heap_sort(first, (unsigned)(last - first), kind); return; }
        depth--;
        qs_record pivot;
        move_record(&pivot, median_record(first, first + (last - first) / 2, last - 1, kind));
        qs_record *cut = partition(first, last, &pivot, kind);
        /* Right recursion followed by the left loop is observable in ties. */
        partition_sort(cut, last, kind, depth);
        last = cut;
    }
}

static void insert_record(qs_record *position, const qs_record *value, unsigned kind)
{
    while (less(value, position - 1, kind)) {
        move_record(position, position - 1);
        position--;
    }
    move_record(position, value);
}

void pf_queue_sort_raw(void *begin, void *end, unsigned kind, unsigned depth)
{
    qs_record *first = begin, *last = end;
    if (first == last) return;
    partition_sort(first, last, kind, depth);
    qs_record *guarded_end = last - first > 16 ? first + 16 : last;
    for (qs_record *item = first + 1; item != guarded_end; item++) {
        qs_record value; move_record(&value, item);
        if (less(&value, first, kind)) {
            for (qs_record *hole = item; hole != first; hole--) move_record(hole, hole - 1);
            move_record(first, &value);
        } else insert_record(item, &value, kind);
    }
    /* The first sorted block supplies the sentinel for the remaining tail. */
    for (qs_record *item = guarded_end; item != last; item++) {
        qs_record value; move_record(&value, item);
        insert_record(item, &value, kind);
    }
}

static int live_holder(unsigned object)
{
    if (!object) return 1;
    unsigned refs = ((const unsigned *)object)[1];
    /* Leave invalid/near-overflow reference behavior in the original path. */
    return refs && refs < 0xffff0000u;
}

void __cdecl pf_queue_sort_run(void *begin, void *end, pf_queue_compare_fn compare)
{
    unsigned first = (unsigned)begin, last = (unsigned)end, kind;
    if (compare && compare == qs_compare[0]) kind = 0;
    else if (compare && compare == qs_compare[1]) kind = 1;
    else goto native;
    if (((first | last) & 3) || last < first || ((last - first) & 31) || last - first > 0x7fffffff)
        goto native;
    unsigned count = (last - first) / 32;
    if (count < 2) return;
    if (!first) goto native;
    for (qs_record *item = begin; item != (qs_record *)end; item++) {
        if (!item->word[0] || item->word[1] > 6 || !live_holder(item->word[0])) goto native;
        for (unsigned i = 0; i < 6; i++) if (!live_holder(item->word[2 + i])) goto native;
    }
    unsigned depth = 0;
    for (unsigned n = count; n > 1; n >>= 1) depth += 2;
    pf_queue_sort_raw(begin, end, kind, depth);
    return;
native:
    qs_original(begin, end, compare);
}

const char *pf_queue_sort_find(const pf_image *image, pf_queue_sort_sites *sites)
{
    unsigned locations[QS_SYMBOL_COUNT];
    if (sites) { sites->entry = 0; sites->mesh = sites->mesh_state = NULL; }
    const char *why = pf_contract_find(image, &qs_contract, locations);
    if (!why && sites) {
        sites->entry = locations[QS_ENTRY];
        sites->mesh = (pf_queue_compare_fn)(image->base + locations[QS_COMPARE_MESH]);
        sites->mesh_state = (pf_queue_compare_fn)(image->base + locations[QS_COMPARE_STATE]);
    }
    return why;
}

static void set_original(unsigned index, void *original)
{ (void)index; qs_original = (pf_queue_sort_fn)original; }

int pf_queue_sort_install(const pf_image *image, const char **why)
{
    pf_queue_sort_sites found;
    const char *error = pf_queue_sort_find(image, &found);
    if (error) { if (why) *why = error; return 0; }
    qs_compare[0] = found.mesh;
    qs_compare[1] = found.mesh_state;
    /* push esi; mov esi,[esp+8] has no position-relative operands. */
    const pf_site site = {"queue-sort", "SortTasks", QS_SIGNATURE, 0, 5, 12, 1, 1, 0, 0};
    const int enabled = 1;
    void *stub = (void *)pf_queue_sort_run;
    pf_result result;
    pf_install(image, &site, 1, &enabled, 1, &stub, set_original, &result);
    if (why) *why = pf_status_name(result.status);
    return result.status == PF_ST_APPLIED;
}
