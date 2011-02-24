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
#include "fsm_input.h"

#include "fsm.h"

switch_status_t fsm_input_node_down(virtual_ip_t *vip, uint32_t nodeid)
{
    switch_status_t status = SWITCH_STATUS_FALSE;
    event_t new_event = EVT_BACKUP_DOWN;

    if (vip->master_id == nodeid) {
        new_event = EVT_MASTER_DOWN;
    }
    vip->node_list = node_remove(vip->node_list, nodeid);

    switch_log_printf(SWITCH_CHANNEL_LOG,
                      SWITCH_LOG_NOTICE, "nodeid = %u\n",nodeid);

    if (vip->rollback_node_id == nodeid) {
        vip->rollback_node_id = 0;
    }

    status = fsm_do_transaction(vip, new_event);
    return status;
}

switch_status_t fsm_input_node_up(virtual_ip_t *vip)
{
    switch_status_t status = SWITCH_STATUS_FALSE;
    event_t new_event = EVT_BACKUP_UP;

    status = fsm_do_transaction(vip, new_event);
    return status;
}



switch_status_t
    fsm_input_new_state_message(virtual_ip_t *vip,
                                node_msg_t *nm, uint32_t nodeid)
{
    switch_status_t status = SWITCH_STATUS_FALSE;
    event_t new_event = MAX_EVENTS;//nullo

/*non mi piace molto scrivere in vip senza essere nella macchina a stati*/
    vip->node_list = node_add(vip->node_list, nodeid, nm->priority);

    if ((nm->state == ST_MASTER) || (nm->state == ST_RBACK)) {
        vip->master_id = nodeid;
        switch_snprintf(vip->runtime_uuid,
                        sizeof(vip->runtime_uuid),"%s", nm->runtime_uuid );
    }
    // cerco duplicati
    if ((nm->priority == vip->config.priority)
            && (nodeid != vip->node_id)) {
        new_event = EVT_DUPLICATE;
        goto process;
    }
    switch(fsm_get_state(vip)) {
        case ST_START:
            // se sono solo il master è sicuramente giù
            if (vip->member_list_entries == 1) {
                new_event = EVT_MASTER_DOWN;
            }
            // if the priority list is complete becomes BACKUP
            else if (list_entries(vip->node_list)
                    == vip->member_list_entries) {
                new_event = EVT_MASTER_UP;
            }
            break;
        case ST_MASTER:
        case ST_RBACK:
            if (vip->node_list != NULL ) {
                if ((vip->config.autorollback == SWITCH_TRUE) &&
                    nm->priority > vip->config.priority) {

                    new_event = EVT_RBACK_REQ;
                }
            }
            break;
        default: break;
    }
process:
    if (new_event != MAX_EVENTS) {
        status = fsm_do_transaction(vip, new_event);
    }
    return status;
}

switch_status_t fsm_input_cmd_start(virtual_ip_t *vip)
{
    switch_status_t status = SWITCH_STATUS_FALSE;
    event_t new_event = EVT_STARTUP;
    status = fsm_do_transaction(vip, new_event);
    return status;
}
switch_status_t fsm_input_cmd_stop(virtual_ip_t *vip)
{
    switch_status_t status = SWITCH_STATUS_FALSE;
    event_t new_event = EVT_STOP;
    status = fsm_do_transaction(vip, new_event);
    return status;
}
