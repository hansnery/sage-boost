/* Native animation work whose results are consumed by actual draws.
 * Reuse our private-image loader/oracles; no native bytes are compiled in. */
#define main unused_decoder_screen_main
#include "bench_native_decode.c"
#undef main
#include "pose_packet.h"

static volatile LONG fill_count;
static unsigned immutable_channels,immutable_streams,immutable_filter;
static unsigned cursor_advance;

void native_pose_init(const char *image,const char *manifest,unsigned advance)
{
    REQUIRE(advance<=1);cursor_advance=advance;
    load_native(image,manifest);setup();controls();
    REQUIRE(!decoder_abi_errors);
    immutable_channels=fnv((const unsigned char*)channels,sizeof channels);
    immutable_streams=fnv((const unsigned char*)streams,sizeof streams);
    immutable_filter=fnv((const unsigned char*)filter,sizeof filter);
    printf("POSE DECODER: native ABI/cursor/alias controls passed; immutable synthetic channels, warm advance=%u\n",advance);
}

void native_pose_fill(const float *base,float *output,unsigned bones)
{
    REQUIRE(base && output && base!=output && bones && bones<=43200);
    memcpy(output,base,bones*32);
    for(unsigned b=0;b<bones;b++) {
        /* The generated channel has three components and 128 frames.
         * The incoming position is a synthetic warm cursor value; outputs
         * update the three position words in this bone, not scratch work.
         * Keep the quaternion and padding bit-identical to the source. */
        Channel *channel=channels+1+(b%16)*3;
        unsigned from=16+b%64,next[3];
        unsigned *position=(unsigned*)(output+b*8+4);
        native[1](channel,position,from,from+cursor_advance,position,next);
        /* Consume the decoded next endpoint even in the zero-advance case.
         * No interpolation/hierarchy composition is modeled here. */
        memcpy(position,next,12);
    }
    InterlockedIncrement(&fill_count);
}

void native_pose_close(unsigned expected_fills)
{
    REQUIRE((unsigned)fill_count==expected_fills && !decoder_abi_errors);
    REQUIRE(immutable_channels==fnv((const unsigned char*)channels,sizeof channels));
    REQUIRE(immutable_streams==fnv((const unsigned char*)streams,sizeof streams));
    REQUIRE(immutable_filter==fnv((const unsigned char*)filter,sizeof filter));
    printf("POSE: fills=%u native_vector_calls=%u; channel/stream/filter bytes unchanged\n",
           expected_fills,expected_fills*43200);
    for(unsigned i=0;i<3;i++)REQUIRE(VirtualFree((void*)native[i],0,MEM_RELEASE));
}
