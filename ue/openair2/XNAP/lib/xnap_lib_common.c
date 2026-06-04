/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "xnap_lib_common.h"
#include "common/utils/assertions.h"
#include "common/utils/utils.h"

int xnap_gNB_set_cause(XNAP_Cause_t *cause_p,const xnap_cause_t *in)
{
  DevAssert(cause_p != NULL);
  switch (in->type) {
    case XNAP_CAUSE_RADIO_NETWORK:
      cause_p->present = XNAP_Cause_PR_radioNetwork;
      cause_p->choice.radioNetwork = in->value;
      break;
    case XNAP_CAUSE_TRANSPORT:
      cause_p->present = XNAP_Cause_PR_transport;
      cause_p->choice.transport = in->value;
      break;
    case XNAP_CAUSE_PROTOCOL:
      cause_p->present = XNAP_Cause_PR_protocol;
      cause_p->choice.protocol = in->value;
      break;
    case XNAP_CAUSE_MISC:
      cause_p->present = XNAP_Cause_PR_misc;
      cause_p->choice.misc = in->value;
      break;
    case XNAP_CAUSE_NOTHING:
    default:
      AssertFatal(false, "unknown cause value %d\n", in->type);
      break;
  }
  return 0;
}

xnap_cause_t decode_xnap_cause(const XNAP_Cause_t *in)
{
  xnap_cause_t out = {0};
  switch (in->present) {
     case XNAP_Cause_PR_radioNetwork:
         out.type = XNAP_CAUSE_RADIO_NETWORK;
         out.value = in->choice.radioNetwork;
         break;

     case XNAP_Cause_PR_transport:
         out.type = XNAP_CAUSE_TRANSPORT;
         out.value = in->choice.transport;
         break;

     case XNAP_Cause_PR_protocol:
         out.type = XNAP_CAUSE_PROTOCOL;
         out.value = in->choice.protocol;
         break;

     case XNAP_Cause_PR_misc:
         out.type = XNAP_CAUSE_MISC;
         out.value = in->choice.misc;
         break;
     case XNAP_Cause_PR_NOTHING:
     default:
         PRINT_ERROR("received illegal XNAP cause %d\n", in->present);
         break;
   }
   return out;
}

bool eq_xnap_cause(const xnap_cause_t *a, const xnap_cause_t *b)
{
  _EQ_CHECK_INT(a->type,  b->type);
  _EQ_CHECK_INT(a->value, b->value);
  return true;
}

bool eq_xnap_plmn(const plmn_id_t *a, const plmn_id_t *b)
{
  _EQ_CHECK_INT(a->mcc, b->mcc);
  _EQ_CHECK_INT(a->mnc, b->mnc);
  _EQ_CHECK_INT(a->mnc_digit_length, b->mnc_digit_length);
  return true;
}

bool eq_xnap_snssai(const nssai_t *a, const nssai_t *b)
{
  _EQ_CHECK_INT(a->sst, b->sst);
  _EQ_CHECK_UINT32(a->sd, b->sd);
  return true;
}

bool eq_xnap_plmn_support(const xnap_plmn_support_t *a, const xnap_plmn_support_t *b)
{
  if (!eq_xnap_plmn(&a->plmn, &b->plmn)) return false;
  _EQ_CHECK_INT(a->num_nssai, b->num_nssai);
  for (int i = 0; i < a->num_nssai; i++) {
    if (!eq_xnap_snssai(&a->nssai[i], &b->nssai[i]))
      return false;
  }
  return true;
}

bool eq_xnap_tai_support(const xnap_tai_support_t *a, const xnap_tai_support_t *b)
{
  _EQ_CHECK_UINT32(a->tac, b->tac);
  _EQ_CHECK_INT(a->num_plmn, b->num_plmn);
  for (int i = 0; i < a->num_plmn; i++) {
    if (!eq_xnap_plmn_support(&a->plmn_support[i], &b->plmn_support[i]))
      return false;
  }
  return true;
}
