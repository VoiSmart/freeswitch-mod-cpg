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
#include "fsm.h"

#include <switch.h>
#include "cpg_utils.h"
#include "cpg_virtual_ip.h"

/*actions definitions*/
switch_status_t reaction(virtual_ip_t *vip);
switch_status_t go_down(virtual_ip_t *vip);
switch_status_t send_state(virtual_ip_t *vip);

switch_status_t noop(virtual_ip_t *vip);
switch_status_t error(virtual_ip_t *vip);
switch_status_t dup_warn(virtual_ip_t * vip);
switch_status_t dup_error(virtual_ip_t * vip);
switch_status_t M_rb_req(virtual_ip_t * vip);
switch_status_t RB_rb_req(virtual_ip_t * vip);

switch_status_t BLAH(virtual_ip_t * vip);

/*actions lookup table*/
action_t table[MAX_EVENTS][MAX_STATES] = {
/*ST_IDLE    ,ST_START    ,ST_BACKUP   ,ST_MASTER   ,ST_RBACK   */
{ BLAH       , error      , error      , error      , error      },/*EVT_START*/
{ error      , dup_error  , dup_warn   , dup_warn   , dup_warn   },/*EVT_DULICATE*/
{ error      , reaction   , reaction   , error      , error      },/*EVT_MASTER_DOWN*/
{ error      , BLAH       , error      , error      , error      },/*EVT_MASTER_UP*/
{ error      , noop       , noop       , noop       , noop       },/*EVT_BACKUP_DOWN*/
{ error      , BLAH       , BLAH       , BLAH       , BLAH       },/*EVT_BACKUP_UP*/
{ error      , error      , error      , M_rb_req   , RB_rb_req  },/*EVT_RBACK_REQ*/
{ error      , go_down    , go_down    , go_down    , go_down    } /*EVT_STOP*/

};

//TODO se sono in RBACK e il nodo che deve sostituirmi è andato giù non devo più spegnermi
// ST_RBACK EVT_BACKUP_DOWN

/*actions chooser*/
action_t fsm_do_transaction(event_t event, state_t state) {

    if (((event < 0) || (event >= MAX_EVENTS))
     || ((state < 0) || (state >= MAX_STATES))) {
        return error;
    }
    return table[state][event];

}

/*state getter*/
state_t fsm_get_state(virtual_ip_t *vip) {
    return vip->state;
}

/*##########################################################################*/
/*actions implementations*/

