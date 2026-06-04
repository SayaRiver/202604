/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief ngap trace procedures for gNB
 */

#include "ngap_gNB_trace.h"
#include <stdint.h>

int ngap_gNB_handle_trace_start(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *pdu)
{
    //TODO
    return 0;
}

int ngap_gNB_handle_deactivate_trace(sctp_assoc_t assoc_id, uint32_t stream, NGAP_NGAP_PDU_t *message_p)
{
    //     NGAP_DeactivateTraceIEs_t *deactivate_trace_p;
    //
    //     deactivate_trace_p = &message_p->msg.deactivateTraceIEs;

    return 0;
}
