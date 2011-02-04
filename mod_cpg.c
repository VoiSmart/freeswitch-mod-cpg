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
#include "mod_cpg.h"

#include "profile.h"
#include "cpg_utils.h"

typedef struct {
    int priority;
    profile_state_t state;
    char runtime_uuid[40];
} node_msg_t;

typedef enum {
    SQL,
    NODE_STATE
} msg_type_t;

struct header {
    msg_type_t type;
    int len;
};
typedef struct header header_t;

/* Prototypes */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_cpg_shutdown);

SWITCH_MODULE_LOAD_FUNCTION(mod_cpg_load);

SWITCH_MODULE_RUNTIME_FUNCTION(mod_cpg_runtime);
/*Defines a switch_loadable_module_function_table_t and a static const char[] modname*/
SWITCH_MODULE_DEFINITION(mod_cpg, mod_cpg_load, mod_cpg_shutdown, mod_cpg_runtime);

void *SWITCH_THREAD_FUNC profile_thread_run(switch_thread_t *thread, void *obj);
void *SWITCH_THREAD_FUNC rollback_thread_run(switch_thread_t *thread, void *obj);
void launch_profile_thread(profile_t *profile);
static switch_status_t send_state(cpg_handle_t h, profile_t *profile);
static switch_status_t send_sql(cpg_handle_t h, char *sql);
static int send_message(cpg_handle_t h, void *buf, int len);
static char *node_pid_format(unsigned int nodeid);
void event_handler(switch_event_t *event);
switch_status_t start_profiles();
switch_status_t stop_profiles();

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

static switch_status_t do_config()
{
    char *cf = "cpg.conf";
    switch_xml_t cfg, xml, xprofile,param,settings;
    profile_t *profile;

    switch_status_t status = SWITCH_STATUS_SUCCESS;

    if (!(xml = switch_xml_open_cfg(cf, &cfg, NULL))) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Open of %s failed\n", cf);
        return SWITCH_STATUS_TERM;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "%s opened\n", cf);

    if ((settings = switch_xml_child(cfg, "global_settings"))) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "global settings\n");
        for (param = switch_xml_child(settings, "param"); param; param = param->next) {
                char *var = (char *) switch_xml_attr_soft(param, "name");
                char *val = (char *) switch_xml_attr_soft(param, "value");
                if (!strcmp(var, "group")) {
                    switch_snprintf(globals.group_name.value,255,"%s",val);
                    globals.group_name.length = strlen(val);
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "group = %s\n", globals.group_name.value);
                }
            }


        for (xprofile= switch_xml_child(settings,"profile"); xprofile; xprofile = xprofile->next) {

            char *name;
            if (!(profile = (profile_t *) switch_core_alloc(globals.pool, sizeof(profile_t)))) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Memory Error!\n");
                return SWITCH_STATUS_FALSE;
            }
            profile->state = STANDBY;
            name = (char *) switch_xml_attr_soft(xprofile, "name");
            switch_snprintf(profile->name,255,"%s",name);
            switch_snprintf(profile->group_name.value,255,"%s",name);
            profile->group_name.length = strlen(name);
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "new profile %s\n",profile->name);

            for (param = switch_xml_child(xprofile, "param"); param; param = param->next) {
                char *var = (char *) switch_xml_attr_soft(param, "name");
                char *val = (char *) switch_xml_attr_soft(param, "value");

                if (!strcmp(var, "virtual-ip")) {
                    unsigned char buf[sizeof(struct in6_addr)];
                    int s = inet_pton(AF_INET, val, buf);
                    if (s <= 0) {
                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                            "Profile %s: Virtual ip is not valid\n", profile->name);
                        status = SWITCH_STATUS_FALSE;
                        goto out;
                    }
                    switch_snprintf(profile->virtual_ip,16,"%s",val);
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                                             "vip = %s\n", profile->virtual_ip);

                } else if (!strcmp(var, "device")) {
                    char *mac;
                    switch_snprintf(profile->device,6,"%s",val);
                    //get local mac address
                    mac = utils_get_mac_addr(profile->device);
                    
                    if (profile->device == NULL || mac == NULL) {
                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                            "Profile %s: Interface is not valid\n", profile->name);
                        status = SWITCH_STATUS_FALSE;
                        goto out;
                    }
                    strcpy(profile->mac,mac);
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                                              "device = %s\n", profile->device);
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                                                    "mac = %s\n", profile->mac);

                } else if (!strcmp(var, "autoload")) {
                    profile->autoload = SWITCH_FALSE;
                    if (!strcmp(val, "true")) {
                        profile->autoload = SWITCH_TRUE;
                    }
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Autoload = %s\n",
                                      profile->autoload == SWITCH_TRUE?"true":"false" );
                } else if (!strcmp(var, "autorecover")) {
                    profile->autorecover = SWITCH_FALSE;
                    if (!strcmp(val, "true")) {
                        profile->autorecover = SWITCH_TRUE;
                    }
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Autorecover = %s\n",
                                      profile->autorecover == SWITCH_TRUE?"true":"false" );
                } else if (!strcmp(var, "autorollback")) {
                    profile->autorollback = SWITCH_FALSE;
                    if (!strcmp(val, "true")) {
                        profile->autorollback = SWITCH_TRUE;
                    }
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Autorollback = %s\n",
                                      profile->autorollback == SWITCH_TRUE?"true":"false" );
                } else if (!strcmp(var, "rollback-delay")) {
                    profile->rollback_delay = atoi(val);
                    if ( profile->rollback_delay == 0) {
                        profile->rollback_delay = 1;
                    }
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Rollback delay = %d\n",
                                      profile->rollback_delay);
                } else if (!strcmp(var, "priority")) {
                    profile->priority = atoi(val);
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Priority = %d\n", profile->priority);
                }
            }
            status = switch_core_hash_insert(globals.profile_hash, profile->name, profile);
            if (utils_profile_control(profile->name) != SWITCH_STATUS_SUCCESS) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "profile %s doesn't exist in sip_profiles directory, before to do anything create it and reloadxml!\n", profile->name);
            }
        }
    }
