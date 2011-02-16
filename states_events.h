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
#ifndef CPG_STATES_EVENTS_H
#define CPG_STATES_EVENTS_H
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
#endif
