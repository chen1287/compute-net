#include "arpcache.h"
#include "arp.h"
#include "ether.h"
#include "icmp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static arpcache_t arpcache;

// initialize IP->mac mapping, request list, lock and sweeping thread
void arpcache_init()
{
    bzero(&arpcache, sizeof(arpcache_t));

    init_list_head(&(arpcache.req_list));
    pthread_mutex_init(&arpcache.lock, NULL);
    pthread_create(&arpcache.thread, NULL, arpcache_sweep, NULL);
}

// release all the resources when exiting
void arpcache_destroy()
{
    pthread_mutex_lock(&arpcache.lock);

    struct arp_req *req_entry = NULL, *req_q;
    list_for_each_entry_safe(req_entry, req_q, &(arpcache.req_list), list) {
        struct cached_pkt *pkt_entry = NULL, *pkt_q;
        list_for_each_entry_safe(pkt_entry, pkt_q, &(req_entry->cached_packets), list) {
            list_delete_entry(&(pkt_entry->list));
            free(pkt_entry->packet);
            free(pkt_entry);
        }

        list_delete_entry(&(req_entry->list));
        free(req_entry);
    }

    pthread_kill(arpcache.thread, SIGTERM);
    pthread_mutex_unlock(&arpcache.lock);
}

// helper function to find an empty entry in the ARP cache
static int find_empty_entry(u32 ip4)
{
    for (int i = 0; i < MAX_ARP_SIZE; i++) {
        if (!arpcache.entries[i].valid) {
            return i;
        }
    }
    return -1;
}

// lookup the IP->mac mapping
int arpcache_lookup(u32 ip4, u8 mac[ETH_ALEN])
{
    pthread_mutex_lock(&arpcache.lock);
    for (int i = 0; i < MAX_ARP_SIZE; i++) {
        if (arpcache.entries[i].ip4 == ip4 && arpcache.entries[i].valid) {
            memcpy(mac, arpcache.entries[i].mac, ETH_ALEN);
            pthread_mutex_unlock(&arpcache.lock);
            return 1;
        }
    }
    pthread_mutex_unlock(&arpcache.lock);
    return 0;
}

// append the packet to arpcache
void arpcache_append_packet(iface_info_t *iface, u32 ip4, char *packet, int len)
{
    pthread_mutex_lock(&arpcache.lock);
    int uncached = 1;
    struct arp_req *entry, *q;
    struct cached_pkt *pkt = (struct cached_pkt *)malloc(sizeof(struct cached_pkt));
    pkt->len = len;
    pkt->packet = packet;
    init_list_head(&pkt->list);

    // Search for an existing entry in the request list
    list_for_each_entry_safe(entry, q, &arpcache.req_list, list)
    {
        if (entry->ip4 == ip4) {
            list_add_tail(&pkt->list, &(entry->cached_packets));
            uncached = 0;
            break;
        }
    }

    if (uncached == 1) {
        // Create a new request entry if not found
        struct arp_req *new_req = (struct arp_req *)malloc(sizeof(struct arp_req));
        init_list_head(&new_req->list);
        new_req->iface = iface;
        new_req->ip4 = ip4;
        new_req->sent = time(NULL);
        new_req->retries = 0;
        init_list_head(&new_req->cached_packets);
        list_add_tail(&new_req->list, &arpcache.req_list);
        list_add_tail(&pkt->list, &new_req->cached_packets);
        arp_send_request(iface, ip4);
    }

    pthread_mutex_unlock(&arpcache.lock);
}

// insert the IP->mac mapping into arpcache, if there are pending packets
void arpcache_insert(u32 ip4, u8 mac[ETH_ALEN])
{
    pthread_mutex_lock(&arpcache.lock);
    int idx = find_empty_entry(ip4);

    // If an empty entry is found, insert the new entry
    if (idx >= 0) {
        arpcache.entries[idx].ip4 = ip4;
        memcpy(arpcache.entries[idx].mac, mac, ETH_ALEN);
        arpcache.entries[idx].added = time(NULL);
        arpcache.entries[idx].valid = 1;
    } else {
        // If no empty entry is available, replace a random entry
        srand(time(NULL));
        int index = rand() % MAX_ARP_SIZE;
        arpcache.entries[index].ip4 = ip4;
        memcpy(arpcache.entries[index].mac, mac, ETH_ALEN);
        arpcache.entries[index].added = time(NULL);
        arpcache.entries[index].valid = 1;
    }

    // Process and send any pending packets
    struct arp_req *entry, *q;
    list_for_each_entry_safe(entry, q, &arpcache.req_list, list)
    {
        if (entry->ip4 == ip4) {
            struct cached_pkt *pkt_entry, *pkt;
            list_for_each_entry_safe(pkt_entry, pkt, &entry->cached_packets, list)
            {
                struct ether_header *eh = (struct ether_header *)(pkt_entry->packet);
                memcpy(eh->ether_shost, entry->iface->mac, ETH_ALEN);
                memcpy(eh->ether_dhost, mac, ETH_ALEN);
                eh->ether_type = htons(ETH_P_IP);
                iface_send_packet(entry->iface, pkt_entry->packet, pkt_entry->len);
                list_delete_entry(&pkt_entry->list);
                free(pkt_entry);
            }
            list_delete_entry(&entry->list);
            free(entry);
        }
    }

    pthread_mutex_unlock(&arpcache.lock);
}

// sweep arpcache periodically
void *arpcache_sweep(void *arg) 
{
    while (1) {
        sleep(1);
        struct cached_pkt *tmp_list = (struct cached_pkt *)malloc(sizeof(struct cached_pkt));
        init_list_head(&tmp_list->list);

        pthread_mutex_lock(&arpcache.lock);

        // Remove expired entries
        for (int i = 0; i < MAX_ARP_SIZE; i++) {
            if (arpcache.entries[i].valid && (time(NULL) - arpcache.entries[i].added) > ARP_ENTRY_TIMEOUT)
                arpcache.entries[i].valid = 0;
        }

        // Process ARP requests
        struct arp_req *entry, *q;
        list_for_each_entry_safe(entry, q, &arpcache.req_list, list)
        {
            if ((time(NULL) - entry->sent) > 1 && entry->retries <= 5) {
                entry->sent = time(NULL);
                entry->retries++;
                arp_send_request(entry->iface, entry->ip4);
            } else if (entry->retries > ARP_REQUEST_MAX_RETRIES) {
                struct cached_pkt *pkt_entry, *pkt;
                list_for_each_entry_safe(pkt_entry, pkt, &entry->cached_packets, list)
                {
                    struct cached_pkt *tmp = (struct cached_pkt *)malloc(sizeof(struct cached_pkt));
                    init_list_head(&tmp->list);
                    tmp->len = pkt_entry->len;
                    tmp->packet = pkt_entry->packet;
                    list_add_tail(&tmp->list, &tmp_list->list);
                    list_delete_entry(&pkt_entry->list);
                    free(pkt_entry);
                }
                list_delete_entry(&entry->list);
                free(entry);
            }
        }

        pthread_mutex_unlock(&arpcache.lock);

        // Send ICMP unreachable messages for dropped packets
        struct cached_pkt *pkt_entry, *pkt;
        list_for_each_entry_safe(pkt_entry, pkt, &tmp_list->list, list)
        {
            icmp_send_packet(pkt_entry->packet, pkt_entry->len, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);
            list_delete_entry(&pkt_entry->list);
            free(pkt_entry);
        }
    }

    return NULL;
}
