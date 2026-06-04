#pragma once

#include <stdint.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define NUMBER_OF_NFAPI_P5_MESSAGE_QUEUE 100
#define SIZE_OF_NFAPI_P5_MESSAGE_PAYLOAD 40000

typedef struct{
  ssize_t payload_length;
  uint8_t payload[SIZE_OF_NFAPI_P5_MESSAGE_PAYLOAD];
}FP_NFAPI_P5_MESSAGE;

typedef struct{
  uint8_t send_index;
  uint8_t recv_index;
  FP_NFAPI_P5_MESSAGE messages[NUMBER_OF_NFAPI_P5_MESSAGE_QUEUE];
}FP_NFAPI_P5_MESSAGE_QUEUE;

typedef struct {
  int port;
  char server_ip[1500];
  int server_socket;
  struct sockaddr_in client_addr;
  socklen_t addr_len;
  FP_NFAPI_P5_MESSAGE_QUEUE *p2v_queue;
  FP_NFAPI_P5_MESSAGE_QUEUE *v2p_queue;
  int client_socket;
}FP_NFAPI_P5_SERVER_OPTION;

typedef struct {
  int port;
  char server_ip[1500];
  int client_socket;
  FP_NFAPI_P5_MESSAGE_QUEUE *p2v_queue;
  FP_NFAPI_P5_MESSAGE_QUEUE *v2p_queue;
  uint8_t vnf_from_ipaddr[4];
  uint8_t vnf_proxy_ipaddr[4];
  uint8_t pnf_from_ipaddr[4];
  uint8_t pnf_proxy_ipaddr[4];
}FP_NFAPI_P5_CLIENT_OPTION;


void* nfapi_p5_sc_start_p2v_server(void *arg);
void* nfapi_p5_sc_start_p2v_client(void *arg);

void* nfapi_p5_sc_start_v2p_client(void *arg);
void* nfapi_p5_sc_start_v2p_client_as_server(void *arg);
