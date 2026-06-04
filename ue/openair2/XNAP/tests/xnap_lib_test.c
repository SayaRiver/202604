/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*
 * \brief Test functions for XnAP encoding/decoding library
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <arpa/inet.h>
#include "common/utils/assertions.h"
#include "common/utils/utils.h"
#include "xnap_messages_types.h"
#include "openair2/XNAP/lib/xnap_lib_common.h"
#include "openair2/XNAP/lib/xnap_gNB_interface_management.h"

void exit_function(const char *file, const char *function, const int line, const char *s, const int assert)
{
  printf("detected error at %s:%d:%s: %s\n", file, line, function, s);
  abort();
}

/* Helper for ASN.1 Encoding/Decoding loop */
static XNAP_XnAP_PDU_t *xnap_encode_decode(const XNAP_XnAP_PDU_t *enc_pdu)
{
  //xer_fprint(stdout, &asn_DEF_XNAP_XnAP_PDU, enc_pdu);
  DevAssert(enc_pdu != NULL);
  char errbuf[1024];
  size_t errlen = sizeof(errbuf);
  int ret = asn_check_constraints(&asn_DEF_XNAP_XnAP_PDU, enc_pdu, errbuf, &errlen);
  AssertFatal(ret == 0, "asn_check_constraints() failed: %s\n", errbuf);

  uint8_t msgbuf[16384];
  // 1. Encode
  asn_enc_rval_t enc = aper_encode_to_buffer(&asn_DEF_XNAP_XnAP_PDU, NULL, enc_pdu, msgbuf, sizeof(msgbuf));
  AssertFatal(enc.encoded > 0, "aper_encode_to_buffer() failed\n");

  // 2. Decode
  XNAP_XnAP_PDU_t *dec_pdu = NULL;
  asn_codec_ctx_t st = {.max_stack_size = 100 * 1000};
  asn_dec_rval_t dec = aper_decode(&st, &asn_DEF_XNAP_XnAP_PDU, (void **)&dec_pdu, msgbuf, enc.encoded, 0, 0);
  AssertFatal(dec.code == RC_OK, "aper_decode() failed\n");

  //xer_fprint(stdout, &asn_DEF_XNAP_XnAP_PDU, dec_pdu);

  return dec_pdu;
}

static void xnap_msg_free(XNAP_XnAP_PDU_t *pdu)
{
  ASN_STRUCT_FREE(asn_DEF_XNAP_XnAP_PDU, pdu);
}

/**
 * 1. Xn Setup Request
 */
