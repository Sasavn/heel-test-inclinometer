#include <assert.h>
#include <stdio.h>
#include "tab5_input.h"
int main(void) {
 tab5_input_switch s;
 for(int boot=0;boot<2;boot++) {
  tab5_input_switch_init(&s,boot,0,20);
  assert(tab5_input_switch_sample(&s,boot,100)==TAB5_INPUT_NONE);
  assert(tab5_input_switch_sample(&s,!boot,101)==TAB5_INPUT_NONE);
  assert(tab5_input_switch_sample(&s,boot,110)==TAB5_INPUT_NONE);
  assert(tab5_input_switch_sample(&s,!boot,111)==TAB5_INPUT_NONE);
  assert(tab5_input_switch_sample(&s,!boot,130)==TAB5_INPUT_NONE);
  assert(tab5_input_switch_sample(&s,!boot,131)==(boot?TAB5_INPUT_OFF_LOCAL:TAB5_INPUT_ON));
  assert(tab5_input_switch_sample(&s,!boot,1000)==TAB5_INPUT_NONE);
 }
 tab5_input_switch_init(&s,0,UINT32_MAX-10,20);
 assert(tab5_input_switch_sample(&s,1,UINT32_MAX-9)==TAB5_INPUT_NONE);
 assert(tab5_input_switch_sample(&s,1,9)==TAB5_INPUT_NONE);
 assert(tab5_input_switch_sample(&s,1,10)==TAB5_INPUT_ON);
 /* Remote stop while physical ON cannot trigger restart. */
 assert(tab5_input_switch_sample(&s,1,10000)==TAB5_INPUT_NONE);
 tab5_input_encoder e;
 const unsigned char cw[]={0,1,3,2}, ccw[]={0,2,3,1};
 for(int dir=0;dir<2;dir++) for(int phase=0;phase<4;phase++) {
  const unsigned char *q=dir?ccw:cw;
  tab5_input_encoder_init(&e,q[phase]);
  for(int i=1;i<=4;i++) {
   assert(tab5_input_encoder_sample(&e,q[(phase+i)%4])==(i==4?(dir?-1:1):0));
   assert(tab5_input_encoder_sample(&e,q[(phase+i)%4])==0);
  }
 }
 tab5_input_encoder_init(&e,0);
 assert(!tab5_input_encoder_sample(&e,1));
 assert(!tab5_input_encoder_sample(&e,0)); /* bounce reverses */
 assert(!tab5_input_encoder_sample(&e,3)); /* lost edge resets */
 assert(e.partial==0);
 puts("tab5_input: PASS");
}
