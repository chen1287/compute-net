#include "tcp.h"
#include "tcp_sock.h"
#include "tcp_timer.h"

#include "log.h"
#include "ring_buffer.h"

#include <stdlib.h>
// update the snd_wnd of tcp_sock
//
// if the snd_wnd before updating is zero, notify tcp_sock_send (wait_send)
static inline void tcp_update_window(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	u16 old_snd_wnd = tsk->snd_wnd;
	tsk->snd_wnd = cb->rwnd;
	if (old_snd_wnd == 0)
		wake_up(tsk->wait_send);
}

// update the snd_wnd safely: cb->ack should be between snd_una and snd_nxt
static inline void tcp_update_window_safe(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	if (less_or_equal_32b(tsk->snd_una, cb->ack) && less_or_equal_32b(cb->ack, tsk->snd_nxt))
		tcp_update_window(tsk, cb);
}

#ifndef max
#	define max(x,y) ((x)>(y) ? (x) : (y))
#endif

// check whether the sequence number of the incoming packet is in the receiving
// window
static inline int is_tcp_seq_valid(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	u32 rcv_end = tsk->rcv_nxt + max(tsk->rcv_wnd, 1);
	if (less_than_32b(cb->seq, rcv_end) && less_or_equal_32b(tsk->rcv_nxt, cb->seq_end)) {
		return 1;
	}
	else {
		log(ERROR, "received packet with invalid seq, drop it.");
		return 0;
	}
}

struct tcp_sock *alloc_child_tcp_sock(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	struct tcp_sock *child = alloc_tcp_sock();
	memcpy((char *)child, (char *)tsk, sizeof(struct tcp_sock));
	child->parent = tsk;
	child->sk_sip = cb->daddr;
	child->sk_sport = cb->dport;
	child->sk_dip = cb->saddr;
	child->sk_dport = cb->sport;
	child->iss = tcp_new_iss();
	child->snd_nxt = child->iss;
	child->rcv_nxt = cb->seq + 1;
	tcp_sock_listen_enqueue(child);
	tcp_set_state(child, TCP_SYN_RECV);
	tcp_hash(child);
	return child;
}

void handle_recv_data(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	while (ring_buffer_free(tsk->rcv_buf) < cb->pl_len) {
		sleep_on(tsk->wait_recv);
	}

	pthread_mutex_lock(&tsk->rcv_buf->rw_lock);
	write_ring_buffer(tsk->rcv_buf, cb->payload, cb->pl_len);
	tsk->rcv_wnd -= cb->pl_len;
	pthread_mutex_unlock(&tsk->rcv_buf->rw_lock);
	wake_up(tsk->wait_recv);

	tsk->rcv_nxt = cb->seq + cb->pl_len;
	tsk->snd_una = cb->ack;
	tcp_send_control_packet(tsk, TCP_ACK);
}

// Process the incoming packet according to TCP state machine. 
void tcp_process(struct tcp_sock *tsk, struct tcp_cb *cb, char *packet)
{
	//fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
	struct tcphdr *tcp = packet_to_tcp_hdr(packet);
	if (tcp->flags & TCP_RST) {
		tcp_sock_close(tsk);
		return;
	}

	switch (tsk->state) {
		case TCP_LISTEN: {
			if (tcp->flags & TCP_SYN) {
				//tcp_set_state(tsk, TCP_SYN_RECV);
				struct tcp_sock *child = alloc_child_tcp_sock(tsk, cb);
				tcp_send_control_packet(child, TCP_SYN|TCP_ACK);
			}
			return;
		}
		case TCP_SYN_SENT: {
			if (tcp->flags & (TCP_ACK | TCP_SYN)) {
				tcp_set_state(tsk, TCP_ESTABLISHED);
				tsk->rcv_nxt = cb->seq + 1;
		    	tsk->snd_una = cb->ack;
				wake_up(tsk->wait_connect);
				tcp_send_control_packet(tsk, TCP_ACK);
			}
			return;
		}
		case TCP_SYN_RECV: {
			if (tcp->flags & TCP_ACK) {
				if (tcp_sock_accept_queue_full(tsk->parent)) {
					return;
				}
				struct tcp_sock *child = tcp_sock_listen_dequeue(tsk->parent);
				if (child != tsk) {
					log(ERROR, "child != tsk\n");
				}
				tcp_sock_accept_enqueue(tsk);
				tcp_set_state(tsk, TCP_ESTABLISHED);
				tsk->rcv_nxt = cb->seq;
		        tsk->snd_una = cb->ack;
				wake_up(tsk->parent->wait_accept);
			}
			return;
		}
		default: {
			break;
		}
	}

	if (!is_tcp_seq_valid(tsk, cb)) {
		return;
	}

	switch (tsk->state) {
		case TCP_ESTABLISHED: {
			if (tcp->flags & TCP_FIN) {
				tcp_set_state(tsk, TCP_CLOSE_WAIT);
				tsk->rcv_nxt = cb->seq + 1;
				tsk->snd_una = cb->ack;
				tcp_send_control_packet(tsk, TCP_ACK);
				wake_up(tsk->wait_recv);
			}
			else if (tcp->flags & TCP_ACK) {
				if (cb->pl_len == 0) {
					tsk->rcv_nxt = cb->seq;
					tsk->snd_una = cb->ack;
					tcp_update_window_safe(tsk, cb);
				}
				else {
					handle_recv_data(tsk, cb);
				}
			}
			break;
		}
		case TCP_LAST_ACK: {
			if (tcp->flags & TCP_ACK) {
				tcp_set_state(tsk, TCP_CLOSED);
				tsk->rcv_nxt = cb->seq;
				tsk->snd_una = cb->ack;
				tcp_unhash(tsk);
				//tcp_bind_unhash(tsk);
			}
			break;
		}
		case TCP_FIN_WAIT_1: {
			if (tcp->flags & TCP_ACK) {
				tcp_set_state(tsk, TCP_FIN_WAIT_2);
				tsk->rcv_nxt = cb->seq;
				tsk->snd_una = cb->ack;
			}
			break;
		}
		case TCP_FIN_WAIT_2: {
			if (tcp->flags & TCP_FIN) {
				tcp_set_state(tsk, TCP_TIME_WAIT);
				tsk->rcv_nxt = cb->seq + 1;
				tsk->snd_una = cb->ack;
				tcp_send_control_packet(tsk, TCP_ACK);
				tcp_set_timewait_timer(tsk);
			}
			break;
		}
		default: {
			break;
		}
	}
}
