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
#include "virtual_ip_types.h"
#include "mod_cpg.h"
#include "cpg_virtual_ip.h"

switch_bool_t
    profile_name_is_present(sofia_profile_t profiles[],int index, char *name);

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

    switch_log_printf(SWITCH_CHANNEL_LOG,
                      SWITCH_LOG_NOTICE, "%s opened\n", cf);


    for (xvip= switch_xml_child(cfg,"virtualip");
                                  xvip; xvip = xvip->next) {
        int counter = 0;
        char *address = (char *) switch_xml_attr_soft(xvip, "address");
        char *netmask = (char *) switch_xml_attr_soft(xvip, "cidr_netmask");

        if (find_virtual_ip(address) != NULL ) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                              "virtualip %s is already present\n", address);
            continue;
        }

        if (utils_ip_is_valid(address) != SWITCH_TRUE) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                              "virtualip %s is not valid\n", address);
            continue;
        }

        if (!(vip = (virtual_ip_t *)
                        switch_core_alloc(globals.pool,
                                          sizeof(virtual_ip_t)))) {
            switch_log_printf(SWITCH_CHANNEL_LOG,
                              SWITCH_LOG_ERROR, "Memory Error!\n");
            return SWITCH_STATUS_FALSE;
        }
        switch_snprintf(vip->config.address,16,"%s",address);
        vip->config.netmask = utils_get_netmask(netmask);

        switch_snprintf(vip->config.group_name.value,255,"%s",address);
        vip->config.group_name.length = strlen(address);


        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                          "new virtual_ip %s/%d\n", vip->config.address,
                          vip->config.netmask);


        for (param = switch_xml_child(xvip, "param");
                                                 param; param = param->next) {
            char *var = (char *) switch_xml_attr_soft(param, "name");
            char *val = (char *) switch_xml_attr_soft(param, "value");

            if (!strcmp(var, "device")) {
                char *mac;
                switch_snprintf(vip->config.device,6,"%s",val);
                //get local mac virtual_ip
                mac = utils_get_mac_addr(vip->config.device);

                if (vip->config.device == NULL || mac == NULL) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                      "virtualip %s:Interface is not valid\n",
                                      vip->config.address);
                    status = SWITCH_STATUS_FALSE;
                    goto out;
                }
                switch_snprintf(vip->config.mac,18,"%s",mac);

                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                                  "device = %s with mac = %s\n",
                                  vip->config.device, vip->config.mac);

            } else if (!strcmp(var, "autoload")) {
                vip->config.autoload = SWITCH_FALSE;
                if (!strcmp(val, "true")) {
                    vip->config.autoload = SWITCH_TRUE;
                }
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                                  "Autoload = %s\n",
                                  (vip->config.autoload == SWITCH_TRUE)?
                                  "true":"false" );

            } else if (!strcmp(var, "autorollback")) {
                vip->config.autorollback = SWITCH_FALSE;
                if (!strcmp(val, "true")) {
                    vip->config.autorollback = SWITCH_TRUE;
                }
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                                  "Autorollback = %s\n",
                                  (vip->config.autorollback == SWITCH_TRUE)?
                                  "true":"false");
            } else if (!strcmp(var, "rollback-delay")) {
                vip->config.rollback_delay = atoi(val);
                if ( vip->config.rollback_delay == 0) {
                    vip->config.rollback_delay = 1;
                }
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                                  "Rollback delay = %d\n",
                                  vip->config.rollback_delay);
            } else if (!strcmp(var, "priority")) {
                vip->config.priority = atoi(val);
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                                  "Priority = %d\n", vip->config.priority);
            }
        }

        for (param = switch_xml_child(xvip, "profile");
                                             param; param = param->next) {
            char *profile_name =
                    (char *) switch_xml_attr_soft(param, "name");
            char *autorecover =
                    (char *) switch_xml_attr_soft(param, "autorecover");

            if (counter >= MAX_SOFIA_PROFILES) {
                switch_snprintf(vip->config.profiles[counter].name, 256,"");
                break;
            }
            if (utils_profile_control(profile_name)!=SWITCH_STATUS_SUCCESS) {

                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                  "Invalid profile name %s\n", profile_name);
                continue;
            }

            if (profile_name_is_present(vip->config.profiles,
                                        counter, profile_name)==SWITCH_TRUE) {

                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                                  "Profile %s is already present\n",
                                  profile_name);
                continue;
            }

            vip->config.profiles[counter].autorecover = SWITCH_FALSE;
            if (!strcmp(autorecover, "true")) {
                vip->config.profiles[counter].autorecover = SWITCH_TRUE;
            }

            switch_snprintf(vip->config.profiles[counter].name,
                            256,"%s",profile_name);

            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                              "profile = %s with autorecover = %s\n",
                              vip->config.profiles[counter].name,
                              (vip->config.profiles[counter].autorecover)?
                              "true":"false" );
            counter++;
        }

        status = switch_core_hash_insert(globals.virtual_ip_hash,
                                         vip->config.address, vip);
        if (status != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                              "Cannot insert virtual_ip data in hash");
            goto out;
        }
    }

out:
    switch_xml_free(xml);

    return status;
}

switch_bool_t
    profile_name_is_present(sofia_profile_t profiles[],int index, char *name)
{
    for (int i = 0; i < index; i++) {
        if (!strcmp(profiles[i].name, name)) return SWITCH_TRUE;
    }
    return SWITCH_FALSE;
}
