#include <stdio.h>
#include <string.h>
#include <nats/nats.h>
#include <arpa/inet.h>
#include <unistd.h>
#pragma pack(1)
#define  MAX_NUM_CCs 5
#include "public_inc/nfapi_nr_interface.h"
#include "public_inc/nfapi_nr_interface_scf.h"

#define NATS_ADDR "192.168.182.10"
#define NATS_PORT 4222

#pragma pack()

void handle_message(natsConnection *nc, natsSubscription *sub, natsMsg *message, void *closure)
{
  nfapi_nr_p7_message_header_t *header = (nfapi_nr_p7_message_header_t *)natsMsg_GetData(message);

  uint16_t message_id = ntohs(header->message_id);
  // printf("received message id %x\n", message_id); 
  
  if (message_id == NFAPI_NR_PHY_MSG_TYPE_RX_DATA_INDICATION) {
    nfapi_nr_rx_data_indication_t *rx_ind = (nfapi_nr_rx_data_indication_t *)header;
   // printf("recived");
  // 1. 统一加上 ntohs 字节序转换
    uint16_t sfn = ntohs(rx_ind->sfn);
    uint16_t slot = ntohs(rx_ind->slot);
    uint16_t num_pdus = ntohs(rx_ind->number_of_pdus); 
    printf("num_pdus %d\n", num_pdus);
    printf("sfn %d\n", sfn);
    printf("slot %d\n", slot);  // 这里也加上 ntohs

     //2. 关键过滤：只在有数据时打印！不被空 Slot 刷屏
    if (num_pdus > 0) {
        printf("\n==================================================\n");
        printf("[SUCCESS] [%d.%d] Captured %d PDU(s)!\n", sfn, slot, num_pdus);
        printf("==================================================\n");

        for (int i = 0; i < num_pdus; i++) {
          printf("step1 \n");
          uint8_t *p = (uint8_t *)header; 
          nfapi_nr_rx_data_pdu_t *pdu = (nfapi_nr_rx_data_pdu_t *)&p[sizeof(nfapi_nr_rx_data_indication_t) - sizeof(nfapi_nr_rx_data_pdu_t*)];
          // nfapi_nr_rx_data_pdu_t *pdu = &rx_ind->pdu_list[i];
          printf("step2 \n");
            uint16_t rnti = ntohs(pdu->rnti);
            uint32_t pdu_len = ntohl(pdu->pdu_length);
            
            uint8_t *payload = (uint8_t *)(pdu + 1);
            
            printf("rnti %x\n", rnti);
            printf("pdu_len %d\n", pdu_len);
            
            for (int j = 0; j < (int)pdu_len; j++) {
              printf("%02x ", payload[j]);
            }
        }
      }     
  }
}            //printf("  -> [PDU %d] RNTI: 0x%04X, Payload Length: %u bytes\n", i, rnti, pdu_len);
        //      if (payload != NULL && pdu_len > 0) {
        //         printf("     Data (Hex): ");
        //          for (int j = 0; j < pdu_len; j++) {
        //              printf("%02X ", payload[j]);
        //          }
        //          printf("\n\n");
        //      } else {
        //          printf("     [WARN] Payload pointer is NULL!\n");
        //      }
        // }
    //}

 /* if(message_id == NFAPI_NR_PHY_MSG_TYPE_DL_TTI_REQUEST){
    printf("receivdd DL_TTI\n");
  }
  if(message_id == NFAPI_NR_PHY_MSG_TYPE_UL_TTI_REQUEST){
    printf("receivdd UL_TTI\n");
  }
  if(message_id ==NFAPI_NR_PHY_MSG_TYPE_SLOT_INDICATION ){
    printf("receivdd SLOT_IND\n");
  }    
  if(message_id == NFAPI_NR_PHY_MSG_TYPE_UL_DCI_REQUEST){
    printf("receivdd UL_DCI\n");
  }    
  if(message_id == NFAPI_NR_PHY_MSG_TYPE_TX_DATA_REQUEST){
    printf("receivdd TX_DATA\n");
  }*/    



int main()
{
  natsConnection *nc = NULL;
  natsSubscription *sub = NULL;

  uint8_t connect_str [1500] = "";
  sprintf(connect_str, "%s:%d", NATS_ADDR, NATS_PORT);
  natsConnection_ConnectTo(&nc, connect_str);
  natsConnection_Subscribe(&sub, nc, "fapi.*",
			   handle_message,
			   NULL);
  while(true){
    sleep(100);
  }
}
