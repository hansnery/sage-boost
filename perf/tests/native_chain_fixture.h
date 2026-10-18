/* Controlled owning methods for native group/chain ABI fixtures. */
static fake_method chain_children[6];
static fake_method *chain_links[7];
static unsigned chain_count=6,chain_allow,chain_broken,chain_begins,chain_sets;

int bench_chain_gate(unsigned count)
{
    return count==1 || (chain_allow && chain_broken!=3 && count>=2 && count<=6);
}

static unsigned chain_index(fake_method *method)
{
    if(method==&g_method)return 0;
    for(unsigned i=0;i<6;i++)if(method==chain_children+i)return i+1;
    REQUIRE(0);return 0;
}

static void __attribute__((thiscall)) chain_set_next(fake_method *self,fake_method *next)
{
    unsigned index=chain_index(self);
    REQUIRE(self->refs>=5);
    if(next) { chain_index(next);REQUIRE(next->refs>=6); }
    /* The native walker transfers an already AddRef'd by-value argument.
     * Keep that reference as the link, releasing the previous owned link. */
    if(chain_broken==2 && !next && chain_links[index])return;
    if(chain_links[index]) { REQUIRE(chain_links[index]->refs>5);chain_links[index]->refs--; }
    chain_links[index]=next;chain_sets++;
}

static void chain_refs(void)
{
    REQUIRE(g_method.refs==5);
    for(unsigned i=0;i<6;i++)REQUIRE(chain_children[i].refs==5);
    for(unsigned i=0;i<7;i++)REQUIRE(!chain_links[i]);
}

static int __attribute__((thiscall)) chain_begin(void *self,int *passes,int flags)
{
    fake_method *method=self;
    unsigned length=0;
    while(method) {
        unsigned index=chain_index(method);
        REQUIRE(++length<=6 && method->refs>5);
        method=chain_links[index];
    }
    REQUIRE(length==chain_count);chain_begins++;
    return nx_begin(self,passes,flags);
}

static void chain_records(unsigned count)
{
    REQUIRE(count>=1 && count<=6);chain_count=count;
    for(unsigned group=0;group<GROUPS;group++)for(unsigned i=0;i<GROUP_SIZE;i++) {
        unsigned *record=(unsigned *)(records[group]+i*PF_RECORD_SIZE);
        record[1]=count;record[2]=(unsigned)&g_method;
        for(unsigned j=1;j<6;j++)record[2+j]=j<count?(unsigned)&chain_children[j-1]:0;
    }
}

