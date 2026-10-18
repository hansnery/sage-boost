/* Optional offline loader policy. Preserve the local relative placement
 * of group, mesh and FX bodies. No image entry point or asset is persisted. */
#ifndef NATIVE_SHARED_ARENA_H
#define NATIVE_SHARED_ARENA_H
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#define NA_REQUIRE(c) do { if(!(c)) {fprintf(stderr,"benchmark refused: native arena: %s\n",#c);exit(1);} } while(0)
static unsigned char *na_base;
static unsigned na_size,na_count;
static struct {unsigned first,last;} na_ranges[64];
static void *na_table[8];
static void *na_allocate(unsigned image_size,unsigned rva,unsigned size)
{
    NA_REQUIRE(image_size>=4096 && image_size<128*1024*1024 && rva<image_size && size<=image_size-rva);
    if(!na_base) {
        na_size=image_size;na_base=VirtualAlloc(NULL,na_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        NA_REQUIRE(na_base);
    }
    NA_REQUIRE(na_size==image_size && na_count<64);
    for(unsigned i=0;i<na_count;i++)NA_REQUIRE(rva+size<=na_ranges[i].first || rva>=na_ranges[i].last);
    na_ranges[na_count].first=rva;na_ranges[na_count++].last=rva+size;
    DWORD old;NA_REQUIRE(VirtualProtect(na_base+rva,size,PAGE_READWRITE,&old));
    return na_base+rva;
}
static int na_release_body(void *body)
{ return (unsigned char *)body>=na_base && (unsigned char *)body<na_base+na_size; }
#define PF_NATIVE_BODY_ALLOC(image_size,rva,size) na_allocate(image_size,rva,size)
#define PF_NATIVE_BODY_FREE(body) na_release_body(body)
#define PF_NATIVE_TABLE_ALLOC(body,size) na_table
#endif
