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

/*SWITCH_MODULE_RUNTIME_FUNCTION(mod_cpg_runtime);*/
/*Defines a switch_loadable_module_function_table_t and a static const char[] modname*/
SWITCH_MODULE_DEFINITION(mod_cpg, mod_cpg_load, mod_cpg_shutdown, NULL/*mod_cpg_runtime*/);


void event_handler(switch_event_t *event);
switch_status_t map_vip(switch_status_t (*vip_func)(virtual_ip_t *vip));
switch_status_t autostart_vip(virtual_ip_t *vip);

static const char *line1 = "========================================"
                           "========================================\n";
static const char *line2 = "----------------------------------------"
                           "----------------------------------------\n";

static switch_status_t
    list_vips(const char *line, const char *cursor,
              switch_console_callback_match_t **matches)
{
        switch_hash_index_t *hi;
        void *val;
        const void *vvar;
        switch_console_callback_match_t *my_matches = NULL;
        switch_status_t status = SWITCH_STATUS_FALSE;

/*TODO    switch_mutex_lock(mod_sofia_globals.hash_mutex);*/
        for (hi = switch_hash_first(NULL, globals.virtual_ip_hash);
             hi; hi = switch_hash_next(hi)) {

            switch_hash_this(hi, &vvar, NULL, &val);
            switch_console_push_match(&my_matches, (const char *) vvar);
        }
/*        switch_mutex_unlock(mod_sofia_globals.hash_mutex);*/

        if (my_matches) {
                *matches = my_matches;
                status = SWITCH_STATUS_SUCCESS;
        }


        return status;
}

switch_status_t cmd_status(switch_stream_handle_t *stream)
{
    switch_hash_index_t *hi;
    void *val;
    const void *vvar;
    virtual_ip_t *vip = NULL;


    stream->write_function(stream, "%25s\t%20s\t%10s\t\n%s",
                           "Virtual Ip", "Master Ip", "State", line1);

    for (hi = switch_hash_first(NULL, globals.virtual_ip_hash);
         hi; hi = switch_hash_next(hi)) {

        node_t *list;
        switch_hash_this(hi, &vvar, NULL, &val);
        vip = (virtual_ip_t *) val;
        list = vip->node_list;

        stream->write_function(stream, "%25s\t%20s\t%10s\t\n%s",
                               vip->config.address,
                               utils_node_pid_format(vip->master_id),
                               virtual_ip_get_state(vip), line2);
        if (!list) {
            stream->write_function(stream, line1);
            continue;
        }
        while (list) {
            stream->write_function(stream,"prio/node%16d\t%20s\n",
                                   list->priority,
                                   utils_node_pid_format(list->nodeid));
            list = list->next;
        }
        stream->write_function(stream, line2);

        for (int i = 0; i < MAX_SOFIA_PROFILES; i++) {
            if (!strcmp(vip->config.profiles[i].name,"")) break;
            stream->write_function(stream,"profile\t%17s\n",
                                   vip->config.profiles[i].name);
        }
        stream->write_function(stream, line2);

//TODO profili e rollback
/*        stream->write_function(stream, "\t%d active channels on this profile\n", utils_count_profile_channels(vip->address));*/
        if (vip->state == ST_RBACK) {
            stream->write_function(stream, line2);
            stream->write_function(stream,
                                "\tRollback timer started, migration to %s\n",
                                utils_node_pid_format(vip->rollback_node_id));
        }
        stream->write_function(stream, line1);

    }
    stream->write_function(stream, line1);

    return SWITCH_STATUS_SUCCESS;
}

switch_status_t cmd_vip(char **argv, int argc, switch_stream_handle_t *stream)
{

    virtual_ip_t *vip = NULL;
    char *address = argv[0];

    vip = find_virtual_ip(address);
    if (!vip) {
        stream->write_function(stream, "Invalid profile %s\n", argv[0]);
        return SWITCH_STATUS_SUCCESS;
    }

    if (argc != 2) {
        stream->write_function(stream, "Invalid Args!\n");
        return SWITCH_STATUS_SUCCESS;
    }

    if (!strcasecmp(argv[1], "start")) {

        if (!virtual_ip_start(vip)) {
            stream->write_function(stream, "starting %s\n", argv[0]);
        } else {
            stream->write_function(stream,
                                   "Profile %s already running\n", argv[0]);
        }
        return SWITCH_STATUS_SUCCESS;
    }

    if (!strcasecmp(argv[1], "stop")) {

        if (!virtual_ip_stop(vip)) {
            stream->write_function(stream, "stopping %s\n", argv[0]);
        } else {
            stream->write_function(stream,
                                   "Profile %s not running\n", argv[0]);
        }
        return SWITCH_STATUS_SUCCESS;
    }

    stream->write_function(stream, "-ERR Unknown command!\n");
    return SWITCH_STATUS_SUCCESS;
}


