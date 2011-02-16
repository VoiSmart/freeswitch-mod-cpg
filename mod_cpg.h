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
#ifndef MOD_CPG_H
#define MOD_CPG_H

#include <switch.h>
#include <corosync/cpg.h>
#include <string.h>

struct {
    switch_memory_pool_t *pool;
    switch_hash_t *virtual_ip_hash;
    short int running;
    switch_bool_t is_connected;
    cpg_handle_t handle;
    switch_event_node_t *node;
} globals;


#endif
