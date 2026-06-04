/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \briefFPGA accelerator integrated into OAI (for one and multi code block)
 */

#ifndef __NRLDPC_CODING_XDMA_OFFLOAD__H_

#define __NRLDPC_CODING_XDMA_OFFLOAD__H_

#include <stdint.h>

#define DEVICE_NAME_DEFAULT_USER "/dev/xdma0_user"
#define DEVICE_NAME_DEFAULT_ENC_READ "/dev/xdma0_c2h_1"
#define DEVICE_NAME_DEFAULT_ENC_WRITE "/dev/xdma0_h2c_1"
#define DEVICE_NAME_DEFAULT_DEC_READ "/dev/xdma0_c2h_0"
#define DEVICE_NAME_DEFAULT_DEC_WRITE "/dev/xdma0_h2c_0"

/**
    \brief LDPC input parameter
    \param Zc shifting size
    \param Rows
    \param baseGraph base graph
    \param CB_num number of code block
    \param numChannelLlrs input soft bits length, Zc x 66 - length of filler bits
    \param numFillerBits filler bits length
*/
typedef struct {
  char *user_device, *enc_write_device, *enc_read_device, *dec_write_device, *dec_read_device;
  unsigned char max_schedule;
  unsigned char SetIdx;
  int Zc;
  unsigned char numCB;
  unsigned char BG;
  unsigned char max_iter;
  int nRows;
  int numChannelLls;
  int numFillerBits;
} DecIFConf;

int nrLDPC_decoder_FPGA_PYM(uint8_t *buf_in, uint8_t *buf_out, DecIFConf dec_conf);

#endif // __NRLDPC_CODING_XDMA_OFFLOAD__H_

