/* Production selection/eligibility, with the ownership fixture's COM stubs. */
#define main ownership_fixture_main
#include "test_inst_ownership.c"
#undef main

int main(void)
{
    void *volatile callback=NULL;
    g_bulk_sites=(pf_inst_bulk_sites){.mesh=(void *)1,.triangles=(void *)2,.draw=(void *)3,
                                    .begin_callback=&callback,.end_callback=&callback};
    g_bulk_on=g_bulk_context=g.skinned=1;
    unsigned cases=0,selected=0,unsampled_open=0;
    for(unsigned extra=0;extra<2;extra++)for(unsigned verify=0;verify<3;verify++)
    for(unsigned window=0;window<2;window++)for(unsigned samples=0;samples<=7;samples++)
    for(unsigned shadow=0;shadow<2;shadow++)for(unsigned shadows=0;shadows<=4;shadows++)
    for(unsigned bonelist=0;bonelist<2;bonelist++)for(unsigned quota=0;quota<=3;quota++)
    for(unsigned priority=0;priority<3;priority++) {
        g_extra=extra;g_verify=verify;g_window_open=window;g_window_samples=samples;
        g.shadow=shadow;g_window_shadow=shadows;g.bonelist=bonelist;g.cat=shadow?PF_INST_VARIANTS:0;
        g_cat_window[g.cat]=g_bl_window[g.cat]=(unsigned char)quota;g_bl_samples=59+priority;
        int expected=0;
        if(!extra && verify && window && samples<6 && (!shadow || shadows<3)) {
            if(bonelist)expected=quota<2;
            else expected=quota<2 && (priority>=1 || samples<3);
        }
        g.dips=1;g.capture_reason=0;g.mode=M_LEADER;
        unsigned groups=g_take.groups,leaders=g_take.leaders;
        evaluate();
        REQUIRE(g.mode==(expected?M_VERIFY:M_INST));
        REQUIRE(g_take.groups==groups+!extra && g_take.leaders==leaders+!extra);
        REQUIRE(g_window_samples==samples && g_window_shadow==shadows);
        REQUIRE(bulk_eligible()==!expected);
        selected+=expected;unsampled_open+=!expected && !extra && verify && window;cases++;
    }
    g_extra=1;g_verify=1;g_window_open=1;
    callback=(void *)4;REQUIRE(!bulk_eligible());callback=NULL;
    g_bulk_context=0;REQUIRE(!bulk_eligible());g_bulk_context=1;
    g_bulk_on=0;REQUIRE(!bulk_eligible());g_bulk_on=1;
    g.skinned=0;REQUIRE(!bulk_eligible());g.skinned=1;
    g_bulk_sites.draw=NULL;REQUIRE(!bulk_eligible());
    printf("VERIFY_POLICY_OK,%u cases,%u selected,%u unsampled regular open-window cases; counters and other gates preserved\n",cases,selected,unsampled_open);
    return 0;
}
