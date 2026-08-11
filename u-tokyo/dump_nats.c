/*
 * Standalone diagnostic: subscribe ONLY to fapi.rx_data_indication, decode
 * using the confirmed 18-byte nfapi_nr_p7_message_header_t (SCF) layout,
 * and hex-dump each PDU's payload bytes directly - to check by eye whether
 * real MQTT PUBLISH traffic is visible in plaintext inside these PDUs (vs.
 * e.g. PDCP ciphertext). No nfapi headers/libnp.a needed - build with:
 *   gcc dump_nats.c -o dump_nats -lnats -pthread
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <nats/nats.h>

#define NATS_ADDR "192.168.182.10"
#define NATS_PORT 4222
#define NFAPI_NR_P7_HEADER_LENGTH 18

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

static void hexdump(const uint8_t *data, int len)
{
  int n = len < 256 ? len : 256;
  for (int i = 0; i < n; i++) {
    printf("%02x ", data[i]);
    if ((i + 1) % 16 == 0) printf("\n");
  }
  if (n % 16 != 0) printf("\n");
  if (len > n) printf("... (%d more bytes)\n", len - n);
}

static void printable(const uint8_t *data, int len)
{
  int n = len < 256 ? len : 256;
  for (int i = 0; i < n; i++) {
    putchar((data[i] >= 32 && data[i] <= 126) ? data[i] : '.');
  }
  printf("\n");
}

void handle_message(natsConnection *nc, natsSubscription *sub, natsMsg *message, void *closure)
{
  const uint8_t *data = (const uint8_t *)natsMsg_GetData(message);
  int len = natsMsg_GetDataLength(message);

  if (len < NFAPI_NR_P7_HEADER_LENGTH + 6) {
    natsMsg_Destroy(message);
    return;
  }

  uint32_t off = NFAPI_NR_P7_HEADER_LENGTH;
  uint16_t sfn = read_be16(data, &off);
  uint16_t slot = read_be16(data, &off);
  uint16_t number_of_pdus = read_be16(data, &off);

  printf("\n================================================================\n");
  printf("[%u.%u] %u PDU(s)\n", sfn, slot, number_of_pdus);

  for (int i = 0; i < number_of_pdus; i++) {
    if (off + 16 > (uint32_t)len) break;
    uint32_t handle = read_be32(data, &off);
    (void)handle;
    uint16_t rnti = read_be16(data, &off);
    uint8_t harq_id = data[off]; off += 1;
    uint32_t pdu_length = read_be32(data, &off);
    off += 1; /* ul_cqi */
    off += 2; /* timing_advance */
    off += 2; /* rssi */

    if (off + pdu_length > (uint32_t)len) break;

    printf("-- PDU %d: rnti=0x%04x harq_id=%u pdu_length=%u\n", i, rnti, harq_id, pdu_length);
    printf("   hex: ");
    hexdump(data + off, (int)pdu_length);
    printf("   text: ");
    printable(data + off, (int)pdu_length);

    off += pdu_length;
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

  status = natsConnection_Subscribe(&sub, nc, "fapi.rx_data_indication", handle_message, NULL);
  if (status != NATS_OK) {
    fprintf(stderr, "Failed to subscribe: %s\n", natsStatus_GetText(status));
    return 1;
  }

  printf("Subscribed to fapi.rx_data_indication on %s, dumping PDU payloads (Ctrl+C to stop)...\n", connect_str);
  while (true) {
    sleep(100);
  }
  return 0;
}
