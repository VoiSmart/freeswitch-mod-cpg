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
#include "profile.h"

#include <switch.h>
#include "cpg_utils.h"
#include "arpator.h"

profile_t *find_profile_by_name(char *profile_name)
{
	// controllo che non sia null
	profile_t *profile = NULL;
	profile = (profile_t *)switch_core_hash_find(globals.profile_hash,profile_name);
	return profile;
}


switch_status_t from_standby_to_init(profile_t *profile) 
{
    profile->state = INIT;
    // arptables rule
	if (utils_add_arp_rule(profile->virtual_ip, profile->mac) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed setup!\n");
        return SWITCH_STATUS_FALSE;
	}
		
	// set the ip to bind to
	if (utils_add_vip(profile->virtual_ip, profile->device) != SWITCH_STATUS_SUCCESS) {
	    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed setup!\n");
	    goto ip_error;
	}
	
	// start sofia profile
	if (utils_start_sofia_profile(profile->name)!=SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed setup!\n");
		goto sofia_error;
	}
	
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From STANDBY to INIT for %s!\n", profile->name);
	return SWITCH_STATUS_SUCCESS;
	
sofia_error:
    utils_remove_vip(profile->virtual_ip, profile->device);

ip_error:
    utils_remove_arp_rule(profile->virtual_ip, profile->mac);
	return SWITCH_STATUS_FALSE;
}

switch_status_t from_init_to_backup(profile_t *profile)
{
    profile->state = BACKUP;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From INIT to BACKUP for %s!\n", profile->name);
	return SWITCH_STATUS_SUCCESS;
}

switch_status_t from_init_to_master(profile_t *profile)
{
    profile->state = MASTER;
    // remove arptables rule
	if (utils_remove_arp_rule(profile->virtual_ip, profile->mac) != SWITCH_STATUS_SUCCESS) {
	    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed Reaction!\n");
		return SWITCH_STATUS_FALSE;
	}

	// gratuitous arp request
	net_send_arp_string(profile->mac, "ff:ff:ff:ff:ff:ff", 1, 
	                    profile->mac, profile->virtual_ip, profile->mac, 
	                    profile->virtual_ip, profile->device);
	
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From INIT to MASTER for %s!\n", profile->name);
	return SWITCH_STATUS_SUCCESS;
}

switch_status_t from_backup_to_master(profile_t *profile)
{
    profile->state = MASTER;
    // remove arptables rule
	if (utils_remove_arp_rule(profile->virtual_ip, profile->mac) != SWITCH_STATUS_SUCCESS) {
	    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed Reaction!\n");
		return SWITCH_STATUS_FALSE;
	}

	// gratuitous arp request
	net_send_arp_string(profile->mac, "ff:ff:ff:ff:ff:ff", 1, 
	                    profile->mac, profile->virtual_ip, profile->mac, 
	                    profile->virtual_ip, profile->device);
	
	// sofia recover!!!
	if (profile->autorecover == SWITCH_TRUE) {
	    utils_recover(profile->name);
    }
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From BACKUP to MASTER for %s!\n", profile->name);
	return SWITCH_STATUS_SUCCESS;
}

switch_status_t from_master_to_standby(profile_t *profile)
{
    profile->state = STANDBY;
    profile->master_id = 0;
    profile->member_list_entries = 0;
    if (utils_remove_vip(profile->virtual_ip, profile->device) != SWITCH_STATUS_SUCCESS) {
	    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed!\n");
	    return SWITCH_STATUS_FALSE;
	}
	
	// stop sofia profile
	if (utils_stop_sofia_profile(profile->name) != SWITCH_STATUS_SUCCESS){
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed!\n");
		return SWITCH_STATUS_FALSE;
	}
	
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From MASTER to STANDBY for %s!\n", profile->name);
	return SWITCH_STATUS_SUCCESS;
}

switch_status_t from_backup_to_standby(profile_t *profile)
{
    profile->state = STANDBY;
    profile->master_id = 0;
    profile->member_list_entries = 0;
    if (utils_remove_vip(profile->virtual_ip, profile->device) != SWITCH_STATUS_SUCCESS) {
	    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed!\n");
	    return SWITCH_STATUS_FALSE;
	}
	
	// remove arptables rule
	if (utils_remove_arp_rule(profile->virtual_ip, profile->mac) != SWITCH_STATUS_SUCCESS) {
	    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed!\n");
	    return SWITCH_STATUS_FALSE;
	}
	
	// stop sofia profile
	if (utils_stop_sofia_profile(profile->name) != SWITCH_STATUS_SUCCESS){
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed!\n");
		return SWITCH_STATUS_FALSE;
	}
	
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From BACKUP to STANDBY for %s!\n", profile->name);
	return SWITCH_STATUS_SUCCESS;
}

switch_status_t from_init_to_standby(profile_t *profile)
{
    profile->state = STANDBY;
    profile->master_id = 0;
    profile->member_list_entries = 0;
    if (utils_remove_vip(profile->virtual_ip, profile->device) != SWITCH_STATUS_SUCCESS) {
	    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed!\n");
	    return SWITCH_STATUS_FALSE;
	}
	
	// remove arptables rule
	if (utils_remove_arp_rule(profile->virtual_ip, profile->mac) != SWITCH_STATUS_SUCCESS) {
	    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed!\n");
	    return SWITCH_STATUS_FALSE;
	}
	
	// stop sofia profile
	if (utils_stop_sofia_profile(profile->name) != SWITCH_STATUS_SUCCESS){
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed!\n");
		return SWITCH_STATUS_FALSE;
	}
	
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From INIT to STANDBY for %s!\n", profile->name);
	return SWITCH_STATUS_SUCCESS;
}

node_t *node_add(node_t *oldlist, uint32_t nodeid, int priority) 
{
    
    node_t *new_node, *prev,*curr;
    
    if (node_search(oldlist, nodeid) != NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"Node %u already present\n", nodeid);
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
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,"New node %u/%d\n",nodeid,priority);
        return oldlist;
    } else {
        new_node->next = oldlist;
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"New node %u/%d\n",nodeid,priority);
        return new_node;
    }
}

node_t *node_remove(node_t *oldlist, uint32_t nodeid) 
{
    node_t *cur, *prev;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"Remove node %u\n",nodeid);
    
    for (prev = NULL, cur = oldlist; cur != NULL && cur->nodeid != nodeid; prev = cur, cur = cur->next);
    
    if (cur == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Node %u not found\n",nodeid);
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
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"Remove node %u\n",prev->nodeid);
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

size_t list_entries(node_t *list) 
{
    int i;
    for (i = 0;list != NULL; list = list->next, i++);
    return (size_t) i;
}
