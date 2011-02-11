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

    // start sofia profile
    for (int i=0; i<3; i++) {
        switch_yield(100000);
        if (utils_start_sofia_profile(profile->name) != SWITCH_STATUS_SUCCESS) {
            goto error;
        }
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From STANDBY to INIT for %s!\n", profile->name);
    return SWITCH_STATUS_SUCCESS;

error:
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to INIT!\n");
    profile->state = STANDBY;
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

    // set the ip to bind to
    if (utils_add_vip(profile->virtual_ip, profile->device) != SWITCH_STATUS_SUCCESS) {
        goto error;
    }

    // gratuitous arp request
    if (utils_send_gARP(profile->mac, profile->virtual_ip, profile->device) != SWITCH_STATUS_SUCCESS) {
        utils_remove_vip(profile->virtual_ip, profile->device);
        goto error;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From INIT to MASTER for %s!\n", profile->name);
    return SWITCH_STATUS_SUCCESS;

error:
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to MASTER!\n");
    profile->state = STANDBY;
    return SWITCH_STATUS_FALSE;
}

switch_status_t from_backup_to_master(profile_t *profile)
{
    profile->state = MASTER;

    // set the ip to bind to
    if (utils_add_vip(profile->virtual_ip, profile->device) != SWITCH_STATUS_SUCCESS) {
        goto error;
    }

    // gratuitous arp request
    if (utils_send_gARP(profile->mac, profile->virtual_ip, profile->device) != SWITCH_STATUS_SUCCESS) {
        utils_remove_vip(profile->virtual_ip, profile->device);
        goto error;
    }

    // sofia recover!!!
    if (profile->autorecover == SWITCH_TRUE) {
        utils_recover(profile->name);
    }
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From BACKUP to MASTER for %s!\n", profile->name);
    return SWITCH_STATUS_SUCCESS;

error:
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to MASTER!\n");
    profile->state = STANDBY;
    return SWITCH_STATUS_FALSE;
}

switch_status_t from_master_to_standby(profile_t *profile)
{
    profile->state = STANDBY;
    profile->master_id = 0;
    profile->member_list_entries = 0;
    if (utils_remove_vip(profile->virtual_ip, profile->device) != SWITCH_STATUS_SUCCESS) {
        goto error;
    }

    // stop sofia profile
    if (utils_stop_sofia_profile(profile->name) != SWITCH_STATUS_SUCCESS){
        goto error;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From MASTER to STANDBY for %s!\n", profile->name);
    return SWITCH_STATUS_SUCCESS;

error:
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to STANDBY!\n");
    return SWITCH_STATUS_FALSE;
}

switch_status_t from_backup_to_standby(profile_t *profile)
{
    profile->state = STANDBY;
    profile->master_id = 0;
    profile->member_list_entries = 0;

    // stop sofia profile
    if (utils_stop_sofia_profile(profile->name) != SWITCH_STATUS_SUCCESS){
        goto error;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From BACKUP to STANDBY for %s!\n", profile->name);
    return SWITCH_STATUS_SUCCESS;

error:
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to STANDBY!\n");
    return SWITCH_STATUS_FALSE;
}

switch_status_t from_init_to_standby(profile_t *profile)
{
    profile->state = STANDBY;
    profile->master_id = 0;
    profile->member_list_entries = 0;

    // stop sofia profile
    if (utils_stop_sofia_profile(profile->name) != SWITCH_STATUS_SUCCESS){
        goto error;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From INIT to STANDBY for %s!\n", profile->name);
    return SWITCH_STATUS_SUCCESS;

error:
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to STANDBY!\n");
    return SWITCH_STATUS_FALSE;
}

switch_status_t go_to_standby(profile_t *profile)
{
    switch_status_t status = SWITCH_STATUS_FALSE;

    if (!profile)
        return status;

    switch(profile->state) {
        case MASTER:
            status = from_master_to_standby(profile);
            break;
        case BACKUP:
            status = from_backup_to_standby(profile);
            break;
        case INIT:
            status = from_init_to_standby(profile);
            break;
        case STANDBY:
            status = SWITCH_STATUS_SUCCESS;
            break;
    }
    return status;
}


