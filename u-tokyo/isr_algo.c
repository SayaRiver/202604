#include "isr_algo.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int cmp_double(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}

static double mean_of(const double *v, int n) {
  double s = 0; for (int i = 0; i < n; i++) s += v[i]; return n > 0 ? s / n : 0;
}

/* population standard deviation, same as numpy.std (ddof=0) */
static double std_of(const double *v, int n) {
  if (n <= 0) return 0;
  double m = mean_of(v, n), s = 0;
  for (int i = 0; i < n; i++) s += (v[i] - m) * (v[i] - m);
  return sqrt(s / n);
}

static double median_of(double *v, int n) {
  if (n <= 0) return 0;
  double *c = malloc(sizeof(double) * n);
  memcpy(c, v, sizeof(double) * n);
  qsort(c, n, sizeof(double), cmp_double);
  double m = (n % 2) ? c[n / 2] : 0.5 * (c[n / 2 - 1] + c[n / 2]);
  free(c);
  return m;
}

int isr_analyze(const isr_msg_t *msgs, int n, isr_result_t *out) {
  memset(out, 0, sizeof(*out));
  out->periodic = -1;
  if (n <= 0 || n > ISR_MAX_MSGS) return -1;

  /* ---------- Algorithm 1 / Algorithm 2 state, one pass over the window ---------- */
  double normal_iv[ISR_MAX_MSGS], burst_iv[ISR_MAX_MSGS], jit[ISR_MAX_MSGS], sizes[ISR_MAX_MSGS];
  int n_norm = 0, n_burst = 0, n_jit = 0, n_sz = 0;
  double mu = 0, sigma = 0;
  int have_last = 0;
  double last_t = 0;

  int in_burst = 0;          /* burst state (Sec 3.3.2) */
  int qos0_after_burst = 0;  /* consecutive QoS0 seen since the last QoS!=0 message */
  int burst_msgs = 0;        /* QoS!=0 messages of the current burst run */
  double burst_first_t = 0, burst_last_t = 0;
  int n_qos0 = 0;
  double first_t = msgs[0].t, last_all_t = msgs[n - 1].t;

  for (int i = 0; i < n; i++) {
    const isr_msg_t *m = &msgs[i];
    if (m->qos == '0') {
      n_qos0++;
      sizes[n_sz++] = m->len;                       /* Alg 1 line 10 */
      if (have_last) {
        double iv = m->t - last_t;
        if (!in_burst) {
          normal_iv[n_norm++] = iv;                  /* Alg 1 line 5-6 */
          mu = mean_of(normal_iv, n_norm);           /* Alg 1 line 11 */
          double expected = last_t + mu;             /* Alg 1 line 7  */
          jit[n_jit++] = fabs(m->t - expected);      /* Alg 1 line 8-9 */
          if (n_jit > 1) sigma = std_of(jit, n_jit);
        } else {
          burst_iv[n_burst++] = iv;
          if (++qos0_after_burst >= 2) {             /* Sec 3.3.2: two consecutive QoS0 end the burst */
            in_burst = 0;
            qos0_after_burst = 0;
          }
        }
      }
      last_t = m->t;
      have_last = 1;
    } else {                                          /* QoS!=0 starts / continues a burst */
      if (!in_burst) {
        in_burst = 1;
        burst_msgs = 0;
        burst_first_t = m->t;
      }
      qos0_after_burst = 0;
      burst_msgs++;
      burst_last_t = m->t;
    }
  }

  /* ---------- UE type (Sec 3.3.3 step b): median of all intervals, < 0.5 s = Continuous ---------- */
  int n_all = n_norm + n_burst;
  double all_iv[2 * ISR_MAX_MSGS];
  memcpy(all_iv, normal_iv, sizeof(double) * n_norm);
  memcpy(all_iv + n_norm, burst_iv, sizeof(double) * n_burst);
  if (n_all > 0) {
    double med = median_of(all_iv, n_all);
    out->periodic = (med >= 0.5) ? 1 : 0;
  }
  out->mu_period_ms = mu * 1000.0;

  /* ---------- Algorithm 1 ---------- */
  if (out->periodic == 1 && n_jit > 0) {
    out->has_jitter = 1;
    double cv = (mu > 0) ? sigma / mu : 0;                       /* Eq 3.3 */
    double alpha = cv * 2.0;                                      /* Eq 3.2, scaling_factor = 2 */
    if (alpha < 0.5) alpha = 0.5;
    if (alpha > 3.0) alpha = 3.0;
    out->cv = cv;
    out->alpha = alpha;

    double base_window = 0.2;                                     /* ms */
    double sr_window = base_window;
    if (n_jit > 1) {
      double jmin = jit[0], jmax = jit[0];
      for (int i = 1; i < n_jit; i++) {
        if (jit[i] < jmin) jmin = jit[i];
        if (jit[i] > jmax) jmax = jit[i];
      }
      sr_window = base_window + alpha * ((jmax - jmin) * 1000.0);   /* Eq 3.1 */
    }
    out->sr_window_ms = sr_window;
    out->sr_period_ms = mu * 1000.0;                              /* transmission_period */
    double avg_size = mean_of(sizes, n_sz);
    out->bsr_bytes = avg_size * (1.0 + 0.1);                      /* Eq 3.4, redundancy 0.1 */
  } else if (n_all > 0) {
    /* Not periodic, or no jitter sample: continuous branch, as in socketclientASR.py.
     * The paper leaves this "default" branch undefined. */
    double maxlen = 0;
    for (int i = 0; i < n_sz; i++) if (sizes[i] > maxlen) maxlen = sizes[i];
    out->bsr_bytes = maxlen * 1.5;
    out->sr_period_ms = median_of(all_iv, n_all) * 1000.0;
    out->sr_window_ms = 0;
  } else {
    /* no intervals at all: nothing to analyse yet */
    out->bsr_bytes = (n_sz > 0) ? sizes[0] * 1.5 : 0;
  }

  /* ---------- Algorithm 2 ---------- */
  out->in_burst_now = in_burst;
  out->burst_msgs = burst_msgs;
  out->active_sr_window_ms = out->sr_window_ms;
  out->active_sr_period_ms = out->sr_period_ms;
  out->active_bsr_bytes = out->bsr_bytes;

  if (in_burst && out->has_jitter) {
    /* rate in messages per second:
     *   normal_rate  = QoS0 messages / window duration
     *   current_rate = burst messages / burst duration (at least one normal period) */
    double window_s = last_all_t - first_t;
    double normal_rate = (window_s > 0) ? n_qos0 / window_s : 0;
    double burst_s = burst_last_t - burst_first_t;
    if (burst_s < mu) burst_s = mu;
    double current_rate = (burst_s > 0) ? burst_msgs / burst_s : 0;

    double intensity = 1.0;
    if (normal_rate > 0 && current_rate > 0) {
      intensity = current_rate / normal_rate;
      if (intensity > ISR_MAX_INTENSITY) intensity = ISR_MAX_INTENSITY;
    }
    out->intensity = intensity;
    out->adj_sr_window_ms = out->sr_window_ms / intensity;   /* Alg 2 line 10 */
    out->adj_sr_period_ms = out->sr_period_ms / intensity;   /* Alg 2 line 11 */
    out->adj_bsr_bytes = out->bsr_bytes * intensity;         /* Alg 2 line 12 */
    out->active_sr_window_ms = out->adj_sr_window_ms;
    out->active_sr_period_ms = out->adj_sr_period_ms;
    out->active_bsr_bytes = out->adj_bsr_bytes;
  }
  return 0;
}
