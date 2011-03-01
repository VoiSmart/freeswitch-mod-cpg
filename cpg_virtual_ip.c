/*
 * Closed Process Group (Corosync CPG) failover module for Freeswitch
 *
 * Copyright (C) 2010 Voismart SRL
 *
 * Authors: Stefano Bossi <sbossi@voismart.it>
 *
 * Further Contributors:
 * Matteo Brancaleoni <mbrancaleoni@voismart.it> - Original idea
 *
 * This program cannot be modified, distributed or used without
 * specific written permission of the copyright holder.
 *
 * The source code is provided only for evaluation purposes.
 * Any usage or license must be negotiated directly with Voismart Srl.
 *
 * Voismart Srl
 * Via Benigno Crespi 12
 * 20159 Milano - MI
 * ITALY
 *
 * Phone : +39.02.70633354
 *
 */

#include "cpg_virtual_ip.h"

#include "cpg_utils.h"
#include "fsm_input.h"
#include "mod_cpg.h"

#define CS_MAX_RETRIES 10

typedef enum {
    SQL,
    NODE_STATE
} msg_type_t;

struct header {
    msg_type_t type;
    int len;
};
typedef struct header header_t;

static void DeliverCallback (
    cpg_handle_t handle,
    const struct cpg_name *groupName,
    uint32_t nodeid,
    uint32_t pid,
    void *msg,
    size_t msg_len);

static void ConfchgCallback (
    cpg_handle_t handle,
    const struct cpg_name *groupName,
    const struct cpg_address *member_list, size_t member_list_entries,
    const struct cpg_address *left_list, size_t left_list_entries,
    const struct cpg_address *joined_list, size_t joined_list_entries);

static cpg_callbacks_t callbacks = {
    .cpg_deliver_fn = DeliverCallback,
    .cpg_confchg_fn = ConfchgCallback,
};

static const char *evt_names[MAX_EVENTS] = {
    "STARTUP",
    "DUPLICATE",
    "MASTER_DOWN",
    "MASTER_UP",
    "BACKUP_DOWN",
    "BACKUP_UP",
    "RBACK_REQ",
    "STOP"
};

static const char *st_names[MAX_STATES] = {
    "IDLE",
    "START",
    "BACKUP",
    "MASTER",
    "ROLLBACK"
};

static switch_status_t send_message(cpg_handle_t h, void *buf, int len);


const char *event_to_string(event_t event)
{
    if (event < 0 || event >= MAX_EVENTS) return "";
    return evt_names[event];
}

const char *state_to_string(state_t state)
{
    if (state<0 || state >= MAX_STATES) return "";
    return st_names[state];
}

virtual_ip_t *find_virtual_ip(char *address)
{
    virtual_ip_t *vip = NULL;

    if (!address)
        return NULL;
    vip = (virtual_ip_t *)
            switch_core_hash_find(globals.virtual_ip_hash, address);
    return vip;
}

virtual_ip_t *find_virtual_ip_from_profile(char *profile_name)
{
    switch_hash_index_t *hi;
    void *val;
    const void *vvar;
    virtual_ip_t *vip = NULL;

    if (!profile_name)
        return NULL;

    for (hi = switch_hash_first(NULL, globals.virtual_ip_hash);
         hi; hi = switch_hash_next(hi)) {

        switch_hash_this(hi, &vvar, NULL, &val);
        vip = (virtual_ip_t *) val;

        for (int i = 0; i < MAX_SOFIA_PROFILES; i++) {
            if (!strcmp(vip->config.profiles[i].name, "")) break;
            if (!strcmp(vip->config.profiles[i].name, profile_name)) {
                return vip;
            }
        }

    }
    return NULL;
}

short int virtual_ip_profile_index(virtual_ip_t *vip, char *profile_name)
{
    for (int i = 0; i < MAX_SOFIA_PROFILES; i++) {
        if (!strcmp(vip->config.profiles[i].name, "")) return -1;
        if (!strcmp(vip->config.profiles[i].name, profile_name)) {
            return i;
        }
    }
    return -2;
}

const char *virtual_ip_get_state(virtual_ip_t *vip)
{
    return state_to_string(vip->state);
}

state_t string_to_state(char *state)
{
    state_t pstate = ST_IDLE;
    if (!strcasecmp(state,"MASTER")) pstate = ST_MASTER;
    else if (!strcasecmp(state,"BACKUP")) pstate = ST_BACKUP;
    else if (!strcasecmp(state,"IDLE")) pstate = ST_IDLE;
    else if (!strcasecmp(state,"RBACK")) pstate = ST_RBACK;
    else if (!strcasecmp(state,"START")) pstate = ST_START;
    return pstate;
}

switch_bool_t vip_is_running(virtual_ip_t *vip)
{
    return (vip->state != ST_IDLE)?SWITCH_TRUE:SWITCH_FALSE;
}

