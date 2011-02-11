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

#include "profile.h"

#include <switch.h>
#include "cpg_utils.h"




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


void *SWITCH_THREAD_FUNC profile_thread_run(switch_thread_t *thread, void *obj);
void *SWITCH_THREAD_FUNC rollback_thread_run(switch_thread_t *thread, void *obj);
static int send_message(cpg_handle_t h, void *buf, int len);

profile_t *find_profile_by_name(char *profile_name)
{
    // controllo che non sia null
    profile_t *profile = NULL;
    profile = (profile_t *)switch_core_hash_find(globals.profile_hash,profile_name);
    return profile;
}


switch_status_t from_standby_to_init(profile_t *profile)
{
    profile->state = INIT;

    // start sofia profile
    for (int i=0; i<3; i++) {
        switch_yield(100000);
        if (utils_start_sofia_profile(profile->name) != SWITCH_STATUS_SUCCESS) {
            goto error;
        }
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From STANDBY to INIT for %s!\n", profile->name);
    return SWITCH_STATUS_SUCCESS;

error:
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to INIT!\n");
    profile->state = STANDBY;
    return SWITCH_STATUS_FALSE;

}

switch_status_t from_init_to_backup(profile_t *profile)
{
    profile->state = BACKUP;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From INIT to BACKUP for %s!\n", profile->name);
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t from_init_to_master(profile_t *profile)
{
    profile->state = MASTER;

    // set the ip to bind to
    if (utils_add_vip(profile->virtual_ip, profile->device) != SWITCH_STATUS_SUCCESS) {
        goto error;
    }

    // gratuitous arp request
    if (utils_send_gARP(profile->mac, profile->virtual_ip, profile->device) != SWITCH_STATUS_SUCCESS) {
        utils_remove_vip(profile->virtual_ip, profile->device);
        goto error;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From INIT to MASTER for %s!\n", profile->name);
    return SWITCH_STATUS_SUCCESS;

error:
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to MASTER!\n");
    profile->state = STANDBY;
    return SWITCH_STATUS_FALSE;
}

switch_status_t from_backup_to_master(profile_t *profile)
{
    profile->state = MASTER;

    // set the ip to bind to
    if (utils_add_vip(profile->virtual_ip, profile->device) != SWITCH_STATUS_SUCCESS) {
        goto error;
    }

    // gratuitous arp request
    if (utils_send_gARP(profile->mac, profile->virtual_ip, profile->device) != SWITCH_STATUS_SUCCESS) {
        utils_remove_vip(profile->virtual_ip, profile->device);
        goto error;
    }

    // sofia recover!!!
    if (profile->autorecover == SWITCH_TRUE) {
        utils_recover(profile->name);
    }
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From BACKUP to MASTER for %s!\n", profile->name);
    return SWITCH_STATUS_SUCCESS;

error:
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to MASTER!\n");
    profile->state = STANDBY;
    return SWITCH_STATUS_FALSE;
}

switch_status_t from_master_to_standby(profile_t *profile)
{
    profile->state = STANDBY;
    profile->master_id = 0;
    profile->member_list_entries = 0;
    if (utils_remove_vip(profile->virtual_ip, profile->device) != SWITCH_STATUS_SUCCESS) {
        goto error;
    }

    // stop sofia profile
    if (utils_stop_sofia_profile(profile->name) != SWITCH_STATUS_SUCCESS){
        goto error;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From MASTER to STANDBY for %s!\n", profile->name);
    return SWITCH_STATUS_SUCCESS;

error:
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to STANDBY!\n");
    return SWITCH_STATUS_FALSE;
}

switch_status_t from_backup_to_standby(profile_t *profile)
{
    profile->state = STANDBY;
    profile->master_id = 0;
    profile->member_list_entries = 0;

    // stop sofia profile
    if (utils_stop_sofia_profile(profile->name) != SWITCH_STATUS_SUCCESS){
        goto error;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From BACKUP to STANDBY for %s!\n", profile->name);
    return SWITCH_STATUS_SUCCESS;

error:
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to STANDBY!\n");
    return SWITCH_STATUS_FALSE;
}

switch_status_t from_init_to_standby(profile_t *profile)
{
    profile->state = STANDBY;
    profile->master_id = 0;
    profile->member_list_entries = 0;

    // stop sofia profile
    if (utils_stop_sofia_profile(profile->name) != SWITCH_STATUS_SUCCESS){
        goto error;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From INIT to STANDBY for %s!\n", profile->name);
    return SWITCH_STATUS_SUCCESS;

error:
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to STANDBY!\n");
    return SWITCH_STATUS_FALSE;
}

switch_status_t go_to_standby(profile_t *profile)
{
    switch_status_t status = SWITCH_STATUS_FALSE;

    if (!profile)
        return status;

    switch(profile->state) {
        case MASTER:
            status = from_master_to_standby(profile);
            break;
        case BACKUP:
            status = from_backup_to_standby(profile);
            break;
        case INIT:
            status = from_init_to_standby(profile);
            break;
        case STANDBY:
            status = SWITCH_STATUS_SUCCESS;
            break;
    }
    return status;
}






void launch_rollback_thread(profile_t *profile)
{
    switch_thread_t *thread;
    switch_threadattr_t *thd_attr = NULL;

    switch_threadattr_create(&thd_attr, globals.pool);
    //switch_threadattr_detach_set(thd_attr, 1);
    switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
    switch_threadattr_priority_increase(thd_attr);
    switch_thread_create(&thread, thd_attr, rollback_thread_run,
                                                         profile, globals.pool);
}
void *SWITCH_THREAD_FUNC rollback_thread_run(switch_thread_t *thread, void *obj)
{
    profile_t *profile = (profile_t *) obj;
    int result;
    uint32_t local_id;
    switch_status_t status;


    local_id = profile->rollback_node_id;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                            "Rollback thread for %s started, waiting for %s!\n",
                                profile->name, utils_node_pid_format(local_id));

    for (int i = 0;i < profile->rollback_delay * 60; i++) {
        switch_yield(1000000);
        if ( profile->rollback_node_id != local_id) {
             switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                              "Rollback node for %s changed!\n", profile->name);
             profile->rollback_node_id = 0;
             return NULL;
        }

        if (utils_count_profile_channels(profile->name) == 0) {
             switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                                   "0 calls for %s!Rollback!\n", profile->name);

             break;
        }

    }
    profile->rollback_node_id = 0;
    profile->running = 0;
    switch_thread_join(&status, profile->profile_thread);

    from_master_to_standby(profile);

    result = cpg_leave(profile->handle, &profile->group_name);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                                 "Leave  result is %d (should be 1)\n", result);
    switch_yield(10000);
    result = cpg_finalize (profile->handle);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                              "Finalize  result is %d (should be 1)\n", result);
    switch_yield(5000000);
    launch_profile_thread(profile);
    return NULL;
}

void launch_profile_thread(profile_t *profile)
{
    switch_threadattr_t *thd_attr = NULL;
    switch_threadattr_create(&thd_attr, globals.pool);
    switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
    switch_threadattr_priority_increase(thd_attr);
    switch_thread_create(&(profile->profile_thread), thd_attr,
                                     profile_thread_run, profile, globals.pool);
}

void *SWITCH_THREAD_FUNC profile_thread_run(switch_thread_t *thread, void *obj)
{
    profile_t *profile = (profile_t *) obj;
    fd_set read_fds;
    int select_fd;
    int result;

    profile->members_number = 0;

    if (from_standby_to_init(profile) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
          "Cannot launch %s, verify virtual ip and arptables\n", profile->name);
        return NULL;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"%s launch\n",
                                                                 profile->name);

    result = cpg_initialize (&profile->handle, &callbacks);
    if (result != CS_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
           "Could not initialize Cluster Process Group API instance error %d\n",
                                                                        result);
        return NULL;
    }

    result = cpg_context_set (profile->handle,profile);
    if (result != CS_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                              "Could not set handle context\n");
        return NULL;
    }

    cpg_fd_get(profile->handle, &select_fd);

    result = cpg_local_get (profile->handle, &(profile->node_id));
    if (result != CS_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Could not get local node id\n");
        return NULL;
    }
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Local node id is %x\n", profile->node_id);

    result = cpg_join(profile->handle, &profile->group_name);
    if (result != CS_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Could not join process group, error %d\n", result);
        return NULL;
    }

    profile->running = 1;

    while((profile->running) && (globals.running)) {
        struct timeval timeout = { 1, 0 };

        FD_ZERO (&read_fds);
        FD_SET (select_fd, &read_fds);
        result = select (select_fd + 1, &read_fds, 0, 0, &timeout);
        if (result == -1) {
            printf ("select %d\n",result);
        }

        if (FD_ISSET (select_fd, &read_fds)) {
            if (cpg_dispatch (profile->handle, CS_DISPATCH_ALL) != CS_OK)
                return NULL;
        }

    }
    //end
    node_remove_all(profile->node_list);
    if (profile->node_list) {
        profile->node_list = NULL;
    }
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"%s profile thread stopped\n",profile->name);
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
    profile_t *profile;
    void *context;

    hd = msg;

    cpg_context_get (handle, &context);
    profile = (profile_t *) context;

    switch (hd->type) {

    case SQL:
        if (profile->node_id != nodeid) {
            char *sql;
            sql = (char *)msg + sizeof(header_t);
            utils_send_track_event(sql, profile->name);

            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"received sql from other node\n");

        } else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"discarded my sql\n");
        }
        break;

    case NODE_STATE:
    {
        node_msg_t *nm;

        nm = (node_msg_t *)(((char *)msg ) + sizeof(header_t));

        profile->node_list = node_add(profile->node_list, nodeid, nm->priority);

        if (nm->state == MASTER) {
            profile->master_id = nodeid;
            switch_snprintf(profile->runtime_uuid, sizeof(profile->runtime_uuid),"%s", nm->runtime_uuid );
        }

        switch (profile->state) {

            case INIT:
                if ((nm->priority == profile->priority) && (nodeid != profile->node_id)) {
                    int result;
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                           "Node with the same priority detected!Please change it!\n");
                    from_init_to_standby(profile);
                    result = cpg_leave(profile->handle, &profile->group_name);
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                                        "Leave  result is %d (should be 1)\n", result);
                    switch_yield(10000);
                    result = cpg_finalize (profile->handle);
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                                     "Finalize  result is %d (should be 1)\n", result);
                    break;
                }

                // if the priority list is complete becomes BACKUP
                if (list_entries(profile->node_list) == profile->member_list_entries) {
                    from_init_to_backup(profile);
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "I'm a BACKUP!\n");
                }
                break;
            case BACKUP:
                break;
            case MASTER:
                if (profile->node_list != NULL ) {
                    if ((profile->autorollback == SWITCH_TRUE) &&
                        (profile->node_list->nodeid != profile->node_id) &&
                        (profile->node_list->nodeid != profile->rollback_node_id)) {

                        profile->rollback_node_id = profile->node_list->nodeid;

                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                                              "ROLLBACK node %u(!=%u) found!\n",
                                                     profile->node_list->nodeid,
                                                              profile->node_id);

                        launch_rollback_thread(profile);
                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                                                   "ROLLBACK timer started!\n");
                    }
                }
                break;
            default:
                break;
        }
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
    profile_t *profile;
    void *context;

    result = cpg_context_get (handle, &context);
    if (result != CS_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Could not get local context\n");
        return;
    }

    profile = (profile_t *) context;

    profile->member_list_entries = member_list_entries;

    // left
    if (left_list_entries > 0) {
        switch_bool_t master_flag = SWITCH_FALSE;
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "LEFT!\n");
        // remove nodes gone down

        for (i = 0; i < left_list_entries; i++) {
            if ( left_list[i].nodeid == profile->master_id) {
                master_flag = SWITCH_TRUE;
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"Master down!\n");
            }
            profile->node_list = node_remove(profile->node_list, left_list[i].nodeid);
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"i = %d, nodeid = %u\n",i,left_list[i].nodeid);
        }
        switch (profile->state) {

            case INIT:
                //
                break;
            case BACKUP:
                // If master is down
                if (master_flag == SWITCH_TRUE) {
                    //
                    // if I'm the first in priority list
                    if ( profile->node_list->nodeid == profile->node_id) {
                        // become master
                        from_backup_to_master(profile);
                        // and I say it to all other nodes
                        send_state(handle, profile);
                    } else {
                        // clean up the table
                        char *sql;
                        sql = switch_mprintf("delete from sip_recovery where "
                                     "runtime_uuid='%q' and profile_name='%q'",
                                          profile->runtime_uuid, profile->name);

                        utils_send_track_event(sql, profile->name);
                        switch_safe_free(sql);
                    }
                }
                break;
            case MASTER:
                break;
            default:
                break;
        }
    }

    // join
    if (joined_list_entries > 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "JOIN!\n");
        // if someone has joined I send him my infos
        send_state(handle,profile);

        switch (profile->state) {
            case INIT:
                // if I'm alone
                if (member_list_entries == 1) {
                    // I'm the master
                    from_init_to_master(profile);
                    send_state(handle, profile);
                }
                // else I have to fill priority table
                break;
            case BACKUP:
                break;
            case MASTER:
                break;
            default:
                break;
        }
    }

}


