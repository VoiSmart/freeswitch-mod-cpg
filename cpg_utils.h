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
#ifndef CPG_UTILS_H
#define CPG_UTILS_H
#include <switch.h>
#include "mod_cpg.h"
#include "profile.h"

/*system utils*/
switch_status_t utils_add_vip(char *ip,char *dev);
switch_status_t utils_remove_vip(char *ip,char *dev);
switch_status_t utils_add_arp_rule(char *ip,char *mac);
switch_status_t utils_remove_arp_rule(char *ip,char *mac);
char *utils_get_mac_addr(char *dev);

/*local utils*/
char *utils_state_to_string(profile_state_t pstate);
profile_state_t utils_string_to_state(char *state);

/*freeswitch related utils*/
switch_status_t utils_profile_control(char *profile_name);
switch_status_t utils_start_sofia_profile(char *profile_name);
switch_status_t utils_stop_sofia_profile(char *profile_name);
void utils_hupall(char *profile_name);
void utils_reloadxml();
void utils_send_track_event(char *sql, char *profile_name);
switch_status_t utils_recover(char *profile_name);
int utils_count_profile_channels(char *profile_name);

/*corosync utils*/
char *utils_node_pid_format(unsigned int nodeid);
#endif
