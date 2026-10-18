#define main sort_screen_main
#include "bench_native_sort.c"
#undef main
#include "queueinsert.h"
#include "native_contract_image.h"

static unsigned refusals, observed, observation_hash;
static unsigned fallback_calls;
static void *expected_first,*expected_last;
static pf_queue_compare_fn expected_compare;
static void __cdecl fallback_spy(void *first,void *last,pf_queue_compare_fn compare)
{
    REQUIRE(first==expected_first && last==expected_last && compare==expected_compare);
    fallback_calls++;
}

static void guard_controls(void)
{
    pf_queue_sort_configure(fallback_spy,(pf_queue_compare_fn)bodies[0][8],(pf_queue_compare_fn)bodies[0][9]);
    for(unsigned test=0;test<10;test++) {
        setup(2,0,1);expected_first=seed.records;expected_last=seed.records+2;
        expected_compare=(pf_queue_compare_fn)bodies[0][8];
        switch(test) {
        case 0:expected_first=(char *)seed.records+1;expected_last=(char *)(seed.records+2)+1;break;
        case 1:expected_first=seed.records+2;expected_last=seed.records;break;
        case 2:expected_last=(char *)seed.records+1;break;
        case 3:seed.records[0].word[0]=0;break;
        case 4:seed.records[0].word[1]=7;break;
        case 5:meshes[0].refs=0;break;
        case 6:meshes[0].refs=0xffff0000;break;
        case 7:methods[0].refs=0;break;
        case 8:methods[0].refs=0xffff0000;break;
        default:expected_compare=NULL;break;
        }
        unsigned hash=fnv((const unsigned char *)&seed,sizeof seed)^fnv((const unsigned char *)meshes,sizeof meshes)^fnv((const unsigned char *)methods,sizeof methods);
        fallback_calls=0;pf_queue_sort_run(expected_first,expected_last,expected_compare);
        REQUIRE(fallback_calls==1);
        REQUIRE(hash==(fnv((const unsigned char *)&seed,sizeof seed)^fnv((const unsigned char *)meshes,sizeof meshes)^fnv((const unsigned char *)methods,sizeof methods)));
    }
    setup(128,0,6);fallback_calls=0;
    pf_queue_sort_run(seed.records,seed.records+128,(pf_queue_compare_fn)bodies[0][8]);
    REQUIRE(!fallback_calls);references(128);
    pf_queue_sort_configure((pf_queue_sort_fn)bodies[0][30],(pf_queue_compare_fn)bodies[0][8],(pf_queue_compare_fn)bodies[0][9]);
    printf("SORT_GUARDS,10,VALID_FAST_PATH,1\n");
}

static unsigned __cdecl observing_compare(const Record *a,const Record *b)
{
    const Mesh *am=(const Mesh *)a->word[0],*bm=(const Mesh *)b->word[0];
    observation_hash=observation_hash*16777619u ^ a->word[0] ^ b->word[0];
    observation_hash=observation_hash*16777619u ^ am->refs ^ (bm->refs<<16);
    for(unsigned i=0;i<32;i++)observation_hash=observation_hash*16777619u ^ methods[i].refs;
    observed++;
    return ((Compare)bodies[0][8])(a,b);
}

static void callback_control(void)
{
    setup(128,0,6);
    memcpy(&original,&seed,sizeof seed);memcpy(&candidate,&seed,sizeof seed);
    observed=observation_hash=0;
    sort_checked(bodies[0][30],original.records,original.records+128,observing_compare,0);
    unsigned calls=observed,hash=observation_hash;
    observed=observation_hash=0;
    sort_checked(candidate_entry,candidate.records,candidate.records+128,observing_compare,0);
    REQUIRE(calls && observed==calls && observation_hash==hash);
    REQUIRE(!memcmp(&original,&candidate,sizeof original) && !sort_abi_errors);references(128);
}

