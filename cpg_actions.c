#include "cpg_actions.h"

#include <switch.h>
#include "cpg_utils.h"

switch_status_t from_standby_to_init(virtual_ip_t *vip)
{
/*    vip->state = INIT;*/

/*    // start sofia profile*/
/*    for (int i=0; i<3; i++) {*/
/*        switch_yield(100000);*/
/*        if (utils_start_sofia_profile(vip->name) != SWITCH_STATUS_SUCCESS) {*/
/*            goto error;*/
/*        }*/
/*    }*/

/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From STANDBY to INIT for %s!\n", vip->name);*/
/*    return SWITCH_STATUS_SUCCESS;*/

/*error:*/
/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to INIT!\n");*/
/*    vip->state = STANDBY;*/
    return SWITCH_STATUS_FALSE;

}

switch_status_t from_init_to_backup(virtual_ip_t *vip)
{
/*    vip->state = BACKUP;*/
/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From INIT to BACKUP for %s!\n", vip->name);*/
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t from_init_to_master(virtual_ip_t *vip)
{
/*    vip->state = MASTER;*/

/*    // set the ip to bind to*/
/*    if (utils_add_vip(vip->virtual_ip, vip->device) != SWITCH_STATUS_SUCCESS) {*/
/*        goto error;*/
/*    }*/

/*    // gratuitous arp request*/
/*    if (utils_send_gARP(vip->mac, vip->virtual_ip, vip->device) != SWITCH_STATUS_SUCCESS) {*/
/*        utils_remove_vip(vip->virtual_ip, vip->device);*/
/*        goto error;*/
/*    }*/

/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From INIT to MASTER for %s!\n", vip->name);*/
/*    return SWITCH_STATUS_SUCCESS;*/

/*error:*/
/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to MASTER!\n");*/
/*    vip->state = STANDBY;*/
    return SWITCH_STATUS_FALSE;
}

switch_status_t from_backup_to_master(virtual_ip_t *vip)
{
/*    vip->state = MASTER;*/

/*    // set the ip to bind to*/
/*    if (utils_add_vip(vip->virtual_ip, vip->device) != SWITCH_STATUS_SUCCESS) {*/
/*        goto error;*/
/*    }*/

/*    // gratuitous arp request*/
/*    if (utils_send_gARP(vip->mac, vip->virtual_ip, vip->device) != SWITCH_STATUS_SUCCESS) {*/
/*        utils_remove_vip(vip->virtual_ip, vip->device);*/
/*        goto error;*/
/*    }*/

/*    // sofia recover!!!*/
/*    if (vip->autorecover == SWITCH_TRUE) {*/
/*        utils_recover(vip->name);*/
/*    }*/
/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From BACKUP to MASTER for %s!\n", vip->name);*/
/*    return SWITCH_STATUS_SUCCESS;*/

/*error:*/
/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to MASTER!\n");*/
/*    vip->state = STANDBY;*/
    return SWITCH_STATUS_FALSE;
}

switch_status_t from_master_to_standby(virtual_ip_t *vip)
{
/*    vip->state = STANDBY;*/
/*    vip->master_id = 0;*/
/*    vip->member_list_entries = 0;*/
/*    if (utils_remove_vip(vip->virtual_ip, vip->device) != SWITCH_STATUS_SUCCESS) {*/
/*        goto error;*/
/*    }*/

/*    // stop sofia profile*/
/*    if (utils_stop_sofia_profile(vip->name) != SWITCH_STATUS_SUCCESS){*/
/*        goto error;*/
/*    }*/

/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From MASTER to STANDBY for %s!\n", vip->name);*/
/*    return SWITCH_STATUS_SUCCESS;*/

/*error:*/
/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to STANDBY!\n");*/
/*    return SWITCH_STATUS_FALSE;*/
/*}*/

/*switch_status_t from_backup_to_standby(virtual_ip_t *vip)*/
/*{*/
/*    vip->state = STANDBY;*/
/*    vip->master_id = 0;*/
/*    vip->member_list_entries = 0;*/

/*    // stop sofia profile*/
/*    if (utils_stop_sofia_profile(vip->name) != SWITCH_STATUS_SUCCESS){*/
/*        goto error;*/
/*    }*/

/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From BACKUP to STANDBY for %s!\n", vip->name);*/
/*    return SWITCH_STATUS_SUCCESS;*/

/*error:*/
/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to STANDBY!\n");*/
    return SWITCH_STATUS_FALSE;
}

switch_status_t from_init_to_standby(virtual_ip_t *vip)
{
/*    vip->state = STANDBY;*/
/*    vip->master_id = 0;*/
/*    vip->member_list_entries = 0;*/

/*    // stop sofia profile*/
/*    if (utils_stop_sofia_profile(vip->name) != SWITCH_STATUS_SUCCESS){*/
/*        goto error;*/
/*    }*/

/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"From INIT to STANDBY for %s!\n", vip->name);*/
/*    return SWITCH_STATUS_SUCCESS;*/

/*error:*/
/*    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Failed transition to STANDBY!\n");*/
    return SWITCH_STATUS_FALSE;
}

switch_status_t go_to_standby(virtual_ip_t *vip)
{
    switch_status_t status = SWITCH_STATUS_FALSE;

    if (!vip)
        return status;

    switch(vip->state) {
        case MASTER:
            status = from_master_to_standby(vip);
            break;
        case BACKUP:
            status = from_backup_to_standby(vip);
            break;
        case INIT:
            status = from_init_to_standby(vip);
            break;
        case STANDBY:
            status = SWITCH_STATUS_SUCCESS;
            break;
    }
    return status;
}
