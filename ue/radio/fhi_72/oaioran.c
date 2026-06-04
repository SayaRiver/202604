/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "xran_fh_o_du.h"
#include "xran_compression.h"
#include "armral_bfp_compression.h"

#if defined(__arm__) || defined(__aarch64__)
#else
// xran_cp_api.h uses SIMD, but does not include it
#include <immintrin.h>
#endif
#include "xran_cp_api.h"
#include "xran_sync_api.h"
#include "oran_isolate.h"
#include "oran-init.h"
#include "oaioran.h"
#include <rte_ethdev.h>

#include "oran-config.h" // for g_kbar

#include "common/utils/threadPool/notified_fifo.h"

#define N_SC_PER_PRB 12

#if OAI_FHI72_USE_POLLING
#define USE_POLLING
#endif

// Declare variable useful for the send buffer function
volatile bool first_call_set = false;

int xran_is_prach_slot(uint8_t PortId, uint32_t subframe_id, uint32_t slot_id);
#include "common/utils/LOG/log.h"

#ifndef USE_POLLING
extern notifiedFIFO_t oran_sync_fifo;
atomic_int xran_queue_length = 0;
#else
volatile oran_sync_info_t oran_sync_info = {0};
#endif

/** @details xran-specific callback, called when all packets for given CC and
 * 1/4, 1/2, 3/4, all symbols of a slot arrived. Currently, only used to get
 * timing information and unblock another thread in xran_fh_rx_read_slot()
 * through either a message queue, or writing in global memory with polling, on
 * a full slot boundary. */
void oai_xran_fh_rx_callback(void *pCallbackTag, xran_status_t status)
{
  struct xran_cb_tag *callback_tag = (struct xran_cb_tag *)pCallbackTag;

  static int32_t last_slot = -1;
  static int32_t last_frame = -1;

  const struct xran_fh_init *fh_init = get_xran_fh_init();
  int num_ports = fh_init->xran_ports;

  /* assuming all RUs have the same numerology */
  const struct xran_fh_config *fh_cfg = get_xran_fh_config(0);
  const int slots_in_sf = 1 << fh_cfg->frame_conf.nNumerology;
  const int sf_in_frame = 10;

  static int rx_RU[XRAN_PORTS_NUM][160] = {0};
  uint32_t tti = callback_tag->slotiId;
  uint32_t frame = XranGetFrameNum(tti, 0, sf_in_frame, slots_in_sf);
  uint32_t subframe = XranGetSubFrameNum(tti, slots_in_sf, sf_in_frame);
  uint32_t slot = XranGetSlotNum(tti, slots_in_sf);

  uint32_t rx_sym = callback_tag->symbol & 0xFF;
  uint32_t ru_id = callback_tag->oXuId;

  LOG_D(HW, "rx_callback at %4d.%3d (subframe %d), rx_sym %d ru_id %d\n", frame, slot, subframe, rx_sym, ru_id);

  if (rx_sym == 7) { // in F release this value is defined as XRAN_FULL_CB_SYM (full slot (offset + 7))
#if defined F_RELEASE
    for (int ru_idx = 0; ru_idx < num_ports; ru_idx++) {
      struct xran_fh_config *fh_config = get_xran_fh_config(ru_idx);
      oran_buf_list_t *bufs = get_xran_buffers(ru_idx);
      for (uint16_t cc_id = 0; cc_id < 1 /* fh_config->nCC */; cc_id++) { // OAI does not support multiple CC yet.
        for(uint32_t ant_id = 0; ant_id < fh_config->neAxc; ant_id++) {
          struct xran_prb_map *pRbMap = (struct xran_prb_map *)bufs->dstcp[ant_id][tti % XRAN_N_FE_BUF_LEN].pBuffers->pData;
          AssertFatal(pRbMap != NULL, "(%d:%d:%d)pRbMap == NULL. Aborting.\n", cc_id, tti % XRAN_N_FE_BUF_LEN, ant_id);

          for (uint32_t sym_id = 0; sym_id < XRAN_NUM_OF_SYMBOL_PER_SLOT; sym_id++) {
            LOG_D(HW, "cb pRbMap->nPrbElm %d\n", pRbMap->nPrbElm);
            for (uint32_t idxElm = 0; idxElm < pRbMap->nPrbElm; idxElm++ ) {
              struct xran_prb_elm *pRbElm = &pRbMap->prbMap[idxElm];
              pRbElm->nSecDesc[sym_id] = 0; // number of section descriptors per symbol; M-plane info <supported-section-types>
            }
          }
        }
      }
    }
#endif
    // if xran did not call xran_physide_dl_tti callback, it's not ready yet.
    // wait till first callback to advance counters, because otherwise users
    // would see periodic output with only "0" in stats counters
    if (!first_call_set)
      return;
    uint32_t slot2 = slot + (subframe * slots_in_sf);
    rx_RU[ru_id][slot2] = 1;
    if (last_frame > 0 && frame > 0
        && ((slot2 > 0 && last_frame != frame) || (slot2 == 0 && last_frame != ((1024 + frame - 1) & 1023))))
      LOG_E(HW, "Jump in frame counter last_frame %d => %d, slot %d\n", last_frame, frame, slot2);
    for (int i = 0; i < num_ports; i++) {
      if (rx_RU[i][slot2] == 0)
        return;
    }
    for (int i = 0; i < num_ports; i++)
      rx_RU[i][slot2] = 0;

    if (last_slot == -1 || slot2 != last_slot) {
#ifndef USE_POLLING
      notifiedFIFO_elt_t *req = newNotifiedFIFO_elt(sizeof(oran_sync_info_t), 0, &oran_sync_fifo, NULL);
      oran_sync_info_t *info = NotifiedFifoData(req);
      info->tti = tti;
      info->sl = slot2;
      info->f = frame;
      LOG_D(HW, "Push %d.%d.%d (slot %d, subframe %d,last_slot %d)\n", frame, info->sl, slot, ru_id, subframe, last_slot);
      atomic_fetch_add(&xran_queue_length, 1);
      pushNotifiedFIFO(&oran_sync_fifo, req);
#else
      LOG_D(HW, "Writing %d.%d.%d (slot %d, subframe %d,last_slot %d)\n", frame, slot2, ru_id, slot, subframe, last_slot);
      oran_sync_info.tti = tti;
      oran_sync_info.sl = slot2;
      oran_sync_info.f = frame;
#endif
    } else
      LOG_E(HW, "Cannot Push %d.%d.%d (slot %d, subframe %d,last_slot %d)\n", frame, slot2, ru_id, slot, subframe, last_slot);
    last_slot = slot2;
    last_frame = frame;
  } // rx_sym == 7
}

