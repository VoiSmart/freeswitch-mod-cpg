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

#include <switch.h>
#include "cpg_utils.h"
#include "fsm_input.h"


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

void launch_rollback_thread(virtual_ip_t *vip);
void *SWITCH_THREAD_FUNC vip_thread_run(switch_thread_t *thread, void *obj);
void *SWITCH_THREAD_FUNC rollback_thread_run(switch_thread_t *thread, void *obj);
static switch_status_t send_message(cpg_handle_t h, void *buf, int len);

virtual_ip_t *find_virtual_ip(char *address)
{
    // controllo che non sia null
    virtual_ip_t *vip = NULL;
    vip = (virtual_ip_t *)switch_core_hash_find(globals.virtual_ip_hash,address);
    return vip;
}

char *virtual_ip_get_state(virtual_ip_t *vip)
{
    char state[12];
    switch (vip->state) {
            case ST_IDLE:
                switch_snprintf(state,sizeof(state),"IDLE");
                break;
            case ST_START:
                switch_snprintf(state,sizeof(state),"START");
                break;
            case ST_MASTER:
                switch_snprintf(state,sizeof(state),"MASTER");
                break;
            case ST_BACKUP:
                switch_snprintf(state,sizeof(state),"BACKUP");
                break;
            case ST_RBACK:
                switch_snprintf(state,sizeof(state),"RBACK");
                break;
            case ST_STOP:
                switch_snprintf(state,sizeof(state),"STOP");
                break;
            default:
                switch_snprintf(state,sizeof(state),"Missing");
                break;
    }
    return strdup(state);
}

state_t string_to_state(char *state)
{
    state_t pstate = ST_IDLE;
    if (!strcasecmp(state,"MASTER")) pstate = ST_MASTER;
    else if (!strcasecmp(state,"BACKUP")) pstate = ST_BACKUP;
    else if (!strcasecmp(state,"IDLE")) pstate = ST_IDLE;
    else if (!strcasecmp(state,"STOP")) pstate = ST_STOP;
    else if (!strcasecmp(state,"RBACK")) pstate = ST_RBACK;
    else if (!strcasecmp(state,"START")) pstate = ST_START;
    return pstate;
}

void launch_rollback_thread(virtual_ip_t *vip)
{
    switch_thread_t *thread;
    switch_threadattr_t *thd_attr = NULL;

    switch_threadattr_create(&thd_attr, globals.pool);
    //switch_threadattr_detach_set(thd_attr, 1);
    switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
    switch_threadattr_priority_increase(thd_attr);
    switch_thread_create(&thread, thd_attr, rollback_thread_run,
                                                         vip, globals.pool);
}
void *SWITCH_THREAD_FUNC rollback_thread_run(switch_thread_t *thread, void *obj)
{
    virtual_ip_t *vip = (virtual_ip_t *) obj;
    int result;
    uint32_t local_id;
    switch_status_t status;


    local_id = vip->rollback_node_id;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                            "Rollback thread for %s started, waiting for %s!\n",
                                vip->address, utils_node_pid_format(local_id));

    for (int i = 0;i < vip->rollback_delay * 60; i++) {
        switch_yield(1000000);
        if ( vip->rollback_node_id != local_id) {
             switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                              "Rollback node for %s changed!\n", vip->address);
             vip->rollback_node_id = 0;
             return NULL;
        }

        if (utils_count_profile_channels(vip->address) == 0) {
             switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                                   "0 calls for %s!Rollback!\n", vip->address);

             break;
        }

    }
    vip->rollback_node_id = 0;
    vip->running = 0;
    switch_thread_join(&status, vip->virtual_ip_thread);

    from_master_to_standby(vip);

    result = cpg_leave(vip->handle, &vip->group_name);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                                 "Leave  result is %d (should be 1)\n", result);
    switch_yield(10000);
    result = cpg_finalize (vip->handle);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                              "Finalize  result is %d (should be 1)\n", result);
    switch_yield(5000000);
    virtual_ip_start(vip);
    return NULL;
}

