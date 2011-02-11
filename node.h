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
#ifndef CPG_NODE_H
#define CPG_NODE_H

#include <switch.h>

struct node {
    uint32_t nodeid;
    int priority;
    struct node *next;
};

typedef struct node node_t;


node_t *node_add(node_t *oldlist, uint32_t nodeid, int priority);
node_t *node_remove(node_t *oldlist, uint32_t nodeid);
switch_status_t node_remove_all(node_t *list);
node_t *node_search(node_t *list, uint32_t nodeid);
size_t list_entries(node_t *list);

#endif