/** @details Only used to unblock timing in oai_xran_fh_rx_callback() on first
 * call. */
int oai_physide_dl_tti_call_back(void *param)
{
  if (!first_call_set)
    LOG_I(HW, "first_call set from phy cb\n");
  first_call_set = true;
  return 0;
}

/** @brief Reads PRACH data from xran buffers.
 *
 * @details Reads PRACH data from xran-specific buffers and, if I/Q compression
 * (bitwidth < 16 bits) is configured, uncompresses the data. Places PRACH data
 * in OAI buffer. */
static int read_prach_data(ru_info_t *ru, int frame, int slot)
{
  /* calculate tti and subframe_id from frame, slot num */
  int sym_idx = 0;

  struct xran_fh_init *fh_init = get_xran_fh_init();
  struct xran_fh_config *fh_cfg = get_xran_fh_config(0);
  nr_prach_info_t prach_info = get_prach_info(0);

  uint16_t N_ZC, num_prbu;
  if ((prach_info.format & 0xff) < 4) {
    N_ZC = 839;
    num_prbu = 70;
    prach_info.N_dur = 1;
  } else {
    N_ZC = 139;
    num_prbu = 12;
  }

  int prach_start_sym = prach_info.start_symbol;
  int prach_end_sym = prach_info.N_dur + prach_start_sym;
  struct xran_ru_config *ru_conf = &fh_cfg->ru_conf;
  int slots_per_frame = 10 << fh_cfg->frame_conf.nNumerology;
  int tti = slots_per_frame * (frame) + (slot);

  int nb_rx_per_ru = ru->nb_rx / fh_init->xran_ports;
  /* If it is PRACH slot, copy prach IQ from XRAN PRACH buffer to OAI PRACH buffer */
  if (ru->prach_buf) {
    for (sym_idx = prach_start_sym; sym_idx < prach_end_sym; sym_idx++) {
      for (int aa = 0; aa < ru->nb_rx; aa++) {
        int16_t *dst, *src;
        int idx = 0;
        oran_buf_list_t *bufs = get_xran_buffers(aa / nb_rx_per_ru);
        // hardcoded to use only first prach occasion
        dst = (int16_t *)ru->prach_buf[aa][0];
        src = (int16_t *)bufs->prachdstdecomp[aa % nb_rx_per_ru][tti % XRAN_N_FE_BUF_LEN].pBuffers[sym_idx].pData;
        /* convert Network order to host order */
        if (ru_conf->compMeth_PRACH == XRAN_COMPMETHOD_NONE) {
          if (sym_idx == prach_start_sym) {
            for (idx = 0; idx < N_ZC * 2; idx++) {
              dst[idx] = ((int16_t)ntohs(src[idx + g_kbar]));
            }
          } else {
            for (idx = 0; idx < N_ZC * 2; idx++) {
              dst[idx] += ((int16_t)ntohs(src[idx + g_kbar]));
            }
          }
        } else if (ru_conf->compMeth_PRACH == XRAN_COMPMETHOD_BLKFLOAT) {

          int16_t local_dst[num_prbu * 2 * N_SC_PER_PRB] __attribute__((aligned(64)));

#if defined(__i386__) || defined(__x86_64__)
          struct xranlib_decompress_request bfp_decom_req = {};
          struct xranlib_decompress_response bfp_decom_rsp = {};
          int payload_len = (3 * ru_conf->iqWidth_PRACH + 1) * num_prbu;

          bfp_decom_req.data_in = (int8_t *)src;
          bfp_decom_req.numRBs = num_prbu;
          bfp_decom_req.len = payload_len;
          bfp_decom_req.compMethod = XRAN_COMPMETHOD_BLKFLOAT;
          bfp_decom_req.iqWidth = ru_conf->iqWidth_PRACH;

          bfp_decom_rsp.data_out = (int16_t *)local_dst;
          bfp_decom_rsp.len = 0;
          xranlib_decompress_avx512(&bfp_decom_req, &bfp_decom_rsp);
#elif defined(__arm__) || defined(__aarch64__)
          armral_bfp_decompression(ru_conf->iqWidth_PRACH, num_prbu, (int8_t *)src, (int16_t *)local_dst);
#else
          AssertFatal(1 == 0, "BFP decompression not supported on this architecture");
#endif
          if (sym_idx == prach_start_sym)
            for (idx = 0; idx < (N_ZC * 2); idx++)
              dst[idx] = local_dst[idx + g_kbar];
          else
            for (idx = 0; idx < (N_ZC * 2); idx++)
              dst[idx] += (local_dst[idx + g_kbar]);
        } // COMPMETHOD_BLKFLOAT
      } // aa
    } // symb_indx
  } // ru->prach_buf
  return (0);
}

