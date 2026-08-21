#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <nats/nats.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#pragma pack(1)
#define  MAX_NUM_CCs 5
#include "public_inc/nfapi_nr_interface.h"
#include "public_inc/nfapi_nr_interface_scf.h"
#pragma pack()

#include "nfapi_server_client/nfapi_proxy_command.h"

#define NATS_ADDR "192.168.182.10"
#define NATS_PORT 4222

/* UDP control channel exposed by nfapi_proxy (control_func_via_udp in sample_agent.c) */
#define PROXY_CONTROL_IP   "192.168.181.20"
#define PROXY_CONTROL_PORT 5555

/* ------------------------------------------------------------------------
 * Per-RNTI bounded history (ring buffer).
 *
 * The original Python prototype (nfapiserver_withsend_realtime.py) kept an
 * unbounded list per topic and re-ran the analysis over the *entire* history
 * on every message (O(n) per message, O(n^2) over a run). For large-scale
 * nr-UE validation that memory/CPU growth is unacceptable, so history here
 * is capped at UE_HISTORY_CAPACITY samples per UE; analysis always runs over
 * just that bounded window.
 * ------------------------------------------------------------------------ */
#define UE_HISTORY_CAPACITY 128
#define UE_HASH_BUCKETS     1024

typedef struct {
  double   timestamp;   /* seconds, monotonic-ish (CLOCK_REALTIME) */
  char     qos;         /* '0' or '2' */
  uint32_t frame_len;   /* MAC PDU length in bytes */
  char     topic[64];   /* MQTT topic, for topic-switch prediction (Algorithm 1) */
} ue_sample_t;

typedef struct ue_state {
  uint16_t          rnti;
  ue_sample_t       history[UE_HISTORY_CAPACITY];
  int               count;   /* valid samples, <= UE_HISTORY_CAPACITY */
  int               head;    /* next write slot */
  struct ue_state  *next;    /* hash chain */
} ue_state_t;

static ue_state_t    *ue_table[UE_HASH_BUCKETS];
static pthread_mutex_t ue_table_mutex = PTHREAD_MUTEX_INITIALIZER;

static ue_state_t *get_or_create_ue(uint16_t rnti)
{
  int idx = rnti % UE_HASH_BUCKETS;
  for (ue_state_t *p = ue_table[idx]; p != NULL; p = p->next) {
    if (p->rnti == rnti) {
      return p;
    }
  }
  ue_state_t *ue = calloc(1, sizeof(ue_state_t));
  ue->rnti = rnti;
  ue->next = ue_table[idx];
  ue_table[idx] = ue;
  return ue;
}

static void ue_add_sample(ue_state_t *ue, double timestamp, char qos, uint32_t frame_len, const char *topic)
{
  ue->history[ue->head].timestamp = timestamp;
  ue->history[ue->head].qos = qos;
  ue->history[ue->head].frame_len = frame_len;
  snprintf(ue->history[ue->head].topic, sizeof(ue->history[ue->head].topic), "%s", topic ? topic : "");
  ue->head = (ue->head + 1) % UE_HISTORY_CAPACITY;
  if (ue->count < UE_HISTORY_CAPACITY) {
    ue->count++;
  }
}

/* Returns the i-th sample in chronological order (0 = oldest, count-1 = newest). */
static ue_sample_t *ue_sample_at(ue_state_t *ue, int i)
{
  int start = (ue->count < UE_HISTORY_CAPACITY) ? 0 : ue->head;
  return &ue->history[(start + i) % UE_HISTORY_CAPACITY];
}

