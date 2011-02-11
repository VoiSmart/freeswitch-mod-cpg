
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
    if (net_send_arp_string(profile->mac, "ff:ff:ff:ff:ff:ff", 1,
                                profile->mac, profile->virtual_ip, profile->mac,
                                    profile->virtual_ip, profile->device) < 0) {
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
    if (net_send_arp_string(profile->mac, "ff:ff:ff:ff:ff:ff", 1,
                                profile->mac, profile->virtual_ip, profile->mac,
                                    profile->virtual_ip, profile->device) < 0) {
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