/** @brief Check if symbol in slot is UL.
 *
 * @param frame_conf xran frame configuration
 * @param slot the current (absolute) slot (number)
 * @param sym_idx the current symbol index */
static bool is_tdd_ul_symbol(const struct xran_frame_config *frame_conf, int slot, int sym_idx)
{
  /* in FDD, every symbol is also UL */
  if (frame_conf->nFrameDuplexType == XRAN_FDD)
    return true;
  int tdd_period = frame_conf->nTddPeriod;
  int slot_in_period = slot % tdd_period;
  /* check if symbol is UL */
  return frame_conf->sSlotConfig[slot_in_period].nSymbolType[sym_idx] == 1 /* UL */;
}

/** @brief Check if symbol in slot is DL.
 *
 * @param frame_conf xran frame configuration
 * @param slot the current (absolute) slot (number)
 * @param sym_idx the current symbol index */
static bool is_tdd_dl_symbol(const struct xran_frame_config *frame_conf, int slot, int sym_idx)
{
  /* in FDD, every symbol is also UL */
  if (frame_conf->nFrameDuplexType == XRAN_FDD)
    return true;
  int tdd_period = frame_conf->nTddPeriod;
  int slot_in_period = slot % tdd_period;
  /* check if symbol is UL */
  return frame_conf->sSlotConfig[slot_in_period].nSymbolType[sym_idx] == 0 /* DL */;
}

/** @brief Check if current slot is guard/mixed */
static bool is_tdd_guard_slot(const struct xran_frame_config *frame_conf, int slot)
{
  return (is_tdd_dl_symbol(frame_conf, slot, 0) && is_tdd_ul_symbol(frame_conf, slot,  XRAN_NUM_OF_SYMBOL_PER_SLOT - 1));
}

/** @brief Check if current slot is DL or guard/mixed without UL (i.e., current
 * slot is not UL). */
static bool is_tdd_dl_guard_slot(const struct xran_frame_config *frame_conf, int slot)
{
  return !is_tdd_ul_symbol(frame_conf, slot, 0);
}

/** @brief Check if current slot is UL or guard/mixed without UL (i.e., current
 * slot is not UL). */
static bool is_tdd_ul_guard_slot(const struct xran_frame_config *frame_conf, int slot)
{
  return is_tdd_ul_symbol(frame_conf, slot, XRAN_NUM_OF_SYMBOL_PER_SLOT - 1);
}

/** @details Read PRACH and PUSCH data from xran buffers.  If
 * I/Q compression (bitwidth < 16 bits) is configured, deccompresses the data
 * before writing. Prints ON TIME counters every 128 frames.
 *
 * Function is blocking and waits for next frame/slot combination. It is unblocked
 * by oai_xran_fh_rx_callback(). It writes the current slot into parameters
 * frame/slot. */