static void test_xn_setup_request(void)
{
  /* ---------- Common PLMNs ---------- */
  plmn_id_t plmn0 = { .mcc = 208, .mnc = 95, .mnc_digit_length = 2 };
  plmn_id_t plmn1 = { .mcc = 208, .mnc = 93, .mnc_digit_length = 2 };
  
  /* ---------- TAI support (2) ---------- */
  xnap_tai_support_t *tai_support = calloc_or_fail(2, sizeof(*tai_support));
  
  /* ================= TAI 0 ================= */
  tai_support[0].tac = 1;
  tai_support[0].num_plmn = 2;
  tai_support[0].plmn_support = calloc_or_fail(2, sizeof(*tai_support[0].plmn_support));
  
  /* ---- TAI 0 / PLMN 0 ---- */
  tai_support[0].plmn_support[0].plmn = plmn0;
  tai_support[0].plmn_support[0].num_nssai = 3;
  tai_support[0].plmn_support[0].nssai = calloc_or_fail(3, sizeof(nssai_t));
  
  tai_support[0].plmn_support[0].nssai[0] = (nssai_t){ .sst = 1, .sd = 0x010203 };
  tai_support[0].plmn_support[0].nssai[1] = (nssai_t){ .sst = 1, .sd = 0x010204 };
  tai_support[0].plmn_support[0].nssai[2] = (nssai_t){ .sst = 1, .sd = 0x010205 };
  
  /* ---- TAI 0 / PLMN 1 ---- */
  tai_support[0].plmn_support[1].plmn = plmn1;
  tai_support[0].plmn_support[1].num_nssai = 3;
  tai_support[0].plmn_support[1].nssai = calloc_or_fail(3, sizeof(nssai_t));
  
  tai_support[0].plmn_support[1].nssai[0] = (nssai_t){ .sst = 2, .sd = 0x020203 };
  tai_support[0].plmn_support[1].nssai[1] = (nssai_t){ .sst = 2, .sd = 0x020204 };
  tai_support[0].plmn_support[1].nssai[2] = (nssai_t){ .sst = 2, .sd = 0x020205 };
  
  /* ================= TAI 1 ================= */
  tai_support[1].tac = 2;
  tai_support[1].num_plmn = 2;
  tai_support[1].plmn_support = calloc_or_fail(2, sizeof(*tai_support[1].plmn_support));
  
  /* ---- TAI 1 / PLMN 0 ---- */
  tai_support[1].plmn_support[0].plmn = plmn0;
  tai_support[1].plmn_support[0].num_nssai = 3;
  tai_support[1].plmn_support[0].nssai = calloc_or_fail(3, sizeof(nssai_t));
  
  tai_support[1].plmn_support[0].nssai[0] = (nssai_t){ .sst = 3, .sd = 0x030203 };
  tai_support[1].plmn_support[0].nssai[1] = (nssai_t){ .sst = 3, .sd = 0x030204 };
  tai_support[1].plmn_support[0].nssai[2] = (nssai_t){ .sst = 3, .sd = 0x030205 };
  
  /* ---- TAI 1 / PLMN 1 ---- */
  tai_support[1].plmn_support[1].plmn = plmn1;
  tai_support[1].plmn_support[1].num_nssai = 3;
  tai_support[1].plmn_support[1].nssai = calloc_or_fail(3, sizeof(nssai_t));
  
  tai_support[1].plmn_support[1].nssai[0] = (nssai_t){ .sst = 4, .sd = 0x040203 };
  tai_support[1].plmn_support[1].nssai[1] = (nssai_t){ .sst = 4, .sd = 0x040204 };
  tai_support[1].plmn_support[1].nssai[2] = (nssai_t){ .sst = 4, .sd = 0x040205 };
  
  /* ---------- AMF region info (2) ---------- */
  xnap_amf_region_info_t *amf_region_info = calloc_or_fail(2, sizeof(*amf_region_info));
  
  amf_region_info[0].plmn = plmn0;
  amf_region_info[0].amf_region_id = 1;
  
  amf_region_info[1].plmn = plmn1;
  amf_region_info[1].amf_region_id = 2;
  
  /* ---------- create message ---------- */
  xnap_setup_req_t orig = {
      .gNB_id = 0xABCDE,
      .plmn = plmn0,
      .num_tai = 2,
      .tai_support = tai_support,
      .num_amf_regions = 2,
      .amf_region_info = amf_region_info,
  };
  /* ---------- encode ---------- */
  XNAP_XnAP_PDU_t *xnenc = encode_xn_setup_request(&orig);
  XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
  xnap_msg_free(xnenc);

  /* ---------- decode ---------- */
  xnap_setup_req_t decoded = {0};
  bool ret = decode_xn_setup_request(&decoded, xndec);
  AssertFatal(ret, "decode_xn_setup_request failed");
  xnap_msg_free(xndec);

  /* ---------- equality ---------- */
  ret = eq_xnap_setup_request(&orig, &decoded);
  AssertFatal(ret, "Xn Setup Req mismatch\n");
  free_xnap_setup_request(&decoded);
  free_xnap_setup_request(&orig);
  printf("%s() successful \n", __func__);
}

/**
 * 2. Xn Setup Response
 */