switch_status_t virtual_ip_start(virtual_ip_t *vip)
{
    switch_threadattr_t *thd_attr = NULL;
    switch_threadattr_create(&thd_attr, globals.pool);
    switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
    switch_threadattr_priority_increase(thd_attr);
    switch_thread_create(&(vip->virtual_ip_thread), thd_attr,
                                     vip_thread_run, vip, globals.pool);
    //TODO cerca i possibili errori
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t virtual_ip_stop(virtual_ip_t *vip)
{
    return go_to_standby(vip);
}

void *SWITCH_THREAD_FUNC vip_thread_run(switch_thread_t *thread, void *obj)
{
    virtual_ip_t *vip = (virtual_ip_t *) obj;
    fd_set read_fds;
    int select_fd;
    int result;

    vip->members_number = 0;

    if (from_standby_to_init(vip) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
          "Cannot launch %s, verify virtual ip and arptables\n", vip->address);
        return NULL;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"%s launch\n",
                                                                 vip->address);

    result = cpg_initialize (&vip->handle, &callbacks);
    if (result != CS_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
           "Could not initialize Cluster Process Group API instance error %d\n",
                                                                        result);
        return NULL;
    }

    result = cpg_context_set (vip->handle,vip);
    if (result != CS_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                              "Could not set handle context\n");
        return NULL;
    }

    cpg_fd_get(vip->handle, &select_fd);

    result = cpg_local_get (vip->handle, &(vip->node_id));
    if (result != CS_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Could not get local node id\n");
        return NULL;
    }
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Local node id is %x\n", vip->node_id);

    result = cpg_join(vip->handle, &vip->group_name);
    if (result != CS_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Could not join process group, error %d\n", result);
        return NULL;
    }

    vip->running = 1;

    while((vip->running) && (globals.running)) {
        struct timeval timeout = { 1, 0 };

        FD_ZERO (&read_fds);
        FD_SET (select_fd, &read_fds);
        result = select (select_fd + 1, &read_fds, 0, 0, &timeout);
        if (result == -1) {
            printf ("select %d\n",result);
        }

        if (FD_ISSET (select_fd, &read_fds)) {
            if (cpg_dispatch (vip->handle, CS_DISPATCH_ALL) != CS_OK)
                return NULL;
        }

    }
    //end
    node_remove_all(vip->node_list);
    if (vip->node_list) {
        vip->node_list = NULL;
    }
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"%s virtual_ip thread stopped\n",vip->address);
    return NULL;


}

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
            char *sql;
            sql = (char *)msg + sizeof(header_t);
            utils_send_track_event(sql, vip->address);

            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"received sql from other node\n");

        } else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"discarded my sql\n");
        }
        break;

    case NODE_STATE:
    {
        node_msg_t *nm;

        nm = (node_msg_t *)(((char *)msg ) + sizeof(header_t));

        fsm_input_new_state_message(vip, nm);
        break;
    }
    default:
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Bad header\n");
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"DeliverCallback: message (len=%lu)from %s\n",
               (unsigned long int) msg_len, utils_node_pid_format(nodeid));
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
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Could not get local context\n");
        return;
    }

    vip = (virtual_ip_t *) context;

    vip->member_list_entries = member_list_entries;

    // left
    if (left_list_entries > 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Someone left!\n");

        for (i = 0; i < left_list_entries; i++) {
            fsm_input_node_down(vip, left_list[i].nodeid);
        }
    }

    // join
    if (joined_list_entries > 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Someone join!\n");
        fsm_input_node_up(vip, member_list_entries);
    }

}


switch_status_t virtual_ip_send_sql(virtual_ip_t *vip, char *sql)
{
    header_t *hd;
    char *buf;
    int len;
    switch_status_t status;

    len = sizeof(header_t) + strlen(sql) + 1;
    buf = malloc(len);
    if (buf == NULL) {
        return SWITCH_STATUS_FALSE;
    }
    memset(buf,0,len);

    hd = (header_t *) buf;
    hd->type = SQL;

    memcpy(buf+sizeof(header_t), sql, strlen(sql) + 1);

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
    nm->priority = vip->priority;
    switch_snprintf(nm->runtime_uuid,sizeof(nm->runtime_uuid),"%s",switch_core_get_uuid());

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
        usleep(1000);
        if (!(retries % 100))
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"cpg_mcast_joined retry %d\n",
                   retries);
        goto retry;
    }
    if (error != CPG_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"cpg_mcast_joined error %d handle %llx\n",
              error, (unsigned long long)h);
        return SWITCH_STATUS_FALSE;
    }

    if (retries)
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"cpg_mcast_joined retried %d\n",
              retries);

    return SWITCH_STATUS_SUCCESS;
}
