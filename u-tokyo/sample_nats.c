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

#define LOCAL_RX_DATA_IND_MAX_PDU NFAPI_NR_RX_DATA_IND_MAX_PDU
#include "isr_algo.h"


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

/* ------------------------------------------------------------------------
 * Algorithm 3 (paper Sec 3.3.3): one message history per MQTT topic.
 * Each topic is analysed by Algorithms 1 and 2 (isr_algo.c) on its own
 * window. The BSR is sent for the RNTI that carried the message.
 * ------------------------------------------------------------------------ */
#define TOPIC_SLOTS 64

typedef struct {
  char        topic[256];
  int         used;
  uint16_t    rnti;               /* RNTI that last carried this topic */
  isr_msg_t   hist[ISR_MAX_MSGS];
  int         count;
  int         head;
  long        seen;
  isr_result_t last;
} topic_state_t;

static topic_state_t topic_table[TOPIC_SLOTS];
static pthread_mutex_t topic_mutex = PTHREAD_MUTEX_INITIALIZER;

static topic_state_t *topic_get(const char *topic)
{
  for (int i = 0; i < TOPIC_SLOTS; i++)
    if (topic_table[i].used && strcmp(topic_table[i].topic, topic) == 0)
      return &topic_table[i];
  for (int i = 0; i < TOPIC_SLOTS; i++) {
    if (!topic_table[i].used) {
      topic_table[i].used = 1;
      snprintf(topic_table[i].topic, sizeof(topic_table[i].topic), "%s", topic);
      return &topic_table[i];
    }
  }
  return NULL;
}

static void topic_push(topic_state_t *ts, isr_msg_t m)
{
  ts->hist[ts->head] = m;
  ts->head = (ts->head + 1) % ISR_MAX_MSGS;
  if (ts->count < ISR_MAX_MSGS) ts->count++;
}

/* copy the window in arrival order (oldest first) */
static int topic_window(const topic_state_t *ts, isr_msg_t *out)
{
  int start = (ts->count < ISR_MAX_MSGS) ? 0 : ts->head;
  for (int i = 0; i < ts->count; i++)
    out[i] = ts->hist[(start + i) % ISR_MAX_MSGS];
  return ts->count;
}

/* print one status line per topic every 100 PDUs */
static int ts_log_due(const topic_state_t *ts)
{
  return (ts->seen % 100) == 1;
}

void handle_message(natsConnection *nc, natsSubscription *sub, natsMsg *message, void *closure)
{
  const uint8_t *data = (const uint8_t *)natsMsg_GetData(message);
  int data_len = natsMsg_GetDataLength(message);

  if (data == NULL || data_len < (int)NFAPI_NR_P7_HEADER_LENGTH) {
    natsMsg_Destroy(message);
    return;
  }

  const nfapi_nr_p7_message_header_t *header = (const nfapi_nr_p7_message_header_t *)data;
  if (ntohs(header->message_id) != NFAPI_NR_PHY_MSG_TYPE_RX_DATA_INDICATION) {
    natsMsg_Destroy(message);
    return;
  }

  local_rx_data_indication_t rx_ind;
  if (!unpack_rx_data_indication(data, data_len, &rx_ind) || rx_ind.number_of_pdus == 0) {
    natsMsg_Destroy(message);
    return;
  }

  double t = now_seconds();

  for (unsigned i = 0; i < rx_ind.number_of_pdus; i++) {
    local_rx_pdu_t *pdu = &rx_ind.pdus[i];
    mqtt_info_t info = parse_mqtt_from_pdu(pdu->pdu, pdu->pdu_length);
    if (!info.valid) continue;

    isr_msg_t m = { .t = t, .qos = info.qos, .len = (double)pdu->pdu_length };
    int bsr = 0;
    isr_result_t r;
    int have = 0;

    /* Algorithm 3: classify by topic, then analyse every active topic */
    pthread_mutex_lock(&topic_mutex);
    topic_state_t *ts = topic_get(info.topic);
    if (ts) {
      ts->rnti = pdu->rnti;
      topic_push(ts, m);
      ts->seen++;
      for (int k = 0; k < TOPIC_SLOTS; k++) {
        topic_state_t *tk = &topic_table[k];
        if (!tk->used || tk->count == 0) continue;
        isr_msg_t win[ISR_MAX_MSGS];
        int n = topic_window(tk, win);
        isr_analyze(win, n, &tk->last);
      }
      r = ts->last;
      bsr = (int)r.active_bsr_bytes;      /* BSR sent for the RNTI of the incoming message */
      have = 1;
    }
    pthread_mutex_unlock(&topic_mutex);

    if (have) {
      send_start_bsr(pdu->rnti, (uint32_t)bsr);
      if (ts_log_due(ts)) {
        printf("[%s] rnti=0x%04x periodic=%d sr_window=%.3f ms sr_period=%.3f ms bsr=%d B burst=%d intensity=%.2f\n",
               info.topic, pdu->rnti, r.periodic, r.active_sr_window_ms, r.active_sr_period_ms,
               bsr, r.in_burst_now, r.intensity);
      }
    }
  }

  natsMsg_Destroy(message);
}

int main(void)
{
  natsConnection *nc = NULL;
  natsSubscription *sub = NULL;

  char connect_str[64];
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
  printf("Subscribed to fapi.* on %s, BSR to %s:%d (ISR algorithms 1-3, per-topic)\n",
         connect_str, PROXY_CONTROL_IP, PROXY_CONTROL_PORT);

  while (true) {
    sleep(100);
  }
  return 0;
}
