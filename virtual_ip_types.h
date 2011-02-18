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
#ifndef CPG_STRUCT_H
#define CPG_STRUCT_H
#define MAX_SOFIA_PROFILES 8

#include <switch.h>
#include <corosync/cpg.h>
#include "node.h"

typedef enum {
    EVT_STARTUP,
    EVT_DUPLICATE,
    EVT_MASTER_DOWN,
    EVT_MASTER_UP,
    EVT_BACKUP_DOWN,
    EVT_RBACK_REQ,
    EVT_STOP,
    MAX_EVENTS
} event_t;

typedef enum {
    ST_IDLE,
    ST_START,
    ST_BACKUP,
    ST_MASTER,
    ST_RBACK,
    MAX_STATES
} state_t;

typedef struct {
    char name[256];
    switch_bool_t autorecover;
    switch_bool_t running;
} sofia_profile_t;

typedef struct {

    /*configuration*/
    struct {
        char address[16];
        int netmask;
        char device[10];
        char mac[18];
        int priority;
        struct cpg_name group_name;
        switch_bool_t autoload;
        switch_bool_t autorollback;
        int rollback_delay;
        sofia_profile_t profiles[MAX_SOFIA_PROFILES];
    } config;
/*runtime information*/
    state_t state;
    switch_thread_t *virtual_ip_thread;
    cpg_handle_t handle;
    uint32_t node_id;
    uint32_t master_id;
    char runtime_uuid[40];
    uint32_t rollback_node_id;
    switch_thread_t *rollback_thread;
    node_t *node_list;
    size_t member_list_entries;
} virtual_ip_t;

typedef struct {
    int priority;
    state_t state;
    char runtime_uuid[40];
} node_msg_t;


#endif
