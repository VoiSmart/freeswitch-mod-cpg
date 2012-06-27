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

#include "cpg_utils.h"
#include "cpg_virtual_ip.h"
#include "mod_cpg.h"

/*actions definitions*/
switch_status_t act(virtual_ip_t *vip);
switch_status_t react(virtual_ip_t *vip);
switch_status_t go_down(virtual_ip_t *vip);
switch_status_t go_up(virtual_ip_t *vip);
switch_status_t observe(virtual_ip_t *vip);
switch_status_t rollback(virtual_ip_t * vip);
switch_status_t rback_stop(virtual_ip_t *vip);
switch_status_t rback_check(virtual_ip_t *vip);
switch_status_t backupup(virtual_ip_t * vip);
switch_status_t nodeup(virtual_ip_t * vip);

switch_status_t noop(virtual_ip_t *vip);
switch_status_t error(virtual_ip_t *vip);
switch_status_t dup_warn(virtual_ip_t * vip);

/*
 * Appena parto sono IDLE e accetto solo un evento di START con il quale faccio partire il thread
 * che gestisce il singolo virtual_ip.
 * Se sono in START:
 * - divento BACKUP solo quando tutti gli altri mi hanno dato le loro info di stato.
 * - se trovo un altro nodo con il mio profilo e la mia stessa priorità mi spengo.
 * - se sono solo divento direttamente MASTER
 * - se un altro si accendo mi pubblicizzo
 * - se ricevo una richiesta di rollback c'è un errore(non ho ancora le chiamate)
 * - se ricevo STOP mi fermo
 * 
 * Se sono MASTER:
 * - se ho un nuovo backup che si alza, allora gli mando il mio stato
 * - se ricevo una richiesta di rollback mi preparo a mollare le chiamate in corso e l'ip virtuale
 * 
 * Se sono BACKUP:
 * - se il master va giù guardo se tocca a me reagire e, nel caso reagisco prendendomi ip e chiamate
 * - EVT_MASTER_UP forse non è un nome corretto ma di sicuro non mi può arrivare
 * - Se ricevo uno stato di un nodo mi pubblicizzo anche io
 * 
 * Se sono ROLLBACK:
 * - e cade un nodo BACKUP allora rimango master
 * - se ricevo una nuova richiesta di rollback blocco il rollback che sta avvenendo
 *
 */

/*actions lookup table*/
action_t table[MAX_EVENTS][MAX_STATES] = {
/*ST_IDLE    , ST_START   , ST_BACKUP  , ST_MASTER  , ST_RBACK  */
{ go_up      , error      , error      , error      , error      },/*EVT_START*/
{ error      , go_down    , dup_warn   , dup_warn   , dup_warn   },/*EVT_DUPLICATE*/
{ error      , act        , react      , error      , error      },/*EVT_MASTER_DOWN*/
{ error      , observe    , error      , error      , error      },/*EVT_MASTER_UP*/
{ error      , noop       , noop       , noop       , rback_stop },/*EVT_BACKUP_DOWN*/
{ error      , nodeup     , nodeup     , backupup   , backupup   },/*EVT_BACKUP_UP*/
{ error      , error      , error      , rollback   , rback_stop },/*EVT_RBACK_REQ*/
{ error      , go_down    , go_down    , go_down    , go_down    } /*EVT_STOP*/

};

/*actions chooser*/
switch_status_t fsm_do_transaction(virtual_ip_t *vip, event_t event) {

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                      "virtual_ip= %s, state= %s, event= %s\n",
                      vip->config.address, state_to_string(vip->state),
                      event_to_string(event));

    if (((event < 0) || (event >= MAX_EVENTS))
     || ((vip->state < 0) || (vip->state >= MAX_STATES))) {
        return SWITCH_STATUS_FALSE;
    }
    return table[event][vip->state] (vip);

}

/*state getter*/
state_t fsm_get_state(virtual_ip_t *vip) {
    return vip->state;
}

/*##########################################################################*/
/*actions implementations*/

switch_status_t noop(virtual_ip_t * vip) {
    switch_log_printf(SWITCH_CHANNEL_LOG,
                      SWITCH_LOG_INFO,"%s: NOOP\n", vip->config.address);
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t error(virtual_ip_t * vip) {
    return SWITCH_STATUS_FALSE;
}

switch_status_t dup_warn(virtual_ip_t * vip){
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                      "%s: duplicated node\n", vip->config.address);
    return SWITCH_STATUS_SUCCESS;
}


/*
 * pubblico il mio stato e quindi segnalo la mia presenza
 */
switch_status_t nodeup(virtual_ip_t * vip)
{
    return virtual_ip_send_state(vip);
}
/*
 * pubblico il mio stato e invio l'elenco delle chiamate presenti nel cluster
 * nel mio profilo
 */
switch_status_t backupup(virtual_ip_t * vip)
{
    switch_status_t status = SWITCH_STATUS_SUCCESS;

    status = virtual_ip_send_state(vip);

    return virtual_ip_send_all_sql(vip) && status;
}

