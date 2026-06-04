/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef _ORAN_H_
#define _ORAN_H_

#include "shared_buffers.h"
#include "common_lib.h"
void oran_fh_if4p5_south_out(RU_t *ru, int frame, int slot, uint64_t timestamp);

void oran_fh_if4p5_south_in(RU_t *ru, int *frame, int *slot);

int transport_init(openair0_device_t *device, openair0_config_t *openair0_cfg, eth_params_t *eth_params);

typedef struct {
  eth_state_t e;
  shared_buffers buffers;
  rru_config_msg_type_t last_msg;
  int capabilities_sent;
  void *oran_priv;
} oran_eth_state_t;
#endif /* _ORAN_H_ */