int xran_fh_rx_read_slot(ru_info_t *ru, int *frame, int *slot)
{
  void *ptr = NULL;
  int32_t *pos = NULL;
  int idx = 0;

  static int64_t old_rx_counter[XRAN_PORTS_NUM] = {0};
  static int64_t old_tx_counter[XRAN_PORTS_NUM] = {0};
  struct xran_common_counters x_counters[XRAN_PORTS_NUM];
  static int outcnt = 0;
#ifndef USE_POLLING
  // pull next event from oran_sync_fifo
  notifiedFIFO_elt_t *res = pullNotifiedFIFO(&oran_sync_fifo);
  atomic_fetch_sub(&xran_queue_length, 1);
  oran_sync_info_t *info = NotifiedFifoData(res);

#define MAX_QUEUE_LENGTH_NO_JUMP 3
  if (xran_queue_length > 0 && xran_queue_length < MAX_QUEUE_LENGTH_NO_JUMP) {
    LOG_D(HW, "%4d.%2d TTI processing delay detected\n", info->f, info->sl);
  } else if (xran_queue_length >= MAX_QUEUE_LENGTH_NO_JUMP) {
    uint32_t old_f = info->f;
    uint32_t old_sl = info->sl;
    // set the frame/slot info to what is in the last message
    notifiedFIFO_elt_t *f;
    while ((f = pollNotifiedFIFO(&oran_sync_fifo)) != NULL) {
      atomic_fetch_sub(&xran_queue_length, 1);
      delNotifiedFIFO_elt(res);
      res = f;
    }
    info = NotifiedFifoData(res);
    LOG_W(HW, "TTI processing delay detected, skipping %4d.%2d => %4d.%2d\n", old_f, old_sl, info->f, info->sl);
    DevAssert(xran_queue_length == 0);
  }

  *slot = info->sl;
  *frame = info->f;
  delNotifiedFIFO_elt(res);
#else
  *slot = oran_sync_info.sl;
  *frame = oran_sync_info.f;
  uint32_t tti_in = oran_sync_info.tti;

  static int last_slot = -1;
  LOG_D(HW, "oran slot %d, last_slot %d\n", *slot, last_slot);
  int cnt = 0;
  // while (*slot == last_slot)  {
  while (tti_in == oran_sync_info.tti) {
    //*slot = oran_sync_info.sl;
    cnt++;
  }
  LOG_D(HW, "cnt %d, Reading %d.%d\n", cnt, *frame, *slot);
  last_slot = *slot;
#endif
  // return(0);

  struct xran_fh_config *fh_cfg = get_xran_fh_config(0);
  int slots_per_frame = 10 << fh_cfg->frame_conf.nNumerology;

  int tti = slots_per_frame * (*frame) + (*slot);

  read_prach_data(ru, *frame, *slot);

  const struct xran_fh_init *fh_init = get_xran_fh_init();
  int fftsize = 1 << fh_cfg->nULFftSize;

  int slot_offset_rxdata = 3 & (*slot);
  uint32_t slot_size = 4 * 14 * fftsize;
  uint8_t *rx_data = (uint8_t *)ru->rxdataF[0];
  uint8_t *start_ptr = NULL;
  int nb_rx_per_ru = ru->nb_rx / fh_init->xran_ports;
  for (uint16_t cc_id = 0; cc_id < 1 /*nSectorNum*/; cc_id++) { // OAI does not support multiple CC yet.
    for (uint8_t ant_id = 0; ant_id < ru->nb_rx; ant_id++) {
      rx_data = (uint8_t *)ru->rxdataF[ant_id];
      start_ptr = rx_data + (slot_size * slot_offset_rxdata);
      const struct xran_frame_config *frame_conf = &get_xran_fh_config(ant_id / nb_rx_per_ru)->frame_conf;
      // skip processing this slot is TX (no RX in this slot)
      if (!is_tdd_ul_guard_slot(frame_conf, *slot))
        continue;
      // This loop would better be more inner to avoid confusion and maybe also errors.
      for (int32_t sym_idx = 0; sym_idx < XRAN_NUM_OF_SYMBOL_PER_SLOT; sym_idx++) {
        /* the callback is for mixed and UL slots. In mixed, we have to
         * skip DL and guard symbols. */
        if (!is_tdd_ul_symbol(frame_conf, *slot, sym_idx))
          continue;

        oran_buf_list_t *bufs = get_xran_buffers(ant_id / nb_rx_per_ru);
        uint8_t *pPrbMapData = bufs->dstcp[ant_id % nb_rx_per_ru][tti % XRAN_N_FE_BUF_LEN].pBuffers->pData;
        struct xran_prb_map *pRbMap = (struct xran_prb_map *)pPrbMapData;

        uint8_t *src = (uint8_t *)ptr;

        // even when the fragmentation occurs, nRBSize & nRBStart carry the same values in each prbMap
        // therefore, I took the liberty to just extract these values from the first prbMap
        int num_totalRB = pRbMap->prbMap[0].nRBSize;
        int start_totalRB = pRbMap->prbMap[0].nRBStart;
        int32_t local_dst[num_totalRB * N_SC_PER_PRB] __attribute__((aligned(64)));

        LOG_D(HW, "[%d.%d] pRbMap->nPrbElm %d\n", *frame, *slot, pRbMap->nPrbElm);
        for (uint32_t idxElm = 0; idxElm < pRbMap->nPrbElm; idxElm++) {
          int numRB, startRB;
          uint8_t *pData;
          struct xran_section_desc *p_sec_desc = NULL;
          struct xran_prb_elm *pRbElm = &pRbMap->prbMap[idxElm];
#if defined F_RELEASE
          // UP_nRBSize & UP_nRBStart are for DL U-plane only
          LOG_D(HW, "[%d.%d] idxElm[%d] startSym[%d]:numSym[%d] UP_startRB[%d]:UP_numRB[%d] sym_idx[%d] ant_id[%d] pRbElm->nRBStart[%d]:pRbElm->nRBSize[%d]\n", *frame, *slot, idxElm, pRbElm->nStartSymb, pRbElm->numSymb, pRbElm->UP_nRBStart, pRbElm->UP_nRBSize, sym_idx, ant_id, pRbElm->nRBStart, pRbElm->nRBSize);
          for (int idxDesc = 0; idxDesc < XRAN_MAX_FRAGMENT; idxDesc++) {
            p_sec_desc = &pRbElm->sec_desc[sym_idx][idxDesc];
            if (p_sec_desc == NULL)
              continue;
            if (sym_idx >= pRbElm->nStartSymb && sym_idx < pRbElm->nStartSymb + pRbElm->numSymb) {
              if (!p_sec_desc->pCtrl)
                continue;
              pData = p_sec_desc->pData;
              numRB = p_sec_desc->num_prbu;
              startRB = p_sec_desc->start_prbu;
              // num_prbu & start_prbu are for UL U-plane only
              LOG_D(HW, "p_sec_desc[%d] startRB[%d]:numRB[%d]\n", idxDesc, startRB, numRB);
#endif
              ptr = pData;
              pos = (int32_t *)(start_ptr + (4 * sym_idx * fftsize));
              if (ptr == NULL || pos == NULL)
                continue;
              src = pData;
              if (pRbElm->compMethod == XRAN_COMPMETHOD_NONE) {
                // NOTE: gcc 11 knows how to generate AVX2 for this!
                for (idx = 0; idx < (numRB * N_SC_PER_PRB) * 2; idx++)
                  ((int16_t *)local_dst)[idx + startRB * N_SC_PER_PRB * 2] = ((int16_t)ntohs(((uint16_t *)src)[idx])) >> 2;
              } else if (pRbElm->compMethod == XRAN_COMPMETHOD_BLKFLOAT) {
#if defined(__i386__) || defined(__x86_64__)
                struct xranlib_decompress_request bfp_decom_req = {};
                struct xranlib_decompress_response bfp_decom_rsp = {};

                int16_t payload_len = (3 * pRbElm->iqWidth + 1) * numRB;

                bfp_decom_req.data_in = (int8_t *)src;
                bfp_decom_req.numRBs = numRB;
                bfp_decom_req.len = payload_len;
                bfp_decom_req.compMethod = pRbElm->compMethod;
                bfp_decom_req.iqWidth = pRbElm->iqWidth;

                bfp_decom_rsp.data_out = (int16_t *) (local_dst + startRB * N_SC_PER_PRB);
                bfp_decom_rsp.len = 0;

                xranlib_decompress_avx512(&bfp_decom_req, &bfp_decom_rsp);
#elif defined(__arm__) || defined(__aarch64__)
                armral_bfp_decompression(pRbElm->iqWidth, numRB, (int8_t *)src, (int16_t *)local_dst);
#else
                AssertFatal(1 == 0, "BFP compression not supported on this architecture");
#endif
                outcnt++;
              } else {
                printf("pRbElm->compMethod == %d is not supported\n", pRbElm->compMethod);
                exit(-1);
              }
              if ((startRB + numRB) == (start_totalRB + num_totalRB)) {
                int pos_len = 0;
                int neg_len = 0;

                if (start_totalRB < (num_totalRB >> 1)) // there are PRBs left of DC
                  neg_len = min((num_totalRB * 6) - (start_totalRB * 12), num_totalRB * N_SC_PER_PRB);
                pos_len = (num_totalRB * N_SC_PER_PRB) - neg_len;
                // Calculation of the pointer for the section in the buffer.
                // positive half
                uint8_t *dst1 = (uint8_t *)(pos + (neg_len == 0 ? ((start_totalRB * N_SC_PER_PRB) - (num_totalRB * 6)) : 0));
                // negative half
                uint8_t *dst2 = (uint8_t *)(pos + (start_totalRB * N_SC_PER_PRB) + fftsize - (num_totalRB * 6));
                memcpy((void *)dst2, (void *)local_dst, neg_len * 4);
                memcpy((void *)dst1, (void *)&local_dst[neg_len], pos_len * 4);
              }
            }
          } // idxDesc
        } // idxElm

      } // sym_ind
    } // ant_ind
  } // vv_inf
  if ((*frame & 0x7f) == 0 && *slot == 0 && xran_get_common_counters(gxran_handle, &x_counters[0]) == XRAN_STATUS_SUCCESS) {
    for (int o_xu_id = 0; o_xu_id < fh_init->xran_ports; o_xu_id++) {
      LOG_I(HW,
            "[%s%d][rx %7ld pps %7ld kbps %7ld][tx %7ld pps %7ld kbps %7ld][Total Msgs_Rcvd %ld]\n",
            "o-du ",
            o_xu_id,
            x_counters[o_xu_id].rx_counter,
            x_counters[o_xu_id].rx_counter - old_rx_counter[o_xu_id],
            x_counters[o_xu_id].rx_bytes_per_sec * 8 / 1000L,
            x_counters[o_xu_id].tx_counter,
            x_counters[o_xu_id].tx_counter - old_tx_counter[o_xu_id],
            x_counters[o_xu_id].tx_bytes_per_sec * 8 / 1000L,
            x_counters[o_xu_id].Total_msgs_rcvd);
      for (int rxant = 0; rxant < ru->nb_rx / fh_init->xran_ports; rxant++)
        LOG_I(HW,
              "[%s%d][pusch%d %7ld prach%d %7ld]\n",
              "o_du",
              o_xu_id,
              rxant,
              x_counters[o_xu_id].rx_pusch_packets[rxant],
              rxant,
              x_counters[o_xu_id].rx_prach_packets[rxant]);
      if (x_counters[o_xu_id].rx_counter > old_rx_counter[o_xu_id])
        old_rx_counter[o_xu_id] = x_counters[o_xu_id].rx_counter;
      if (x_counters[o_xu_id].tx_counter > old_tx_counter[o_xu_id])
        old_tx_counter[o_xu_id] = x_counters[o_xu_id].tx_counter;
    }
  }
  return (0);
}