static void shared_controls(void)
{
    unsigned checked=0;
    for(unsigned pattern=0;pattern<6;pattern++)for(unsigned count=1;count<=6;count+=5)
        for(unsigned comparator=0;comparator<2;comparator++)for(unsigned heap=0;heap<2;heap++) {
            setup(129,pattern,count);
            for(unsigned i=0;i<129;i++)meshes[i].refs=0;
            for(unsigned i=0;i<32;i++)methods[i].refs=0;
            for(unsigned i=0;i<129;i++) {
                Mesh *mesh=meshes+(i/3)*3;mesh->refs++;
                seed.records[i].word[0]=(unsigned)mesh;
                for(unsigned j=0;j<count;j++) {
                    Method *method=(pattern&1) && (j&1)?NULL:methods;
                    seed.records[i].word[2+j]=(unsigned)method;
                    if(method)method->refs++;
                }
            }
            memcpy(&original,&seed,sizeof seed);memcpy(&candidate,&seed,sizeof seed);
            unsigned mesh_hash=fnv((const unsigned char *)meshes,sizeof meshes);
            unsigned method_hash=fnv((const unsigned char *)methods,sizeof methods);
            sort_checked(bodies[0][heap?29:30],original.records,original.records+129,(Compare)bodies[0][8+comparator],heap);
            if(heap)sort_checked(bodies[0][26],original.records,original.records+129,(Compare)bodies[0][8+comparator],0);
            REQUIRE(mesh_hash==fnv((const unsigned char *)meshes,sizeof meshes));
            REQUIRE(method_hash==fnv((const unsigned char *)methods,sizeof methods));
            run_candidate(candidate.records,candidate.records+129,comparator,heap);
            REQUIRE(!memcmp(&original,&candidate,sizeof original) && !sort_abi_errors);
            REQUIRE(mesh_hash==fnv((const unsigned char *)meshes,sizeof meshes));
            REQUIRE(method_hash==fnv((const unsigned char *)methods,sizeof methods));
            checked++;
        }
    printf("SHARED_SORT_CONTROLS,%u,ABI,%u\n",checked,sort_abi_errors);
}

static void refused(ContractImage *image,const pf_image *scan,const char *expected)
{
    unsigned hash=fnv(image->base,image->size);
    pf_queue_sort_sites sites;memset(&sites,0xa5,sizeof sites);
    const char *why=pf_queue_sort_find(scan,&sites);
    if(!why || strcmp(why,expected)) {
        fprintf(stderr,"sort finder expected %s, got %s\n",expected,why?why:"success");REQUIRE(0);
    }
    REQUIRE(!sites.entry && !sites.mesh && !sites.mesh_state);
    REQUIRE(!pf_queue_sort_install(scan,&why) && why);
    REQUIRE(hash==fnv(image->base,image->size));refusals++;
}

static void finder_controls(ContractImage *image,ContractManifest *sort,pf_image *scan)
{
    pf_queue_sort_sites sites;
    const char *why=pf_queue_sort_find(scan,&sites);
    if(why)fprintf(stderr,"unmodified sort finder: %s\n",why);
    REQUIRE(!why && sites.entry==sort->rva[30]);
    REQUIRE((unsigned char *)sites.mesh==image->base+sort->rva[8]);
    REQUIRE((unsigned char *)sites.mesh_state==image->base+sort->rva[9]);
    const unsigned bodies_to_change[]={0,10,26,29,32};
    for(unsigned i=0;i<sizeof bodies_to_change/sizeof bodies_to_change[0];i++) {
        unsigned body=bodies_to_change[i],off=15;
        for(unsigned j=0;j<sort->patch_count;j++) {
            ContractPatch p=sort->patches[j];
            if(p.body==body && off>=p.field && off<p.field+4)off=p.field+4;
        }
        REQUIRE(off<sort->size[body]);
        image->base[sort->rva[body]+off]^=1;refused(image,scan,"complete body differs");
        image->base[sort->rva[body]+off]^=1;
    }
    for(unsigned i=0;i<sort->patch_count;i++) {
        ContractPatch p=sort->patches[i];unsigned *field=(void *)(image->base+p.rva+p.field),save=*field;
        *field=p.absolute?(unsigned)image->base:-(p.rva+p.field+4);
        refused(image,scan,"dependency section role");*field=save;
    }
    const unsigned roots[]={30,8,9},lengths[]={48,29,47};
    for(unsigned i=0;i<3;i++) {
        unsigned rva=sort->rva[roots[i]],size=lengths[i];
        image->base[rva]^=1;refused(image,scan,"entry signature missing");image->base[rva]^=1;
        unsigned char *copy=image->base+scan->text_rva+scan->text_size-48,saved[48];
        memcpy(saved,copy,size);memcpy(copy,image->base+rva,size);
        refused(image,scan,"ambiguous entry signature");memcpy(copy,saved,size);
    }
    IMAGE_NT_HEADERS32 *nt=(void *)(image->base+((IMAGE_DOS_HEADER *)image->base)->e_lfanew);
    unsigned size=nt->OptionalHeader.SizeOfImage;
    nt->OptionalHeader.SizeOfImage=scan->text_rva+scan->text_size-1;
    refused(image,scan,"invalid text range");nt->OptionalHeader.SizeOfImage=size;
    REQUIRE(!pf_queue_sort_find(scan,&sites));
}

