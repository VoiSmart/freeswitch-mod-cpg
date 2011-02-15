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

switch_status_t fsm_input_node_up(virtual_ip_t vip, size_t member_list_entries)
{
    event_t new_event = EVT_BACKUP_UP;

    if (member_list_entries == 1)
    && (fsm_get_state(vip) == ST_INIT) {
        // I'm the master
        new_event = EVT_MASTER_DOWN;
    }

    return do_transaction(new_event, vip->state)(vip);
}

switch_Status_t fsm_input_node_down(virtual_ip_t vip, uint32_t nodeid)
{
    event_t new_event = EVT_BACKUP_DOWN;

    if ( vip->master_id == nodeid) {
        new_event = EVT_MASTER_DOWN;
    }
    vip->node_list = node_remove(vip->node_list, nodeid);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                      "i = %d, nodeid = %u\n",i,nodeid);

    return do_transaction(new_event, vip->state)(vip);

}


switch_status_t fsm_input_new_state_message(virtual_ip_t *vip, node_msg_t *nm)
{
    switch_status_t status = SWITCH_STATUS_FALSE;
    event_t new_event = MAX_EVENTS;//nullo

/*non mi piace molto scrivere in vip senza essere nella macchina a stati*/
    vip->node_list = node_add(vip->node_list, nodeid, nm->priority);

    if (nm->state == ST_MASTER) {
        vip->master_id = nodeid;
        switch_snprintf(vip->runtime_uuid, sizeof(vip->runtime_uuid),"%s", nm->runtime_uuid );
    }

    switch(fsm_get_state(vip)) {
        case ST_INIT:
            if ((nm->priority == vip->priority) && (nodeid != vip->node_id)) {
                new_event = EVT_DUPLICATE;
            }
            // if the priority list is complete becomes BACKUP
            if (list_entries(profile->node_list) == profile->member_list_entries) {
                new_event = MASTER_UP;
            }
            break;
        case ST_MASTER:
        case ST_RBACK:
            if (profile->node_list != NULL ) {
                if ((vip->autorollback == SWITCH_TRUE) &&
                    nm->priority > vip->priority) {

                    new_event = EVT_RBACK_REQ;
                }
            }
            break;

        default: break;
    }

    if (new_event != MAX_EVENTS) {
        status = do_transaction(new_event, vip->state) (vip);
    }
    return status;
}