/** @details Write PDSCH IQ-data from OAI txdataF_BF buffer to xran buffers. If
 * I/Q compression (bitwidth < 16 bits) is configured, compresses the data
 * before writing. */
int xran_fh_tx_send_slot(ru_info_t *ru, int frame, int slot, uint64_t timestamp)
{
  int tti = /*frame*SUBFRAMES_PER_SYSTEMFRAME*SLOTNUM_PER_SUBFRAME+*/ 20 * frame
            + slot; // commented out temporarily to check that compilation of oran 5g is working.

  void *ptr = NULL;
  int32_t *pos = NULL;
  int idx = 0;

  const struct xran_fh_init *fh_init = get_xran_fh_init();
  const struct xran_fh_config *fh_cfg = get_xran_fh_config(0);
  int fftsize = 1 << fh_cfg->nDLFftSize;
  int nb_tx_per_ru = ru->nb_tx / fh_init->xran_ports;
  int nb_rx_per_ru = ru->nb_rx / fh_init->xran_ports;

  // Handle CP UL packet here instead of at xran_fh_rx_read_slot() as oran_fh_if4p5_south_in() lags behind
  // oran_fh_if4p5_south_out() (which is invoked at the right time slot) by 4 slots.
  // Need to use --continuous-tx so that this routine will be triggered in RX slot.
  for (uint16_t cc_id = 0; cc_id < 1 /*nSectorNum*/; cc_id++) { // OAI does not support multiple CC yet.
    for (uint8_t ant_id = 0; ant_id < ru->nb_rx; ant_id++) {
      const struct xran_frame_config *frame_conf = &get_xran_fh_config(ant_id / nb_rx_per_ru)->frame_conf;
      // skip processing this slot is TX (no RX in this slot)
      if (!is_tdd_ul_guard_slot(frame_conf, slot)) {
        continue;
      }
      // This loop would better be more inner to avoid confusion and maybe also errors.
      for (int32_t sym_idx = 0; sym_idx < XRAN_NUM_OF_SYMBOL_PER_SLOT; sym_idx++) {
        /* skip DL and guard symbols. */
        if (!is_tdd_ul_symbol(frame_conf, slot, sym_idx)) {
          continue;
        }
        oran_buf_list_t *bufs = get_xran_buffers(ant_id / nb_rx_per_ru);
        uint8_t *pPrbMapData = bufs->dstcp[ant_id % nb_rx_per_ru][tti % XRAN_N_FE_BUF_LEN].pBuffers->pData;
        struct xran_prb_map *pPrbMap = (struct xran_prb_map *)pPrbMapData;

        LOG_D(HW, "pPrbMap->nPrbElm %d\n", pPrbMap->nPrbElm);
        for (uint32_t idxElm = 0; idxElm < pPrbMap->nPrbElm; idxElm++) {
          struct xran_prb_elm *pRbElm = &pPrbMap->prbMap[idxElm];
          int numRB, startRB;
#if defined F_RELEASE
          numRB = pRbElm->UP_nRBSize;
          startRB = pRbElm->UP_nRBStart;
          struct xran_section_desc *p_sec_desc = &pRbElm->sec_desc[sym_idx][0];
#endif
          LOG_D(HW, "pPrbMap[%d] : PRBstart %d nPRBs %d\n", idxElm, startRB, numRB);
          // For Liteon FR2 with RunSlotPrbMapBySymbolEnable xran_prb_map will have xran_prb_elm prbMap[14], each idxElm matches to sym_idx.
          if (fh_cfg->RunSlotPrbMapBySymbolEnable) {
            if (sym_idx >= pRbElm->nStartSymb && sym_idx < pRbElm->nStartSymb + pRbElm->numSymb) {
              if (!p_sec_desc->pCtrl)
                continue;
              // ant_id / no of antenna per beam gives the beam_nb
              pRbElm->nBeamIndex = ru->beam_id[ant_id / (ru->nb_rx / ru->num_beams_period)][slot * XRAN_NUM_OF_SYMBOL_PER_SLOT + sym_idx];
              // In phy-f-1.0/fhi_lib/lib/api/xran_pkt_cp.h, beamId:15 is of 15bit. -1 set extension bit ef:1 to 1 mistakenly.
              if (pRbElm->nBeamIndex == -1)
                pRbElm->nBeamIndex = 0;
            }
          } else {
            // ant_id / no of antenna per beam gives the beam_nb
            int16_t beam_id = ru->beam_id[ant_id / (ru->nb_tx / ru->num_beams_period)][slot * XRAN_NUM_OF_SYMBOL_PER_SLOT + sym_idx];
            if (beam_id != -1)
              pRbElm->nBeamIndex = beam_id;
          }
        }
      }
    }
  }

  for (uint16_t cc_id = 0; cc_id < 1 /*nSectorNum*/; cc_id++) { // OAI does not support multiple CC yet.
    for (uint8_t ant_id = 0; ant_id < ru->nb_tx; ant_id++) {
      oran_buf_list_t *bufs = get_xran_buffers(ant_id / nb_tx_per_ru);
      const struct xran_frame_config *frame_conf = &get_xran_fh_config(ant_id / nb_tx_per_ru)->frame_conf;
      // skip processing this slot is TX (no TX in this slot)
      if (!is_tdd_dl_guard_slot(frame_conf, slot)) {
        continue;
      }

      // For Liteon FR2 with RunSlotPrbMapBySymbolEnable. Set nPrbElm if beam_id = -1 for all downlink symbols
      if (fh_cfg->RunSlotPrbMapBySymbolEnable) {
        bool beam_used = false;
        uint8_t *pPrbMapData = bufs->srccp[ant_id % nb_tx_per_ru][tti % XRAN_N_FE_BUF_LEN].pBuffers->pData;
        struct xran_prb_map *pPrbMap = (struct xran_prb_map *)pPrbMapData;
        struct xran_prb_map *pRbMap = pPrbMap;
        int32_t dl_sym_end = 0;
        for (int32_t sym_idx = 0; sym_idx < XRAN_NUM_OF_SYMBOL_PER_SLOT; sym_idx++) {
          if (is_tdd_dl_symbol(frame_conf, slot, sym_idx)) {
            if (ru->beam_id[ant_id / (ru->nb_tx / ru->num_beams_period)][slot * XRAN_NUM_OF_SYMBOL_PER_SLOT+ sym_idx] != -1)
              beam_used |= true;
          }
          else {
              dl_sym_end = sym_idx;
              break;
          }
        }
        if (is_tdd_guard_slot(frame_conf, slot))
          pRbMap->nPrbElm = dl_sym_end;
        else
          pRbMap->nPrbElm = XRAN_NUM_OF_SYMBOL_PER_SLOT;
        if (!beam_used) {
          pRbMap->nPrbElm = 0;
          continue;
        }
      }

      // This loop would better be more inner to avoid confusion and maybe also errors.
      for (int32_t sym_idx = 0; sym_idx < XRAN_NUM_OF_SYMBOL_PER_SLOT; sym_idx++) {
        /* skip UL and guard symbols. */
        if (!is_tdd_dl_symbol(frame_conf, slot, sym_idx)) {
          continue;
        }
        uint8_t *pData =
            bufs->src[ant_id % nb_tx_per_ru][tti % XRAN_N_FE_BUF_LEN].pBuffers[sym_idx % XRAN_NUM_OF_SYMBOL_PER_SLOT].pData;
        uint8_t *pPrbMapData = bufs->srccp[ant_id % nb_tx_per_ru][tti % XRAN_N_FE_BUF_LEN].pBuffers->pData;
        struct xran_prb_map *pPrbMap = (struct xran_prb_map *)pPrbMapData;
        ptr = pData;
        pos = &ru->txdataF_BF[ant_id][sym_idx * fftsize];

        uint8_t *u8dptr;
        // even when the fragmentation occurs, nRBSize & nRBStart carry the same values in each prbMap
        // therefore, I took the liberty to just extract these values from the first prbMap
        struct xran_prb_elm *p_prbMapElm = &pPrbMap->prbMap[0];
        int num_totalRB = p_prbMapElm->nRBSize;
        int start_totalRB = p_prbMapElm->nRBStart;

        if (ptr && pos) {
          u8dptr = (uint8_t *)ptr;
          int16_t payload_len = 0;

          uint8_t *dst = (uint8_t *)u8dptr;

          for (uint32_t idxElm = 0; idxElm < pPrbMap->nPrbElm; idxElm++) {
            struct xran_section_desc *p_sec_desc = NULL;
            struct xran_prb_elm *p_prbMapElm = &pPrbMap->prbMap[idxElm];

            // radio-transport fragmentation is not supported in xran F release;
            // E-bit = 1 => each ethernet frame is considered as the last fragment;
            // a group of PRBs per each symbol is encapsulated in one ethernet frame.
            // => seems that the RUs don't check for E-bit
#if defined F_RELEASE
            p_sec_desc = &p_prbMapElm->sec_desc[sym_idx][0];
            int16_t startRB = p_prbMapElm->UP_nRBStart;
            int16_t numRB = p_prbMapElm->UP_nRBSize;
#endif

            if (p_sec_desc == NULL) {
              printf("p_sec_desc == NULL\n");
              exit(-1);
            }

            // For Liteon FR2 with RunSlotPrbMapBySymbolEnable xran_prb_map will have xran_prb_elm prbMap[14], each idxElm matches to sym_idx.
            if (fh_cfg->RunSlotPrbMapBySymbolEnable) {
              /* skip, if not scheduled */
              if(sym_idx < p_prbMapElm->nStartSymb || sym_idx >= p_prbMapElm->nStartSymb + p_prbMapElm->numSymb){
                  p_sec_desc->iq_buffer_offset = 0;
                  p_sec_desc->iq_buffer_len    = 0;
                  continue;
              }
              // ant_id / no of antenna per beam gives the beam_nb
              p_prbMapElm->nBeamIndex = ru->beam_id[ant_id / (ru->nb_tx / ru->num_beams_period)][slot * XRAN_NUM_OF_SYMBOL_PER_SLOT+ sym_idx];
              // In phy-f-1.0/fhi_lib/lib/api/xran_pkt_cp.h, beamId:15 is of 15bit. -1 set extension bit ef:1 to 1 mistakenly.
              if (p_prbMapElm->nBeamIndex == -1)
                p_prbMapElm->nBeamIndex = 0;
            } else {
              // ant_id / no of antenna per beam gives the beam_nb
              int16_t beam_id = ru->beam_id[ant_id / (ru->nb_tx / ru->num_beams_period)][slot * XRAN_NUM_OF_SYMBOL_PER_SLOT + sym_idx];
              if ( beam_id != -1)
                p_prbMapElm->nBeamIndex = beam_id;
            }

            dst = xran_add_hdr_offset(dst, p_prbMapElm->compMethod);

            uint16_t *dst16 = (uint16_t *)dst;

            // Start of this section
            int32_t *pos_start = pos + (start_totalRB + startRB) * N_SC_PER_PRB;

            if (p_prbMapElm->compMethod == XRAN_COMPMETHOD_NONE) {
              payload_len = numRB * N_SC_PER_PRB * 4L;
              /* convert to Network order */
              // NOTE: ggc 11 knows how to generate AVX2 for this!
              for (idx = 0; idx < (numRB * N_SC_PER_PRB) * 2; idx++)
                ((uint16_t *)dst16)[idx] = htons(((uint16_t *)pos_start)[idx]);
            } else if (p_prbMapElm->compMethod == XRAN_COMPMETHOD_BLKFLOAT) {
              payload_len = (3 * p_prbMapElm->iqWidth + 1) * numRB;

              /* Although arm intrinsics natively handle unaligned memory
              access, we use a 64 byte aligned input here for maximum
              performance. So the src_compr buffer is used for both x86 and arm.
              */
              uint32_t src_compr[num_totalRB * N_SC_PER_PRB] __attribute__((aligned(64)));

              /* Copy from txdataF with current symbol's PRB start (nRBStart) +
              current section's PRB start (UP_nPRBStart) */
              memcpy(src_compr, pos_start, (numRB * N_SC_PER_PRB) * sizeof(*pos_start));

#if defined(__i386__) || defined(__x86_64__)
              struct xranlib_compress_request bfp_com_req = {};
              struct xranlib_compress_response bfp_com_rsp = {};

              bfp_com_req.data_in = (int16_t *)src_compr;

              bfp_com_req.numRBs = numRB;
              bfp_com_req.len = payload_len;
              bfp_com_req.compMethod = p_prbMapElm->compMethod;
              bfp_com_req.iqWidth = p_prbMapElm->iqWidth;

              bfp_com_rsp.data_out = (int8_t *)dst;
              bfp_com_rsp.len = 0;

              xranlib_compress_avx512(&bfp_com_req, &bfp_com_rsp);
#elif defined(__arm__) || defined(__aarch64__)
              armral_bfp_compression(p_prbMapElm->iqWidth, numRB, (int16_t *)src_compr, (int8_t *)dst);
#else
              AssertFatal(1 == 0, "BFP compression not supported on this architecture");
#endif
            } else {
              printf("p_prbMapElm->compMethod == %d is not supported\n", p_prbMapElm->compMethod);
              exit(-1);
            }

            p_sec_desc->iq_buffer_offset = RTE_PTR_DIFF(dst, u8dptr);
            p_sec_desc->iq_buffer_len = payload_len;

            dst += payload_len;
            dst = xran_add_hdr_offset(dst, p_prbMapElm->compMethod);
          }

          // The tti should be updated as it increased.
          pPrbMap->tti_id = tti;

        } else {
          printf("ptr ==NULL\n");
          exit(-1); // fails here??
        }
      }
    }
  }
  return (0);
}
