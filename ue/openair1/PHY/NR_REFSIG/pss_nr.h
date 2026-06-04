/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/**********************************************************************
*
* FILENAME    :  pss_nr.h
*
* MODULE      :  primary synchronisation signal
*
* DESCRIPTION :  elements related to pss
*
************************************************************************/

#ifndef PSS_NR_H
#define PSS_NR_H

#include "PHY/defs_nr_common.h"
#include "PHY/NR_REFSIG/ss_pbch_nr.h"

/************** CODE GENERATION ***********************************/

//#define PSS_DECIMATOR                          /* decimation of sample is done between time correlation */

//#define CIC_DECIMATOR                          /* it allows enabling decimation based on CIC filter. By default, decimation is based on a FIF filter */

#define TEST_SYNCHRO_TIMING_PSS        (1)       /* enable time profiling */

//#define DBG_PSS_NR

/************** DEFINE ********************************************/

/* PROFILING */
#define TIME_PSS                      (0)
#define TIME_RATE_CHANGE              (TIME_PSS+1)
#define TIME_SSS                      (TIME_RATE_CHANGE+1)
#define TIME_LAST                     (TIME_SSS+1)

/* PSS configuration */

#define SYNCHRO_FFT_SIZE_MAX           (8192)                       /* maximum size of fft for synchronisation */

#define  NO_RATE_CHANGE                (1)

#ifdef PSS_DECIMATOR
  #define  RATE_CHANGE                 (SYNCHRO_FFT_SIZE_MAX/SYNCHRO_FFT_SIZE_PSS)
  #define  SYNCHRO_FFT_SIZE_PSS        (256)
  #define  OFDM_SYMBOL_SIZE_PSS        (SYNCHRO_FFT_SIZE_PSS)
  #define  SYNCHRO_RATE_CHANGE_FACTOR  (SYNCHRO_FFT_SIZE_MAX/SYNCHRO_FFT_SIZE_PSS)
  #define  CIC_FILTER_STAGE_NUMBER     (4)
#else
  #define  RATE_CHANGE                 (1)
  #define  SYNCHRO_RATE_CHANGE_FACTOR  (1)
#endif

void init_context_synchro_nr(NR_DL_FRAME_PARMS *frame_parms_ue);
int pss_synchro_nr(const c16_t **rxdata,
                   const NR_DL_FRAME_PARMS *frame_parms,
                   const c16_t pssTime[NUMBER_PSS_SEQUENCE][frame_parms->ofdm_symbol_size],
                   int is,
                   bool fo_flag,
                   int target_Nid_cell,
                   int *nid2,
                   int *f_off,
                   int *pssPeak,
                   int *pssAvg);
int pss_search_time_nr(const c16_t **rxdata,
                       const NR_DL_FRAME_PARMS *frame_parms,
                       const c16_t pssTime[NUMBER_PSS_SEQUENCE][frame_parms->ofdm_symbol_size],
                       bool fo_flag,
                       int is,
                       int target_Nid_cell,
                       int *nid2,
                       int *f_off,
                       int *pssPeak,
                       int *pssAvg,
                       int search_start,
                       int search_length);
void generate_pss_nr_time(const NR_DL_FRAME_PARMS *fp, const int N_ID_2, int ssbFirstSCS, c16_t pssTime[fp->ofdm_symbol_size]);
int16_t *get_primary_synchro_nr2(const int nid2);

#endif /* PSS_NR_H */


