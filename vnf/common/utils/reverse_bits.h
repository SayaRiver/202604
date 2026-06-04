/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef REVERSE_BITS_H_
#define REVERSE_BITS_H_

#include <stddef.h>
#include <stdint.h>

uint64_t reverse_bits(uint64_t in, int n_bits);
void reverse_bits_u8(uint8_t const* in, size_t sz, uint8_t* out);

#endif /* REVERSE_BITS_H_ */