/* threads loops*/
void
*SWITCH_THREAD_FUNC rollback_thread(switch_thread_t *thread, void *obj)
{
    virtual_ip_t *vip = (virtual_ip_t *) obj;
    uint32_t local_id;

    local_id = vip->rollback_node_id;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                      "Rollback thread for %s started, waiting for %s!\n",
                       vip->config.address, utils_node_pid_format(local_id));

    for (int i = 0;i < vip->config.rollback_delay * 60; i++) {
        switch_yield(1000000);
        if (vip->state != ST_RBACK)
            goto stop_rollback;

/*TODO        if (utils_count_profile_channels(vip->config.address) == 0) {*/
/*             switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,*/
/*                            "0 calls for %s!Rollback!\n", vip->config.address);*/
/*             break;*/
/*        }*/
    }
/*rollback:*/
    fsm_input_cmd_stop(vip);
    switch_yield(1000000);
    fsm_input_cmd_start(vip);
    return NULL;

stop_rollback:
    return NULL;
}

switch_status_t virtual_ip_start(virtual_ip_t *vip)
{
    return fsm_input_cmd_start(vip);
}

switch_status_t virtual_ip_stop(virtual_ip_t *vip)
{
    return fsm_input_cmd_stop(vip);
}

void *SWITCH_THREAD_FUNC vip_thread(switch_thread_t *thread, void *obj)
{
    virtual_ip_t *vip = (virtual_ip_t *) obj;
    fd_set read_fds;
    int select_fd;
    int result;

    switch_log_printf(SWITCH_CHANNEL_LOG,
                      SWITCH_LOG_DEBUG,"%s launch\n", vip->config.address);

    for (int i=0; i<CS_MAX_RETRIES; i++) {
        result = cpg_initialize (&vip->handle, &callbacks);
        if ((result == CS_OK) || (result != CS_ERR_TRY_AGAIN)) break;
        switch_yield(10000);
    }
    if (result != CS_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                          "Could not initialize Cluster Process Group API"
                          " instance error %d\n", result);
        goto end;
    }

    result = cpg_context_set (vip->handle,vip);
    if (result != CS_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                          "Could not set handle context\n");
        goto finalize;
    }

    cpg_fd_get(vip->handle, &select_fd);

    result = cpg_local_get (vip->handle, &(vip->node_id));
    if (result != CS_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                          "Could not get local node id\n");
        goto finalize;
    }
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                      "Local node id is %s\n",
                      utils_node_pid_format(vip->node_id));


    for (int i=0; i< CS_MAX_RETRIES; i++) {
        result = cpg_join(vip->handle, &vip->config.group_name);
        if ((result == CS_OK) || (result != CS_ERR_TRY_AGAIN)) break;
        switch_yield(10000);
    }
    if (result != CS_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                          "Could not join process group, error %d\n", result);
        goto finalize;
    }

    while((vip->state != ST_IDLE) && (globals.running == SWITCH_TRUE)) {
        struct timeval timeout = { 1, 0 };

        FD_ZERO (&read_fds);
        FD_SET (select_fd, &read_fds);
        result = select (select_fd + 1, &read_fds, 0, 0, &timeout);
        if (result == -1) {
            switch_log_printf(SWITCH_CHANNEL_LOG,
                              SWITCH_LOG_ERROR, "select failed %d\n",result);
        }

        if (FD_ISSET (select_fd, &read_fds)) {
            if (cpg_dispatch (vip->handle, CS_DISPATCH_ALL) != CS_OK)
                goto leave;
        }

    }
    //end

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                      "%s virtual_ip thread stopped\n",vip->config.address);
leave:
    result = cpg_leave(vip->handle, &vip->config.group_name);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                      "Leave  result is %d (should be 1)\n", result);
    switch_yield(10000);
finalize:
    result = cpg_finalize (vip->handle);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                      "Finalize  result is %d (should be 1)\n", result);
end:
    switch_yield(10000);

    return NULL;
}

/*CALLBACKS*/
static void DeliverCallback (
    cpg_handle_t handle,
    const struct cpg_name *groupName,
    uint32_t nodeid,
    uint32_t pid,
    void *msg,
    size_t msg_len)
{
    header_t *hd;
    virtual_ip_t *vip;
    void *context;

    hd = msg;

    cpg_context_get (handle, &context);
    vip = (virtual_ip_t *) context;

    switch (hd->type) {
        case SQL:
            if (vip->node_id != nodeid) {
                char *sql = NULL;
                char *profile_name = NULL;

                profile_name = (char *)msg + sizeof(header_t);
                sql = (char *)msg + sizeof(header_t) + MAX_SOFIA_NAME;

                for (int i=0; i<MAX_SOFIA_PROFILES; i++) {
                    if (!strcmp(vip->config.profiles[i].name, profile_name)) {

                        utils_send_track_event(sql,
                                               vip->config.profiles[i].name);

                        switch_log_printf(SWITCH_CHANNEL_LOG,
                                          SWITCH_LOG_DEBUG,
                                          "received sql from other node\n");
                        goto end;
                    }
                }
                switch_log_printf(SWITCH_CHANNEL_LOG,
                                  SWITCH_LOG_ERROR,
                                  "invalid profile index!\n");

            } else {
                switch_log_printf(SWITCH_CHANNEL_LOG,
                                  SWITCH_LOG_DEBUG,"discarded my sql\n");
            }
            break;
        case NODE_STATE:
        {
            node_msg_t *nm;
            nm = (node_msg_t *)(((char *)msg ) + sizeof(header_t));

            fsm_input_new_state_message(vip, nm, nodeid);
            break;
        }
        default:
            switch_log_printf(SWITCH_CHANNEL_LOG,
                              SWITCH_LOG_ERROR,"Bad header\n");
    }
end:
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                      "DeliverCallback: message (len=%lu)from %s\n",
                      (unsigned long int) msg_len,
                      utils_node_pid_format(nodeid));
}

