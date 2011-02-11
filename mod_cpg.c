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

#include "cpg_utils.h"
#include "cpg_config.h"
#include "cpg_virtual_ip.h"




/* Prototypes */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_cpg_shutdown);

SWITCH_MODULE_LOAD_FUNCTION(mod_cpg_load);

SWITCH_MODULE_RUNTIME_FUNCTION(mod_cpg_runtime);
/*Defines a switch_loadable_module_function_table_t and a static const char[] modname*/
SWITCH_MODULE_DEFINITION(mod_cpg, mod_cpg_load, mod_cpg_shutdown, mod_cpg_runtime);


void event_handler(switch_event_t *event);
switch_status_t start_virtual_ips();
switch_status_t stop_virtual_ips();


switch_status_t cmd_status(switch_stream_handle_t *stream)
{
    switch_hash_index_t *hi;
    void *val;
    const void *vvar;
    virtual_ip_t *vip = NULL;
    const char *line = "=================================================================================================";
    const char *line2 = "-------------------------------------------------------------------------------------------------";

    stream->write_function(stream, "%25s\t  %20s\t  %10s\t \n",
                                                         "Name", "Ip", "State");
    stream->write_function(stream, "%s\n", line);
    for (hi = switch_hash_first(NULL, globals.profile_hash); hi;
                                                    hi = switch_hash_next(hi)) {
        node_t *list;
        switch_hash_this(hi, &vvar, NULL, &val);
        vip = (virtual_ip_t *) val;
        list = vip->node_list;

        stream->write_function(stream, "%25s\t  %20s\t  %10s\t is%s running\n",
                                             vip->address, vip->address,
                                          utils_state_to_string(vip->state),
                                                    vip->running?"":" not");
        stream->write_function(stream, "%s\n", line2);
        stream->write_function(stream,"\tMy master is %s\n",
                                     utils_node_pid_format(vip->master_id));
        stream->write_function(stream, "%s\n", line2);
        if (list == NULL)
            stream->write_function(stream,"\tEmpty list\n");
        while (list != NULL) {
            stream->write_function(stream,"\t%s priority %d\n",
                           utils_node_pid_format(list->nodeid), list->priority);
            list = list->next;
        }
        stream->write_function(stream, "%s\n", line2);
        stream->write_function(stream, "\t%d active channels on this profile\n", utils_count_profile_channels(vip->address));
        if (vip->rollback_node_id != 0) {
            stream->write_function(stream, "%s\n", line2);
            stream->write_function(stream,
                                  "\tRollback timer started, migration to %s\n",
                              utils_node_pid_format(vip->rollback_node_id));
        }
        stream->write_function(stream, "%s\n", line);

    }
    stream->write_function(stream, "%s\n", line);

    return SWITCH_STATUS_SUCCESS;
}

switch_status_t start_profiles()
{
    switch_hash_index_t *hi;
    void *val;
    const void *vvar;
    virtual_ip_t *vip = NULL;

    for (hi = switch_hash_first(NULL, globals.profile_hash); hi;
                                                      hi = switch_hash_next(hi))
    {

        switch_hash_this(hi, &vvar, NULL, &val);
        vip = (virtual_ip_t *) val;
        if (vip->autoload == SWITCH_TRUE) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                                                  "launch %s\n", vip->address);
            virtual_ip_start(vip);
        }
    }
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t stop_virtual_ips()
{
    switch_hash_index_t *hi;
    void *val;
    const void *vvar;
    virtual_ip_t *vip = NULL;
    switch_status_t status;

    for (hi = switch_hash_first(NULL, globals.profile_hash); hi;
                                                      hi = switch_hash_next(hi))
    {

        switch_hash_this(hi, &vvar, NULL, &val);
        vip = (virtual_ip_t *) val;
        if (vip->running) {
            int result;
            vip->running = 0;
            switch_thread_join(&status, vip->virtual_ip_thread);

            virtual_ip_stop(vip);

            result = cpg_leave(vip->handle, &vip->group_name);
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                                 "Leave  result is %d (should be 1)\n", result);
            switch_yield(10000);
            result = cpg_finalize (vip->handle);
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                              "Finalize  result is %d (should be 1)\n", result);
        }
    }
    return SWITCH_STATUS_SUCCESS;
}


switch_status_t cmd_profile(char **argv, int argc,switch_stream_handle_t *stream)
{

    virtual_ip_t *vip = NULL;
    char *address = argv[0];

    vip = find_virtual_ip(address);

    if (argc != 2) {
        stream->write_function(stream, "Invalid Args!\n");
        return SWITCH_STATUS_SUCCESS;
    }

    if (!strcasecmp(argv[1], "start")) {

        if (vip != NULL) {
            if (!vip->running) {

                virtual_ip_start(vip);
                //FIXME okkio che non voglio esporre sti flag.. 
                //TODO cmq dovrei separare quelli di runtime da quelli di conf
                vip->autoload = SWITCH_TRUE;
                /*se lo accendo metto l'autoload così riparte se c'è una riconnessione*/
                stream->write_function(stream, "starting %s\n", argv[0]);
            } else {
                stream->write_function(stream,
                                       "Profile %s already running\n", argv[0]);
            }
        } else {
            stream->write_function(stream,
                             "Failure starting %s, invalid profile\n", argv[0]);
        }
        goto done;
    }
    if (!strcasecmp(argv[1], "stop")) {

        if (vip != NULL) {
            if (vip->running) {
                stream->write_function(stream, "stopping %s\n", argv[0]);

                stop_virtual_ips();
            } else {
                stream->write_function(stream,
                                           "Profile %s not running\n", argv[0]);
            }
        } else {
            stream->write_function(stream,
                             "Failure stopping %s, invalid profile\n", argv[0]);
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
    *module_interface =
                  switch_loadable_module_create_module_interface(pool, modname);

    globals.pool = pool;

    switch_core_hash_init(&globals.profile_hash, globals.pool);

    if (do_config("cpg.conf") != SWITCH_STATUS_SUCCESS) {
        return SWITCH_STATUS_TERM;
    }

    if (switch_event_bind_removable(modname, SWITCH_EVENT_CUSTOM,
                                    "sofia::recovery_send",
                                    event_handler, NULL,
                                    &globals.node) != SWITCH_STATUS_SUCCESS) {

        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                                            "Couldn't bind!\n");
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

    stop_virtual_ips();
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
    virtual_ip_t *vip = NULL;

    for (hi = switch_hash_first(NULL, globals.profile_hash); hi; hi = switch_hash_next(hi)) {

        switch_hash_this(hi, &vvar, NULL, &val);
        vip = (virtual_ip_t *) val;
        if (vip->running) {

            virtual_ip_send_state(vip);

        }
    }
    return SWITCH_STATUS_SUCCESS;
}

void event_handler(switch_event_t *event)
{
    char *sql = NULL;
    char *address = NULL;

    switch_assert(event);        // Just a sanity check

    if ((sql = switch_event_get_header_nil(event, "sql")) && (address = switch_event_get_header_nil(event, "profile_name"))) {
        virtual_ip_t *vip;

        if ((vip = find_virtual_ip(address))) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "%s recovery_send event\n",vip->address);
            virtual_ip_send_sql(vip,sql);
        } else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Profile not found!\n");
        }

    }

    return;
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
            stop_virtual_ips();
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
