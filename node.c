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


#include "node.h"

#include "cpg_utils.h"

node_t *node_add(node_t *oldlist, uint32_t nodeid, int priority)
{

    node_t *new_node, *prev,*curr;

    if (node_search(oldlist, nodeid) != NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"Node %s already present\n",
                                                       utils_node_pid_format(nodeid));
        return oldlist;
    }

    new_node = malloc(sizeof(node_t));
    if (!new_node) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Cannot allocate node\n");
        return NULL;
    }
    memset(new_node, 0, sizeof(node_t));

    prev = NULL;
    curr = oldlist;
    while (curr != NULL && curr->priority > priority) {
        prev = curr;
        curr = curr->next;
    }

    new_node->nodeid = nodeid;
    new_node->priority = priority;
    if (prev != NULL) {
        prev->next = new_node;
        new_node->next = curr;
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,"New node %s/%d\n",
                                              utils_node_pid_format(nodeid),priority);
        return oldlist;
    } else {
        new_node->next = oldlist;
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"New node %s/%d\n",
                                              utils_node_pid_format(nodeid),priority);
        return new_node;
    }
}

node_t *node_remove(node_t *oldlist, uint32_t nodeid)
{
    node_t *cur, *prev;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"Remove node %s\n",
                                                       utils_node_pid_format(nodeid));

    for (prev = NULL, cur = oldlist; cur != NULL && cur->nodeid != nodeid; prev = cur, cur = cur->next);

    if (cur == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Node %s not found\n",
                                                       utils_node_pid_format(nodeid));
        return oldlist;
    }
    if (prev == NULL) {
        oldlist = oldlist->next;
    } else {
        prev->next = cur->next;
    }
    free(cur);


    return oldlist;
}

switch_status_t node_remove_all(node_t *list)
{
    node_t *prev, *current;
    for (prev = NULL, current = list; current != NULL; prev = current, current = current->next) {
        if (prev != NULL) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"Remove node %s\n",
                                                 utils_node_pid_format(prev->nodeid));
            free(prev);
        }
    }
    return SWITCH_STATUS_SUCCESS;
}

node_t *node_search(node_t *list, uint32_t nodeid)
{
    while (list != NULL && list->nodeid != nodeid) {
        list = list->next;
    }
    return list;
}

uint32_t node_first(node_t *list)
{
    if (!list)
        return 0;
    return (list->nodeid)?list->nodeid:0;
}

size_t list_entries(node_t *list)
{
    int i;
    for (i = 0;list != NULL; list = list->next, i++);
    return (size_t) i;
}
