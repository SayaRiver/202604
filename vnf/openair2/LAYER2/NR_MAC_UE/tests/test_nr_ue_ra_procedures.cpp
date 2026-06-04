/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "gtest/gtest.h"
extern "C" {
#include "openair2/LAYER2/NR_MAC_UE/mac_proto.h"
#include "executables/softmodem-common.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_oai_api.h"
#include "common/utils/ocp_itti/intertask_interface.h"

static softmodem_params_t softmodem_params;
softmodem_params_t *get_softmodem_params(void)
{
  return &softmodem_params;
}
void nr_mac_rrc_ra_ind(const module_id_t mod_id, bool success)
{
}
void nr_mac_rrc_msg3_ind(const module_id_t mod_id, const int rnti, bool prepare_payload)
{
}
void nr_mac_rlc_status_ind(uint16_t ue_id, frame_t frame, int n_ch, const logical_chan_id_t *ch, mac_rlc_status_resp_t *ret)
{
}
void nr_mac_rrc_inactivity_timer_ind(const module_id_t mod_id)
{
}
tbs_size_t nr_mac_rlc_data_req(const module_id_t module_idP,
                               const uint16_t ue_id,
                               const bool gnb_flagP,
                               const logical_chan_id_t channel_idP,
                               const tb_size_t tb_sizeP,
                               char *buffer_pP)
{
  return 0;
}
void nr_mac_rlc_data_ind(const module_id_t module_idP,
                         const uint16_t ue_id,
                         const bool gnb_flagP,
                         const nr_rlc_data_ind_t *data,
                         int num_data)
{
}
void nr_mac_rrc_verification_failed(const module_id_t mod_id)
{
}
bool nr_rlc_activate_srb0(int ue_id,
                          void *data,
                          void (*send_initial_ul_rrc_message)(int ue_id, const uint8_t *sdu, sdu_size_t sdu_len, void *data))
{
  return true;
}
int nr_rlc_module_init(nr_rlc_op_mode_t mode)
{
  return 0;
}
MessageDef *itti_alloc_new_message(task_id_t origin_task_id, instance_t originInstance, MessagesIds message_id)
{
  return NULL;
}
int itti_send_msg_to_task(task_id_t task_id, instance_t instance, MessageDef *message)
{
  return 0;
}
typedef uint32_t channel_t;
int8_t nr_mac_rrc_data_ind_ue(const module_id_t module_id,
                              const int CC_id,
                              const uint8_t gNB_index,
                              const int hfn,
                              const frame_t frame,
                              const int slot,
                              const rnti_t rnti,
                              const uint32_t cellid,
                              const long arfcn,
                              const channel_t channel,
                              const uint8_t *pduP,
                              const sdu_size_t pdu_len)
{
  return 0;
}
bool check_csi_report_consistency(const NR_CSI_MeasConfig_t *meas)
{
  return true;
}
void nr_mac_rrc_meas_ind_ue(module_id_t module_id,
                            uint32_t gNB_index,
                            uint16_t Nid_cell,
                            bool csi_meas,
                            bool is_neighboring_cell,
                            int rsrp_dBm)
{
}
}
#include <cstdio>
#include "common/utils/LOG/log.h"

TEST(test_init_ra, four_step_cbra)
{
  NR_UE_MAC_INST_t mac = {0};
  RA_config_t *ra = &mac.ra;
  NR_RACH_ConfigCommon_t nr_rach_ConfigCommon = {0};
  NR_RACH_ConfigGeneric_t rach_ConfigGeneric = {0};
  NR_RACH_ConfigDedicated_t rach_ConfigDedicated = {0};
  NR_UE_UL_BWP_t current_bwp;
  NR_UE_DL_BWP_t dl_bwp;
  mac.current_UL_BWP = &current_bwp;
  mac.current_DL_BWP = &dl_bwp;
  mac.mib_ssb = 0;
  long scs = 1;
  current_bwp.scs = scs;
  current_bwp.bwp_id = 0;
  dl_bwp.bwp_id = 0;
  current_bwp.channel_bandwidth = 40;
  nr_rach_ConfigCommon.msg1_SubcarrierSpacing = &scs;
  nr_rach_ConfigCommon.rach_ConfigGeneric = rach_ConfigGeneric;
  current_bwp.rach_ConfigCommon = &nr_rach_ConfigCommon;
  ra->rach_ConfigDedicated = &rach_ConfigDedicated;
  mac.p_Max = 23;
  mac.nr_band = 78;
  mac.frame_structure.frame_type = TDD;
  mac.frame_structure.numb_slots_frame = 20;
  mac.frequency_range = FR1;

  init_RA(&mac);

  EXPECT_EQ(mac.ra.ra_type, RA_4_STEP);
  EXPECT_EQ(mac.state, UE_PERFORMING_RA);
  EXPECT_EQ(mac.ra.RA_active, true);
  EXPECT_EQ(mac.ra.cfra, 0);
}

TEST(test_init_ra, four_step_cfra)
{
  NR_UE_MAC_INST_t mac = {0};
  RA_config_t *ra = &mac.ra;
  NR_RACH_ConfigCommon_t nr_rach_ConfigCommon = {0};
  NR_RACH_ConfigGeneric_t rach_ConfigGeneric = {0};
  NR_UE_UL_BWP_t current_bwp;
  NR_UE_DL_BWP_t dl_bwp;
  mac.current_UL_BWP = &current_bwp;
  mac.current_DL_BWP = &dl_bwp;
  mac.mib_ssb = 0;
  long scs = 1;
  current_bwp.scs = scs;
  current_bwp.bwp_id = 0;
  dl_bwp.bwp_id = 0;
  current_bwp.channel_bandwidth = 40;
  nr_rach_ConfigCommon.msg1_SubcarrierSpacing = &scs;
  nr_rach_ConfigCommon.rach_ConfigGeneric = rach_ConfigGeneric;
  current_bwp.rach_ConfigCommon = &nr_rach_ConfigCommon;
  mac.p_Max = 23;
  mac.nr_band = 78;
  mac.frame_structure.frame_type = TDD;
  mac.frame_structure.numb_slots_frame = 20;
  mac.frequency_range = FR1;

  NR_RACH_ConfigDedicated_t rach_ConfigDedicated = {0};
  NR_CFRA_t cfra;
  rach_ConfigDedicated.cfra = &cfra;
  ra->rach_ConfigDedicated = &rach_ConfigDedicated;

  init_RA(&mac);

  EXPECT_EQ(mac.ra.ra_type, RA_4_STEP);
  EXPECT_EQ(mac.state, UE_PERFORMING_RA);
  EXPECT_EQ(mac.ra.RA_active, true);
  EXPECT_EQ(mac.ra.cfra, 1);
}

int main(int argc, char **argv)
{
  logInit();
  load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY);
  g_log->log_component[MAC].level = OAILOG_DEBUG;
  g_log->log_component[NR_MAC].level = OAILOG_DEBUG;
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
