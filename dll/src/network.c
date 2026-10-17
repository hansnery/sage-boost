#include "network.h"
#include "log.h"

static volatile LONG g_written, g_read, g_rejected;

/* Avoid overflow in cursor+size and never partially read/write a field. The
 * native helpers allow a null limit; their callers supply a real end pointer. */
static int room(const unsigned char *cursor, const unsigned char *limit)
{
    return cursor && (!limit || ((UINT_PTR)cursor <= (UINT_PTR)limit &&
           (UINT_PTR)limit - (UINT_PTR)cursor >= FU_NETWORK_BYTES));
}

unsigned char *__cdecl fu_network_write(unsigned char *cursor, unsigned ignored,
                                       const unsigned char *limit, const void *slot)
{
    const int *fields = (const int *)slot;
    int color = fields[3], faction = fields[6], start = fields[4];
    unsigned packed;
    (void)ignored; /* The old nibble pack's result has already lost information. */
    if (!room(cursor, limit) || color < -1 || color > 14 || faction < -2 || faction > 125 ||
        start < -1 || start > 7) {
        /* The serializer does not check progress. Poison the rest of this
         * record and return limit so subsequent native writes cannot append a
         * plausible slot. No partial valid marker is emitted. */
        if (cursor && limit && (UINT_PTR)cursor <= (UINT_PTR)limit) {
            while (cursor < limit)
                *cursor++ = 0xFF;
        }
        InterlockedIncrement(&g_rejected);
        return cursor;
    }
    packed = 0x8000u | (unsigned)(start + 1) << 11 |
             (unsigned)(faction + 2) << 4 | (unsigned)(color + 1);
    cursor[0] = (unsigned char)packed;
    cursor[1] = (unsigned char)(packed >> 8);
    InterlockedIncrement(&g_written);
    /* The following start writer consumes the second byte without changing
     * it. Packet size and every subsequent field offset remain unchanged. */
    return cursor + 1;
}

unsigned char *__cdecl fu_network_read(unsigned char *cursor, unsigned *out,
                                      const unsigned char *limit)
{
    if (!out || !room(cursor, limit) || !(cursor[1] & 0x80) ||
        ((cursor[1] >> 3) & 15) > 8) {
        InterlockedIncrement(&g_rejected);
        /* The engine checks next>cursor and rejects the entire update. */
        return cursor;
    }
    *out = (unsigned)cursor[0] | (unsigned)cursor[1] << 8;
    InterlockedIncrement(&g_read);
    return cursor + 1;
}

void __cdecl fu_network_split(unsigned packed, char color_offset, char faction_offset,
                             unsigned char *color, unsigned char *faction)
{
    *color = (unsigned char)((packed & 15) + color_offset);
    *faction = (unsigned char)(((packed >> 4) & 127) + faction_offset);
}

/* Both callers advance by one byte, matching their original helpers. The
 * reader deliberately touches only the single byte allocated for startPos. */
unsigned char *__cdecl fu_network_write_start(unsigned char *cursor, unsigned ignored,
                                             const unsigned char *limit)
{
    (void)ignored;
    if (!cursor || (limit && (UINT_PTR)cursor >= (UINT_PTR)limit))
        return cursor;
    return cursor + 1;
}

unsigned char *__cdecl fu_network_read_start(unsigned char *cursor, unsigned char *out,
                                            const unsigned char *limit)
{
    if (!out || !cursor || (limit && (UINT_PTR)cursor >= (UINT_PTR)limit) ||
        !(*cursor & 0x80) || ((*cursor >> 3) & 15) > 8) {
        InterlockedIncrement(&g_rejected);
        return cursor;
    }
    *out = (unsigned char)(((*cursor >> 3) & 15) - 1);
    return cursor + 1;
}

/* At these two verified writer calls ESI is GameSlot*. The original cdecl
 * arguments stay on the caller's stack. Supply ESI as a fourth argument,
 * preserve every callee-saved register, and return the new cursor in EAX. */
__asm__(".text\n"
        "_fu_network_write_stub:\n"
        "pushl %esi\n"
        "pushl 16(%esp)\n"
        "pushl 16(%esp)\n"
        "pushl 16(%esp)\n"
        "call _fu_network_write\n"
        "addl $16, %esp\n"
        "ret\n");
extern char fu_network_write_stub[];

void *fu_network_stub(fu_hook hook)
{
    switch (hook) {
    case FU_HOOK_NETWORK_WRITE: return fu_network_write_stub;
    case FU_HOOK_NETWORK_READ: return fu_network_read;
    case FU_HOOK_NETWORK_SPLIT: return fu_network_split;
    case FU_HOOK_NETWORK_WRITE_START: return fu_network_write_start;
    case FU_HOOK_NETWORK_READ_START: return fu_network_read_start;
    default: return NULL;
    }
}

void fu_network_report(void)
{
    fu_logf("network: protocol 1 slot fields: %u written, %u read, %u rejected",
            (unsigned)g_written, (unsigned)g_read, (unsigned)g_rejected);
}
