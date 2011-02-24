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

#include "cpg_utils.h"

#include <net/if.h>
#include <arpa/inet.h>
#include <netlink/route/addr.h>
#include <netlink/route/link.h>
#include "arpator.h"
#include "mod_cpg.h"

typedef enum {
    ADD_IP,
    DEL_IP
} cmd_t;

/* libnl bug */
extern inline void *nl_object_priv(struct nl_object *obj) {return obj;}

/*helpers*/
int init_handle(struct nl_handle **handle);
int ip_addr(char *ip4addr, char *interface, cmd_t cmd);

int init_handle(struct nl_handle **handle)
{

    *handle = nl_handle_alloc();
    if (!*handle)
        return -1;

    if (nl_connect(*handle, NETLINK_ROUTE)) {
        nl_handle_destroy(*handle);
        return -1;
    }
    return 0;
}

int ip_addr(char *ip4addr, char *interface, cmd_t cmd)
{
    struct nl_handle *nlh = NULL;
    struct rtnl_addr *addr = NULL;
    struct nl_addr *nl_addr = NULL;
    uint32_t binaddr = 0;
    int iface_idx = -1;
    int err,ret = 0;

    if (init_handle(&nlh) != 0) {
        return -1;
    }

    iface_idx = if_nametoindex(interface);
    if (iface_idx < 0) {
        return -1;
    }

    addr = rtnl_addr_alloc ();
    if (!addr) {
        return -1;
    }

    if (inet_pton(AF_INET, ip4addr, &binaddr) == 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                                      "not valid ip address\n");
        ret = -1;
        goto out;
    }

    nl_addr = nl_addr_build (AF_INET, &binaddr, sizeof(binaddr));
    if (!nl_addr) {
        ret = -1;
        goto out;
    }

    rtnl_addr_set_local (addr, nl_addr);
    nl_addr_put (nl_addr);

    rtnl_addr_set_ifindex (addr, iface_idx);
    switch (cmd) {
        case ADD_IP:
            err = rtnl_addr_add (nlh, addr, 0);
            if ( err == -17 ) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "%s is already on %s interface\n", ip4addr, interface);
                ret = 0;
            } else if ( err < 0 ) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                "error %d returned from rtnl_addr_add():\n%s\n",
                                                            err, nl_geterror());
                ret = -1;
            } else {
                ret = 0;
            }
            break;
        case DEL_IP:
            err = rtnl_addr_delete (nlh, addr, 0);
            if (err == -99) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                     "%s is not present on %s interface\n", ip4addr, interface);
                ret = 0;
            } else if (err < 0) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                             "error %d returned from rtnl_addr_delete():\n%s\n",
                                                            err, nl_geterror());
                ret = -1;
            } else {
                ret = 0;
            }
            break;
    }

out:
    if (addr) {
        rtnl_addr_put (addr);
    }
    if (nlh) {
        nl_close(nlh);
        nl_handle_destroy(nlh);
    }
    return ret;
}

/*helpers end*/

switch_status_t utils_add_vip(char *ip,char *dev)
{

    if ((!zstr(ip)) && (!zstr(dev))) {
        if (ip_addr(ip,dev, ADD_IP) < 0) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"cannot add IP %s\n", ip);
            return SWITCH_STATUS_FALSE;
        }
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"Ip added\n");
        return SWITCH_STATUS_SUCCESS;
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Invalid ip or device\n");
        return SWITCH_STATUS_FALSE;
    }
}

switch_status_t utils_remove_vip(char *ip,char *dev)
{

    if ((!zstr(ip)) && (!zstr(dev))) {
        if (ip_addr(ip,dev, DEL_IP) < 0) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Cannot remove Ip %s\n", ip);
            return SWITCH_STATUS_FALSE;
        }
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"Ip removed\n");
        return SWITCH_STATUS_SUCCESS;
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Invalid ip or device\n");
        return SWITCH_STATUS_FALSE;
    }
}

