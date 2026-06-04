#pragma once
#include <stdint.h>

typedef enum : uint16_t {
  GET_RNTI_LIST,
  RESPONSE_RNTI_LIST,
  START_BSR,
  STOP_BSR,
  START_SR,
  STOP_SR
}NFAPI_PROXY_COMMAND;

typedef struct {
  NFAPI_PROXY_COMMAND id;
}NFAPI_PROXY_COMMAND_BASE;

typedef struct {
  NFAPI_PROXY_COMMAND id;
}NFAPI_PROXY_GET_RNTI_LIST;

typedef struct {
  NFAPI_PROXY_COMMAND id;
  uint8_t number_of_rnti;
  uint16_t RNTI[256];
}NFAPI_PROXY_RESPONSE_RNTI_LIST;
 
typedef struct {
  NFAPI_PROXY_COMMAND id;
  uint16_t RNTI;
  uint32_t bsr_value;
}NFAPI_PROXY_START_BSR;

typedef struct {
  NFAPI_PROXY_COMMAND id;
  uint16_t RNTI;
}NFAPI_PROXY_STOP_BSR;

typedef struct {
  NFAPI_PROXY_COMMAND id;
  uint16_t RNTI;
}NFAPI_PROXY_START_SR;

typedef struct {
  NFAPI_PROXY_COMMAND id;
  uint16_t RNTI;
}NFAPI_PROXY_STOP_SR;
