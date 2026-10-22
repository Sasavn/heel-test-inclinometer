#include "../main/tab5_time.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(tab5_time_fields_valid(2026,10,22,14,32,0));
    assert(tab5_time_fields_valid(2000,2,29,23,59,59));
    assert(tab5_time_fields_valid(2028,2,29,0,0,0));
    assert(tab5_time_fields_valid(2099,12,31,23,59,59));
    assert(!tab5_time_fields_valid(2026,2,29,0,0,0));
    assert(!tab5_time_fields_valid(2026,4,31,0,0,0));
    assert(!tab5_time_fields_valid(1900,2,28,0,0,0));
    assert(!tab5_time_fields_valid(2100,1,1,0,0,0));
    assert(!tab5_time_fields_valid(2026,0,1,0,0,0));
    assert(!tab5_time_fields_valid(2026,13,1,0,0,0));
    assert(!tab5_time_fields_valid(2026,1,0,0,0,0));
    assert(!tab5_time_fields_valid(2026,1,1,24,0,0));
    assert(!tab5_time_fields_valid(2026,1,1,0,60,0));
    assert(!tab5_time_fields_valid(2026,1,1,0,0,60));
    for(int i=0;i<6;i++){int v[]={2026,1,1,0,0,0};v[i]=-1;assert(!tab5_time_fields_valid(v[0],v[1],v[2],v[3],v[4],v[5]));}
    puts("touch clock calendar: 20 cases PASS");return 0;
}
