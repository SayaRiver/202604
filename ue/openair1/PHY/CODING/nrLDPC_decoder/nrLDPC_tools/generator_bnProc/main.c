/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdio.h>
#include <stdint.h>
#define NB_R  3
void nrLDPC_bnProc_BG1_generator_AVX2(const char*, int);
void nrLDPC_bnProc_BG2_generator_AVX2(const char*, int);
void nrLDPC_bnProcPc_BG1_generator_AVX2(const char*, int);
void nrLDPC_bnProcPc_BG2_generator_AVX2(const char*, int);

const char *__asan_default_options()
{
  /* don't do leak checking in nr_ulsim, creates problems in the CI */
  return "detect_leaks=0";
}

int main(int argc, char *argv[])
{
  if (argc != 2) {
    fprintf(stderr, "usage: %s <output-dir>\n", argv[0]);
    return 1;
  }
  const char *dir = argv[1];

  int R[NB_R]={0,1,2};
  for(int i=0; i<NB_R;i++){
    nrLDPC_bnProc_BG1_generator_AVX2(dir, R[i]);
    nrLDPC_bnProc_BG2_generator_AVX2(dir, R[i]);

    nrLDPC_bnProcPc_BG1_generator_AVX2(dir, R[i]);
    nrLDPC_bnProcPc_BG2_generator_AVX2(dir, R[i]);
  }

  return(0);
}

