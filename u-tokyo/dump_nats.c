/*
 * Standalone diagnostic: subscribe to fapi.* and hex-dump raw NATS payloads,
 * decoding candidate (sfn, slot, number_of_pdus) fields under both possible
 * P7 header lengths (8-byte fapi_message_header_t vs 18-byte SCF
 * nfapi_nr_p7_message_header_t) so we can tell from real traffic which one
 * matches. No nfapi headers/libnp.a needed - build with just:
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

static uint16_t be16(const uint8_t *p)
{
  return (uint16_t)((p[0] << 8) | p[1]);
}

static void hexdump(const uint8_t *data, int len)
{
  int n = len < 64 ? len : 64;
  for (int i = 0; i < n; i++) {
    printf("%02x ", data[i]);
    if ((i + 1) % 16 == 0) printf("\n");
  }
  if (n % 16 != 0) printf("\n");
  if (len > n) printf("... (%d more bytes)\n", len - n);
}

void handle_message(natsConnection *nc, natsSubscription *sub, natsMsg *message, void *closure)
{
  const uint8_t *data = (const uint8_t *)natsMsg_GetData(message);
  int len = natsMsg_GetDataLength(message);
  const char *subject = natsMsg_GetSubject(message);

  printf("\n================================================================\n");
  printf("subject=%s len=%d\n", subject, len);
  printf("raw bytes:\n");
  hexdump(data, len);

  if (len >= 4) {
    /* message_id sits at byte offset 2 in BOTH candidate header layouts,
     * so this part alone can't disambiguate - shown for reference. */
    printf("message_id (offset 2, both layouts agree here) = 0x%04x\n", be16(data + 2));
  }

  if (len >= 8 + 6) {
    printf("-- if header_len=8 (fapi_message_header_t): body starts at byte 8\n");
    printf("   sfn=%u slot=%u number_of_pdus=%u\n",
           be16(data + 8), be16(data + 10), be16(data + 12));
  }
  if (len >= 18 + 6) {
    printf("-- if header_len=18 (nfapi_nr_p7_message_header_t / SCF): body starts at byte 18\n");
    printf("   sfn=%u slot=%u number_of_pdus=%u\n",
           be16(data + 18), be16(data + 20), be16(data + 22));
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
    fprintf(stderr, "Failed to subscribe: %s\n", natsStatus_GetText(status));
    return 1;
  }

  printf("Subscribed to fapi.* on %s, dumping raw bytes (Ctrl+C to stop)...\n", connect_str);
  while (true) {
    sleep(100);
  }
  return 0;
}
