/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "PHY/CODING/nrSmallBlock/nr_small_block_defs.h"
#include "common/utils/assertions.h"

//input = [0 ... 0 c_K-1 ... c_2 c_1 c_0]
//output = [d_31 d_30 ... d_2 d_1 d_0]
uint32_t encodeSmallBlock(int in, int len)
{
  AssertFatal(len >= 3, "encodeSmallBlock only supports input lengths A >= 3, got A=%d", len);

  uint32_t out = 0;
  for (int i = 0; i < len; i++)
    if ((in & (1 << i)) > 0)
      out ^= nrSmallBlockBasis[i];

  return out;
}