switch_status_t rollback(virtual_ip_t * vip)
{
    switch_status_t status = SWITCH_STATUS_SUCCESS;

    if (vip->node_list->nodeid != vip->node_id) {
        switch_threadattr_t *thd_attr = NULL;

        vip->rollback_node_id = vip->node_list->nodeid;
        vip->state = ST_RBACK;

        switch_threadattr_create(&thd_attr, globals.pool);
        switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
        switch_threadattr_priority_increase(thd_attr);
        status = switch_thread_create(&(vip->rollback_thread), thd_attr,
                                      rollback_thread, vip, globals.pool);

    }
    return status;
}

switch_status_t act(virtual_ip_t *vip)
{
    // if I'm the first in priority list
    if ( vip->node_id == node_first(vip->node_list)) {
        // become master
        vip->state = ST_MASTER;

        // set the ip to bind to
        if (utils_add_vip(vip->config.address,
                          vip->config.device) != SWITCH_STATUS_SUCCESS) {
            goto error;
        }

        // gratuitous arp request
        if (utils_send_gARP(vip->config.mac,
                            vip->config.address,
                            vip->config.device) != SWITCH_STATUS_SUCCESS) {

            utils_remove_vip(vip->config.address, vip->config.device);
            goto error;
        }

        // and I say it to all other nodes
        virtual_ip_send_state(vip);
        return SWITCH_STATUS_SUCCESS;
    }

error:
    switch_log_printf(SWITCH_CHANNEL_LOG,
                      SWITCH_LOG_ERROR,"Failed transition to MASTER!\n");
    go_down(vip);
    return SWITCH_STATUS_FALSE;

}

switch_status_t react(virtual_ip_t *vip)
{
    // if I'm the first in priority list
    if ( vip->node_id == node_first(vip->node_list)) {
        // become master
        vip->state = ST_MASTER;

        // set the ip to bind to
        if (utils_add_vip(vip->config.address,
                          vip->config.device) != SWITCH_STATUS_SUCCESS) {
            goto error;
        }

        // gratuitous arp request
        if (utils_send_gARP(vip->config.mac,
                            vip->config.address,
                            vip->config.device) != SWITCH_STATUS_SUCCESS) {
            utils_remove_vip(vip->config.address, vip->config.device);
            goto error;
        }

        // sofia recover!!!
        for (int i=0; i< MAX_SOFIA_PROFILES; i++) {
            if (!strcmp(vip->config.profiles[i].name, "")) break;
            if (vip->config.profiles[i].autorecover == SWITCH_TRUE) {
                utils_recover(vip->config.profiles[i].name);
            }
        }

        // and I say it to all other nodes
        virtual_ip_send_state(vip);


    } else {
        // se è cascato il master e non devo reagire mi ripulisco la tabella
        for (int i=0; i< MAX_SOFIA_PROFILES; i++) {
            if (!strcmp(vip->config.profiles[i].name, "")) break;
            utils_clean_up_table(vip->runtime_uuid,
                                 vip->config.profiles[i].name);
        }
    }
    return SWITCH_STATUS_SUCCESS;

error:
    switch_log_printf(SWITCH_CHANNEL_LOG,
                      SWITCH_LOG_ERROR,"Failed transition to MASTER!\n");
    go_down(vip);
    return SWITCH_STATUS_FALSE;

}

switch_status_t go_down(virtual_ip_t *vip)
{
    switch_status_t status;
    vip->state = ST_IDLE;
    vip->master_id = 0;
    vip->member_list_entries = 0;
    vip->node_id = 0;
    vip->master_id = 0;
    vip->rollback_node_id = 0;

    node_remove_all(vip->node_list);
    if (vip->node_list) {
        vip->node_list = NULL;
    }

    if (vip->rollback_thread) {
        switch_thread_join(&status, vip->rollback_thread);
        vip->rollback_thread = NULL;
    }
    if (vip->virtual_ip_thread) {
        switch_thread_join(&status, vip->virtual_ip_thread);
        vip->virtual_ip_thread = NULL;
    }

    utils_remove_vip(vip->config.address, vip->config.device);

    // chiudo le chiamate rimaste su
    for (int i=0; i< MAX_SOFIA_PROFILES; i++) {
        if (!strcmp(vip->config.profiles[i].name, "")) break;
        utils_hupall(vip->config.profiles[i].name);
    }

    return SWITCH_STATUS_SUCCESS;

}

switch_status_t observe(virtual_ip_t *vip)
{
    vip->state = ST_BACKUP;
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t go_up(virtual_ip_t *vip)
{
    switch_threadattr_t *thd_attr = NULL;
    vip->state = ST_START;

    switch_threadattr_create(&thd_attr, globals.pool);
    switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
    switch_threadattr_priority_increase(thd_attr);
    switch_thread_create(&(vip->virtual_ip_thread),
                         thd_attr, vip_thread, vip, globals.pool);
    //TODO start sofia profile?
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t rback_stop(virtual_ip_t *vip)
{
    if ((vip->rollback_node_id == 0) ||
        (vip->rollback_node_id != node_first(vip->node_list))){
        vip->state = ST_MASTER;
    }
    return SWITCH_STATUS_SUCCESS;
}
