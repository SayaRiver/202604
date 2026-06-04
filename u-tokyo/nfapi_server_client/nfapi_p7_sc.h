#pragma once

#include <stdint.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <nats/nats.h>

#include "nfapi_server_client/nfapi_proxy_command.h"
#include "fapi_proxy_message.h"

#define NUMBER_OF_NFAPI_MESSAGE_QUEUE 100
#define SIZE_OF_NFAPI_MESSAGE_PAYLOAD 40000

/* typedef struct{ */
/*   ssize_t payload_length; */
/*   uint8_t payload[SIZE_OF_NFAPI_MESSAGE_PAYLOAD]; */
/* }FP_NFAPI_MESSAGE; */

typedef struct{
  uint8_t send_index;
  uint8_t recv_index;
  pthread_cond_t cond;
  pthread_mutex_t mutex;
  FAPI_PROXY_MESSAGE messages[NUMBER_OF_NFAPI_MESSAGE_QUEUE];
}FP_NFAPI_MESSAGE_QUEUE;

typedef struct {
  int vnf_port;
  int pnf_port;
  char vnf_ip[1500];
  char pnf_ip[1500];
  int socket_with_vnf; 
  int socket_with_pnf;
  struct sockaddr_in vnf_addr;
  socklen_t vnf_addr_len;
  struct sockaddr_in pnf_addr;
  socklen_t pnf_addr_len;  
  FP_NFAPI_MESSAGE_QUEUE *p2v_queue;
  FP_NFAPI_MESSAGE_QUEUE *v2p_queue;
  natsConnection *nc;
}NFAPI_P7_SC_OPTION;

void* nfapi_p7_sc_start_v2p_server(void *arg);
void* nfapi_p7_sc_start_v2p_client(void *arg);

void* nfapi_p7_sc_start_p2v_client(void *arg);
void* nfapi_p7_sc_start_p2v_client_as_server(void *arg);
