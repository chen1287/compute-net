#include "ip.h"
#include "icmp.h"
#include "rtable.h"
#include "arp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// handle ip packet
//
// If the packet is ICMP echo request and the destination IP address is equal to
// the IP address of the iface, send ICMP echo reply; otherwise, forward the
// packet.
void handle_ip_packet(iface_info_t *iface, char *packet, int len)
{
    struct iphdr *ip_hdr = packet_to_ip_hdr(packet);  // Extract the IP header
    struct icmphdr *icmp_hdr = (struct icmphdr *)IP_DATA(ip_hdr);  // ICMP header
    struct ether_header *eh = (struct ether_header *)(packet);  // Ethernet header

    u32 dst = htonl(ip_hdr->daddr);  // Destination IP in network byte order
    memcpy(eh->ether_shost, iface->mac, ETH_ALEN);  // Set source MAC address

    // If the destination is this router's IP and the packet is an ICMP echo request
    if (icmp_hdr->type == ICMP_ECHOREQUEST && iface->ip == dst)
    {
        icmp_send_packet(packet, len, ICMP_ECHOREPLY, ICMP_NET_UNREACH);
    }
    else
    {
        // Decrement TTL, and if it becomes 0, send an ICMP Time Exceeded message
        ip_hdr->ttl--;
        if (ip_hdr->ttl <= 0)
        {
            icmp_send_packet(packet, len, ICMP_TIME_EXCEEDED, ICMP_NET_UNREACH);
            free(packet);  // Free the packet since it's discarded
            return;
        }

        // Recompute the checksum after modifying the TTL
        ip_hdr->checksum = ip_checksum(ip_hdr);

        // Find the route to forward the packet
        rt_entry_t *entry = longest_prefix_match(dst);
        if (entry)
        {
            u32 dest = entry->gw ? entry->gw : dst;  // Use gateway if available, otherwise use the destination
            iface_send_packet_by_arp(entry->iface, dest, packet, len);
        }
        else
        {
            // No route found, send an ICMP Destination Unreachable message
            icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_NET_UNREACH);
        }
    }
}