static void insertion_control(ContractImage *image,const ContractManifest *insert)
{
    struct { Record *first,*last,*limit; } queue[3]={{0}};
    Record slot;queue[0].first=queue[0].last=&slot;queue[0].limit=&slot+1;
    unsigned char model[192]={0};Mesh mesh={0};Method method={0};
    mesh.refs=2;mesh.key=(unsigned)model;method.refs=1;
    void *words[]={&method};struct {void **first,**last,**limit;} stack={words,words+1,words+1};
    ((pf_queue_add_fn)(image->base+insert->rva[0]))(queue,&mesh,&stack);
    REQUIRE(queue[0].last==&slot+1 && slot.word[0]==(unsigned)&mesh && slot.word[1]==1 &&
            slot.word[2]==(unsigned)&method && mesh.refs==2 && method.refs==2);
    for(unsigned i=3;i<8;i++)REQUIRE(!slot.word[i]);
    ((void *(__attribute__((thiscall)) *)(void *,void *,void *))(image->base+insert->rva[9]))(queue,&slot,&slot+1);
    REQUIRE(queue[0].last==&slot && mesh.refs==1 && method.refs==1);
}

int main(int argc,char **argv)
{
    REQUIRE(argc==4 && bench_no_game());load(argv[1],argv[2]);guard_controls();
    unsigned refused_once=0;
    for(unsigned order=0;order<2;order++) {
        ContractImage image;ContractManifest sort,insert;pf_image scan;
        contract_map(&image,argv[1]);contract_rebase(&image,&sort,argv[2]);contract_rebase(&image,&insert,argv[3]);
        REQUIRE(pf_image_open(&scan,image.base));
        if(!order) {finder_controls(&image,&sort,&scan);refused_once=refusals;}
        pf_queue_sort_sites found;REQUIRE(!pf_queue_sort_find(&scan,&found));
        contract_execute(&image);
        const char *why;
        if(!order)REQUIRE(pf_queue_insert_install(&scan,&why));
        REQUIRE(pf_queue_sort_install(&scan,&why));
        if(order)REQUIRE(pf_queue_insert_install(&scan,&why));
        candidate_entry=image.base+found.entry;
        candidate_compare[0]=(Compare)found.mesh;candidate_compare[1]=(Compare)found.mesh_state;
        cases=0;controls();shared_controls();callback_control();insertion_control(&image,&insert);
        REQUIRE(!pf_queue_sort_install(&scan,&why));
        printf("RUNTIME_SORT,%u,ORDER,%u,REFUSALS,%u,ABI,%u\n",cases,order,refused_once,sort_abi_errors);
        REQUIRE(VirtualFree(image.base,0,MEM_RELEASE));
    }
    REQUIRE(bench_no_game());return 0;
}
