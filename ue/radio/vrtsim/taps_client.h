/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef TAPS_CLIENT_H
#define TAPS_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sim.h"

void taps_client_connect(int id, const char *socket_path, int num_tx_antennas, int num_rx_antennas, channel_desc_t **channel_desc);
void taps_client_stop();

#ifdef __cplusplus
}
#endif

#endif
