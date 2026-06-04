/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief log tools used by data recording application (to be merged to existing OAI log tools)
 */

#ifndef __PHY_LOG_TOOLS_H__
#define __PHY_LOG_TOOLS_H__

#include <stdint.h>
#include "PHY/TOOLS/tools_defs.h"

char* get_time_stamp_usec(char time_stamp_str[]);
int convert_time_stamp_to_int(const char* timestamp);
int split_time_stamp_and_convert_to_int(char time_stamp_str[], int shift, int length);

#endif /*__PHY_LOG_TOOLS_H__ */
