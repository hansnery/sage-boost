#ifndef FU_NETWORK_H
#define FU_NETWORK_H

#include "engine.h"

/* Protocol 1: marker:1 start+1:4 template+2:7 colour+1:4, little-endian.
 * Outputs are signed bytes in
 * the game's caller; templates -2/-1 mean Observer/Random. */
#define FU_NETWORK_BYTES 2

unsigned char *__cdecl fu_network_write(unsigned char *cursor, unsigned ignored,
                                       const unsigned char *limit, const void *slot);
unsigned char *__cdecl fu_network_read(unsigned char *cursor, unsigned *out,
                                      const unsigned char *limit);
void __cdecl fu_network_split(unsigned packed, char color_offset, char faction_offset,
                             unsigned char *color, unsigned char *faction);
unsigned char *__cdecl fu_network_write_start(unsigned char *cursor, unsigned ignored,
                                             const unsigned char *limit);
unsigned char *__cdecl fu_network_read_start(unsigned char *cursor, unsigned char *out,
                                            const unsigned char *limit);
void *fu_network_stub(fu_hook hook);
void fu_network_report(void);

#endif
