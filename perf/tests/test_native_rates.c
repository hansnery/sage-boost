/* Offline native rate-setter check. No executable entry, imports or game
 * subsystems run. Native bytes are read locally into private process memory. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define REQUIRE(c) do { if (!(c)) { fprintf(stderr,"rate check refused line %d: %s\n",__LINE__,#c); exit(1); } } while (0)

static struct { unsigned before[4], values[10], after[4]; } storage;
static const float constants[3] = { 0.001f, 1000.0f, 1.0f };
volatile unsigned rate_abi_errors;
void rates_checked(void *,int,int);
__asm__(
".text\n.globl _rates_checked\n_rates_checked:\n"
"pushl %ebp\nmovl %esp,%ebp\npushl %ebx\npushl %esi\npushl %edi\nsubl $4,%esp\n"
"movl %esp,-16(%ebp)\nmovl 8(%ebp),%eax\npushl 16(%ebp)\npushl 12(%ebp)\n"
"movl $0x13579bdf,%ebx\nmovl $0x2468ace0,%esi\nmovl $0x76543210,%edi\ncall *%eax\naddl $8,%esp\n"
"cmpl -16(%ebp),%esp\nje 1f\nincl _rate_abi_errors\n1:\n"
"cmpl $0x13579bdf,%ebx\nje 2f\nincl _rate_abi_errors\n2:\n"
"cmpl $0x2468ace0,%esi\nje 3f\nincl _rate_abi_errors\n3:\n"
"cmpl $0x76543210,%edi\nje 4f\nincl _rate_abi_errors\n4:\n"
"leal -12(%ebp),%esp\npopl %edi\npopl %esi\npopl %ebx\npopl %ebp\nret\n");

static unsigned fnv(const unsigned char *p,unsigned n)
{ unsigned h=2166136261u; while(n--)h=(h^*p++)*16777619u; return h; }

static void *load_body(const char *path,const char *manifest)
{
    FILE *f=fopen(path,"rb"); REQUIRE(f && !fseek(f,0,SEEK_END));
    long length=ftell(f); rewind(f); REQUIRE(length>4096 && length<128*1024*1024);
    unsigned char *raw=malloc(length); REQUIRE(raw);
    REQUIRE(fread(raw,1,length,f)==(size_t)length); fclose(f);
    REQUIRE(((IMAGE_DOS_HEADER *)raw)->e_magic==IMAGE_DOS_SIGNATURE);
    unsigned pe=((IMAGE_DOS_HEADER *)raw)->e_lfanew;
    REQUIRE(pe<=(unsigned)length-sizeof(IMAGE_NT_HEADERS32));
    IMAGE_NT_HEADERS32 *nt=(void *)(raw+pe);
    REQUIRE(nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_I386 &&
            nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC);
    IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    REQUIRE((unsigned char *)(sections+nt->FileHeader.NumberOfSections)<=raw+length);
    f=fopen(manifest,"r"); REQUIRE(f);
    unsigned rva,size,hash,count,source=0,matches=0;
    REQUIRE(fscanf(f,"%u %u %u %u",&rva,&size,&hash,&count)==4 && size==191 && count==13);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER *s=sections+i;
        if(rva>=s->VirtualAddress && rva-s->VirtualAddress<=s->SizeOfRawData &&
           size<=s->SizeOfRawData-(rva-s->VirtualAddress) && (s->Characteristics&IMAGE_SCN_MEM_EXECUTE)) {
            REQUIRE(s->PointerToRawData<=(unsigned)length && s->SizeOfRawData<=(unsigned)length-s->PointerToRawData);
            source=s->PointerToRawData+rva-s->VirtualAddress; matches++;
        }
    }
    REQUIRE(matches==1 && fnv(raw+source,size)==hash);
    unsigned char *code=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); REQUIRE(code);
    memcpy(code,raw+source,size); free(raw);
    unsigned seen=0,fields[13];
    for(unsigned i=0;i<count;i++) {
        unsigned field,slot;
        REQUIRE(fscanf(f,"%u %u",&field,&slot)==2 && field<=size-4 && slot<13 && !(seen&(1u<<slot)));
        for(unsigned j=0;j<i;j++)REQUIRE(field+4<=fields[j] || fields[j]+4<=field);
        fields[i]=field; seen|=1u<<slot;
        const void *target=slot<3?(const void *)(constants+slot):(const void *)(storage.values+slot-3);
        memcpy(code+field,&target,4);
    }
    char extra; REQUIRE(fscanf(f," %c",&extra)==EOF && seen==8191); fclose(f);
    DWORD old; REQUIRE(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));
    REQUIRE(FlushInstructionCache(GetCurrentProcess(),code,4096)); return code;
}

static double as_float(unsigned word)
{ float value; memcpy(&value,&word,4); return value; }

static void check_result(int logic,int client)
{
    REQUIRE(!rate_abi_errors && storage.values[0]==(unsigned)logic && storage.values[1]==(unsigned)client);
    const double expected[8]={logic/1000.0,1000.0/logic,(double)logic,1.0/logic,
                              1000.0/client,client/1000.0,(double)client,1.0/client};
    for(unsigned i=0;i<8;i++) {
        double actual=as_float(storage.values[i+2]),error=actual-expected[i];
        if(error<0)error=-error;
        REQUIRE(error<=expected[i]*2e-7); /* Includes native single-precision rounding. */
    }
    for(unsigned i=0;i<4;i++)REQUIRE(storage.before[i]==0xa5a5a5a5 && storage.after[i]==0xa5a5a5a5);
}

