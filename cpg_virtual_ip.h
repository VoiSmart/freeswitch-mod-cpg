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
#include <corosync/cpg.h>

#include "node.h"

#define MAX_SOFIA_PROFILE 8

typedef enum{
    MASTER,
    BACKUP,
    INIT,
    STANDBY
} virtual_ip_state_t;

typedef struct {
    char name[256];
    switch_bool_t autorecover;
} sofia_profile_t;

typedef struct {

/*configuration*/
    char address[16];
    int netmask;
    char device[10];
    char mac[18];
    struct cpg_name group_name;
    int priority;
    switch_bool_t autoload;
    switch_bool_t autorollback;
    int rollback_delay;
    sofia_profile_t profiles[MAX_SOFIA_PROFILE];

/*runtime information*/
    virtual_ip_state_t state;
    switch_thread_t *virtual_ip_thread;
    switch_bool_t running;
    cpg_handle_t handle;
    int members_number;
    uint32_t node_id;
    uint32_t master_id;
    char runtime_uuid[40];
    uint32_t rollback_node_id;
    node_t *node_list;
    size_t member_list_entries;
} virtual_ip_t;

virtual_ip_t *find_virtual_ip(char *address);


switch_status_t virtual_ip_start(virtual_ip_t *vip);
switch_status_t virtual_ip_stop(virtual_ip_t *vip);
switch_status_t virtual_ip_send_sql(virtual_ip_t *vip, char *sql);
switch_status_t virtual_ip_send_state(virtual_ip_t *vip);

/*local utils*/
char *utils_state_to_string(virtual_ip_state_t pstate);
virtual_ip_state_t utils_string_to_state(char *state);

#endif