SWITCH_STANDARD_API(cpg_function)
{

    char *argv[1024] = { 0 };
    int argc = 0;
    char *mycmd = NULL;
    int lead = 1;
    const char *usage_string = "USAGE:\n"
        "----------------------------------------"
        "----------------------------------------\n"
        "cpg help\n"
        "cpg status\n"
        "cpg vip address start/stop\n"
        "----------------------------------------"
        "----------------------------------------\n";
    switch_status_t status = SWITCH_STATUS_SUCCESS;

    if (zstr(cmd)) {
        stream->write_function(stream, "%s", usage_string);
        goto done;
    }
    if (!(mycmd = strdup(cmd))) {
        status = SWITCH_STATUS_MEMERR;
        goto done;
    }
    if (!(argc = switch_separate_string(mycmd, ' ', argv,
       (sizeof(argv) / sizeof(argv[0])))) || !argv[0]) {

        stream->write_function(stream, "%s", usage_string);
        goto done;
    }
    if (!strcasecmp(argv[0], "status")) {
        status = cmd_status(stream);
        goto done;
    }
    if (!strcasecmp(argv[0], "vip")) {
        if (!zstr(argv[1])) {
            status = cmd_vip(&argv[lead],argc - lead,stream);
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

    switch_core_hash_init(&globals.virtual_ip_hash, globals.pool);

    if (do_config("cpg.conf") != SWITCH_STATUS_SUCCESS) {
        return SWITCH_STATUS_TERM;
    }

    if (switch_event_bind_removable(modname, SWITCH_EVENT_CUSTOM,
                                    "sofia::recovery_send",
                                    event_handler, NULL,
                                    &globals.node) !=
                                    SWITCH_STATUS_SUCCESS) {

        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_ERROR, "Couldn't bind!\n");
        return SWITCH_STATUS_TERM;
    }

    globals.running = SWITCH_TRUE;

    map_vip(autostart_vip);

    SWITCH_ADD_API(api_interface, "cpg", "cpg API", cpg_function, "syntax");
    switch_console_set_complete("add cpg help");
    switch_console_set_complete("add cpg status");
    switch_console_set_complete("add cpg vip");
    switch_console_set_complete("add cpg vip ::cpg::list_vips start");
    switch_console_set_complete("add cpg vip ::cpg::list_vips stop");

    switch_console_add_complete_func("::cpg::list_vips", list_vips);

    /* indicate that the module should continue to be loaded */
    return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_cpg_shutdown)
{
    switch_console_del_complete_func("::cpg::list_vips");
    switch_console_set_complete("del cpg");

    map_vip(virtual_ip_stop);
    globals.running = SWITCH_FALSE;
    switch_event_unbind(&globals.node);
    switch_core_hash_destroy(&globals.virtual_ip_hash);

    return SWITCH_STATUS_SUCCESS;
}

void event_handler(switch_event_t *event)
{
    char *sql = NULL;
    char *profile_name = NULL;
    short int pindex = -1;

    switch_assert(event);        // Just a sanity check

    if ((sql = switch_event_get_header_nil(event, "sql"))
     && (profile_name = switch_event_get_header_nil(event, "profile_name"))) {
        virtual_ip_t *vip;

        if ((vip = find_virtual_ip_from_profile(profile_name))) {
            if ((pindex = virtual_ip_profile_index(vip, profile_name)) >= 0) {

                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                                  "%s recovery_send event\n",
                                  profile_name);
                virtual_ip_send_sql(vip, pindex, sql);
printf("%s\n",sql);
printf("%d\n", pindex);
            }
        } else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                              "Profile not found!\n");
        }

    }

    return;
}

/*functional helper*/
switch_status_t map_vip(switch_status_t (*vip_func)(virtual_ip_t *vip))
{
    switch_hash_index_t *hi;
    void *val;
    const void *vvar;
    virtual_ip_t *vip = NULL;

    for (hi = switch_hash_first(NULL, globals.virtual_ip_hash);
         hi; hi = switch_hash_next(hi)) {

        switch_hash_this(hi, &vvar, NULL, &val);
        vip = (virtual_ip_t *) val;
        (*vip_func)(vip);
    }
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t autostart_vip(virtual_ip_t *vip)
{
    if (vip->config.autoload == SWITCH_TRUE) {
        virtual_ip_start(vip);
    }
    return SWITCH_STATUS_SUCCESS;
}

/*SWITCH_MODULE_RUNTIME_FUNCTION(mod_cpg_runtime)*/
/*{*/
/*    char cmd[128];*/
/*TODO runtime per ping*/
/*    switch_snprintf(cmd,sizeof(cmd), "%s/bin/arbiter.sh", SWITCH_GLOBAL_dirs.base_dir);*/
/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Arbiter path: %s\n", cmd);*/
/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Runtime Started\n");*/
/*    globals.is_connected = SWITCH_TRUE;*/

/*    while(globals.running) {*/

/*        if (system(cmd) != 0) {*/
/*            globals.is_connected = SWITCH_FALSE;*/
/*            stop_virtual_ips();*/
/*        } else { //è andato a buon fine*/
/*            if (globals.is_connected == SWITCH_FALSE) { //se ero standby divento init*/
/*                globals.is_connected = SWITCH_TRUE;*/
/*                start_profiles();*/
/*            }*/
/*        }*/

/*        switch_yield(5000000);*/

/*    }*/
/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Runtime terminated\n");*/
/*    return SWITCH_STATUS_TERM;*/
/*}*/