out:
    switch_xml_free(xml);

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
                                      profile->name, node_pid_format(local_id));

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
               (unsigned long int) msg_len, node_pid_format(nodeid));
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

switch_status_t cmd_status(switch_stream_handle_t *stream)
{
    switch_hash_index_t *hi;
    void *val;
    const void *vvar;
    profile_t *profile = NULL;
    const char *line = "=================================================================================================";
    const char *line2 = "-------------------------------------------------------------------------------------------------";

    stream->write_function(stream, "%25s\t  %20s\t  %10s\t \n", "Name", "Ip", "State");
    stream->write_function(stream, "%s\n", line);
    for (hi = switch_hash_first(NULL, globals.profile_hash); hi; hi = switch_hash_next(hi)) {
        node_t *list;
        switch_hash_this(hi, &vvar, NULL, &val);
        profile = (profile_t *) val;
        list = profile->node_list;

        stream->write_function(stream, "%25s\t  %20s\t  %10s\t is%s running\n", profile->name, profile->virtual_ip, utils_state_to_string(profile->state), profile->running?"":" not");
        stream->write_function(stream, "%s\n", line2);
        stream->write_function(stream,"\tMy master is %s\n", node_pid_format(profile->master_id));
        stream->write_function(stream, "%s\n", line2);
        if (list == NULL)
            stream->write_function(stream,"\tEmpty list\n");
        while (list != NULL) {
            stream->write_function(stream,"\t%s priority %d\n",node_pid_format(list->nodeid), list->priority);
            list = list->next;
        }
        stream->write_function(stream, "%s\n", line2);
        stream->write_function(stream, "\t%d active channels on this profile\n", utils_count_profile_channels(profile->name));
        if (profile->rollback_node_id != 0) {
            stream->write_function(stream, "%s\n", line2);
            stream->write_function(stream,"\tRollback timer started, migration to %s\n", node_pid_format(profile->rollback_node_id));
        }
        stream->write_function(stream, "%s\n", line);
        //stream->write_function(stream, "lista nodi %d membri lista%d\n",list_entries(profile->node_list), profile->member_list_entries);

    }
    stream->write_function(stream, "%s\n", line);

    return SWITCH_STATUS_SUCCESS;
}

