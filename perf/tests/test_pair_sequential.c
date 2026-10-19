/* Pure policy qualification: no game, renderer or FPS measurement. */
#include <stdio.h>
#include <string.h>
#include "pairpace.h"

static unsigned checks, failures;
#define CHECK(c) do { checks++; if(!(c)) { failures++; \
    if(failures<12)fprintf(stderr,"FAIL %s:%u: %s\n",__FILE__,__LINE__,#c); } } while(0)

static unsigned native_time(unsigned long long t) { return (unsigned)(t/1000)*1000; }

static void phases_and_changes(void)
{
    for(unsigned wrap=0;wrap<2;wrap++) for(unsigned phase=0;phase<1000;phase++) {
        const unsigned long long start=wrap?4294960000ull:1000000ull;
        for(unsigned fault=0;fault<4;fault++) {
            unsigned long long now=start+phase+12000;
            unsigned original=native_time(start), deadline=original+33000;
            pf_pairpace p;pf_pair_reset(&p);
            pf_pair_begin(&p,1,native_time(now),12000,33000,1);
            pf_pair_limit_sequential(&p,native_time(now),12000,7078,3);
            CHECK(p.quota==2 && p.deadline==deadline);
            unsigned count=0;
            while(count<3) {
                unsigned delay=0, expected=fault==2 && count?20000:12000;
                if(!pf_pair_plan_sequential(&p,native_time(now),expected,7078,&delay))break;
                now+=delay;
                if(fault==3 && !count)now+=25000; /* late wake must recheck */
                unsigned before=native_time(now);
                if(!pf_pair_claim(&p,before,expected))break;
                CHECK((int)(deadline-before)>=(int)(expected+PF_PAIR_RESERVE_US));
                now+=(fault==1 && !count)?12000:(!count?7000:12000);
                count++;
                CHECK((int)(native_time(now)-deadline)<=0);
                CHECK(p.deadline==deadline);
            }
            CHECK(count==(fault==0?2u:fault==3?0u:1u));
            unsigned quota=p.quota;
            pf_pair_limit_sequential(&p,native_time(now),1,1,3);
            CHECK(p.quota==quota); /* a repeated call cannot enlarge the ceiling */
        }
    }
}

static void one_extra_equivalence(void)
{
    for(unsigned period=10000;period<=200000;period+=1000)
        for(unsigned elapsed=0;elapsed<period;elapsed+=997) {
            unsigned expected=100+elapsed%17000,avg=expected/2+1;
            pf_pairpace old,new;
            pf_pair_reset(&old);pf_pair_reset(&new);
            pf_pair_begin(&old,1,0xffff0000u+elapsed,elapsed,period,1);
            new=old;
            pf_pair_limit(&old,old.observed,expected,1);
            pf_pair_limit_sequential(&new,new.observed,expected,avg,1);
            CHECK(!memcmp(&old,&new,sizeof old));
            unsigned a=123,b=123;
            CHECK(pf_pair_plan(&old,old.observed,expected,avg,&a)==
                  pf_pair_plan_sequential(&new,new.observed,expected,avg,&b));
            CHECK(a==b);
        }
}

static void invalid_and_periods(void)
{
    const unsigned inputs[][3]={{0,0,3},{100,101,3},{~0u,1,3},{100,0,3},{100,50,0},{100,50,4}};
    for(unsigned i=0;i<sizeof inputs/sizeof inputs[0];i++) {
        pf_pairpace p;pf_pair_reset(&p);
        pf_pair_begin(&p,1,12000,12000,33000,1);
        pf_pair_limit_sequential(&p,12000,inputs[i][0],inputs[i][1],inputs[i][2]);
        CHECK(p.quota==1);
    }
    pf_pairpace p;pf_pair_reset(&p);
    unsigned long long now=4294960000ull,origin=now;
    unsigned sum=0;
    for(unsigned seq=1;seq<=6000;seq++) {
        unsigned period=(31+seq%7)*1000;
        unsigned base=native_time(now),delay;
        now+=6000+seq%1000;
        pf_pair_begin(&p,seq,native_time(now),native_time(now)-base,period,1);
        pf_pair_limit_sequential(&p,native_time(now),12000,4125,3);
        while(p.used<p.quota && pf_pair_plan_sequential(&p,native_time(now),12000,4125,&delay)) {
            now+=delay;
            if(!pf_pair_claim(&p,native_time(now),12000))break;
            now+=seq%64==0?12000:4000;
        }
        sum+=period;
        CHECK(p.deadline==(unsigned)origin+sum);
        CHECK((int)(native_time(now)-p.deadline)<=0);
        while(!pf_pair_wait_done(&p,native_time(now)))now=(now/1000+1)*1000;
        CHECK(now-origin==sum);
    }
    pf_pair_begin(&p,6001,native_time(now)+10000,10000,33000,0);
    CHECK(!p.active && !pf_pair_plan_sequential(&p,native_time(now),12000,7000,&sum));
}

int main(void)
{
    phases_and_changes();
    one_extra_equivalence();
    invalid_and_periods();
    printf("sequential policy: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
