/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "config-mplane.h"
#include "rpc-send-recv.h"
#include "common/utils/assertions.h"

#include <libyang/libyang.h>
#include <nc_client.h>

static bool edit_config_mplane(ru_session_t *ru_session, const char *content)
{
  int timeout = CLI_RPC_REPLY_TIMEOUT;
  struct nc_rpc *rpc;
  NC_WD_MODE wd = NC_WD_ALL;
  NC_PARAMTYPE param = NC_PARAMTYPE_CONST;
  NC_DATASTORE target = NC_DATASTORE_CANDIDATE;
  NC_RPC_EDIT_DFLTOP op = NC_RPC_EDIT_DFLTOP_MERGE;
  NC_RPC_EDIT_TESTOPT test = NC_RPC_EDIT_TESTOPT_UNKNOWN;
  NC_RPC_EDIT_ERROPT err = NC_RPC_EDIT_ERROPT_UNKNOWN;

  MP_LOG_I("RPC request to RU \"%s\" = <edit-config>:\n%s\n", ru_session->ru_ip_add, content);
  rpc = nc_rpc_edit(target, op, test, err, content, param);
  AssertError(rpc != NULL, return false, "[MPLANE] <edit-config> RPC creation failed.\n");

  bool success = rpc_send_recv((struct nc_session *)ru_session->session, rpc, wd, timeout, NULL);
  AssertError(success, return false, "[MPLANE] Failed to edit configuration for the candidate datastore.\n");

  MP_LOG_I("Successfully edited the candidate datastore for RU \"%s\".\n", ru_session->ru_ip_add);

  nc_rpc_free(rpc);

  return true;
}

static bool validate_config_mplane(ru_session_t *ru_session)
{
  int timeout = CLI_RPC_REPLY_TIMEOUT;
  struct nc_rpc *rpc;
  NC_WD_MODE wd = NC_WD_ALL;
  NC_PARAMTYPE param = NC_PARAMTYPE_CONST;
  char *src_start = NULL;
  NC_DATASTORE source = NC_DATASTORE_CANDIDATE;

  MP_LOG_I("RPC request to RU \"%s\" = <validate> candidate datastore.\n", ru_session->ru_ip_add);
  rpc = nc_rpc_validate(source, src_start, param);
  AssertError(rpc != NULL, return false, "[MPLANE] <validate> RPC creation failed.\n");

  bool success = rpc_send_recv((struct nc_session *)ru_session->session, rpc, wd, timeout, NULL);
  AssertError(success, return false, "[MPLANE] Failed to validate candidate datastore.\n");

  MP_LOG_I("Successfully validated candidate datastore for RU \"%s\".\n", ru_session->ru_ip_add);

  nc_rpc_free(rpc);

  return true;
}

static bool commit_config_mplane(ru_session_t *ru_session)
{
  int timeout = CLI_RPC_REPLY_TIMEOUT;
  struct nc_rpc *rpc;
  NC_WD_MODE wd = NC_WD_ALL;
  NC_PARAMTYPE param = NC_PARAMTYPE_CONST;
  int confirmed = 0;
  int32_t confirm_timeout = 0;
  char *persist = NULL, *persist_id = NULL;

  MP_LOG_I("RPC request to RU \"%s\" = <commit> candidate datastore.\n", ru_session->ru_ip_add);
  rpc = nc_rpc_commit(confirmed, confirm_timeout, persist, persist_id, param);
  AssertError(rpc != NULL, return false, "[MPLANE] <commit> RPC creation failed.\n");

  bool success = rpc_send_recv((struct nc_session *)ru_session->session, rpc, wd, timeout, NULL);
  AssertError(success, return false, "[MPLANE] Failed to commit candidate datastore.\n");

  MP_LOG_I("Successfully commited configuration into running datastore for RU \"%s\".\n", ru_session->ru_ip_add);

  nc_rpc_free(rpc);

  return true;
}

bool edit_val_commmit_rpc(ru_session_t *ru_session, const char *content)
{
  bool success = false;

  success = edit_config_mplane(ru_session, content);
  AssertError(success, return false, "[MPLANE] Unable to edit the RU configuration.\n");

  success = validate_config_mplane(ru_session);
  AssertError(success, return false, "[MPLANE] Unable to validate the RU configuration.\n");

  success = commit_config_mplane(ru_session);
  AssertError(success, return false, "[MPLANE] Unable to commit the RU configuration.\n");

  return success;
}