static double now_seconds(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ------------------------------------------------------------------------
 * MQTT PUBLISH extraction from a MAC PDU payload.
 *
 * The original Python prototype (nfapiserver_withsend_realtime.py) scanned
 * for a magic 0x80 marker byte, 4 bytes ahead of a QoS byte - that pattern
 * was tuned to a different (non-IP) capture pipeline and does not appear
 * anywhere in this system's actual over-the-air encapsulation.
 *
 * Live capture against real pubx.py traffic (RNTI 0x3cb5, 2026-08-11) shows
 * these PDUs actually carry: [variable-length MAC subheader] [IPv4 header]
 * [TCP header, dst port 1883] [MQTT bytes] - e.g. one captured PDU decoded
 * to src=12.1.1.67 (the UE's own PDU session address) dst=192.168.70.150
 * (our mqtt-broker), and another fragment's payload was plaintext
 * "...] Current tempe..." matching pubx.py's generated message text. So
 * instead of the magic-byte heuristic, this parses the real IPv4/TCP/MQTT
 * framing: find the IPv4 header (no assumption on the MAC subheader length,
 * which was observed to vary between samples - 8 and 13 bytes seen), skip
 * IP+TCP headers using their own length fields, and read the MQTT PUBLISH
 * fixed header (message type 0x3, QoS bits) + topic string from what's left.
 *
 * A single PDU only matches when it happens to contain the *start* of a
 * PUBLISH packet (large messages get segmented across multiple PDUs by
 * lower layers, which this does not reassemble) - continuation-only
 * fragments legitimately won't match, which is fine since only one
 * fragment per message needs to carry the header for QoS/topic detection.
 * ------------------------------------------------------------------------ */
typedef struct {
  bool valid;
  char qos;               /* '0' or '2' */
  char topic[256];
} mqtt_info_t;

static bool find_ipv4_tcp_start(const uint8_t *data, uint32_t len, uint32_t *ip_off)
{
  for (uint32_t i = 0; i + 20 <= len; i++) {
    if ((data[i] & 0xF0) != 0x40) continue;               /* IPv4 */
    uint8_t ihl = (uint8_t)(data[i] & 0x0F) * 4;
    if (ihl < 20 || i + ihl > len) continue;
    uint16_t total_len = ((uint16_t)data[i + 2] << 8) | data[i + 3];
    /* total_len may legitimately exceed what's left in this PDU - the IP
     * packet can be the first fragment of an RLC-segmented SDU that
     * continues in later PDUs. Don't reject on that; just don't read past
     * ihl/tcp header bounds, checked separately below. */
    if (total_len < ihl) continue;
    if (data[i + 9] != 6) continue;                        /* protocol == TCP */
    *ip_off = i;
    return true;
  }
  return false;
}

static mqtt_info_t parse_mqtt_from_pdu(const uint8_t *data, uint32_t len)
{
  mqtt_info_t info = {0};
  if (data == NULL || len < 20 + 20) {
    return info;
  }

  uint32_t ip_off;
  if (!find_ipv4_tcp_start(data, len, &ip_off)) {
    return info;
  }

  uint8_t ihl = (uint8_t)(data[ip_off] & 0x0F) * 4;
  uint16_t ip_total_len = ((uint16_t)data[ip_off + 2] << 8) | data[ip_off + 3];
  uint32_t tcp_off = ip_off + ihl;

  if (tcp_off + 20 > len) {
    return info;
  }
  uint16_t dst_port = ((uint16_t)data[tcp_off + 2] << 8) | data[tcp_off + 3];
  if (dst_port != 1883) {
    return info; /* not MQTT */
  }
  uint8_t tcp_hdr_len = (uint8_t)((data[tcp_off + 12] >> 4) & 0x0F) * 4;
  if (tcp_hdr_len < 20 || tcp_off + tcp_hdr_len > len) {
    return info;
  }

  uint32_t mqtt_off = tcp_off + tcp_hdr_len;
  /* End of available TCP payload bytes within *this* PDU: the IP packet
   * may continue into later PDUs (RLC segmentation), so clip to what's
   * actually present here rather than trusting the full declared
   * ip_total_len, which can exceed len for a first fragment. */
  uint32_t ip_end = ip_off + ip_total_len;
  uint32_t payload_end = (ip_end < len) ? ip_end : len;
  if (mqtt_off >= payload_end) {
    return info; /* pure ACK, or no TCP payload present in this PDU */
  }

  uint8_t fixed_header = data[mqtt_off];
  if ((fixed_header >> 4) != 0x3) {
    return info; /* not a PUBLISH packet (could be a continuation fragment) */
  }
  uint8_t qos_bits = (fixed_header >> 1) & 0x3;
  if (qos_bits != 0 && qos_bits != 2) {
    return info; /* only QoS 0 / 2 are meaningful for this algorithm */
  }

  /* Remaining Length: 1-4 bytes, MQTT variable-length encoding. */
  uint32_t pos = mqtt_off + 1;
  uint32_t multiplier = 1;
  uint32_t remaining_length = 0;
  uint8_t enc_byte;
  int enc_bytes = 0;
  do {
    if (pos >= payload_end || enc_bytes >= 4) return info;
    enc_byte = data[pos++];
    remaining_length += (uint32_t)(enc_byte & 0x7F) * multiplier;
    multiplier *= 128;
    enc_bytes++;
  } while (enc_byte & 0x80);
  (void)remaining_length;

  if (pos + 2 > payload_end) {
    return info;
  }
  uint16_t topic_len = ((uint16_t)data[pos] << 8) | data[pos + 1];
  uint32_t topic_start = pos + 2;
  uint32_t topic_end = topic_start + topic_len;
  if (topic_end > payload_end) {
    return info;
  }

  uint32_t out = 0;
  for (uint32_t i = topic_start; i < topic_end && out < sizeof(info.topic) - 1; i++) {
    if (data[i] >= 32 && data[i] <= 126) {
      info.topic[out++] = (char)data[i];
    }
  }
  info.topic[out] = '\0';
  info.qos = (qos_bits == 0) ? '0' : '2';
  info.valid = true;
  return info;
}

/* ------------------------------------------------------------------------
 * UE traffic analysis, ported from UEAnalyzer.analyze_ue_traffic() /
 * classify_ue() in nfapiserver_withsend_realtime.py. Runs over the bounded
 * ring-buffer window for a single RNTI (chronological order).
 * ------------------------------------------------------------------------ */
typedef struct {
  bool   is_periodic;      /* false => "Continuous" or "Unknown" */
  bool   type_known;
  double bsr_size;
  bool   has_burst;
  double last_message_time;

  bool   has_normal_intervals;
  double sr_period_ms;
  double sr_window_ms;
  double min_interval_ms;
  double max_interval_ms;
  double median_interval_ms;

  bool   burst_valid;
  double burst_start_time;
  double burst_end_time;
  double burst_duration_ms;
  int    burst_message_count;
} ue_analysis_t;

static int cmp_double(const void *a, const void *b)
{
  double da = *(const double *)a, db = *(const double *)b;
  return (da > db) - (da < db);
}

static double median_of(double *values, int n)
{
  if (n <= 0) {
    return 0.0;
  }
  double *sorted = malloc(sizeof(double) * (size_t)n);
  memcpy(sorted, values, sizeof(double) * (size_t)n);
  qsort(sorted, (size_t)n, sizeof(double), cmp_double);
  double result = (n % 2 == 1) ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
  free(sorted);
  return result;
}

static void analyze_ue_traffic(ue_state_t *ue, ue_analysis_t *out)
{
  memset(out, 0, sizeof(*out));
  if (ue->count == 0) {
    return;
  }

  double normal_intervals[UE_HISTORY_CAPACITY];
  int normal_count = 0;
  double burst_intervals[UE_HISTORY_CAPACITY];
  int burst_interval_count = 0;

  bool   have_last_qos0 = false;
  double last_qos0_time = 0.0;
  bool   in_burst = false;
  double burst_start_time = 0.0, burst_end_time = 0.0;
  int    burst_run_start = -1;   /* index (in window) where current burst run began */
  int    burst_message_count = 0;
  bool   burst_ever_started = false;

  double normal_max_len = 0.0, burst_max_len = 0.0;

  for (int i = 0; i < ue->count; i++) {
    ue_sample_t *s = ue_sample_at(ue, i);

    if (s->qos == '0') {
      if (s->frame_len > normal_max_len) normal_max_len = s->frame_len;

      if (have_last_qos0) {
        double interval = s->timestamp - last_qos0_time;
        if (!in_burst && normal_count < UE_HISTORY_CAPACITY) {
          normal_intervals[normal_count++] = interval;
        }
      }
      last_qos0_time = s->timestamp;
      have_last_qos0 = true;

      if (in_burst) {
        /* burst run just ended: this QoS0 sample closes it. burst_end_time
         * was already set to the last QoS2 sample's own timestamp below -
         * matching the Python original, do NOT overwrite it with this
         * QoS0 sample's timestamp. */
        in_burst = false;
        if (burst_run_start >= 0) {
          int run_len = 0;
          double prev_ts = 0.0;
          for (int j = burst_run_start; j < i; j++) {
            ue_sample_t *b = ue_sample_at(ue, j);
            if (run_len > 0 && burst_interval_count < UE_HISTORY_CAPACITY) {
              burst_intervals[burst_interval_count++] = b->timestamp - prev_ts;
            }
            prev_ts = b->timestamp;
            run_len++;
          }
        }
      }
    } else if (s->qos == '2') {
      if (s->frame_len > burst_max_len) burst_max_len = s->frame_len;

      if (!in_burst) {
        in_burst = true;
        burst_ever_started = true;
        burst_start_time = s->timestamp;
        burst_run_start = i;
      }
      burst_end_time = s->timestamp;
      burst_message_count++;
    }
  }

  out->type_known = (normal_count > 0);
  if (out->type_known) {
    double med = median_of(normal_intervals, normal_count);
    out->is_periodic = (med >= 0.5);
  }

  double bsr_coefficient = 1.5;
  double base = (normal_max_len > burst_max_len) ? normal_max_len : burst_max_len;
  out->bsr_size = base * bsr_coefficient;

  out->has_burst = burst_ever_started;
  out->last_message_time = ue_sample_at(ue, ue->count - 1)->timestamp;

  if (normal_count > 0) {
    out->has_normal_intervals = true;
    double med = median_of(normal_intervals, normal_count);
    double mn = normal_intervals[0], mx = normal_intervals[0];
    for (int i = 1; i < normal_count; i++) {
      if (normal_intervals[i] < mn) mn = normal_intervals[i];
      if (normal_intervals[i] > mx) mx = normal_intervals[i];
    }
    out->sr_period_ms = med * 1000.0;
    out->sr_window_ms = (mx - mn) * 1000.0;
    out->min_interval_ms = mn * 1000.0;
    out->max_interval_ms = mx * 1000.0;
    out->median_interval_ms = med * 1000.0;
  }

  if (burst_ever_started) {
    out->burst_valid = true;
    out->burst_start_time = burst_start_time;
    out->burst_end_time = burst_end_time;
    out->burst_duration_ms = (burst_end_time - burst_start_time) * 1000.0;
    out->burst_message_count = burst_message_count;
  }
}

/* ------------------------------------------------------------------------
 * Second algorithm, ported from analyze_ue_traffic() / "Algorithm 1 SR
 * Window Analysis" in socketclientASR.py. Runs alongside the file1-style
 * analyze_ue_traffic() above (both computed and logged, on the same
 * ring-buffer window) rather than replacing it, so results can be compared
 * side by side against the same live traffic:
 *
 *   - file1 (above): sr_window from raw interval spread (max-min), bsr_size
 *     from worst-case packet size (max * 1.5) - simple, conservative.
 *   - file2 (below): sr_window from jitter around a running period
 *     estimate, scaled by an adaptive coefficient derived from the
 *     coefficient of variation (CV) of that jitter; bsr_size from the
 *     average packet size with a light 10% margin; plus burst-specific
 *     optimized_sr_period/optimized_bsr_size, and a topic-switch-based
 *     sr/bsr prediction independent of the interval-based estimate.
 * ------------------------------------------------------------------------ */
typedef struct {
  bool   is_periodic;
  bool   type_known;
  double last_message_time;
  bool   has_burst;

  /* Periodic-path outputs (mirrors socketclientASR.py's "if ue_type ==
   * Periodic and jitter_samples" branch) */
  bool   has_jitter_stats;
  double sr_period_ms;
  double sr_window_ms;
  double bsr_size;
  double cv;
  double alpha;
  double sigma_jitter_ms;
  double avg_jitter_ms;
  double max_jitter_ms;
  double min_jitter_ms;
  int    jitter_samples_count;

  /* Continuous-path outputs (the Python's "else" branch - also taken for
   * Periodic classifications that had no jitter samples, matching the
   * original's exact control flow) */
  double min_interval_ms;
  double max_interval_ms;
  double median_interval_ms;

  /* Burst-specific refinement, only set when has_burst && qos2 samples seen */
  bool   burst_valid;
  double burst_start_time;
  double burst_end_time;
  double burst_duration_ms;
  int    burst_message_count;
  double burst_interval_ms;
  double optimized_sr_period_ms;
  double optimized_bsr_size;

  /* Topic-switch-based prediction, independent signal from the interval
   * analysis above - only set when this RNTI's window contains at least
   * one topic transition. */
  bool   has_prediction;
  double predicted_sr_ms;
  double predicted_bsr_size;
} ue_analysis_jitter_t;

static double mean_of(const double *values, int n)
{
  if (n <= 0) return 0.0;
  double sum = 0.0;
  for (int i = 0; i < n; i++) sum += values[i];
  return sum / n;
}

static double std_of(const double *values, int n)
{
  if (n <= 0) return 0.0;
  double m = mean_of(values, n);
  double sq = 0.0;
  for (int i = 0; i < n; i++) {
    double d = values[i] - m;
    sq += d * d;
  }
  return sqrt(sq / n); /* population std, matching numpy.std's default ddof=0 */
}

static void analyze_ue_traffic_jitter(ue_state_t *ue, ue_analysis_jitter_t *out)
{
  memset(out, 0, sizeof(*out));
  if (ue->count == 0) {
    return;
  }

  double normal_intervals[UE_HISTORY_CAPACITY];
  int normal_count = 0;
  double burst_intervals[UE_HISTORY_CAPACITY];
  int burst_interval_count = 0;
  double jitter_samples[UE_HISTORY_CAPACITY];
  int jitter_count = 0;
  double packet_sizes[UE_HISTORY_CAPACITY];
  int packet_count = 0;

  double qos2_ts[UE_HISTORY_CAPACITY];
  double qos2_len[UE_HISTORY_CAPACITY];
  int qos2_count = 0;

  bool   have_last_qos0 = false;
  double last_qos0_time = 0.0;
  bool   in_burst = false;
  double mu_period = 0.0;
  bool   have_mu_period = false;
  double sigma_jitter = 0.0;

  for (int i = 0; i < ue->count; i++) {
    ue_sample_t *s = ue_sample_at(ue, i);

    if (s->qos == '0') {
      if (packet_count < UE_HISTORY_CAPACITY) {
        packet_sizes[packet_count++] = s->frame_len;
      }

      if (have_last_qos0) {
        double actual_interval = s->timestamp - last_qos0_time;
        if (!in_burst) {
          if (normal_count < UE_HISTORY_CAPACITY) {
            normal_intervals[normal_count++] = actual_interval;
          }
          mu_period = mean_of(normal_intervals, normal_count);
          have_mu_period = true;

          double expected_time = last_qos0_time + mu_period;
          double jitter = fabs(s->timestamp - expected_time);
          if (jitter_count < UE_HISTORY_CAPACITY) {
            jitter_samples[jitter_count++] = jitter;
          }
          if (jitter_count > 1) {
            sigma_jitter = std_of(jitter_samples, jitter_count);
          }
        } else {
          if (burst_interval_count < UE_HISTORY_CAPACITY) {
            burst_intervals[burst_interval_count++] = actual_interval;
          }
          in_burst = false;
        }
      }
      last_qos0_time = s->timestamp;
      have_last_qos0 = true;
    } else if (s->qos == '2') {
      in_burst = true;
      if (qos2_count < UE_HISTORY_CAPACITY) {
        qos2_ts[qos2_count] = s->timestamp;
        qos2_len[qos2_count] = s->frame_len;
        qos2_count++;
      }
    }
  }

  double all_intervals[UE_HISTORY_CAPACITY * 2];
  int all_count = 0;
  for (int i = 0; i < normal_count && all_count < UE_HISTORY_CAPACITY * 2; i++) {
    all_intervals[all_count++] = normal_intervals[i];
  }
  for (int i = 0; i < burst_interval_count && all_count < UE_HISTORY_CAPACITY * 2; i++) {
    all_intervals[all_count++] = burst_intervals[i];
  }

  out->type_known = (all_count > 0);
  if (out->type_known) {
    double med = median_of(all_intervals, all_count);
    out->is_periodic = (med >= 0.5);
  }
  out->last_message_time = ue_sample_at(ue, ue->count - 1)->timestamp;
  out->has_burst = (burst_interval_count > 0);

  if (out->is_periodic && jitter_count > 0) {
    double cv = (have_mu_period && mu_period > 0.0) ? (sigma_jitter / mu_period) : 0.0;
    double alpha = cv * 2.0;
    if (alpha < 0.5) alpha = 0.5;
    if (alpha > 3.0) alpha = 3.0;

    double base_window = 0.2; /* ms */
    double jmin = 0.0, jmax = 0.0, sr_window;
    if (jitter_count > 1) {
      jmin = jmax = jitter_samples[0];
      for (int i = 1; i < jitter_count; i++) {
        if (jitter_samples[i] < jmin) jmin = jitter_samples[i];
        if (jitter_samples[i] > jmax) jmax = jitter_samples[i];
      }
      sr_window = base_window + alpha * ((jmax - jmin) * 1000.0);
    } else {
      sr_window = base_window;
    }

    double sr_period = mu_period * 1000.0;
    double avg_packet_size = mean_of(packet_sizes, packet_count);
    double bsr_size = avg_packet_size * 1.1;

    out->has_jitter_stats = true;
    out->cv = cv;
    out->alpha = alpha;
    out->sr_period_ms = sr_period;
    out->sr_window_ms = sr_window;
    out->bsr_size = bsr_size;
    out->sigma_jitter_ms = sigma_jitter * 1000.0;
    out->jitter_samples_count = jitter_count;
    out->avg_jitter_ms = mean_of(jitter_samples, jitter_count) * 1000.0;
    out->max_jitter_ms = jmax * 1000.0;
    out->min_jitter_ms = jmin * 1000.0;

    if (out->has_burst && qos2_count > 0) {
      double b_start = qos2_ts[0], b_end = qos2_ts[0], b_max_len = qos2_len[0];
      for (int i = 1; i < qos2_count; i++) {
        if (qos2_ts[i] < b_start) b_start = qos2_ts[i];
        if (qos2_ts[i] > b_end) b_end = qos2_ts[i];
        if (qos2_len[i] > b_max_len) b_max_len = qos2_len[i];
      }
      out->burst_valid = true;
      out->burst_start_time = b_start;
      out->burst_end_time = b_end;
      out->burst_duration_ms = (b_end - b_start) * 1000.0;
      out->burst_message_count = qos2_count;

      double burst_interval_ms = median_of(burst_intervals, burst_interval_count) * 1000.0;
      out->burst_interval_ms = burst_interval_ms;

      double optimized_sr_period;
      if (sr_period > 0.0 && burst_interval_count > 0) {
        double factor = burst_interval_ms / sr_period;
        if (factor > 0.1) factor = 0.1;
        optimized_sr_period = sr_period * factor;
      } else {
        optimized_sr_period = sr_period;
      }
      out->optimized_sr_period_ms = optimized_sr_period;

      double optimized_bsr_size;
      if (bsr_size > 0.0) {
        optimized_bsr_size = bsr_size * (b_max_len / bsr_size);
      } else {
        optimized_bsr_size = b_max_len;
      }
      out->optimized_bsr_size = optimized_bsr_size;
    }
  } else {
    /* "Continuous" path - also covers a Periodic classification that had
     * no jitter samples, matching socketclientASR.py's control flow
     * exactly (its else branch is reached in both cases). */
    double min_i = 0.0, max_i = 0.0, med_i = 0.0;
    if (all_count > 0) {
      min_i = max_i = all_intervals[0];
      for (int i = 1; i < all_count; i++) {
        if (all_intervals[i] < min_i) min_i = all_intervals[i];
        if (all_intervals[i] > max_i) max_i = all_intervals[i];
      }
      med_i = median_of(all_intervals, all_count);
    }
    double max_qos0_len = 0.0;
    for (int i = 0; i < packet_count; i++) {
      if (packet_sizes[i] > max_qos0_len) max_qos0_len = packet_sizes[i];
    }

    out->min_interval_ms = min_i * 1000.0;
    out->max_interval_ms = max_i * 1000.0;
    out->median_interval_ms = med_i * 1000.0;
    out->sr_period_ms = med_i * 1000.0;
    out->bsr_size = max_qos0_len * 1.5;
  }
}

/* Topic-switch-based prediction, ported from the topic_switches loop in
 * socketclientASR.py's analyze_data(). The Python version computed this
 * globally across all topics/messages; per-RNTI here it means "how often
 * does this UE switch which topic it publishes to" - only fires if a
 * single UE/RNTI actually alternates between topics (unlikely in a setup
 * where each pubx.py instance sticks to one topic, but implemented for
 * when it does). Requires ue_table_mutex held by the caller (reads the
 * ring buffer directly). */
static void predict_from_topic_switches(ue_state_t *ue, ue_analysis_jitter_t *out)
{
  if (ue->count < 2) {
    return;
  }

  double switch_intervals_ms[UE_HISTORY_CAPACITY];
  int switch_count = 0;

  for (int i = 1; i < ue->count; i++) {
    ue_sample_t *prev = ue_sample_at(ue, i - 1);
    ue_sample_t *cur = ue_sample_at(ue, i);
    if (strcmp(prev->topic, cur->topic) != 0) {
      if (switch_count < UE_HISTORY_CAPACITY) {
        switch_intervals_ms[switch_count++] = (cur->timestamp - prev->timestamp) * 1000.0;
      }
    }
  }

  if (switch_count == 0) {
    return;
  }

  int recent_count = (switch_count < 10) ? switch_count : 10;
  double *recent = &switch_intervals_ms[switch_count - recent_count];

  if (out->has_burst) {
    double mn = recent[0];
    for (int i = 1; i < recent_count; i++) {
      if (recent[i] < mn) mn = recent[i];
    }
    out->predicted_sr_ms = mn * 0.8;
    out->predicted_bsr_size = out->bsr_size * 2.0;
  } else {
    double sorted[UE_HISTORY_CAPACITY];
    memcpy(sorted, recent, sizeof(double) * (size_t)recent_count);
    qsort(sorted, (size_t)recent_count, sizeof(double), cmp_double);
    int take = (recent_count < 3) ? recent_count : 3;
    double sum = 0.0;
    for (int i = 0; i < take; i++) sum += sorted[i];
    /* Faithfully replicates socketclientASR.py dividing by a fixed 3
     * (smallest_n) even when fewer than 3 switches are available, rather
     * than by `take` - preserved as-is rather than silently "fixed". */
    out->predicted_sr_ms = sum / 3.0;
  }
  out->has_prediction = true;
}

static void log_analysis_jitter(uint16_t rnti, const ue_analysis_jitter_t *a)
{
  printf("\n--- [Algorithm 1 / jitter-CV] UE RNTI 0x%04x ---\n", rnti);
  printf("type: %s\n", !a->type_known ? "Unknown" : (a->is_periodic ? "Periodic" : "Continuous"));
  printf("bsr_size: %.0f bytes\n", a->bsr_size);
  printf("has_burst: %s\n", a->has_burst ? "true" : "false");
  if (a->has_jitter_stats) {
    printf("sr_period: %.3f ms, sr_window: %.3f ms\n", a->sr_period_ms, a->sr_window_ms);
    printf("  CV: %.4f, alpha: %.4f, sigma_jitter: %.4f ms, avg_jitter: %.4f ms (min=%.4f max=%.4f, n=%d)\n",
           a->cv, a->alpha, a->sigma_jitter_ms, a->avg_jitter_ms, a->min_jitter_ms, a->max_jitter_ms,
           a->jitter_samples_count);
  } else {
    printf("min_interval: %.3f ms, max_interval: %.3f ms, median_interval: %.3f ms\n",
           a->min_interval_ms, a->max_interval_ms, a->median_interval_ms);
  }
  if (a->burst_valid) {
    printf("burst_duration: %.3f ms, burst_message_count: %d, burst_interval: %.3f ms\n",
           a->burst_duration_ms, a->burst_message_count, a->burst_interval_ms);
    printf("optimized_sr_period: %.3f ms, optimized_bsr_size: %.0f bytes\n",
           a->optimized_sr_period_ms, a->optimized_bsr_size);
  }
  if (a->has_prediction) {
    printf("predicted_sr: %.3f ms", a->predicted_sr_ms);
    if (a->has_burst) {
      printf(", predicted_bsr_size: %.0f bytes", a->predicted_bsr_size);
    }
    printf("\n");
  }
}

/* ------------------------------------------------------------------------
 * UDP control channel to nfapi_proxy (see u-tokyo/sample_agent.c
 * control_func_via_udp for the receiving side / wire format reference).
 * ------------------------------------------------------------------------ */
static int control_sock = -1;
static struct sockaddr_in control_addr;

static void control_channel_init(void)
{
  control_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (control_sock < 0) {
    perror("control channel socket");
    return;
  }
  memset(&control_addr, 0, sizeof(control_addr));
  control_addr.sin_family = AF_INET;
  control_addr.sin_port = htons(PROXY_CONTROL_PORT);
  inet_pton(AF_INET, PROXY_CONTROL_IP, &control_addr.sin_addr);
}

static void send_start_bsr(uint16_t rnti_host, uint32_t bsr_value_bytes)
{
  if (control_sock < 0) return;
  NFAPI_PROXY_START_BSR cmd = { .id = START_BSR, .RNTI = htons(rnti_host), .bsr_value = htonl(bsr_value_bytes) };
  sendto(control_sock, &cmd, sizeof(cmd), 0, (struct sockaddr *)&control_addr, sizeof(control_addr));
}

/*
 * TODO(sr-params): NFAPI_PROXY_START_SR / STOP_SR (nfapi_proxy_command.h)
 * only carry a RNTI - there is currently no field to carry the computed
 * sr_period_ms / sr_window_ms. analyze_ue_traffic() above still computes
 * them (see ue_analysis_t) and they are logged, but nothing sends them
 * anywhere yet - confirm with the rest of the pipeline how/where those two
 * values are meant to be delivered (extend this command struct? separate
 * P5 config path?) before wiring up an automatic START_SR/STOP_SR trigger
 * here.
 */
static void send_start_sr(uint16_t rnti_host)
{
  if (control_sock < 0) return;
  NFAPI_PROXY_START_SR cmd = { .id = START_SR, .RNTI = htons(rnti_host) };
  sendto(control_sock, &cmd, sizeof(cmd), 0, (struct sockaddr *)&control_addr, sizeof(control_addr));
}

static void send_stop_sr(uint16_t rnti_host)
{
  if (control_sock < 0) return;
  NFAPI_PROXY_STOP_SR cmd = { .id = STOP_SR, .RNTI = htons(rnti_host) };
  sendto(control_sock, &cmd, sizeof(cmd), 0, (struct sockaddr *)&control_addr, sizeof(control_addr));
}

static void log_analysis(uint16_t rnti, const ue_analysis_t *a)
{
  printf("\n--- UE RNTI 0x%04x ---\n", rnti);
  printf("type: %s\n", !a->type_known ? "Unknown" : (a->is_periodic ? "Periodic" : "Continuous"));
  printf("bsr_size: %.0f bytes\n", a->bsr_size);
  printf("has_burst: %s\n", a->has_burst ? "true" : "false");
  if (a->has_normal_intervals) {
    printf("sr_period: %.3f ms, sr_window: %.3f ms (min=%.3f max=%.3f median=%.3f)\n",
           a->sr_period_ms, a->sr_window_ms, a->min_interval_ms, a->max_interval_ms, a->median_interval_ms);
  }
  if (a->burst_valid) {
    printf("burst_duration: %.3f ms, burst_message_count: %d\n", a->burst_duration_ms, a->burst_message_count);
  }
}

/* ------------------------------------------------------------------------
 * Minimal, self-contained RX_DATA_INDICATION decoder.
 *
 * We deliberately do NOT call the OAI-provided pack/unpack library:
 *   - nfapi_nr_p7_message_unpack() (nfapi/src/nfapi_p7.c) has an EMPTY
 *     nfapi/CMakeLists.txt - it is dead code, not built into libnp.a or
 *     anything else, which is why linking against it fails.
 *   - fapi_nr_p7_message_unpack() (fapi/src/nr_fapi_p7.c) IS actively
 *     built, but uses a different, shorter 8-byte wire header
 *     (fapi_message_header_t) meant for in-process FAPI, not the nFAPI SCF
 *     transport header actually used on this P7 network link.
 *
 * Byte-level inspection of a real capture (nats_capture_test1_20260803.pcap,
 * fapi.rx_data_indication messages) confirms the wire format is the
 * 18-byte nfapi_nr_p7_message_header_t (NFAPI_NR_P7_HEADER_LENGTH) followed
 * by sfn/slot/number_of_pdus and, per PDU, handle/rnti/harq_id/pdu_length/
 * ul_cqi/timing_advance/rssi + payload - sfn increases monotonically across
 * samples, slot/number_of_pdus land in sane ranges, and pdu_length matches
 * the exact remaining byte count in every sample checked. Hand-rolling the
 * decode for just this one message type avoids dragging in nfapi_p7.c's
 * large, partly-dead dependency chain (assertions.h/debug.h/vendor_ext.h)
 * for CMake targets that aren't even in libnp.a.
 */
#define LOCAL_RX_DATA_IND_MAX_PDU NFAPI_NR_RX_DATA_IND_MAX_PDU

typedef struct {
  uint32_t       handle;
  uint16_t       rnti;
  uint8_t        harq_id;
  uint32_t       pdu_length;
  uint8_t        ul_cqi;
  uint16_t       timing_advance;
  uint16_t       rssi;
  const uint8_t *pdu;   /* points directly into the NATS message buffer, no copy */
} local_rx_pdu_t;

typedef struct {
  uint16_t       sfn;
  uint16_t       slot;
  uint16_t       number_of_pdus;
  local_rx_pdu_t pdus[LOCAL_RX_DATA_IND_MAX_PDU];
} local_rx_data_indication_t;

static uint16_t read_be16(const uint8_t *data, uint32_t *off)
{
  uint16_t v = ((uint16_t)data[*off] << 8) | data[*off + 1];
  *off += 2;
  return v;
}

static uint32_t read_be32(const uint8_t *data, uint32_t *off)
{
  uint32_t v = ((uint32_t)data[*off] << 24) | ((uint32_t)data[*off + 1] << 16)
             | ((uint32_t)data[*off + 2] << 8) | (uint32_t)data[*off + 3];
  *off += 4;
  return v;
}

static bool unpack_rx_data_indication(const uint8_t *data, int data_len, local_rx_data_indication_t *out)
{
  const uint32_t hdr_len = NFAPI_NR_P7_HEADER_LENGTH;
  if (data_len < 0 || (uint32_t)data_len < hdr_len + 6) {
    return false;
  }

  uint32_t off = hdr_len;
  out->sfn = read_be16(data, &off);
  out->slot = read_be16(data, &off);
  out->number_of_pdus = read_be16(data, &off);

  if (out->number_of_pdus > LOCAL_RX_DATA_IND_MAX_PDU) {
    return false;
  }

  for (int i = 0; i < out->number_of_pdus; i++) {
    if (off + 16 > (uint32_t)data_len) {
      return false;
    }
    local_rx_pdu_t *p = &out->pdus[i];
    p->handle = read_be32(data, &off);
    p->rnti = read_be16(data, &off);
    p->harq_id = data[off]; off += 1;
    p->pdu_length = read_be32(data, &off);
    p->ul_cqi = data[off]; off += 1;
    p->timing_advance = read_be16(data, &off);
    p->rssi = read_be16(data, &off);
    if (off + p->pdu_length > (uint32_t)data_len) {
      return false;
    }
    p->pdu = data + off;
    off += p->pdu_length;
  }
  return true;
}

/* ------------------------------------------------------------------------
 * NATS message handler
 * ------------------------------------------------------------------------ */
void handle_message(natsConnection *nc, natsSubscription *sub, natsMsg *message, void *closure)
{
  const uint8_t *data = (const uint8_t *)natsMsg_GetData(message);
  int data_len = natsMsg_GetDataLength(message);

  if (data == NULL || data_len < (int)NFAPI_NR_P7_HEADER_LENGTH) {
    natsMsg_Destroy(message);
    return;
  }

  /* Header has no pointer members and is exactly NFAPI_NR_P7_HEADER_LENGTH
   * (18) bytes under #pragma pack(1), so an overlay cast is safe here. */
  const nfapi_nr_p7_message_header_t *header = (const nfapi_nr_p7_message_header_t *)data;
  uint16_t message_id = ntohs(header->message_id);

  if (message_id != NFAPI_NR_PHY_MSG_TYPE_RX_DATA_INDICATION) {
    natsMsg_Destroy(message);
    return;
  }

  local_rx_data_indication_t rx_ind;
  if (!unpack_rx_data_indication(data, data_len, &rx_ind) || rx_ind.number_of_pdus == 0) {
    natsMsg_Destroy(message);
    return;
  }

  printf("\n==================================================\n");
  printf("[%d.%d] %u PDU(s)\n", rx_ind.sfn, rx_ind.slot, rx_ind.number_of_pdus);
  printf("==================================================\n");

  double t = now_seconds();

  for (int i = 0; i < rx_ind.number_of_pdus; i++) {
    local_rx_pdu_t *pdu = &rx_ind.pdus[i];

    mqtt_info_t info = parse_mqtt_from_pdu(pdu->pdu, pdu->pdu_length);
    if (info.valid) {
      ue_analysis_t result;
      ue_analysis_jitter_t result_jitter;

      pthread_mutex_lock(&ue_table_mutex);
      ue_state_t *ue = get_or_create_ue(pdu->rnti);
      ue_add_sample(ue, t, info.qos, pdu->pdu_length, info.topic);
      analyze_ue_traffic(ue, &result);
      analyze_ue_traffic_jitter(ue, &result_jitter);
      predict_from_topic_switches(ue, &result_jitter);
      pthread_mutex_unlock(&ue_table_mutex);

      log_analysis(pdu->rnti, &result);
      log_analysis_jitter(pdu->rnti, &result_jitter);

      /* Actual control-channel BSR send still driven by the file1 result
       * (already validated against live traffic) - the jitter/CV result
       * above is computed and logged for side-by-side comparison, not
       * wired to any output yet. */
      send_start_bsr(pdu->rnti, (uint32_t)result.bsr_size);

      /* SR period/window intentionally not sent yet - see TODO(sr-params)
       * above send_start_sr(). */
    }
  }

  natsMsg_Destroy(message);
}

int main(void)
{
  natsConnection *nc = NULL;
  natsSubscription *sub = NULL;

  char connect_str[1500];
  snprintf(connect_str, sizeof(connect_str), "%s:%d", NATS_ADDR, NATS_PORT);

  natsStatus status = natsConnection_ConnectTo(&nc, connect_str);
  if (status != NATS_OK) {
    fprintf(stderr, "Failed to connect to NATS at %s: %s\n", connect_str, natsStatus_GetText(status));
    return 1;
  }

  status = natsConnection_Subscribe(&sub, nc, "fapi.*", handle_message, NULL);
  if (status != NATS_OK) {
    fprintf(stderr, "Failed to subscribe to fapi.*: %s\n", natsStatus_GetText(status));
    natsConnection_Destroy(nc);
    return 1;
  }

  control_channel_init();

  printf("Subscribed to fapi.* on %s, sending BSR updates to %s:%d\n",
         connect_str, PROXY_CONTROL_IP, PROXY_CONTROL_PORT);

  while (true) {
    sleep(100);
  }

  return 0;
}
