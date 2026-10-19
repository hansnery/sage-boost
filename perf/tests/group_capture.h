/* Fixed-fixture follower capture only. No production pointer/lifetime or
 * native exception contract is established by this adapter. */
extern int bench_scope_bulk_cancel(void);
static int gc_capture_eligible(const unsigned *regs)
{
    unsigned *frame=(unsigned *)regs[2];
    const unsigned char *current=(const void *)regs[0],*first=(const void *)regs[4];
    const unsigned char *end=(const void *)frame[2];
    if(gc_mode<3 || !gc_fast_capture || ng_current_group>=GROUPS ||
       ((nm_begin_callback || nm_end_callback) && gc_fault!=11) ||
       first!=records[ng_current_group] || end!=first+sizeof records[0] ||
       !gc_group.count || gc_group.count>=GROUP_SIZE || current!=first+gc_group.count*PF_RECORD_SIZE ||
       frame[-1]!=1 || frame[-9]!=1 || frame[-11]!=1 || regs[6]!=1 ||
       frame[-7]!=(unsigned)(current+PF_RECORD_SIZE) || regs[1]!=(unsigned)&g_method ||
       g_method.refs<2 || gc_group.passes!=1 || *(const unsigned *)(first+4)!=1)return 0;
    for(const unsigned char *p=current;p<end;p+=PF_RECORD_SIZE) {
        unsigned index=ng_current_group*GROUP_SIZE+(p-first)/PF_RECORD_SIZE;
        fake_mesh *mesh=*(fake_mesh *const *)p;
        if(mesh!=many_meshes+index || *(const unsigned *)(p+4)!=1 ||
           *(const void *const *)(p+8)!=&g_method ||
           *(const void *const *)(mesh->b+PF_MESH_MODEL)!=g_model ||
           *(const unsigned *)(mesh->b+PF_MESH_BASE_VERTEX)!=0)return 0;
    }
    return 1;
}
static unsigned gc_capture_guard_cases;
static void gc_capture_guard_checks(const unsigned *actual)
{
    unsigned regs[8],storage[14];memcpy(regs,actual,sizeof regs);
    memcpy(storage,(const unsigned *)actual[2]-11,sizeof storage);regs[2]=(unsigned)(storage+11);
    unsigned *frame=storage+11;
    unsigned char *first=records[ng_current_group],*current=(unsigned char *)regs[0];
    fake_mesh *mesh=*(fake_mesh **)current;
    REQUIRE(gc_capture_eligible(regs));
#define GC_REFUSE(field,value) do { __typeof__(field) saved=(field);(field)=(value); \
    REQUIRE(!gc_capture_eligible(regs));(field)=saved;REQUIRE(gc_capture_eligible(regs));gc_capture_guard_cases++; } while(0)
    GC_REFUSE(gc_fast_capture,0);
    GC_REFUSE(regs[0],(unsigned)first);
    GC_REFUSE(regs[4],(unsigned)(first+PF_RECORD_SIZE));
    GC_REFUSE(frame[2],frame[2]-PF_RECORD_SIZE);
    GC_REFUSE(gc_group.count,0);GC_REFUSE(gc_group.count,GROUP_SIZE);
    GC_REFUSE(frame[-1],0);GC_REFUSE(frame[-9],0);GC_REFUSE(frame[-11],0);
    GC_REFUSE(regs[6],2);GC_REFUSE(frame[-7],regs[0]);
    GC_REFUSE(regs[1],0);GC_REFUSE(g_method.refs,1);GC_REFUSE(gc_group.passes,2);
    GC_REFUSE(*(unsigned *)(first+4),2);
    GC_REFUSE(*(unsigned *)(current+4),2);GC_REFUSE(*(void **)(current+8),NULL);
    GC_REFUSE(*(fake_mesh **)current,NULL);
    GC_REFUSE(*(void **)(mesh->b+PF_MESH_MODEL),NULL);
    GC_REFUSE(*(unsigned *)(mesh->b+PF_MESH_BASE_VERTEX),1);
#undef GC_REFUSE
    printf("CAPTURE GUARDS: %u actual-frame predicate refusals; each restored to eligible\n",gc_capture_guard_cases);
}
int gc_capture_bulk(unsigned *regs)
{
    if(!gc_capture_eligible(regs))return 0;
    if(!gc_capture_guard_cases && !gc_fault)gc_capture_guard_checks(regs);
    unsigned *frame=(unsigned *)regs[2];
    const unsigned char *current=(const void *)regs[0],*end=(const void *)frame[2];
    if(!bench_scope_bulk_begin())return 0;
    unsigned refs=g_method.refs,n=(unsigned)(end-current)/PF_RECORD_SIZE;
    for(const unsigned char *p=current;p<end;p+=PF_RECORD_SIZE) {
        if(gc_fault==9 && p+PF_RECORD_SIZE==end)continue;
        gc_capture_palette(*(fake_mesh *const *)p);
    }
    REQUIRE(g_method.refs==refs);
    /* No native owning temporary was created for skipped followers. */
    if(gc_fault==10)REQUIRE(bench_scope_bulk_cancel());
    else REQUIRE(bench_scope_bulk_end(n));
    gc_bulk_captured+=n;frame[-7]=(unsigned)end;return 1;
}
void gc_bulk_bridge(void);
__asm__(".text\n.globl _gc_bulk_bridge\n_gc_bulk_bridge:\n"
        "pushfl\ncmpl $0,_gc_mode\njne 1f\npopfl\njmp _ng_group_bridge\n1:\n"
        "pushal\npushl %esp\ncall _gc_capture_bulk\naddl $4,%esp\ntestl %eax,%eax\nje 2f\n"
        "popal\npopfl\njmp *_ng_group_resume\n2:\npopal\npopfl\nleal -0x134(%ebp),%eax\njmp *_ng_group_fallback\n");
