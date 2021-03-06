/*
 * coro_sched - A mini coroutine schedule framework
 * Copyright (C) 2014 xiongj(yuxingfirst@gmail.com).
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#include "cs_scheduler.h"
#include "cs_eventmgr.h"

struct scheduler *g_mastersched;
struct scheduler *g_parallelsched;
struct salfschedulebackadapter *g_schedulebackadapter;

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct rbtree coroutine_rbt; /*coroutine rbtree*/
static struct rbnode coroutine_rbs; /*coroutine rbtree sentinel*/

void sched_register_coro(coroutine* coro)
{
     rbtree_insert(&coroutine_rbt, &coro->node);
}

coroutine* sched_get_coro(coroid_t pid)
{
    struct rbnode *value = rbtree_lookup(&coroutine_rbt, pid);; 
    if(value == nil) {
        return nil; 
    } 
    return value->data;
}

void sched_run_once()
{
	while(!g_mastersched->stop) {
		if(!sched_has_task()) {
			break;
		}		
		coroutine *c = pop(&g_mastersched->wait_sched_queue);
		if(c == nil) {
			break;
		}
		if(c->status == M_EXIT) {
			continue;
		}	

		coro_ready(g_mastersched->current_coro);

		if(c->need_parallel) {
			coro_switch_to_parallel(c);
			continue;
		}
		coroutine *my = g_mastersched->current_coro;
		g_mastersched->current_coro = c;
		coro_switch(my, g_mastersched->current_coro);		
		break;
	}	
}

bool sched_has_task() {
    return !empty(&g_mastersched->wait_sched_queue); 
}

static void sched_run(void *arg) 
{
	int i = -1;
	while(!g_mastersched->stop) {
		coroutine *c = pop(&g_mastersched->wait_sched_queue);
		if(c == nil) {
			break;
		}
        if(c->need_parallel) {   
		    if(coro_sent_parallel(c) != M_ERR) {
                coro_ready_immediatly(c);   
            }
            continue; 
        }
		c->status = M_RUN;
		g_mastersched->current_coro = c;
		coro_switch(g_mastersched->sched_coro, g_mastersched->current_coro);
		//ASSERT(c == g_mastersched->current_coro);
		g_mastersched->current_coro = nil;
		if(c->status == M_EXIT) {	//need free
            rbtree_delete(&coroutine_rbt, &c->node);  
			coro_dealloc(c);
		}
	}
    log_warn("no tasks, exit scheduler");
	coro_transfer(&g_mastersched->sched_coro->ctx, &g_mastersched->main_coro);
}

void* parallel_main(void *arg)
{
    scheduler *psched = (scheduler*)arg;
    ASSERT(psched != nil);
    pthread_mutex_lock(&g_mutex);
    log(LOG_INFO, "...parallel environment start...");
    pthread_mutex_unlock(&g_mutex);
    coro_transfer(&psched->main_coro, &psched->sched_coro->ctx);
    return NULL;
}

static void parallel_run(void *arg)
{
    coroutine *co = nil;
    while(!g_parallelsched->stop) {
        if(pthread_mutex_trylock(&g_mutex) == 0) { 
            if(empty(&g_parallelsched->wait_sched_queue)) {
                usleep(1000 * 10);
                continue;
            }
            co = pop(&g_parallelsched->wait_sched_queue);
        } else {
            usleep(1000 * 10); 
            continue; 
        }
        ASSERT(co);
        coro_switch(g_parallelsched->sched_coro, co);
        rstatus_t rs = coro_switch_to_master(co);
        if(rs != M_OK) {
            log_error("switch back to master sched fail, coro->id:%ld", co->cid); 
        }
    }
    coro_transfer(&g_parallelsched->sched_coro->ctx, &g_parallelsched->main_coro);
}