switch_status_t start_profiles()
{
    switch_hash_index_t *hi;
    void *val;
    const void *vvar;
    profile_t *profile = NULL;

    for (hi = switch_hash_first(NULL, globals.profile_hash); hi; hi = switch_hash_next(hi))
    {

        switch_hash_this(hi, &vvar, NULL, &val);
        profile = (profile_t *) val;
        if (profile->autoload == SWITCH_TRUE) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"launch %s\n", profile->name);
            launch_profile_thread(profile);
        }
    }
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t stop_profiles()
{
    switch_hash_index_t *hi;
    void *val;
    const void *vvar;
    profile_t *profile = NULL;
    switch_status_t status;

    for (hi = switch_hash_first(NULL, globals.profile_hash); hi; hi = switch_hash_next(hi))
    {

        switch_hash_this(hi, &vvar, NULL, &val);
        profile = (profile_t *) val;
        if (profile->running) {
            int result;
            profile->running = 0;
            switch_thread_join(&status, profile->profile_thread);
            switch(profile->state) {
                case MASTER:
                    from_master_to_standby(profile);
                    break;
                case BACKUP:
                    from_backup_to_standby(profile);
                    break;
                case INIT:
                    from_init_to_standby(profile);
                    break;
                case STANDBY:
                    break;
            }
            result = cpg_leave(profile->handle, &profile->group_name);
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"Leave  result is %d (should be 1)\n", result);
            switch_yield(10000);
            result = cpg_finalize (profile->handle);
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"Finalize  result is %d (should be 1)\n", result);
            printf("thread finito\n");
        }
    }
    return SWITCH_STATUS_SUCCESS;
}
switch_status_t cmd_profile(char **argv, int argc,switch_stream_handle_t *stream)
{

    profile_t *profile = NULL;
    char *profile_name = argv[0];

    profile = find_profile_by_name(profile_name);

    if (argc != 2) {
        stream->write_function(stream, "Invalid Args!\n");
        return SWITCH_STATUS_SUCCESS;
    }

    if (!strcasecmp(argv[1], "start")) {

        if (profile != NULL) {
            if (!profile->running) {

                launch_profile_thread(profile);
                profile->autoload = SWITCH_TRUE;
                /*se lo accendo metto l'autoload così riparte se c'è una riconnessione*/
                stream->write_function(stream, "starting %s\n", argv[0]);
            } else {
                stream->write_function(stream, "Profile %s already running\n", argv[0]);
            }
        } else {
            stream->write_function(stream, "Failure starting %s, invalid profile\n", argv[0]);
        }
        goto done;
    }
    if (!strcasecmp(argv[1], "stop")) {

        if (profile != NULL) {
            if (profile->running) {
                int result;
                switch_status_t status;
                stream->write_function(stream, "stopping %s\n", argv[0]);
                profile->autoload = SWITCH_FALSE;
                /*se lo spengo tolgo l'autoload altrimenti riparte se c'è una riconnessione'*/

                profile->running = 0;
                switch_thread_join(&status, profile->profile_thread);

                switch(profile->state) {
                    case MASTER:
                        from_master_to_standby(profile);
                        break;
                    case BACKUP:
                        from_backup_to_standby(profile);
                        break;
                    case INIT:
                        from_init_to_standby(profile);
                        break;
                    case STANDBY:
                        break;
                }

                result = cpg_leave(profile->handle, &profile->group_name);
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"Leave  result is %d (should be 1)\n", result);
                switch_yield(10000);
                result = cpg_finalize (profile->handle);
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"Finalize  result is %d (should be 1)\n", result);
            } else {
                stream->write_function(stream, "Profile %s not running\n", argv[0]);
            }
        } else {
            stream->write_function(stream, "Failure stopping %s, invalid profile\n", argv[0]);
        }
        goto done;
    }

    stream->write_function(stream, "-ERR Unknown command!\n");

    done:
        return SWITCH_STATUS_SUCCESS;
}
SWITCH_STANDARD_API(cpg_function)
{

    char *argv[1024] = { 0 };
    int argc = 0;
    char *mycmd = NULL;
    int lead = 1;
    const char *usage_string = "USAGE:\n"
        "--------------------------------------------------------------------------------\n"
        "cpg help\n"
        "cpg status\n"
        "cpg profile profile_name start/stop\n"
        "--------------------------------------------------------------------------------\n";
    switch_status_t status = SWITCH_STATUS_SUCCESS;

    if (zstr(cmd)) {
        stream->write_function(stream, "%s", usage_string);
        goto done;
    }
    if (!(mycmd = strdup(cmd))) {
        status = SWITCH_STATUS_MEMERR;
        goto done;
    }
    if (!(argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])))) || !argv[0]) {
        stream->write_function(stream, "%s", usage_string);
        goto done;
    }
    if (!strcasecmp(argv[0], "status")) {
        status = cmd_status(stream);
        goto done;
    }
    if (!strcasecmp(argv[0], "profile")) {
        if (!zstr(argv[1])) {
            status = cmd_profile(&argv[lead],argc - lead,stream);
        } else {
            stream->write_function(stream, "%s", usage_string);
        }
    } else if (!strcasecmp(argv[0], "help")) {
        stream->write_function(stream, "%s", usage_string);
        goto done;
    } else {
        stream->write_function(stream, "Unknown Command [%s]\n", argv[0]);
    }

    done:
        switch_safe_free(mycmd);
        return status;

}

