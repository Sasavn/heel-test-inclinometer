#include "tab5_ui_graph.h"
#include <math.h>
#include <string.h>

void tab5_ui_graph_reset(tab5_ui_graph_t *g)
{
    memset(g, 0, sizeof(*g));
}

static void empty(tab5_ui_graph_point_t *p)
{
    for (unsigned k = 0; k < 4; k++) p->v[k] = TAB5_UI_GRAPH_NONE;
}

static void sample(tab5_ui_graph_point_t *ring, unsigned cap, unsigned *next, unsigned *count,
                   uint64_t *bucket, uint64_t last[2], uint64_t step,
                   const tab5_ui_state_t *s, bool first, tab5_ui_graph_t *meta)
{
    uint64_t now = s->now_us / step;
    if (first) {
        /* A first snapshot can already contain a delayed measurement. Initialize
         * the whole history before exposing any earlier slots through count. */
        for (unsigned j = 0; j < cap; j++) empty(&ring[j]);
        if(meta){memset(meta->fast_real,0,sizeof(meta->fast_real));memset(meta->fast_bridge,0,sizeof(meta->fast_bridge));}
        *bucket = now;
        *next = 0;
        *count = 1;
    }
    if (now > *bucket) {
        uint64_t n = now - *bucket;
        if (n > cap) n = cap;
        for (uint64_t j = 0; j < n; j++) {
            *next = (*next + 1) % cap;
            empty(&ring[*next]);
            if(meta)meta->fast_real[*next]=meta->fast_bridge[*next]=0;
            if (*count < cap) (*count)++;
        }
        *bucket = now;
    }
    for (unsigned i = 0; i < 2; i++) {
        const tab5_measure_sensor_t *m = &s->measurement.sensor[i];
        if(meta&&(!m->valid||m->fail_streak||m->errors!=meta->render_errors[i]))meta->render_blocked[i]=true;
        if(meta)meta->render_errors[i]=m->errors;
        if (!m->valid) {
            ring[*next].v[2 * i] = ring[*next].v[2 * i + 1] = TAB5_UI_GRAPH_NONE;
            if(meta){meta->fast_real[*next]|=1u<<i;meta->fast_bridge[*next]&=~(1u<<i);}
            continue;
        }
        /* Snapshot arrival advances the grid; only a new sensor timestamp
         * supplies data. Never let a repeated/older reply replace newer data. */
        if (!m->good || !m->timestamp_us || m->timestamp_us > s->now_us ||
            m->timestamp_us <= last[i]) continue;
        uint64_t age = now - m->timestamp_us / step;
        if (age >= cap) continue;
        unsigned slot = (*next + cap - (unsigned)age) % cap;
        if (*count <= age) *count = (unsigned)age + 1;
        if(meta){
            /* Two sequential 115200 8N1 transactions: gap + 17 wire bytes
             * each. UI snapshots are nominally 100ms; faster bus replies
             * can legitimately be absent from the presentation snapshot. */
            unsigned hz=s->measurement.config.frequency_hz;
            if(hz<1)hz=1;
            if(hz>50)hz=50;
            unsigned gap=s->measurement.config.gap_ms;
            if(gap<15)gap=15;
            if(gap>100)gap=100;
            uint64_t period=(1000000u+hz-1)/hz;
            uint64_t bus=2*(gap*1000u+1476u);
            if(period<bus)period=bus;
            if(period<100000)period=100000;
            /* good counts replies, not UI snapshots. A healthy UI snapshot
             * may skip several replies. Still cap the presentation edge to
             * 500ms (or three periods for slow acquisition): a reconnect must
             * never draw an arbitrary multi-second line through missing UI. */
            uint64_t replies=m->good>meta->render_good[i]?m->good-meta->render_good[i]:0;
            uint64_t limit=period*3;
            if(limit<500000)limit=500000;
            /* Saturate before multiplying an untrusted/reset reply counter. */
            uint64_t allowance=replies>limit/period+1?limit:period*replies*3/2;
            if(allowance>limit)allowance=limit;
            bool bridge=last[i]&&replies&&!meta->render_blocked[i]&&
                        m->timestamp_us-last[i]<=allowance;
            /* Later replies within one bucket must not turn an earlier
             * dropout edge into a healthy edge. Keep its original decision. */
            bool same=last[i]/step==m->timestamp_us/step&&(meta->fast_real[slot]&(1u<<i));
            if(!same||meta->render_blocked[i]){
                meta->fast_bridge[slot]&=~(1u<<i);
                if(bridge&&!same)meta->fast_bridge[slot]|=1u<<i;
            }
            meta->fast_real[slot]|=1u<<i;
            meta->render_good[i]=m->good;meta->render_blocked[i]=false;
        }
        last[i] = m->timestamp_us;
        float v[2] = {m->x, m->y};
        for (unsigned a = 0; a < 2; a++) {
            ring[slot].v[2 * i + a] = isfinite(v[a]) && fabsf(v[a]) <= 327.66f
                ? (int16_t)lroundf(v[a] * 100) : TAB5_UI_GRAPH_NONE;
        }
    }
}

void tab5_ui_graph_sample(tab5_ui_graph_t *g, const tab5_ui_state_t *s)
{
    if (!s->now_known) return;
    if (g->initialized && s->now_us < g->last_now) tab5_ui_graph_reset(g);
    sample(g->slow, 1200, &g->slow_next, &g->slow_count, &g->slow_bucket,
           g->last_slow, 500000, s, !g->initialized, NULL);
    sample(g->fast, 300, &g->fast_next, &g->fast_count, &g->fast_bucket,
           g->last_fast, 100000, s, !g->initialized, g);
    g->initialized = true;
    g->last_now = s->now_us;
}

unsigned tab5_ui_graph_read(const tab5_ui_graph_t *g, unsigned seconds, unsigned series, int32_t *out)
{
    bool fast = seconds == 30;
    unsigned n = fast ? 300 : seconds * 2, cap = fast ? 300 : 1200;
    unsigned count = fast ? g->fast_count : g->slow_count, next = fast ? g->fast_next : g->slow_next;
    const tab5_ui_graph_point_t *ring = fast ? g->fast : g->slow;
    if (n > cap) n = cap;
    for (unsigned i = 0; i < n; i++) {
        unsigned age = n - 1 - i;
        int16_t v = age < count ? ring[(next + cap - age) % cap].v[series] : TAB5_UI_GRAPH_NONE;
        out[i] = v == TAB5_UI_GRAPH_NONE ? LV_CHART_POINT_NONE : v;
    }
    return n;
}

unsigned tab5_ui_graph_read_render(const tab5_ui_graph_t *g,unsigned seconds,unsigned series,int32_t *out)
{
    unsigned n=tab5_ui_graph_read(g,seconds,series,out);
    if(seconds!=30)return n;
    int previous=-1;unsigned bit=1u<<(series/2);
    for(unsigned j=0;j<n;j++){
        unsigned slot=(g->fast_next+300-(n-1-j))%300;
        if(out[j]==LV_CHART_POINT_NONE){
            /* An actual sample with an invalid axis (or explicit offline)
             * is a barrier, unlike an unsampled grid bucket. */
            if(g->fast_real[slot]&bit)previous=-1;
            continue;
        }
        if(previous>=0&&(g->fast_bridge[slot]&bit)){
            int32_t a=out[previous],b=out[j];
            for(unsigned k=(unsigned)previous+1;k<j;k++)
                out[k]=a+(int32_t)lround((double)(b-a)*(k-previous)/(j-previous));
        }
        previous=(int)j;
    }
    return n;
}
