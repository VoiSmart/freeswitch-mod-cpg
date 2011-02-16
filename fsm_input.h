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
#ifndef FSM_INPUT_H
#define FSM_INPUT_H

#include <switch.h>
#include "virtual_ip_type.h"


switch_status_t
    fsm_input_node_down(virtual_ip_t *vip, uint32_t nodeid);
switch_status_t
    fsm_input_new_state_message(virtual_ip_t *vip,
                                node_msg_t *nm, uint32_t nodeid);
switch_status_t
    fsm_input_node_up(virtual_ip_t *vip, size_t member_list_entries);
switch_status_t
    fsm_input_cmd_start(virtual_ip_t *vip);
switch_status_t
    fsm_input_cmd_stop(virtual_ip_t *vip);
#endif
