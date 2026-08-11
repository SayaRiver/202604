#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
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

static void ue_add_sample(ue_state_t *ue, double timestamp, char qos, uint32_t frame_len)
{
  ue->history[ue->head].timestamp = timestamp;
  ue->history[ue->head].qos = qos;
  ue->history[ue->head].frame_len = frame_len;
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
 * MQTT PUBLISH scan inside a MAC PDU payload.
 *
 * Ported from parse_message_from_buffer() in nfapiserver_withsend_realtime.py.
 * That version had to reassemble a segmented "userdata" table first because
 * it was reading raw NFAPI socket frames; here the payload arrives already
 * contiguous as nfapi_nr_rx_data_pdu_t.pdu, so the segment reassembly is
 * gone but the byte-scan itself (0x80 marker, +4 QoS byte, +7 topic length)
 * is kept identical to the validated Python logic.
 * ------------------------------------------------------------------------ */
typedef struct {
  bool valid;
  char qos;               /* '0' or '2' */
  char topic[256];
} mqtt_info_t;

static mqtt_info_t parse_mqtt_from_pdu(const uint8_t *data, uint32_t len)
{
  mqtt_info_t info = {0};
  if (data == NULL || len < 5) {
    return info;
  }

  int topic_len_pos = -1;
  char qos = 0;
  for (uint32_t i = 0; i + 4 < len; i++) {
    if (data[i] == 0x80) {
      if (data[i + 4] == 0x30) {
        qos = '0';
        topic_len_pos = (int)i + 7;
        break;
      } else if (data[i + 4] == 0x34) {
        qos = '2';
        topic_len_pos = (int)i + 7;
        break;
      }
    }
  }

  if (topic_len_pos < 0 || (uint32_t)topic_len_pos >= len) {
    return info;
  }

  uint8_t topic_len = data[topic_len_pos];
  uint32_t topic_start = (uint32_t)topic_len_pos + 1;
  uint32_t topic_end = topic_start + topic_len;
  if (topic_end > len) {
    return info;
  }

  uint32_t out = 0;
  for (uint32_t i = topic_start; i < topic_end && out < sizeof(info.topic) - 1; i++) {
    if (data[i] >= 32 && data[i] <= 126) {
      info.topic[out++] = (char)data[i];
    }
  }
  info.topic[out] = '\0';
  info.qos = qos;
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
   * bytes under #pragma pack(1), so an overlay cast is safe here - unlike
   * the body types below, which contain real C pointers (pdu_list, pdu)
   * that cannot simply be overlaid on wire bytes and must go through
   * nfapi_nr_p7_message_unpack(). */
  const nfapi_nr_p7_message_header_t *header = (const nfapi_nr_p7_message_header_t *)data;
  uint16_t message_id = ntohs(header->message_id);

  if (message_id != NFAPI_NR_PHY_MSG_TYPE_RX_DATA_INDICATION) {
    natsMsg_Destroy(message);
    return;
  }

  nfapi_nr_rx_data_indication_t rx_ind = {0};
  nfapi_p7_codec_config_t codec_config = {0};

  if (!nfapi_nr_p7_message_unpack((void *)data, (uint32_t)data_len, &rx_ind, sizeof(rx_ind), &codec_config)) {
    fprintf(stderr, "Failed to unpack RX_DATA_INDICATION\n");
    natsMsg_Destroy(message);
    return;
  }

  if (rx_ind.number_of_pdus == 0 || rx_ind.pdu_list == NULL) {
    natsMsg_Destroy(message);
    return;
  }

  printf("\n==================================================\n");
  printf("[%d.%d] %u PDU(s)\n", rx_ind.sfn, rx_ind.slot, rx_ind.number_of_pdus);
  printf("==================================================\n");

  double t = now_seconds();

  for (int i = 0; i < rx_ind.number_of_pdus; i++) {
    nfapi_nr_rx_data_pdu_t *pdu = &rx_ind.pdu_list[i];

    mqtt_info_t info = parse_mqtt_from_pdu(pdu->pdu, pdu->pdu_length);
    if (info.valid) {
      ue_analysis_t result;

      pthread_mutex_lock(&ue_table_mutex);
      ue_state_t *ue = get_or_create_ue(pdu->rnti);
      ue_add_sample(ue, t, info.qos, pdu->pdu_length);
      analyze_ue_traffic(ue, &result);
      pthread_mutex_unlock(&ue_table_mutex);

      log_analysis(pdu->rnti, &result);

      /* BSR value has a well-defined wire field, send it every time we get
       * an updated estimate. */
      send_start_bsr(pdu->rnti, (uint32_t)result.bsr_size);

      /* SR period/window intentionally not sent yet - see TODO(sr-params)
       * above send_start_sr(). */
    }

    free(pdu->pdu);
  }
  free(rx_ind.pdu_list);

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