int main(int argc,char **argv)
{
    REQUIRE(argc==4); unsigned fault=(unsigned)atoi(argv[3]); REQUIRE(fault<=3);
    void *body=load_body(argv[1],argv[2]);
    /* Expected clamps are explicit observations to test, not a second copy
     * of the native branch algorithm. Nonintegral ratios are not endorsed. */
    static const int cases[][4]={
        {5,30,5,30},{10,60,10,60},{5,60,5,30},{10,120,10,60},
        {0,0,1,1},{-7,-2,1,1},{10,5,10,10},{5,34,5,34},
        {5,35,5,30},{1,60,1,6},{30,60,30,60},{60,60,60,60},{5,30,5,30}
    };
    /* This baseline is the setter's computed 5/30 result, not the file's
     * startup constants. Python records the two one-ULP differences. */
    unsigned baseline[10]; memset(baseline,0,sizeof baseline);
    for(unsigned c=0;c<sizeof cases/sizeof cases[0];c++) {
        memset(&storage,0xa5,sizeof storage);
        rates_checked(body,cases[c][0],cases[c][1]);
        if(fault==1)storage.values[1]=30;
        if(fault==2)storage.values[2]=0x3ba3d70a; /* stale 5 Hz milliseconds scale */
        if(fault==3)storage.values[9]=0x3d088889; /* stale 30 Hz client step */
        check_result(cases[c][2],cases[c][3]);
        if(!c)memcpy(baseline,storage.values,sizeof baseline);
        printf("RATE,%d,%d",cases[c][0],cases[c][1]);
        for(unsigned i=0;i<10;i++)printf(",%08x",storage.values[i]);
        puts("");
    }
    REQUIRE(!memcmp(baseline,storage.values,sizeof baseline));
    for(unsigned repeat=0;repeat<256;repeat++) {
        rates_checked(body,10,60); check_result(10,60);
        rates_checked(body,5,30); check_result(5,30);
        REQUIRE(!memcmp(baseline,storage.values,sizeof baseline));
    }
    REQUIRE(VirtualFree(body,0,MEM_RELEASE));
    printf("PASS,13,512,ABI,%u\n",rate_abi_errors); return 0;
}
