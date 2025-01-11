#include "tcp.h"
#include "tcp_timer.h"
#include "tcp_sock.h"

#include <stdio.h>
#include <unistd.h>

static struct list_head timer_list;

// scan the timer_list, find the tcp sock which stays for at 2*MSL, release it
void tcp_scan_timer_list()
{
    struct tcp_sock *tsk;
    struct tcp_timer *timer, *q;

    // 遍历定时器列表，安全地移除已超时的定时器
    list_for_each_entry_safe(timer, q, &timer_list, list) 
    {
        // 减少定时器剩余时间
        timer->timeout -= TCP_TIMER_SCAN_INTERVAL;

        // 如果超时时间到，处理定时器事件
        if (timer->timeout <= 0) 
        {
            // 从定时器列表中删除该定时器
            list_delete_entry(&timer->list);

            // 获取定时器对应的TCP套接字
            tsk = timewait_to_tcp_sock(timer);

            // 如果套接字没有父级，解除其绑定
            if (!tsk->parent) 
            {
                tcp_bind_unhash(tsk);
            }

            // 将TCP状态设置为CLOSED并释放套接字
            tcp_set_state(tsk, TCP_CLOSED);
            free_tcp_sock(tsk);
        }
    }
}

// set the timewait timer of a tcp sock, by adding the timer into timer_list
void tcp_set_timewait_timer(struct tcp_sock *tsk)
{
    // 初始化timewait定时器类型和超时时间
    tsk->timewait.type = 0;
    tsk->timewait.timeout = TCP_TIMEWAIT_TIMEOUT;

    // 将定时器添加到全局定时器列表末尾
    list_add_tail(&tsk->timewait.list, &timer_list);

    // 增加TCP套接字的引用计数
    tsk->ref_cnt++;
}

// scan the timer_list periodically by calling tcp_scan_timer_list
void *tcp_timer_thread(void *arg)
{
	init_list_head(&timer_list);
	while (1) {
		usleep(TCP_TIMER_SCAN_INTERVAL);
		tcp_scan_timer_list();
	}

	return NULL;
}
