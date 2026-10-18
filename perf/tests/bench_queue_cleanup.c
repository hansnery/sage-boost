/* Offline normal-return screen only. The native loader verifies bodies
 * from the user's image and keeps them in private RAM. No runtime hook. */
#define PF_NATIVE_INSERT_EMBED
#include "bench_native_insert.c"

typedef Record *(__attribute__((thiscall)) *Erase)(Queue *,Record *,Record *);
typedef void (__attribute__((thiscall)) *Destroy)(Record *);
static unsigned fast_records,native_records,negative;
static unsigned delete_count;
typedef struct {unsigned object,finish,method_refs[8];} DeleteEvent;
static DeleteEvent delete_events[CAPACITY+8],expected_events[CAPACITY+8];
static Queue *observed_queue;
static Object objects[CAPACITY+8];
static Record records[CAPACITY],saved_records[CAPACITY];

static void __attribute__((thiscall)) observe_delete(Object *object)
{
    REQUIRE(!object->refs);
    REQUIRE(delete_count<CAPACITY+8);
    DeleteEvent *event=delete_events+delete_count++;
    event->object=(unsigned)(object-objects);
    /* Native callbacks observe the old finish, and all prior releases. */
    event->finish=(unsigned)(observed_queue->last-observed_queue->first);
    for(unsigned i=0;i<8;i++)event->method_refs[i]=objects[i].refs;
}
static void *object_vt[]={observe_delete};

static int direct_record(Record *record)
{
    Object *mesh=(void *)record->word[0];
    if(mesh && mesh->refs<=1)return 0;
    for(unsigned i=2;i<8;i++) {
        Object *method=(void *)record->word[i];
        /* At most six repeated method holders can be released here. */
        if(method && (method==mesh || method->refs<=6))return 0;
    }
    for(unsigned i=8;i-->2;) {
        Object *method=(void *)record->word[i];
        if(method)method->refs--;
    }
    if(mesh && !negative)mesh->refs--;
    return 1;
}
static Record *__attribute__((thiscall)) candidate_clear(Queue *queue,Record *first,Record *last)
{
    REQUIRE(first==queue->first && last==queue->last);
    for(Record *record=first;record!=last;record++) {
        if(direct_record(record))fast_records++;
        else {native_records++;((Destroy)bodies[11])(record);}
    }
    queue->last=first;
    return first;
}
static void prepare(Queue *queue,unsigned count,unsigned methods,unsigned pattern,unsigned external)
{
    for(unsigned i=0;i<CAPACITY+8;i++) {objects[i].vtable=object_vt;objects[i].refs=external;}
    memset(records,0xa5,sizeof records);
    for(unsigned i=0;i<count;i++) {
        Record *r=records+i;
        Object *mesh=objects+8+i;
        r->word[0]=(unsigned)mesh;r->word[1]=methods;mesh->refs++;
        for(unsigned j=0;j<6;j++) {
            Object *method=j<methods?objects+(pattern==1?0:j):NULL;
            if(pattern==2 && (j&1))method=NULL;
            if(pattern==3 && j<methods)method=mesh;
            r->word[2+j]=(unsigned)method;
            if(method)method->refs++;
        }
    }
    *queue=(Queue){records,records+count,records+CAPACITY};observed_queue=queue;
    delete_count=0;fast_records=native_records=0;
}
static void oracle(int condition)
{ if(!condition) {fputs("cleanup oracle mismatch\n",stderr);exit(1);} }
static void check(unsigned count,unsigned methods,unsigned pattern,unsigned external)
{
    static unsigned expected_refs[CAPACITY+8];
    unsigned expected_deletes=0;
    for(unsigned mode=0;mode<2;mode++) {
        Queue q;prepare(&q,count,methods,pattern,external);
        memcpy(saved_records,records,sizeof records);
        Erase clear=mode?candidate_clear:(Erase)bodies[9];
        Record *result=clear(&q,q.first,q.last);
        oracle(result==records && q.first==records && q.last==records && q.limit==records+CAPACITY);
        oracle(!memcmp(records,saved_records,sizeof records));
        if(!mode) {
            expected_deletes=delete_count;
            memcpy(expected_events,delete_events,delete_count*sizeof *delete_events);
            for(unsigned i=0;i<CAPACITY+8;i++)expected_refs[i]=objects[i].refs;
        } else {
            oracle(delete_count==expected_deletes);
            oracle(!memcmp(delete_events,expected_events,delete_count*sizeof *delete_events));
            for(unsigned i=0;i<CAPACITY+8;i++)oracle(objects[i].refs==expected_refs[i]);
            oracle(fast_records+native_records==count);
            if(count && !methods && external)oracle(fast_records==count);
            if(count && (!external || (pattern==3 && methods)))oracle(native_records==count);
        }
    }
}
static unsigned long long timed(unsigned mode,unsigned methods)
{
    unsigned long long total=0;
    for(unsigned repeat=0;repeat<8;repeat++) {
        Queue q;prepare(&q,CAPACITY,methods,0,2);
        Erase clear=mode?candidate_clear:(Erase)bodies[9];
        LARGE_INTEGER before,after;
        REQUIRE(QueryPerformanceCounter(&before));
        Record *result=clear(&q,q.first,q.last);
        REQUIRE(QueryPerformanceCounter(&after));
        total+=(unsigned long long)(after.QuadPart-before.QuadPart);
        REQUIRE(result==records && q.last==q.first && !delete_count);
        for(unsigned i=0;i<CAPACITY+8;i++)REQUIRE(objects[i].refs==2);
    }
    return total;
}
int main(int argc,char **argv)
{
    REQUIRE(argc==5 && bench_no_game());load(argv[1],argv[2]);
    unsigned samples=(unsigned)atoi(argv[3]);negative=(unsigned)atoi(argv[4]);
    REQUIRE(samples<=101 && negative<=1);
    const unsigned sizes[]={0,1,2,25,256,CAPACITY},method_counts[]={0,1,3,6};
    unsigned cases=0;
    for(unsigned n=0;n<6;n++)for(unsigned m=0;m<4;m++)for(unsigned p=0;p<4;p++)for(unsigned e=0;e<2;e++) {
        check(sizes[n],method_counts[m],p,e?2:0);cases++;
    }
    printf("CLEANUP_CONTROLS,%u,exact records/references/callback order and observed state\n",cases);
    LARGE_INTEGER frequency;REQUIRE(QueryPerformanceFrequency(&frequency));
    printf("FREQUENCY,%llu\n",(unsigned long long)frequency.QuadPart);
    for(unsigned m=0;m<4 && samples;m++) {
        REQUIRE(bench_no_game());
        for(unsigned warm=0;warm<4;warm++) {timed(0,method_counts[m]);timed(1,method_counts[m]);}
        for(unsigned sample=0;sample<samples;sample++) {
            unsigned long long a,b;
            if(sample&1) {b=timed(1,method_counts[m]);a=timed(0,method_counts[m]);}
            else {a=timed(0,method_counts[m]);b=timed(1,method_counts[m]);}
            printf("CLEANUP_TIMING,%u,%u,%llu,%llu\n",method_counts[m],sample,a,b);
        }
    }
    REQUIRE(bench_no_game());return 0;
}