static void ConfchgCallback (
    cpg_handle_t handle,
    const struct cpg_name *groupName,
    const struct cpg_address *member_list, size_t member_list_entries,
    const struct cpg_address *left_list, size_t left_list_entries,
    const struct cpg_address *joined_list, size_t joined_list_entries)
{
    int i, result;
    virtual_ip_t *vip;
    void *context;

    result = cpg_context_get (handle, &context);
    if (result != CS_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_ERROR,
                          "Could not get local context\n");
        return;
    }

    vip = (virtual_ip_t *) context;

    vip->member_list_entries = member_list_entries;

    // left
    if (left_list_entries > 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_INFO, "Someone left!\n");

        for (i = 0; i < left_list_entries; i++) {
            fsm_input_node_down(vip, left_list[i].nodeid);
        }
    }

    // join
    if (joined_list_entries > 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_INFO, "Someone join!\n");

        fsm_input_node_up(vip);

    }

}

/* send messages */
switch_status_t virtual_ip_send_all_sql(virtual_ip_t *vip)
{

    char *sql = NULL;

    sql = switch_mprintf("");

    for (int i=0;i<MAX_SOFIA_PROFILES; i++) {
        if (!strcmp(vip->config.profiles[i].name,"")) break;
        utils_send_request_all(vip->config.profiles[i].name);
    }
    switch_safe_free(sql);
    return SWITCH_STATUS_SUCCESS;
}


switch_status_t virtual_ip_send_sql(virtual_ip_t *vip, char *name, char *sql)
{
    header_t *hd;
    char *buf;
    int len;
    switch_status_t status;

    if (!name) {
        return SWITCH_STATUS_FALSE;
    }

    len = sizeof(header_t) + MAX_SOFIA_NAME + strlen(sql) + 1;
    buf = malloc(len);
    if (buf == NULL) {
        return SWITCH_STATUS_FALSE;
    }
    memset(buf,0,len);

    hd = (header_t *) buf;
    hd->type = SQL;

    memcpy(buf+sizeof(header_t), name, MAX_SOFIA_NAME);
    memcpy(buf+sizeof(header_t) + MAX_SOFIA_NAME, sql, strlen(sql) + 1);

    status = send_message(vip->handle,buf,len);

    free(buf);

    return status;
}

switch_status_t virtual_ip_send_state(virtual_ip_t *vip)
{
    header_t *hd;
    node_msg_t *nm;
    char *buf;
    int len;
    switch_status_t status;

    len = sizeof(header_t) + sizeof(node_msg_t);
    buf = malloc(len);
    if (buf == NULL) {
        return SWITCH_STATUS_FALSE;
    }
    memset(buf,0,len);

    hd = (header_t *) buf;
    hd->type = NODE_STATE;
    hd->len = 10;

    nm = ( node_msg_t *)(buf + sizeof(header_t));
    nm->state = vip->state;
    nm->priority = vip->config.priority;
    switch_snprintf(nm->runtime_uuid,
                    sizeof(nm->runtime_uuid), "%s", switch_core_get_uuid());

    status = send_message(vip->handle,buf,len);

    free(buf);

    return status;
}

static switch_status_t send_message(cpg_handle_t h, void *buf, int len)
{
    struct iovec iov;
    cpg_error_t error;
    int retries = 0;

    iov.iov_base = buf;
    iov.iov_len = len;

 retry:
    error = cpg_mcast_joined(h, CPG_TYPE_AGREED, &iov, 1);
    if (error == CPG_ERR_TRY_AGAIN) {
        retries++;
        switch_yield(1000);
        if (!(retries % 100))
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                              "cpg_mcast_joined retry %d\n", retries);
        goto retry;
    }
    if (error != CPG_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                          "cpg_mcast_joined error %d handle %llx\n",
                          error, (unsigned long long)h);
        return SWITCH_STATUS_FALSE;
    }

    if (retries)
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                          "cpg_mcast_joined retried %d\n", retries);

    return SWITCH_STATUS_SUCCESS;
}
