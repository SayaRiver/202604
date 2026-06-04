/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief ngap trace procedures for gNB
 */

#ifndef NGAP_GNB_TRACE_H_
#define NGAP_GNB_TRACE_H_

#include <netinet/in.h>
#include <netinet/sctp.h>
#include <stdint.h>
#include "NGAP_NGAP-PDU.h"

int ngap_gNB_handle_trace_start(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu);

int ngap_gNB_handle_deactivate_trace(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu);

#endif /* NGAP_GNB_TRACE_H_ */
