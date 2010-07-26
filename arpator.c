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
/***************************************************************************************************

TOOL MOLTO SEMPLICE PER L'INVIO DI ARP

Funziona sicuramente per le arp-request o gratuitous arp (cioè quelle con opcode 0x01 e lenght 42)
E funzione anche con le reply (cioè opcode 0x02). Non capisco perché alcuni apparati mandino le
reply da 60 byte anziché 48! Con wireshark ho visto che nelle reply più lunghe ci sono 18 byte di
zeri di trailer... boh! Tanti altri apparati invece mandano sia request che reply da 42 byte!
Io faccio così! Sempre 42 byte.

***************************************************************************************************/
#include <switch.h>
#include "arpator.h"

struct ether_addr mac_broadcast;

in_addr_t * ip_aton( char * ip ){
	
	static 	short unsigned int 		index = 0;
	static 	in_addr_t 				result[ 8 ];
			in_addr_t *				ret;
	
	//per convertire c'e' gia' la funzione fatta
	result[ index ] = inet_addr( ip );

	//nel buffer circolare ci metto interi di 32 bit
	ret = &result[ index ];
	
	index = ( index + 1 ) & 7;
	return ret;
}

struct ether_addr * eth_aton( char * mac ){

	static 	short unsigned int 		index = 0;
	static 	struct ether_addr 		result[ 8 ];
			struct ether_addr * 	ret;
	
	//per convertire c'e' gia' la funzione fatta
	ret = ether_aton( mac );
	
	memcpy( &result[ index ], ret, sizeof( struct ether_addr ) );
	
	//nel buffer circolare ci metto strutture con un solo campo fatto dei 6 ottetti
	ret = &result[ index ];
	
	index = ( index + 1 ) & 7;
	return ret;
}

int ifx_aton( char * itrf ){
	
	struct ifreq 	ifr;
	int				sd;
	
	if( itrf == NULL ){
		
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"No interface specified!\n" );
		return -3;
	}
	
	
	sd = socket( AF_INET, SOCK_DGRAM, 0 );
	
	strncpy( ifr.ifr_name, itrf, IFNAMSIZ );
	
	//mando ifr (con solo il nome impostato) per richiedere con ioctl l'id
	if( ioctl( sd, SIOCGIFINDEX, &ifr ) < 0 ){
		
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Interface \"%s\" not found!\n", itrf );
		return -1;
	}
	
	//chiudo il socket
	close( sd );
	
	//restituisco il valore
	return ifr.ifr_ifindex;
}

int net_send_arp( struct ether_addr * mac_source, struct ether_addr * mac_destination, short unsigned int operation, struct ether_addr * mac_sender, in_addr_t * ip_sender, struct ether_addr * mac_target, in_addr_t * ip_target, int id ){

	//dichiaro le variabili necessarie alla funzione
	unsigned char			pktbuf[ 128 ];
	unsigned int			pktlen = 0;
	int 					sd = -1;

	//dichiaro le variabili che conterranno il pacchetto Arp
	struct sockaddr_ll	ll;
	struct ethhdr		eh;
	struct arphdr		ah;

	

	//creo il socket per la comunicazione
	if( ( sd = socket( AF_PACKET, SOCK_RAW, htons( ETH_P_ALL ) ) ) < 0 ){
		
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Can't have a raw socket, error: %s!\n", strerror( errno ) );
		return -1;
	}
	
	//inizializzo le varie strutture dati
	memset( &ll, 0, sizeof( struct sockaddr_ll ) );
	memset( &eh, 0, sizeof( struct ethhdr ) );
	memset( &ah, 0, sizeof( struct arphdr ) );

	//riempio l'header del socket di livello due
    ll.sll_ifindex 	= id;
	ll.sll_family 	= AF_PACKET;
    ll.sll_halen 	= sizeof( struct ether_addr );
	memcpy( ll.sll_addr, mac_destination, sizeof( struct ether_addr ) );
	
	if( !memcmp( mac_destination, &mac_broadcast, sizeof( struct ether_addr ) ) ) {
		
		int option;
		
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,"Broadcast socket...\n");
		
		//imposto il socket come broadcast
		option = 1;
		
		if( setsockopt( sd, SOL_SOCKET, SO_BROADCAST, &option, sizeof( option ) ) < 0 ){
			
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Can't set broadcast on socket: %d, error: %s!\n", sd, strerror( errno ) );
			close( sd );
			return -1;
		}
	}
	
	//riempio l'header ethernet
	memcpy( eh.h_source, mac_source, sizeof( struct ether_addr ) );
	memcpy( eh.h_dest, mac_destination, sizeof( struct ether_addr ) );
    eh.h_proto = htons( ETH_P_ARP );
	
	//riempio l'header arp con le informazioni necessarie sul protocollo
	ah.ar_hrd = htons( ARPHRD_ETHER );
	ah.ar_op  = htons( operation );	//costante ARPOP_REQUEST = 0x01 oppure ARPOP_REPLY = 0x02
    ah.ar_pro = htons( ETH_P_IP );
    ah.ar_hln = sizeof( struct ether_addr );
    ah.ar_pln = sizeof( in_addr_t );

	//aggiorno il contenuto del pacchetto
	memcpy( pktbuf, &eh, sizeof( struct ethhdr ) );
    pktlen += sizeof( struct ethhdr );
    memcpy( pktbuf + pktlen, &ah, sizeof( struct arphdr ) );
    pktlen += sizeof( struct arphdr );
	memcpy( pktbuf + pktlen, mac_sender, sizeof( struct ether_addr ) );
    pktlen += sizeof( struct ether_addr );
    memcpy( pktbuf + pktlen, ip_sender, sizeof( in_addr_t ) );
    pktlen += sizeof( in_addr_t );
    memcpy( pktbuf + pktlen, mac_target, sizeof( struct ether_addr ) );
    pktlen += sizeof( struct ether_addr );
    memcpy( pktbuf + pktlen, ip_target, sizeof( in_addr_t ) );
    pktlen += sizeof( in_addr_t );
	
	//invio il pacchetto sul socket
	if ( sendto( sd, pktbuf, pktlen, 0, ( struct sockaddr * ) &ll, sizeof( struct sockaddr_ll ) ) < 0 ){
		
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"Can't send arp packet on socket: %d, error: %s!\n", sd, strerror( errno ) );
		close( sd );
		return -1;
	}
	
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,"Sent arp packet (%d bytes) on interface with id: %d\n", pktlen, id );
	
/*	for(int i=0; i<pktlen; i++){*/
/*		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,"0x%02X \n", (unsigned char) pktbuf[i]);*/
/*	}*/
	
	//printf("\n");
	
	close( sd );
	return 0;

}

int net_send_arp_string( char * mac_source, char * mac_destination, short unsigned int operation, char * mac_sender, char * ip_sender, char * mac_target, char * ip_target, char * dev ){
	
	int result;
	
	memcpy( &mac_broadcast, eth_aton("ff:ff:ff:ff:ff:ff"), sizeof( struct ether_addr ) );
	
	result = net_send_arp( eth_aton(mac_source), eth_aton(mac_destination), operation, eth_aton(mac_sender), ip_aton(ip_sender), eth_aton(mac_target), ip_aton(ip_target), ifx_aton(dev) );
	
	return result;
}
