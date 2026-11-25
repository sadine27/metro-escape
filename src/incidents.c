/*
 * INCIDENT LOG - a STACK of closures and delays.
 * Applying an incident mutates the network; popping it undoes exactly
 * that change (closures reopen, delay ranges are subtracted again).
 */
#include "metro.h"

static Incident stack_[MAX_INCIDENTS];
static int top_ = 0;

int g_clock = 8 * 60 + 30;        /* shared simulation clock, 08:30 */

static void delay_range(Metro *m, int line, int from, int to, int amount)
{
    if (from <= to) seg_add(&m->delay[line], from, to, amount);
    else {                                         /* wraps past midnight */
        seg_add(&m->delay[line], from, DAY_MIN - 1, amount);
        seg_add(&m->delay[line], 0, to, amount);
    }
}

int incident_count(void) { return top_; }

const Incident *incident_at(int i) { return &stack_[i]; }

/* returns 1 on success, 0 if full / invalid / already closed */
int incident_push(Metro *m, Incident in)
{
    if (top_ >= MAX_INCIDENTS) return 0;
    if (in.type == INC_CLOSE) {
        if (in.station < 0 || in.station >= m->nStations || m->closed[in.station]) return 0;
        m->closed[in.station] = 1;
    } else {
        if (in.line < 0 || in.line >= m->nLines || in.amount <= 0) return 0;
        delay_range(m, in.line, in.from, in.to, in.amount);
    }
    stack_[top_++] = in;
    return 1;
}

int incident_pop(Metro *m, Incident *out)
{
    Incident in;
    if (top_ == 0) return 0;
    in = stack_[--top_];
    if (in.type == INC_CLOSE) m->closed[in.station] = 0;
    else delay_range(m, in.line, in.from, in.to, -in.amount);
    if (out) *out = in;
    return 1;
}
