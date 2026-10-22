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
                   const tab5_ui_state_t *s, bool first)
{
    uint64_t now = s->now_us / step;
    if (first) {
        /* A first snapshot can already contain a delayed measurement. Initialize
         * the whole history before exposing any earlier slots through count. */
        for (unsigned j = 0; j < cap; j++) empty(&ring[j]);
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
            if (*count < cap) (*count)++;
        }
        *bucket = now;
    }
    for (unsigned i = 0; i < 2; i++) {
        const tab5_measure_sensor_t *m = &s->measurement.sensor[i];
        if (!m->valid) {
            ring[*next].v[2 * i] = ring[*next].v[2 * i + 1] = TAB5_UI_GRAPH_NONE;
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
           g->last_slow, 500000, s, !g->initialized);
    sample(g->fast, 300, &g->fast_next, &g->fast_count, &g->fast_bucket,
           g->last_fast, 100000, s, !g->initialized);
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