static void test_xn_setup_response(void)
{
  /* ---------- Common PLMNs ---------- */
  plmn_id_t plmn0 = { .mcc = 208, .mnc = 95, .mnc_digit_length = 2 };
  plmn_id_t plmn1 = { .mcc = 208, .mnc = 93, .mnc_digit_length = 2 };
  
  /* ---------- TAI support (2) ---------- */
  xnap_tai_support_t *tai_support = calloc_or_fail(2, sizeof(*tai_support));
  
  /* ================= TAI 0 ================= */
  tai_support[0].tac = 1;
  tai_support[0].num_plmn = 2;
  tai_support[0].plmn_support = calloc_or_fail(2, sizeof(*tai_support[0].plmn_support));
  
  /* ---- TAI 0 / PLMN 0 ---- */
  tai_support[0].plmn_support[0].plmn = plmn0;
  tai_support[0].plmn_support[0].num_nssai = 3;
  tai_support[0].plmn_support[0].nssai = calloc_or_fail(3, sizeof(nssai_t));
  
  tai_support[0].plmn_support[0].nssai[0] = (nssai_t){ .sst = 1, .sd = 0x010203 };
  tai_support[0].plmn_support[0].nssai[1] = (nssai_t){ .sst = 1, .sd = 0x010204 };
  tai_support[0].plmn_support[0].nssai[2] = (nssai_t){ .sst = 1, .sd = 0x010205 };
  
  /* ---- TAI 0 / PLMN 1 ---- */
  tai_support[0].plmn_support[1].plmn = plmn1;
  tai_support[0].plmn_support[1].num_nssai = 3;
  tai_support[0].plmn_support[1].nssai = calloc_or_fail(3, sizeof(nssai_t));
  
  tai_support[0].plmn_support[1].nssai[0] = (nssai_t){ .sst = 2, .sd = 0x020203 };
  tai_support[0].plmn_support[1].nssai[1] = (nssai_t){ .sst = 2, .sd = 0x020204 };
  tai_support[0].plmn_support[1].nssai[2] = (nssai_t){ .sst = 2, .sd = 0x020205 };
  
  /* ================= TAI 1 ================= */
  tai_support[1].tac = 2;
  tai_support[1].num_plmn = 2;
  tai_support[1].plmn_support = calloc_or_fail(2, sizeof(*tai_support[1].plmn_support));
  
  /* ---- TAI 1 / PLMN 0 ---- */
  tai_support[1].plmn_support[0].plmn = plmn0;
  tai_support[1].plmn_support[0].num_nssai = 3;
  tai_support[1].plmn_support[0].nssai = calloc_or_fail(3, sizeof(nssai_t));
  
  tai_support[1].plmn_support[0].nssai[0] = (nssai_t){ .sst = 3, .sd = 0x030203 };
  tai_support[1].plmn_support[0].nssai[1] = (nssai_t){ .sst = 3, .sd = 0x030204 };
  tai_support[1].plmn_support[0].nssai[2] = (nssai_t){ .sst = 3, .sd = 0x030205 };
  
  /* ---- TAI 1 / PLMN 1 ---- */
  tai_support[1].plmn_support[1].plmn = plmn1;
  tai_support[1].plmn_support[1].num_nssai = 3;
  tai_support[1].plmn_support[1].nssai = calloc_or_fail(3, sizeof(nssai_t));
  
  tai_support[1].plmn_support[1].nssai[0] = (nssai_t){ .sst = 4, .sd = 0x040203 };
  tai_support[1].plmn_support[1].nssai[1] = (nssai_t){ .sst = 4, .sd = 0x040204 };
  tai_support[1].plmn_support[1].nssai[2] = (nssai_t){ .sst = 4, .sd = 0x040205 };
  
  /* ---------- create message ---------- */
  xnap_setup_resp_t orig = {
      .gNB_id = 0xABCDE,
      .plmn = plmn0,
      .num_tai = 2,
      .tai_support = tai_support,
  };
  /* ---------- encode ---------- */
  XNAP_XnAP_PDU_t *xnenc = encode_xn_setup_response(&orig);
  XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
  xnap_msg_free(xnenc);

  /* ---------- decode ---------- */
  xnap_setup_resp_t decoded = {0};
  bool ret = decode_xn_setup_response(&decoded, xndec);
  AssertFatal(ret, "decode_xn_setup_response failed");
  xnap_msg_free(xndec);

  /* ---------- equality ---------- */
  ret = eq_xnap_setup_response(&orig, &decoded);
  AssertFatal(ret, "Xn Setup Response mismatch\n");
  free_xnap_setup_response(&decoded);
  free_xnap_setup_response(&orig);
  printf("%s() successful \n", __func__);
}

/**
 * 3. Xn Setup Failure
 */
static void test_xn_setup_failure(void)
{
  /* ---------- create message ---------- */
  xnap_setup_failure_t orig = {
    .cause.type = XNAP_CAUSE_TRANSPORT,
    .cause.value = XNAP_CAUSE_TRANSPORT_LAYER_TRANSPORT_RESOURCE_UNAVAILABLE,
  };

  /* ---------- encode ---------- */
  XNAP_XnAP_PDU_t *xnenc = encode_xn_setup_failure(&orig);
  XNAP_XnAP_PDU_t *xndec = xnap_encode_decode(xnenc);
  xnap_msg_free(xnenc);

  /* ---------- decode ---------- */
  xnap_setup_failure_t decoded = {0};
  bool ret = decode_xn_setup_failure(&decoded, xndec);
  AssertFatal(ret, "decode_xn_setup_failure failed");
  xnap_msg_free(xndec);

  /* ---------- equality ---------- */
  ret = eq_xnap_setup_failure(&orig, &decoded);
  AssertFatal(ret, "Xn Setup Failure mismatch\n");
  free_xnap_setup_failure(&decoded);
  free_xnap_setup_failure(&orig);
  printf("%s() successful \n", __func__);
}

int main() {
  printf("Starting XnAP Library Unit Tests...\n");

  /* Xn Interface Testing */
  test_xn_setup_request();
  test_xn_setup_response();
  test_xn_setup_failure();

  printf("All XnAP tests passed!\n");
  return 0;
}
