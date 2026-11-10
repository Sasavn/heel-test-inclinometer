#include "tab5_input.h"
void tab5_input_switch_init(tab5_input_switch *s, bool on, uint32_t now, uint32_t ms) {
    s->stable=s->candidate=on; s->since=now; s->debounce_ms=ms;
}
tab5_input_event tab5_input_switch_sample(tab5_input_switch *s, bool on, uint32_t now) {
    if(on!=s->candidate) { s->candidate=on; s->since=now; }
    if(s->candidate!=s->stable && (uint32_t)(now-s->since)>=s->debounce_ms) {
        s->stable=s->candidate;
        return s->stable ? TAB5_INPUT_ON : TAB5_INPUT_OFF_LOCAL;
    }
    return TAB5_INPUT_NONE;
}
void tab5_input_encoder_init(tab5_input_encoder *s, uint8_t ab) {
    s->previous=ab&3; s->partial=0;
}
int tab5_input_encoder_sample(tab5_input_encoder *s, uint8_t ab) {
    static const int8_t delta[16]={0,1,-1,0,-1,0,0,1,1,0,0,-1,0,-1,1,0};
    ab&=3;
    if((s->previous^ab)==3) s->partial=0;
    else s->partial+=delta[(s->previous<<2)|ab];
    s->previous=ab;
    if(s->partial==4) { s->partial=0; return 1; }
    if(s->partial==-4) { s->partial=0; return -1; }
    return 0;
}