char *utils_get_mac_addr(char *interface)
{
    int buflen = 20;
    char *buf = NULL;
    struct nl_handle *nlh = NULL;
    struct nl_cache *cache = NULL;
    struct rtnl_link *link = NULL;
    struct nl_addr *addr = NULL;

    if (zstr(interface)) {
        return NULL;
    }

    if (init_handle(&nlh) != 0) {
        return NULL;
    }

    if ((cache = rtnl_link_alloc_cache(nlh)) == NULL) {
        return NULL;
    }

    if ((link = rtnl_link_get_by_name(cache, interface)) == NULL) {
        goto mac2str_error2;
    }

    if ((addr = rtnl_link_get_addr(link)) == NULL) {
        goto mac2str_error3;
    }

    if ((buf = calloc(sizeof(char *), buflen)) == NULL) {
        goto mac2str_error4;
    }

    buf = nl_addr2str(addr, buf, buflen);

mac2str_error4:
    nl_addr_destroy(addr);
mac2str_error3:
    rtnl_link_put(link);
mac2str_error2:
    nl_close(nlh);
    nl_handle_destroy(nlh);

    return buf;
}


void utils_reloadxml()
{
    const char *err;
    switch_xml_t xml_root;
    if ((xml_root = switch_xml_open_root(1, &err))) {

        switch_xml_free(xml_root);

    } else switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Reload XML [%s]\n", err);
    return;
}

switch_status_t utils_start_sofia_profile(char *profile_name)
{
    char cmd[128];
    char arg[128];
    switch_stream_handle_t mystream = { 0 };
    if (!zstr(profile_name)) {
        if (utils_profile_control(profile_name) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                    "profile %s doesn't exist in sip_profiles directory\n",profile_name);
            return SWITCH_STATUS_FALSE;
        }
        switch_snprintf(cmd, sizeof(cmd),"sofia");
        switch_snprintf(arg, sizeof(arg), "profile %s start",profile_name);
        SWITCH_STANDARD_STREAM(mystream);
        if (switch_api_execute(cmd, arg, NULL, &mystream) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"cannot execute sofia api %s\n", profile_name);
            return SWITCH_STATUS_FALSE;
        }
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"profile %s started\n", profile_name);
        switch_safe_free(mystream.data);
        return SWITCH_STATUS_SUCCESS;
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Invalid profile name\n");
        return SWITCH_STATUS_FALSE;
    }

}

switch_status_t utils_stop_sofia_profile(char *profile_name)
{
    char arg[128];
    switch_stream_handle_t mystream = { 0 };

    if (!zstr(profile_name)) {
        switch_snprintf(arg, sizeof(arg),"profile %s stop",profile_name);
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"sofia %s\n", arg);
        SWITCH_STANDARD_STREAM(mystream);

        if (switch_api_execute("sofia", arg, NULL, &mystream) != SWITCH_STATUS_SUCCESS){
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"cannot stop profile %s\n", profile_name);
            return SWITCH_STATUS_FALSE;
        }

        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"profile %s stopped\n", profile_name);
        switch_safe_free(mystream.data);
        return SWITCH_STATUS_SUCCESS;
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Invalid profile name\n");
        return SWITCH_STATUS_FALSE;
    }
    return SWITCH_STATUS_FALSE;
}

void utils_hupall(char *profile_name)
{
    char cmd[128];
    char arg[128];
    switch_stream_handle_t mystream = { 0 };

    switch_snprintf(cmd, sizeof(cmd),"hupall");
    switch_snprintf(arg, sizeof(arg),"normal_clearing sofia_profile_name %s",profile_name);
    SWITCH_STANDARD_STREAM(mystream);
    if (switch_api_execute(cmd, arg, NULL, &mystream) != SWITCH_STATUS_SUCCESS){
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"cannot hupall for %s\n",profile_name);
        return;
    }
    switch_safe_free(mystream.data);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"hupall for %s done\n",profile_name);
    return;
}
switch_status_t utils_recover(char *profile_name)
{
    char arg[128];
    switch_stream_handle_t mystream = { 0 };

    if (!zstr(profile_name)) {
        switch_snprintf(arg, sizeof(arg),"profile %s recover",profile_name);
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"sofia %s\n", arg);
        SWITCH_STANDARD_STREAM(mystream);

        if (switch_api_execute("sofia", arg, NULL, &mystream) != SWITCH_STATUS_SUCCESS){
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"cannot recover profile %s\n", profile_name);
            return SWITCH_STATUS_FALSE;
        }
        switch_safe_free(mystream.data);
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"profile %s recovered\n", profile_name);
        return SWITCH_STATUS_SUCCESS;
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Invalid profile name\n");
        return SWITCH_STATUS_FALSE;
    }
    return SWITCH_STATUS_FALSE;
}