SWITCH_MODULE_LOAD_FUNCTION(mod_cpg_load)
{

    switch_api_interface_t *api_interface;

    memset(&globals, 0, sizeof(globals));

    /* connect my internal structure to the blank pointer passed to me */
    *module_interface = switch_loadable_module_create_module_interface(pool, modname);

    globals.pool = pool;

    switch_core_hash_init(&globals.profile_hash, globals.pool);

    if (do_config() != SWITCH_STATUS_SUCCESS) {
        return SWITCH_STATUS_TERM;
    }

    if (switch_event_bind_removable(modname, SWITCH_EVENT_CUSTOM, "sofia::recovery_send", event_handler, NULL, &globals.node) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't bind!\n");
        return SWITCH_STATUS_TERM;
    }

    globals.running = 1;

    start_profiles();

    SWITCH_ADD_API(api_interface, "cpg", "cpg API", cpg_function, "syntax");
    switch_console_set_complete("add cpg help");
    switch_console_set_complete("add cpg status");
    switch_console_set_complete("add cpg profile");

    /* indicate that the module should continue to be loaded */
    return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_cpg_shutdown)
{

    switch_console_set_complete("del cpg");

    stop_profiles();
    globals.running = 0;
    switch_event_unbind(&globals.node);
    switch_core_hash_destroy(&globals.profile_hash);

    return SWITCH_STATUS_SUCCESS;
}

switch_status_t profiles_state_notification()
{
    switch_hash_index_t *hi;
    void *val;
    const void *vvar;
    profile_t *profile = NULL;

    for (hi = switch_hash_first(NULL, globals.profile_hash); hi; hi = switch_hash_next(hi)) {

        switch_hash_this(hi, &vvar, NULL, &val);
        profile = (profile_t *) val;
        if (profile->running) {

            send_state(profile->handle, profile);

        }
    }
    return SWITCH_STATUS_SUCCESS;
}

void event_handler(switch_event_t *event)
{
    char *sql = NULL;
    char *profile_name = NULL;

    switch_assert(event);        // Just a sanity check

    if ((sql = switch_event_get_header_nil(event, "sql")) && (profile_name = switch_event_get_header_nil(event, "profile_name"))) {
        profile_t *profile;

        if ((profile = find_profile_by_name(profile_name))) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "%s recovery_send event\n",profile->name);
            send_sql(profile->handle,sql);
        } else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Profilo non trovato!\n");
        }

    }

    return;
}

static switch_status_t send_sql(cpg_handle_t h, char *sql)
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
static switch_status_t send_state(cpg_handle_t h, profile_t *profile)
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

static char * node_pid_format(unsigned int nodeid) {
    static char buffer[100];
    struct in_addr saddr;
#if __BYTE_ORDER == __BIG_ENDIAN
    saddr.s_addr = swab32(nodeid);
#else
    saddr.s_addr = nodeid;
#endif
    sprintf(buffer, "node %s", inet_ntoa(saddr));

    return buffer;
}

SWITCH_MODULE_RUNTIME_FUNCTION(mod_cpg_runtime)
{
    char cmd[128];

    switch_snprintf(cmd,sizeof(cmd), "%s/bin/arbiter.sh", SWITCH_GLOBAL_dirs.base_dir);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Arbiter path: %s\n", cmd);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Runtime Started\n");
    globals.is_connected = SWITCH_TRUE;

    while(globals.running) {

        if (system(cmd) != 0) {
            globals.is_connected = SWITCH_FALSE;
            stop_profiles();
        } else { //è andato a buon fine
            if (globals.is_connected == SWITCH_FALSE) { //se ero standby divento init
                globals.is_connected = SWITCH_TRUE;
                start_profiles();
            }
        }

        switch_yield(5000000);

    }
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Runtime terminated\n");
    return SWITCH_STATUS_TERM;
}
