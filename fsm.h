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
#ifndef FSM_H
#define FSM_H

#include <switch.h>
#include "states_events.h"
#include "virtual_ip_type.h"

typedef switch_status_t (*action_t) (virtual_ip_t *vip);

action_t fsm_do_transaction(event_t event, state_t state);

state_t fsm_get_state(virtual_ip_t *vip);

#endif