switch_status_t send_sql(cpg_handle_t h, char *sql)
{
    header_t *hd;
    char *buf;
    int len;

    len = sizeof(header_t) + strlen(sql) + 1;
    buf = malloc(len);
    if (buf == NULL) {
        return SWITCH_FALSE;
    }
    memset(buf,0,len);

    hd = (header_t *) buf;
    hd->type = SQL;

    memcpy(buf+sizeof(header_t), sql, strlen(sql) + 1);

    send_message(h,buf,len);

    free(buf);

    return SWITCH_STATUS_SUCCESS;
}
switch_status_t send_state(cpg_handle_t h, profile_t *profile)
{
    header_t *hd;
    node_msg_t *nm;
    char *buf;
    int len;

    len = sizeof(header_t) + sizeof(node_msg_t);
    buf = malloc(len);
    if (buf == NULL) {
        return SWITCH_FALSE;
    }
    memset(buf,0,len);

    hd = (header_t *) buf;
    hd->type = NODE_STATE;
    hd->len = 10;

    nm = ( node_msg_t *)(buf + sizeof(header_t));
    nm->state = profile->state;
    nm->priority = profile->priority;
    switch_snprintf(nm->runtime_uuid,sizeof(nm->runtime_uuid),"%s",switch_core_get_uuid());

    send_message(h,buf,len);

    free(buf);

    return SWITCH_STATUS_SUCCESS;
}


static int send_message(cpg_handle_t h, void *buf, int len)
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
        return -1;
    }

    if (retries)
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"cpg_mcast_joined retried %d\n",
              retries);

    return 0;
}
