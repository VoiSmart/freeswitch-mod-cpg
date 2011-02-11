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
#include "cpg_virtual_ip.h"

/*
    TODO riceve filename e puntatore all'area di memoria da riempire
    ritorna SWITCH_STATUS_SUCCESS o SWITCH_STATUS_FALSE
*/
switch_status_t do_config(char *cf)
{
    switch_xml_t cfg, xml, xvip, param;
    virtual_ip_t *vip;

    switch_status_t status = SWITCH_STATUS_SUCCESS;

    if (!(xml = switch_xml_open_cfg(cf, &cfg, NULL))) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                                   "Open of %s failed\n", cf);
        return SWITCH_STATUS_TERM;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
                                                           "%s opened\n", cf);


    for (xvip= switch_xml_child(cfg,"virtual_ip");
                                  xvip; xvip = xvip->next) {

        char *address = NULL;
        char *netmask = NULL;

        if (!(vip = (virtual_ip_t *) switch_core_alloc(globals.pool, sizeof(virtual_ip_t)))) {
            switch_log_printf(SWITCH_CHANNEL_LOG,
                                         SWITCH_LOG_ERROR, "Memory Error!\n");
            return SWITCH_STATUS_FALSE;
        }

        address = (char *) switch_xml_attr_soft(xvip, "virtual_ip");
        netmask = (char *) switch_xml_attr_soft(xvip, "cidr_netmask");

        if (utils_ip_is_valid(address) != SWITCH_TRUE) {
            //TODO devo liberare la memoria?
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                      "virtual_ip %s is not valid\n", address);
            continue;
        }
        switch_snprintf(vip->address,16,"%s",address);
        vip->netmask = utils_get_netmask(netmask);

        switch_snprintf(vip->group_name.value,255,"%s",address);
        vip->group_name.length = strlen(address);


        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
               "new virtual_ip %s/%d\n", vip->address, vip->netmask);


        for (param = switch_xml_child(xvip, "param"); 
                                                 param; param = param->next) {
            char *var = (char *) switch_xml_attr_soft(param, "name");
            char *val = (char *) switch_xml_attr_soft(param, "value");
            printf("%s = %s\n", var, val);

            if (!strcmp(var, "device")) {
                char *mac;
                switch_snprintf(vip->device,6,"%s",val);
                //get local mac virtual_ip
                mac = utils_get_mac_addr(vip->device);

                if (vip->device == NULL || mac == NULL) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                     "virtual_ip %s: Interface is not valid\n",
                                                          vip->address);
                    status = SWITCH_STATUS_FALSE;
                    goto out;
                }
                switch_snprintf(vip->mac,18,"%s",mac);

                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                                          "device = %s with mac = %s\n",
                                           vip->device, vip->mac);

            } else if (!strcmp(var, "autoload")) {
                vip->autoload = SWITCH_FALSE;
                if (!strcmp(val, "true")) {
                    vip->autoload = SWITCH_TRUE;
                }
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
                        "Autoload = %s\n", vip->autoload == SWITCH_TRUE?
                                                             "true":"false" );

            } else if (!strcmp(var, "autorollback")) {
                vip->autorollback = SWITCH_FALSE;
                if (!strcmp(val, "true")) {
                    vip->autorollback = SWITCH_TRUE;
                }
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
                "Autorollback = %s\n", vip->autorollback == SWITCH_TRUE?
                                                             "true":"false" );
            } else if (!strcmp(var, "rollback-delay")) {
                vip->rollback_delay = atoi(val);
                if ( vip->rollback_delay == 0) {
                    vip->rollback_delay = 1;
                }
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                          "Rollback delay = %d\n", vip->rollback_delay);
            } else if (!strcmp(var, "priority")) {
                vip->priority = atoi(val);
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
                                      "Priority = %d\n", vip->priority);
            }

        }

        status = switch_core_hash_insert(globals.virtual_ip_hash, vip->address, vip);
        if (status != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, 
                                      "Cannot insert virtual_ip data in hash");
            goto out;
        }
    }

/*    for (xvip= switch_xml_child(cfg,"profile");*/
/*                                        xvip; xvip = xvirtual_ip->next) {*/

/*        char *name = NULL;*/
/*        char *virtual_ip = NULL;*/

/*        if (!(vip = (virtual_ip_t *) switch_core_alloc(globals.pool, sizeof(virtual_ip_t)))) {*/
/*            switch_log_printf(SWITCH_CHANNEL_LOG,*/
/*                                         SWITCH_LOG_ERROR, "Memory Error!\n");*/
/*            return SWITCH_STATUS_FALSE;*/
/*        }*/

/*        name = (char *) switch_xml_attr_soft(xvip, "name");*/
/*        virtual_ip = (char *) switch_xml_attr_soft(xvip, "virtual_ip");*/

/*//TODO esiste il profilo? esiste l'indirizzo'?*/

/*        for (param = switch_xml_child(xvip, "param"); param; param = param->next) {*/
/*            char *var = (char *) switch_xml_attr_soft(param, "name");*/
/*            char *val = (char *) switch_xml_attr_soft(param, "value");*/

/*            if (!strcmp(var, "autorecover")) {*/
/*                virtual_ip->autorecover = SWITCH_FALSE;*/
/*                if (!strcmp(val, "true")) {*/
/*                    virtual_ip->autorecover = SWITCH_TRUE;*/
/*                }*/
/*                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Autorecover = %s\n",*/
/*                                  virtual_ip->autorecover == SWITCH_TRUE?"true":"false" );*/
/*            }*/
/*        }*/

/*        status = switch_core_hash_insert(globals.profile_hash, virtual_ip->virtual_ip, profile);*/
/*        if (utils_profile_control(virtual_ip->virtual_ip) != SWITCH_STATUS_SUCCESS) {*/
/*            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "profile %s doesn't exist in sip_profiles directory, before to do anything create it and reloadxml!\n", virtual_ip->virtual_ip);*/
/*        }*/
/*    }*/
out:
    switch_xml_free(xml);

    return status;
}
