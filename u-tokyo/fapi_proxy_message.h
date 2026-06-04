#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <aio.h>

/* #ifdef OAI_FAPI */
#define MAX_LENGTH_OF_FAPI_PROXY_MESSAGE 32768
/* #endif */

typedef enum {
  FAPI_PROXY_SLOT_INDICAIONT = 1,
  FAPI_PROXY_SR_INDICAIONT,
  FAPI_PROXY_RX_DATA_INDICATION,
  FAPI_PROXY_UL_TTI_REQUEST,
  FPAI_PROXY_OTHER
}FAPI_PROXY_FAPI_MESSAGE_TYPE;

typedef enum {
  UPLINK = 1,
  DOWNLINK = 2,  
}FAPI_COMMUNICATION_TYPE;

typedef struct {
  FAPI_PROXY_FAPI_MESSAGE_TYPE message_id;
  FAPI_COMMUNICATION_TYPE communication_type;
  int counter; // if counter is 0, this message should be freed.
  ssize_t payload_length;
  uint8_t payload[MAX_LENGTH_OF_FAPI_PROXY_MESSAGE];
}FAPI_PROXY_MESSAGE;
