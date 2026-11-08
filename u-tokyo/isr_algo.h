/* ISR algorithms (paper Sec 3.3, Algorithms 1-3), pure computation, no I/O.
 *
 * Algorithm 1  SR window analysis      -> sr_window, sr_period, bsr_size
 * Algorithm 2  burst parameter adjust  -> adjusted sr_window, sr_period, bsr_size
 * Algorithm 3  per-topic (per-UE) run  -> the caller keeps one message buffer per topic
 */
#ifndef ISR_ALGO_H
#define ISR_ALGO_H

#include <stdint.h>

#define ISR_MAX_MSGS 128   /* window size per topic (ring buffer in the caller) */
#define ISR_MAX_INTENSITY 3.0  /* Algorithm 2 max_intensity: NOT given in paper, assumption */

typedef struct {
  double t;      /* arrival time, seconds */
  char   qos;    /* '0', '1', '2' */
  double len;    /* frame length, bytes */
} isr_msg_t;

typedef struct {
  int    periodic;        /* 1 = Periodic, 0 = Continuous, -1 = unknown (no intervals) */
  int    has_jitter;      /* Algorithm 1 SR-window branch taken */
  double cv, alpha;       /* Eq 3.2, 3.3 */
  double mu_period_ms;    /* transmission period */
  double sr_window_ms;    /* Eq 3.1 (periodic) or spread-based fallback (continuous) */
  double sr_period_ms;
  double bsr_bytes;       /* Eq 3.4 (periodic) or max*1.5 (continuous fallback) */

  int    in_burst_now;    /* Algorithm 2 condition: still in burst at end of window */
  int    burst_msgs;      /* QoS!=0 messages in the current burst run */
  double intensity;       /* min(current_rate / normal_rate, ISR_MAX_INTENSITY) */
  double adj_sr_window_ms;   /* Algorithm 2 output, valid when in_burst_now */
  double adj_sr_period_ms;
  double adj_bsr_bytes;

  /* the parameters that should be applied to the gNB for this topic */
  double active_sr_window_ms, active_sr_period_ms, active_bsr_bytes;
} isr_result_t;

/* msgs[0..n-1] in arrival order (one topic). Returns 0 on success. */
int isr_analyze(const isr_msg_t *msgs, int n, isr_result_t *out);

#endif
