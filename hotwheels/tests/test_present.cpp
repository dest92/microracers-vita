#include "present/present_math.h"
#include "present/presenter_init_state.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);std::abort(); } } while(0)
int main() {
    using namespace hw::present;
    auto r=fit_rect(640,480,960,544);
    CHECK(r.x==117 && r.y==0 && r.w==725 && r.h==544);
    CHECK(argb_to_abgr(0xFF123456u)==0xFF563412u);
    uint8_t src[12]={1,2,3,9,9,9,4,5,6,9,9,9};
    uint8_t dst[10]={0xEE,0,0,0,0xEE,0,0,0,0xEE,0xEE};
    CHECK(copy_index_rows(dst,4,src,6,3,2));
    CHECK(dst[0]==1 && dst[1]==2 && dst[2]==3 && dst[3]==0);
    CHECK(dst[4]==4 && dst[5]==5 && dst[6]==6 && dst[7]==0);
    CHECK(dst[8]==0xEE && dst[9]==0xEE);
    CHECK(!copy_index_rows(dst,2,src,6,3,2));

    PresenterInitState state;
    CHECK(!state.context_started() && !state.ready());
    int context_starts=0;
    auto attempt_init=[&](bool succeeds) {
        if(!state.context_started()) { ++context_starts;state.mark_context_started(); }
        state.mark_failed(); // e.g. shader allocation/link failed
        if(succeeds) state.mark_ready();
        return state.ready();
    };
    CHECK(!attempt_init(false));
    CHECK(state.context_started() && !state.ready()); // resize must retry full init
    CHECK(attempt_init(true));
    CHECK(context_starts==1); // retry resources, keep the existing GL context
    CHECK(state.context_started() && state.ready());
}
