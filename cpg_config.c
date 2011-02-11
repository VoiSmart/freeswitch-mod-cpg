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
#include "cpg_profile.h"

/*
    TODO riceve filename e puntatore all'area di memoria da riempire
    ritorna SWITCH_STATUS_SUCCESS o SWITCH_STATUS_FALSE
*/
switch_status_t do_config(char *cf)
{
    switch_xml_t cfg, xml, xprofile, param;
    profile_t *profile;

    switch_status_t status = SWITCH_STATUS_SUCCESS;

    if (!(xml = switch_xml_open_cfg(cf, &cfg, NULL))) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                                   "Open of %s failed\n", cf);
        return SWITCH_STATUS_TERM;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
                                                           "%s opened\n", cf);


    for (xprofile= switch_xml_child(cfg,"virtual_ip");
                                  xprofile; xprofile = xprofile->next) {

        char *virtual_ip = NULL;
        char *netmask = NULL;

        if (!(profile = (profile_t *) switch_core_alloc(globals.pool, sizeof(profile_t)))) {
            switch_log_printf(SWITCH_CHANNEL_LOG,
                                         SWITCH_LOG_ERROR, "Memory Error!\n");
            return SWITCH_STATUS_FALSE;
        }

        virtual_ip = (char *) switch_xml_attr_soft(xprofile, "virtual_ip");
        netmask = (char *) switch_xml_attr_soft(xprofile, "cidr_netmask");

        if (utils_ip_is_valid(virtual_ip) != SWITCH_TRUE) {
            //TODO devo liberare la memoria?
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                      "virtual_ip %s is not valid\n", virtual_ip);
            continue;
        }
        switch_snprintf(profile->virtual_ip,16,"%s",virtual_ip);
        switch_snprintf(profile->name,255,"%s",virtual_ip);
        profile->netmask = utils_get_netmask(netmask);

        switch_snprintf(profile->group_name.value,255,"%s",virtual_ip);
        profile->group_name.length = strlen(virtual_ip);


        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
               "new virtual_ip %s/%d\n", profile->virtual_ip, profile->netmask);


        for (param = switch_xml_child(xprofile, "param"); 
                                                 param; param = param->next) {
            char *var = (char *) switch_xml_attr_soft(param, "name");
            char *val = (char *) switch_xml_attr_soft(param, "value");
            printf("%s = %s\n", var, val);

            if (!strcmp(var, "device")) {
                char *mac;
                switch_snprintf(profile->device,6,"%s",val);
                //get local mac virtual_ip
                mac = utils_get_mac_addr(profile->device);

                if (profile->device == NULL || mac == NULL) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                     "virtual_ip %s: Interface is not valid\n",
                                                          profile->virtual_ip);
                    status = SWITCH_STATUS_FALSE;
                    goto out;
                }
                switch_snprintf(profile->mac,18,"%s",mac);

                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                                          "device = %s with mac = %s\n",
                                           profile->device, profile->mac);

            } else if (!strcmp(var, "autoload")) {
                profile->autoload = SWITCH_FALSE;
                if (!strcmp(val, "true")) {
                    profile->autoload = SWITCH_TRUE;
                }
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
                        "Autoload = %s\n", profile->autoload == SWITCH_TRUE?
                                                             "true":"false" );

            } else if (!strcmp(var, "autorollback")) {
                profile->autorollback = SWITCH_FALSE;
                if (!strcmp(val, "true")) {
                    profile->autorollback = SWITCH_TRUE;
                }
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
                "Autorollback = %s\n", profile->autorollback == SWITCH_TRUE?
                                                             "true":"false" );
            } else if (!strcmp(var, "rollback-delay")) {
                profile->rollback_delay = atoi(val);
                if ( profile->rollback_delay == 0) {
                    profile->rollback_delay = 1;
                }
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                          "Rollback delay = %d\n", profile->rollback_delay);
            } else if (!strcmp(var, "priority")) {
                profile->priority = atoi(val);
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
                                      "Priority = %d\n", profile->priority);
            }

        }

        status = switch_core_hash_insert(globals.profile_hash, profile->virtual_ip, profile);
        if (status != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, 
                                      "Cannot insert virtual_ip data in hash");
            goto out;
        }
    }

/*    for (xprofile= switch_xml_child(cfg,"profile");*/
/*                                        xprofile; xprofile = xvirtual_ip->next) {*/

/*        char *name = NULL;*/
/*        char *virtual_ip = NULL;*/

/*        if (!(profile = (virtual_ip_t *) switch_core_alloc(globals.pool, sizeof(virtual_ip_t)))) {*/
/*            switch_log_printf(SWITCH_CHANNEL_LOG,*/
/*                                         SWITCH_LOG_ERROR, "Memory Error!\n");*/
/*            return SWITCH_STATUS_FALSE;*/
/*        }*/

/*        name = (char *) switch_xml_attr_soft(xprofile, "name");*/
/*        virtual_ip = (char *) switch_xml_attr_soft(xprofile, "virtual_ip");*/

/*//TODO esiste il profilo? esiste l'indirizzo'?*/

/*        for (param = switch_xml_child(xprofile, "param"); param; param = param->next) {*/
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
