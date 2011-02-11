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

#include "cpg_config.h"
#include "cpg_utils.h"
#include "cpg_address.h"

/*
    TODO riceve filename e puntatore all'area di memoria da riempire
    ritorna SWITCH_STATUS_SUCCESS o SWITCH_STATUS_FALSE
*/
switch_status_t do_config(char *cf)
{
    switch_xml_t cfg, xml, xaddress, param;
    address_t *address;
/*    profile_t *profile;*/

    switch_status_t status = SWITCH_STATUS_SUCCESS;

    if (!(xml = switch_xml_open_cfg(cf, &cfg, NULL))) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                                   "Open of %s failed\n", cf);
        return SWITCH_STATUS_TERM;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
                                                           "%s opened\n", cf);


    for (xaddress= switch_xml_child(cfg,"address");
                                  xaddress; xaddress = xvirtualip->next) {

        char *address = NULL;
        char *netmask = NULL;

        if (!(address = (address_t *) switch_core_alloc(globals.pool, sizeof(address_t)))) {
            switch_log_printf(SWITCH_CHANNEL_LOG,
                                         SWITCH_LOG_ERROR, "Memory Error!\n");
            return SWITCH_STATUS_FALSE;
        }

        address = (char *) switch_xml_attr_soft(xaddress, "address");
        netmask = (char *) switch_xml_attr_soft(xaddress, "cidr_netmask");

        if (utils_ip_is_valid(address) != SWITCH_TRUE) {
            //TODO devo liberare la memoria?
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                      "address %s is not valid\n", address);
            continue;
        }
        switch_snprintf(virtualip->address,255,"%s",address);
        virtualip->netmask = utils_get_netmask(netmask);

        switch_snprintf(virtualip->group_name.value,255,"%s",address);
        virtualip->group_name.length = strlen(address);


        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
               "new address %s/%d\n", virtualip->address, virtualip->netmask);


        for (param = switch_xml_child(xaddress, "param"); 
                                                 param; param = param->next) {
            char *var = (char *) switch_xml_attr_soft(param, "name");
            char *val = (char *) switch_xml_attr_soft(param, "value");
            printf("%s = %s\n", var, val);

            if (!strcmp(var, "device")) {
                char *mac;
                switch_snprintf(virtualip->device,6,"%s",val);
                //get local mac address
                mac = utils_get_mac_addr(virtualip->device);

                if (virtualip->device == NULL || mac == NULL) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                     "address %s: Interface is not valid\n",
                                                          virtualip->address);
                    status = SWITCH_STATUS_FALSE;
                    goto out;
                }
                switch_snprintf(virtualip->mac,18,"%s",mac);

                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                                          "device = %s with mac = %s\n",
                                           virtualip->device, virtualip->mac);

            } else if (!strcmp(var, "autoload")) {
                virtualip->autoload = SWITCH_FALSE;
                if (!strcmp(val, "true")) {
                    virtualip->autoload = SWITCH_TRUE;
                }
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
                        "Autoload = %s\n", virtualip->autoload == SWITCH_TRUE?
                                                             "true":"false" );

            } else if (!strcmp(var, "autorollback")) {
                virtualip->autorollback = SWITCH_FALSE;
                if (!strcmp(val, "true")) {
                    virtualip->autorollback = SWITCH_TRUE;
                }
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
                "Autorollback = %s\n", virtualip->autorollback == SWITCH_TRUE?
                                                             "true":"false" );
            } else if (!strcmp(var, "rollback-delay")) {
                virtualip->rollback_delay = atoi(val);
                if ( virtualip->rollback_delay == 0) {
                    virtualip->rollback_delay = 1;
                }
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                          "Rollback delay = %d\n", virtualip->rollback_delay);
            } else if (!strcmp(var, "priority")) {
                virtualip->priority = atoi(val);
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
                                      "Priority = %d\n", virtualip->priority);
            }

        }

        status = switch_core_hash_insert(globals.address_hash, virtualip->address, address);
        if (status != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, 
                                      "Cannot insert address data in hash");
            goto out;
        }
    }

/*    for (xprofile= switch_xml_child(cfg,"profile");*/
/*                                        xprofile; xprofile = xvirtualip->next) {*/

/*        char *name = NULL;*/
/*        char *address = NULL;*/

/*        if (!(profile = (address_t *) switch_core_alloc(globals.pool, sizeof(address_t)))) {*/
/*            switch_log_printf(SWITCH_CHANNEL_LOG,*/
/*                                         SWITCH_LOG_ERROR, "Memory Error!\n");*/
/*            return SWITCH_STATUS_FALSE;*/
/*        }*/

/*        name = (char *) switch_xml_attr_soft(xprofile, "name");*/
/*        address = (char *) switch_xml_attr_soft(xprofile, "address");*/

/*//TODO esiste il profilo? esiste l'indirizzo'?*/

/*        for (param = switch_xml_child(xprofile, "param"); param; param = param->next) {*/
/*            char *var = (char *) switch_xml_attr_soft(param, "name");*/
/*            char *val = (char *) switch_xml_attr_soft(param, "value");*/

/*            if (!strcmp(var, "autorecover")) {*/
/*                virtualip->autorecover = SWITCH_FALSE;*/
/*                if (!strcmp(val, "true")) {*/
/*                    virtualip->autorecover = SWITCH_TRUE;*/
/*                }*/
/*                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Autorecover = %s\n",*/
/*                                  virtualip->autorecover == SWITCH_TRUE?"true":"false" );*/
/*            }*/
/*        }*/

/*        status = switch_core_hash_insert(globals.profile_hash, virtualip->address, profile);*/
/*        if (utils_profile_control(virtualip->address) != SWITCH_STATUS_SUCCESS) {*/
/*            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "profile %s doesn't exist in sip_profiles directory, before to do anything create it and reloadxml!\n", virtualip->address);*/
/*        }*/
/*    }*/
out:
    switch_xml_free(xml);

    return status;
}
