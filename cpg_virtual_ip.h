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
#ifndef CPG_VIRTUAL_IP_H
#define CPG_VIRTUAL_IP_H

#include <switch.h>
#include "node.h"
#include "virtual_ip_types.h"


switch_status_t virtual_ip_start(virtual_ip_t *vip);
switch_status_t virtual_ip_stop(virtual_ip_t *vip);
switch_status_t virtual_ip_send_sql(virtual_ip_t *vip, char *sql);
switch_status_t virtual_ip_send_state(virtual_ip_t *vip);

/*local utils*/
char *virtual_ip_get_state(virtual_ip_t *vip);
virtual_ip_t *find_virtual_ip(char *address);
switch_bool_t vip_is_running(virtual_ip_t *vip);

char *state_to_string(state_t state);
char *event_to_string(event_t state);

/*private*/
void
    *SWITCH_THREAD_FUNC vip_thread(switch_thread_t *thread, void *obj);
void
    *SWITCH_THREAD_FUNC rollback_thread(switch_thread_t *thread, void *obj);
#endif
