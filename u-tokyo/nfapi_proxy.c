#define _GNU_SOURCE 
#include <sched.h>
#include <errno.h>

#include "nfapi_server_client/nfapi_p7_sc.h"
#include "nfapi_server_client/nfapi_p5_sc.h"
#include "sample_agent.h"

#include <pthread.h>

static FP_NFAPI_MESSAGE_QUEUE p2v_queue = {0};
static FP_NFAPI_MESSAGE_QUEUE v2p_queue = {0};

static FP_NFAPI_P5_MESSAGE_QUEUE p5_p2v_queue = {0};
static FP_NFAPI_P5_MESSAGE_QUEUE p5_v2p_queue = {0};

void assign_rr(pthread_attr_t *attr, int cpu_core)
{
  pthread_attr_init(attr);
  int policy = SCHED_RR;
  int ret = pthread_attr_setinheritsched(attr, PTHREAD_EXPLICIT_SCHED);
  ret = pthread_attr_setschedpolicy(attr, policy);
  struct sched_param sparam = {0};
  sparam.sched_priority = sched_get_priority_max(policy) - 1;  
  ret = pthread_attr_setschedparam(attr, &sparam);

  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu_core, &cpuset);
  ret = pthread_attr_setaffinity_np(attr, sizeof(cpu_set_t), &cpuset);
  if (ret != 0) {
    perror("pthread_attr_setaffinity_np");
    exit(1);
  }
}

static  natsConnection *nc;

int main()
{
  natsConnection_ConnectTo(&nc , "192.168.182.10:4222");
  
  FP_NFAPI_P5_SERVER_OPTION p5_p2v_server_option = {.port = 50001,
						    .server_ip = "192.168.181.20",
						    .p2v_queue = &p5_p2v_queue,
						    .v2p_queue = &p5_v2p_queue,
						    .client_addr = {0},
						    .client_socket = -1};
  FP_NFAPI_P5_CLIENT_OPTION p5_p2v_client_option = {.port = 50001,
						    .server_ip = "192.168.181.10",
						    .client_socket = -1,
						    .p2v_queue = &p5_p2v_queue,
						    .v2p_queue = &p5_v2p_queue,
						    .vnf_from_ipaddr =  {192, 168, 181, 10},
						    .vnf_proxy_ipaddr = {192, 168, 181, 20},
						    .pnf_from_ipaddr =  {192, 168, 181, 1},
						    .pnf_proxy_ipaddr = {192, 168, 181, 20}};
  
  pthread_t p5_p2v_server, p5_p2v_client, p5_v2p_server, p5_v2p_client;
  pthread_create(&p5_p2v_server, NULL, nfapi_p5_sc_start_p2v_server, &p5_p2v_server_option);
  pthread_create(&p5_p2v_client, NULL, nfapi_p5_sc_start_p2v_client, &p5_p2v_server_option);

  pthread_create(&p5_v2p_server, NULL, nfapi_p5_sc_start_v2p_client, &p5_p2v_client_option);
  pthread_create(&p5_v2p_client, NULL, nfapi_p5_sc_start_v2p_client_as_server, &p5_p2v_client_option);

  NFAPI_P7_SC_OPTION sc_option = {.vnf_port = 50011,
				  .pnf_port = 50010,
				  .vnf_ip = "192.168.181.10",
				  .pnf_ip = "192.168.181.1",
				  .socket_with_vnf = -1,
				  .socket_with_pnf = -1,
				  .p2v_queue = &p2v_queue,
				  .v2p_queue = &v2p_queue,
				  .pnf_addr = {},
				  .pnf_addr_len = sizeof(struct sockaddr_in),
				  .vnf_addr = {},
				  .vnf_addr_len = sizeof(struct sockaddr_in),
				  .nc = nc
  };
    
  pthread_t p2v_server, p2v_client, v2p_server, v2p_client;
  pthread_attr_t p2v_server_att, p2v_client_att, v2p_server_att, v2p_client_att;
  assign_rr(&p2v_server_att, 4);
  assign_rr(&p2v_client_att, 5);
  assign_rr(&v2p_server_att, 6);
  assign_rr(&v2p_client_att, 7);

  pthread_cond_init(&p2v_queue.cond, NULL);
  pthread_mutex_init(&p2v_queue.mutex, NULL);

  pthread_cond_init(&v2p_queue.cond, NULL);
  pthread_mutex_init(&v2p_queue.mutex, NULL);

  pthread_create(&p2v_server,  &p2v_server_att, nfapi_p7_sc_start_v2p_server, &sc_option); //vnf -> pnf
  pthread_create(&p2v_client,  &p2v_client_att, nfapi_p7_sc_start_v2p_client, &sc_option); //vnf -> pnf
  
  pthread_create(&v2p_server, &v2p_server_att, nfapi_p7_sc_start_p2v_client, &sc_option); //pnf -> real vnf
  pthread_create(&v2p_client, &v2p_client_att, nfapi_p7_sc_start_p2v_client_as_server, &sc_option); //real pnf -> vnf

  pthread_t input_thread;
  pthread_create(&input_thread, NULL, &control_func_via_udp, NULL);
  
  pthread_join(p2v_server, NULL);
  
  return 0;
}