switch_status_t noop(virtual_ip_t * vip) {
    switch_log_printf(SWITCH_CHANNEL_LOG, 
                      SWITCH_LOG_INFO,"%s: NOOP\n", vip->address);
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t error(virtual_ip_t * vip) {
    switch_log_printf(SWITCH_CHANNEL_LOG,
                      SWITCH_LOG_ERROR,"%s: ERROR\n", vip->address);
    return SWITCH_STATUS_FALSE;
}

switch_status_t dup_warn(virtual_ip_t * vip){
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                      "%s: DUPLICATE NODE\n", vip->address);
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t dup_error(virtual_ip_t * vip){
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                      "%s: DUPLICATE NODE\n", vip->address);
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t Mrback_req(virtual_ip_t * vip){
    switch_status_t status = SWITCH_STATUS_FALSE;

    if (vip->node_list->nodeid != vip->node_id) {
        vip->rollback_node_id = vip->node_list->nodeid;
        //TODO status = launch_rollback_thread(vip)
        vip->state = ST_RBACK;
        status = SWITCH_STATUS_SUCCESS;
    }
    return status;
}

switch_status_t RBrback_req(virtual_ip_t * vip){
    switch_status_t status = SWITCH_STATUS_FALSE;

    if ((vip->node_list->nodeid != vip->node_id) &&
       (vip->node_list->nodeid != vip->rollback_node_id)) {

        vip->rollback_node_id = vip->node_list->nodeid;
        //TODO status = launch_rollback_thread(vip)
        status = SWITCH_STATUS_SUCCESS;
    }
    return status;
}

switch_status_t reaction(virtual_ip_t *vip)
{
    // if I'm the first in priority list
    if ( vip->node_list->nodeid == vip->node_id) {
        // become master
        vip->state = ST_MASTER;

        // set the ip to bind to
        if (utils_add_vip(vip->address, vip->device) != SWITCH_STATUS_SUCCESS) {
            goto error;
        }

        // gratuitous arp request
        if (utils_send_gARP(vip->mac, vip->address, vip->device) != SWITCH_STATUS_SUCCESS) {
            utils_remove_vip(vip->address, vip->device);
            goto error;
        }

        // and I say it to all other nodes
        virtual_ip_send_state(vip);

//TODO sofia recover
/*        // sofia recover!!!*/
/*        if (vip->autorecover == SWITCH_TRUE) {*/
/*            utils_recover(vip->name);*/
/*        }*/

//TODO se è cascato il master e non devo reagire mi ripulisco la tabella
/*    } else {*/
/*        // clean up the table*/
/*        char *sql;*/
/*        //FIXME sistemare la delete con il nome del profilo!*/
/*        sql = switch_mprintf("delete from sip_recovery where "*/
/*                     "runtime_uuid='%q' and profile_name='%q'",*/
/*                          vip->runtime_uuid, vip->address);*/

/*        utils_send_track_event(sql, vip->address);*/
/*        switch_safe_free(sql);*/
    }
    return SWITCH_STATUS_SUCCESS;

error:
    switch_log_printf(SWITCH_CHANNEL_LOG, 
                      SWITCH_LOG_ERROR,"Failed transition to MASTER!\n");
    //TODO chiamare shutdown direttamente?
    vip->state = ST_IDLE;
    return SWITCH_STATUS_FALSE;

}


switch_status_t send_state(virtual_ip_t *vip)
{
    return virtual_ip_send_state(vip);
}


switch_status_t godown(virtual_ip_t *vip)
{
    vip->state = ST_IDLE;
    vip->master_id = 0;
    vip->member_list_entries = 0;
    vip->running = SWITCH_FALSE;
    vip->members_number = 0;
    vip->node_id = 0;
    vip->master_id = 0;
    vip->rollback_node_id = 0;
//    node_t *node_list;remove all?

//    if profile_thread allora aspetta
//    if rollback_thread allora aspetta

    utils_remove_vip(vip->address, vip->device);

    //TODO stop sofia profile, I don't check errors
/*    utils_stop_sofia_profile(vip->name);*/

    return SWITCH_STATUS_SUCCESS;

}











/*switch_status_t from_standby_to_init(virtual_ip_t *vip)*/
/*{*/
/*    vip->state = INIT;*/

/*    // start sofia profile*/
/*    for (int i=0; i<3; i++) {*/
/*        switch_yield(100000);*/
/*        if (utils_start_sofia_profile(vip->name) != SWITCH_STATUS_SUCCESS) {*/
/*            goto error;*/
/*        }*/
/*    }*/

/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From STANDBY to INIT for %s!\n", vip->name);*/
/*    return SWITCH_STATUS_SUCCESS;*/

/*error:*/
/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to INIT!\n");*/
/*    vip->state = STANDBY;*/
/*    return SWITCH_STATUS_FALSE;*/

/*}*/

/*switch_status_t from_init_to_backup(virtual_ip_t *vip)*/
/*{*/
/*    vip->state = BACKUP;*/
/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From INIT to BACKUP for %s!\n", vip->name);*/
/*    return SWITCH_STATUS_SUCCESS;*/
/*}*/


/*switch_status_t from_master_to_standby(virtual_ip_t *vip)*/
/*{*/
/*    vip->state = STANDBY;*/
/*    vip->master_id = 0;*/
/*    vip->member_list_entries = 0;*/
/*    if (utils_remove_vip(vip->virtual_ip, vip->device) != SWITCH_STATUS_SUCCESS) {*/
/*        goto error;*/
/*    }*/

/*    // stop sofia profile*/
/*    if (utils_stop_sofia_profile(vip->name) != SWITCH_STATUS_SUCCESS){*/
/*        goto error;*/
/*    }*/

/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From MASTER to STANDBY for %s!\n", vip->name);*/
/*    return SWITCH_STATUS_SUCCESS;*/

/*error:*/
/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to STANDBY!\n");*/
/*    return SWITCH_STATUS_FALSE;*/
/*}*/

/*switch_status_t from_backup_to_standby(virtual_ip_t *vip)*/
/*{*/
/*    vip->state = STANDBY;*/
/*    vip->master_id = 0;*/
/*    vip->member_list_entries = 0;*/

/*    // stop sofia profile*/
/*    if (utils_stop_sofia_profile(vip->name) != SWITCH_STATUS_SUCCESS){*/
/*        goto error;*/
/*    }*/

/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From BACKUP to STANDBY for %s!\n", vip->name);*/
/*    return SWITCH_STATUS_SUCCESS;*/

/*error:*/
/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to STANDBY!\n");*/
/*    return SWITCH_STATUS_FALSE;*/
/*}*/

/*switch_status_t from_init_to_standby(virtual_ip_t *vip)*/
/*{*/
/*    vip->state = STANDBY;*/
/*    vip->master_id = 0;*/
/*    vip->member_list_entries = 0;*/

/*    // stop sofia profile*/
/*    if (utils_stop_sofia_profile(vip->name) != SWITCH_STATUS_SUCCESS){*/
/*        goto error;*/
/*    }*/

/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From INIT to STANDBY for %s!\n", vip->name);*/
/*    return SWITCH_STATUS_SUCCESS;*/

/*error:*/
/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to STANDBY!\n");*/
/*    return SWITCH_STATUS_FALSE;*/
/*}*/

