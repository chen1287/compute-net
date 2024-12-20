#include "ip.h"
#include "icmp.h"
#include "arpcache.h"
#include "rtable.h"
#include "arp.h"

// #include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// initialize IP header 
void ip_init_hdr(struct iphdr *ip, u32 saddr, u32 daddr, u16 len, u8 proto)
{
    ip->version = 4;
    ip->ihl = 5;
    ip->tos = 0;
    ip->tot_len = htons(len);
    ip->id = rand();
    ip->frag_off = htons(IP_DF);
    ip->ttl = DEFAULT_TTL;
    ip->protocol = proto;
    ip->saddr = htonl(saddr);
    ip->daddr = htonl(daddr);
    ip->checksum = ip_checksum(ip);
}

// lookup in the routing table, to find the entry with the same and longest prefix.
// The input address is in host byte order.
rt_entry_t *longest_prefix_match(u32 dst)
{
    rt_entry_t *entry;
    rt_entry_t *max = NULL;  // The longest prefix match entry
    u32 max_mask = 0;        // The mask of the longest prefix match

    // Traverse through the routing table and find the best match
    list_for_each_entry(entry, &rtable, list)
    {
        // Check if the destination address matches the route prefix and has the longest mask
        if (((entry->dest & entry->mask) == (dst & entry->mask)) && (entry->mask > max_mask))
        {
            max_mask = entry->mask;
            max = entry;
        }
    }
    return max;
}

// send IP packet
//
// Different from forwarding packet, ip_send_packet sends packets generated by
// the router itself. This function is used to send ICMP packets or other IP packets.
void ip_send_packet(char *packet, int len)
{
    struct iphdr *header = packet_to_ip_hdr(packet);  // Extract the IP header from the packet
    rt_entry_t *entry = longest_prefix_match(ntohl(header->daddr));  // Find the route for the destination address

    if (!entry)
    {
        // If no matching route is found, drop the packet
        fprintf(stderr, "No route found for destination IP: %s\n", inet_ntoa(*(struct in_addr *)&header->daddr));
        free(packet);
        return;
    }

    // If the router interface is in the same network as the destination IP, send directly
    // Otherwise, send through the gateway
    u32 dst = entry->gw ? entry->gw : ntohl(header->daddr);

    // Prepare Ethernet header
    struct ether_header *eh = (struct ether_header *)(packet);
    memcpy(eh->ether_shost, entry->iface->mac, ETH_ALEN);  // Set the source MAC address
    eh->ether_type = htons(ETH_P_IP);  // Set the EtherType to IP

    // Send the packet using ARP (if gateway is required)
    iface_send_packet_by_arp(entry->iface, dst, packet, len);
}
