#include "sample_agent.h"

#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h>
#include <nats/nats.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "nfapi_server_client/nfapi_proxy_command.h"

#define UDP_CONTROL_SERVER_PORT 5555

static void get_rnti_list(NFAPI_PROXY_RESPONSE_RNTI_LIST *response, int client_sock, struct sockaddr_in *server_addr)
{
  NFAPI_PROXY_GET_RNTI_LIST request;
  request.id = GET_RNTI_LIST;
  sendto(client_sock, (char *)&request, sizeof(NFAPI_PROXY_GET_RNTI_LIST), 0,
	 (struct sockaddr *)server_addr, sizeof(struct sockaddr_in));
  
  struct sockaddr_in from_addr;
  socklen_t addr_len = sizeof(from_addr);
  recvfrom(client_sock, response, sizeof(NFAPI_PROXY_RESPONSE_RNTI_LIST), 0, (struct sockaddr *)&from_addr, &addr_len);
  if(response->id != RESPONSE_RNTI_LIST){
    return ;
  }
}

void* control_func_via_udp(void *arg)
{
  int client_sock;
  struct sockaddr_in server_addr;
  client_sock = socket(AF_INET, SOCK_DGRAM, 0);

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(UDP_CONTROL_SERVER_PORT);
  inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
  
  sleep(1);
  while(true){
    uint8_t buffer[255];
    printf("[bsr sr ls stop rb exit]\n");
    int length = scanf("%s", buffer);

    if(strncmp(buffer, "sr", 2) == 0){
      NFAPI_PROXY_RESPONSE_RNTI_LIST response;
      get_rnti_list(&response, client_sock, &server_addr);
      for(int i = 0; i < response.number_of_rnti; i++){
	NFAPI_PROXY_START_SR start_sr = {.id = START_SR,
					   .RNTI = response.RNTI[i]};
	sendto(client_sock, (char *)&start_sr, sizeof(NFAPI_PROXY_START_SR), 0,
	       (struct sockaddr *)&server_addr, sizeof(server_addr));
      }
    }else if(strncmp(buffer, "bsr", 3) == 0){
      uint32_t bsr_value = 0;
      printf("- BSR value(bytes): ");
      scanf("%d", &bsr_value);

      NFAPI_PROXY_RESPONSE_RNTI_LIST response;
      get_rnti_list(&response, client_sock, &server_addr);
      for(int i = 0; i < response.number_of_rnti; i++){
	NFAPI_PROXY_START_BSR bsr_request = {.id = START_BSR,
					     .RNTI = response.RNTI[i],
					     .bsr_value = ntohl(bsr_value)};
	sendto(client_sock, (char *)&bsr_request, sizeof(NFAPI_PROXY_START_BSR), 0,
	       (struct sockaddr *)&server_addr, sizeof(server_addr));
      }
      
    }else if(strncmp(buffer, "ls", 2) == 0){
      NFAPI_PROXY_RESPONSE_RNTI_LIST response;
      get_rnti_list(&response, client_sock, &server_addr);
      //show RNTI LIST
      uint8_t output_buffer[1500];
      uint32_t output_index = 0;
      for(int i = 0; i < response.number_of_rnti; i++){
	output_index += sprintf(&output_buffer[output_index],
				"[%d] RNTI:%04x\n", i, ntohs(response.RNTI[i]));
      }
      printf("%s\n", output_buffer);
      
    }else{
      NFAPI_PROXY_RESPONSE_RNTI_LIST response;
      get_rnti_list(&response, client_sock, &server_addr);
      for(int i = 0; i < response.number_of_rnti; i++){
	NFAPI_PROXY_STOP_BSR stop_bsr = {.id = STOP_BSR,
					 .RNTI = response.RNTI[i]};
	sendto(client_sock, (char *)&stop_bsr, sizeof(NFAPI_PROXY_STOP_BSR), 0,
	       (struct sockaddr *)&server_addr, sizeof(server_addr));
	
	NFAPI_PROXY_STOP_SR stop_sr = {.id = STOP_SR,
				       .RNTI = response.RNTI[i]};
	sendto(client_sock, (char *)&stop_sr, sizeof(NFAPI_PROXY_STOP_SR), 0,
	       (struct sockaddr *)&server_addr, sizeof(server_addr));	
      }
    }
  }
}
