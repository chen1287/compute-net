#include "base.h"
#include <stdio.h>
extern ustack_t *instance;

void broadcast_packet(iface_info_t *iface, const char *packet, int len)
{
	// TODO: broadcast packet 
	iface_info_t *ifa=NULL;
	list_for_each_entry(ifa, &instance->iface_list, list){
		if(ifa->index!=iface->index)
		{
			iface_send_packet(ifa,packet,len);
			// fprintf(stdout, "TODO: broadcast packet.\n");
		}
//		if(memcmp(&iface->mac,&ifa->mac,sizeof(iface->mac)))
	}
	
}
