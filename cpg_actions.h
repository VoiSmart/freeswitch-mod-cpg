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
#ifndef CPG_ACTIONS_H
#define CPG_ACTIONS_H

#include "profile.h"

switch_status_t from_standby_to_init(profile_t *profile);
switch_status_t from_init_to_backup(profile_t *profile);
switch_status_t from_init_to_master(profile_t *profile);
switch_status_t from_backup_to_master(profile_t *profile);
switch_status_t from_master_to_standby(profile_t *profile);
switch_status_t from_backup_to_standby(profile_t *profile);
switch_status_t from_init_to_standby(profile_t *profile);

switch_status_t go_to_standby(profile_t *profile);


#endif