switch_status_t utils_profile_control(char *profile_name)
{
    char pathname[128];
    switch_snprintf(pathname,sizeof(pathname),"%s/sip_profiles/%s.xml",SWITCH_GLOBAL_dirs.conf_dir,profile_name);
    return switch_file_exists(pathname,globals.pool);
}

void utils_send_track_event(char *sql, char *profile_name)
{
    switch_event_t *event = NULL;

    if (switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, "sofia::recovery_recv") == SWITCH_STATUS_SUCCESS) {
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "profile_name", profile_name);
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "sql",sql);
        switch_event_fire(&event);
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "recovery sent\n");
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "recovery not sent\n");
    }

}

void utils_send_request_all(char *profile_name)
{
    switch_event_t *event = NULL;

    if (switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, "sofia::recovery_recv") == SWITCH_STATUS_SUCCESS) {
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "profile_name", profile_name);
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "request_all","true");
        switch_event_fire(&event);
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "request_all sent\n");
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "request_all not sent\n");
    }

}

static int show_callback(void *pArg, int argc, char **argv, char **columnNames)
{
    int *count;
    count = pArg;

    (*count)++;
    return 0;
}

int utils_count_profile_channels(char *profile_name)
{
    char *sql;
    char *errmsg;
    switch_cache_db_handle_t *db;
    int count = 0;
    switch_status_t status;
    char hostname[256] = "";
    gethostname(hostname, sizeof(hostname));

    if (switch_core_db_handle(&db) != SWITCH_STATUS_SUCCESS) {
        return -1;
    }

    sql = switch_mprintf("select * from channels where hostname='%s' AND (name LIKE 'sofia/%s/%')", hostname, profile_name);

    if (!sql)
        return -1;

    status = switch_cache_db_execute_sql_callback(db, sql, show_callback, &count, &errmsg);

    switch_safe_free(sql);

    if (errmsg) {
        free(errmsg);
        errmsg = NULL;
    }

    if (db) {
        switch_cache_db_release_db_handle(&db);
    }
    if (status == SWITCH_STATUS_FALSE)
        return -1;

    return count;
}


switch_status_t
    utils_clean_up_table(char *runtime_uuid, char *sofia_profile_name)
{
    // clean up the table
    char *sql = NULL;

    sql = switch_mprintf("delete from sip_recovery where "
                         "runtime_uuid='%q' and profile_name='%q'",
                         runtime_uuid, sofia_profile_name);

    utils_send_track_event(sql, sofia_profile_name);
    switch_safe_free(sql);
    return SWITCH_STATUS_SUCCESS;
}

char * utils_node_pid_format(unsigned int nodeid) {
    static char buffer[100];
    struct in_addr saddr;
#if __BYTE_ORDER == __BIG_ENDIAN
    saddr.s_addr = swab32(nodeid);
#else
    saddr.s_addr = nodeid;
#endif
    sprintf(buffer, "%s", inet_ntoa(saddr));

    return buffer;
}

switch_bool_t utils_ip_is_valid(char *address) {

    unsigned char buf[sizeof(struct in6_addr)];
    int s;

    if (!address)
        return SWITCH_FALSE;
//TODO controlla anche ipv6
    s = inet_pton(AF_INET, address, buf);

    if (s != 1)
        return SWITCH_FALSE;

    return SWITCH_TRUE;

}
int utils_get_netmask(char *netmask) {

    int nm = atoi(netmask);
    return (nm <= 0 || nm > 32)? 32: nm;

}

switch_status_t utils_send_gARP(char *mac, char *address, char *device) {
    int ret = net_send_arp_string(mac, "ff:ff:ff:ff:ff:ff", 1,
                                  mac,address,mac, address, device);
    if (ret == 0)
        return SWITCH_STATUS_SUCCESS;

    return SWITCH_STATUS_FALSE;

}