static void scheduleback_run(void *arg)
{
    while(true) {
        uint32_t mask = ReadMask;
        event ev; 
        ev.mask = ReadMask;
        ev.sockfd = g_schedulebackadapter->readfd;
        ev.events = -1;
        ev.coro = g_schedulebackadapter->scbd_coro;
        register_event(g_eventmgr, &ev);
        remove_event(g_eventmgr, &ev);
        coroutine *c = nil;
        while(true) {
            ssize_t n = recv(ev.sockfd, (void*)&c, sizeof(struct coroutine*), MSG_DONTWAIT);
            if(n == sizeof(struct coroutine*)) {
                break;
            }
            if(n == 0) {
                log_warn("recv schedule back coroutine fail");
                break;
            }
            if(n < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        if(c != nil) {
            c->need_parallel = false;
            coro_ready(c);
        }
    }
}

rstatus_t env_init() 
{

    rstatus_t rs = M_ERR;

    g_mastersched = cs_alloc(sizeof(scheduler)); 
    g_parallelsched = cs_alloc(sizeof(scheduler)); 
    g_schedulebackadapter = cs_alloc(sizeof(salfschedulebackadapter));

    if(g_mastersched == nil || g_parallelsched == nil || g_schedulebackadapter == nil) {
        log_error("init scheduler malloc fail.");
        //cs_free(g_mastersched);
        //cs_free(g_parallel_sched);
        //cs_free(g_schedulebackadapter);
        return rs; 
    }
    {
        g_mastersched->stop = 0;
        g_mastersched->sched_coro = coro_alloc(&sched_run, nil, DEFAULT_STACK_SIZE);
        if(g_mastersched->sched_coro == nil) {
            log_error("init scheduler alloc sched_coro fail");
            return rs;
        }
        g_mastersched->current_coro = nil;
        TAILQ_INIT(&g_mastersched->wait_sched_queue);
    }

    {
        g_parallelsched->stop = 0;
        g_parallelsched->sched_coro = coro_alloc(&parallel_run, nil, DEFAULT_STACK_SIZE);
        g_parallelsched->current_coro = nil;
        TAILQ_INIT(&g_parallelsched->wait_sched_queue);
    }

    {
        int fd[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
        g_schedulebackadapter->scbd_coro = coro_alloc(&scheduleback_run, nil, DEFAULT_STACK_SIZE);
        g_schedulebackadapter->readfd = fd[0];
        g_schedulebackadapter->writefd = fd[1];
    }

    rbtree_init(&coroutine_rbt, &coroutine_rbs);

    rs = M_OK;
    rs = eventmgr_init(g_eventmgr, M_EVENT_SIZE_HINT);
	return rs;
}

rstatus_t env_run() 
{
	ASSERT(g_mastersched != nil && g_parallelsched != nil);

    pthread_t parallel_thread_id;
    if( pthread_create(&parallel_thread_id, nil, &parallel_main, g_parallelsched) != 0) {
        log_error("env_run create thread fail, errno:%d", errno);
        return M_ERR;
    }
    pthread_mutex_lock(&g_mutex);
    log(LOG_INFO, "env_run create thread succ, threadid:%lu", parallel_thread_id);
    log(LOG_INFO, "...master environment start...");
    pthread_mutex_unlock(&g_mutex);
	coro_transfer(&g_mastersched->main_coro, &g_mastersched->sched_coro->ctx);
	return M_OK;
}

void env_stop() 
{
	g_mastersched->stop = 1;
    g_mastersched->stop = 1;
    g_eventmgr->stop = 1; 
}

void insert_tail(coro_tqh *queue, coroutine* coro)
{
	TAILQ_INSERT_TAIL(queue, coro, ws_tqe);
}

void insert_head(coro_tqh *queue, coroutine* coro)
{
	TAILQ_INSERT_HEAD(queue, coro, ws_tqe);
}

bool empty(coro_tqh *queue) 
{
	return TAILQ_EMPTY(queue);
}

coroutine* pop(coro_tqh *queue) 
{
	coroutine *head = TAILQ_FIRST(queue);
	if(head == nil) {
		return nil;
	}
	TAILQ_REMOVE(queue, head, ws_tqe);
	return head;
}

