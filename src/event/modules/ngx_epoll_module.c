
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#if (NGX_TEST_BUILD_EPOLL)

/* epoll declarations */

#define EPOLLIN        0x001
#define EPOLLPRI       0x002
#define EPOLLOUT       0x004
#define EPOLLRDNORM    0x040
#define EPOLLRDBAND    0x080
#define EPOLLWRNORM    0x100
#define EPOLLWRBAND    0x200
#define EPOLLMSG       0x400
#define EPOLLERR       0x008
#define EPOLLHUP       0x010

#define EPOLLET        0x80000000
#define EPOLLONESHOT   0x40000000

#define EPOLL_CTL_ADD  1
#define EPOLL_CTL_DEL  2
#define EPOLL_CTL_MOD  3

typedef union epoll_data {
    void         *ptr;
    int           fd;
    uint32_t      u32;
    uint64_t      u64;
} epoll_data_t;

struct epoll_event {
    uint32_t      events;
    epoll_data_t  data;
};

int epoll_create(int size)
{
    return -1;
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    return -1;
}

int epoll_wait(int epfd, struct epoll_event *events, int nevents, int timeout)
{
    return -1;
}

#if (NGX_HAVE_FILE_AIO)

#define SYS_io_setup      245
#define SYS_io_destroy    246
#define SYS_io_getevents  247
#define SYS_eventfd       323

typedef u_int  aio_context_t;

struct io_event {
    uint64_t  data;  /* the data field from the iocb */
    uint64_t  obj;   /* what iocb this event came from */
    int64_t   res;   /* result code for this event */
    int64_t   res2;  /* secondary result */
};


int eventfd(u_int initval)
{
    return -1;
}

#endif
#endif


typedef struct {
    ngx_uint_t  events;
    ngx_uint_t  aio_requests;
} ngx_epoll_conf_t;


static ngx_int_t ngx_epoll_init(ngx_cycle_t *cycle, ngx_msec_t timer);
static void ngx_epoll_done(ngx_cycle_t *cycle);
static ngx_int_t ngx_epoll_add_event(ngx_event_t *ev, ngx_int_t event,
    ngx_uint_t flags);
static ngx_int_t ngx_epoll_del_event(ngx_event_t *ev, ngx_int_t event,
    ngx_uint_t flags);
static ngx_int_t ngx_epoll_add_connection(ngx_connection_t *c);
static ngx_int_t ngx_epoll_del_connection(ngx_connection_t *c,
    ngx_uint_t flags);
static ngx_int_t ngx_epoll_process_events(ngx_cycle_t *cycle, ngx_msec_t timer,
    ngx_uint_t flags);

#if (NGX_HAVE_FILE_AIO)
static void ngx_epoll_eventfd_handler(ngx_event_t *ev);
#endif

static void *ngx_epoll_create_conf(ngx_cycle_t *cycle);
static char *ngx_epoll_init_conf(ngx_cycle_t *cycle, void *conf);

static int                  ep = -1;							/* [analy] epoll描述符 */
static struct epoll_event  *event_list;							/* [analy] epoll从内核获取到的事件集合 */
static ngx_uint_t           nevents;							/* [analy] event_list事件集合列表的大小 */

#if (NGX_HAVE_FILE_AIO)

int                         ngx_eventfd = -1;
aio_context_t               ngx_aio_ctx = 0;

static ngx_event_t          ngx_eventfd_event;
static ngx_connection_t     ngx_eventfd_conn;

#endif

static ngx_str_t      epoll_name = ngx_string("epoll");

static ngx_command_t  ngx_epoll_commands[] = {

    { ngx_string("epoll_events"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_epoll_conf_t, events),
      NULL },

    { ngx_string("worker_aio_requests"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_epoll_conf_t, aio_requests),
      NULL },

      ngx_null_command
};


ngx_event_module_t  ngx_epoll_module_ctx = {
    &epoll_name,
    ngx_epoll_create_conf,               /* create configuration */
    ngx_epoll_init_conf,                 /* init configuration */

    {
        ngx_epoll_add_event,             /* add an event */
        ngx_epoll_del_event,             /* delete an event */
        ngx_epoll_add_event,             /* enable an event */
        ngx_epoll_del_event,             /* disable an event */
        ngx_epoll_add_connection,        /* add an connection */
        ngx_epoll_del_connection,        /* delete an connection */
        NULL,                            /* process the changes */
        ngx_epoll_process_events,        /* process the events */
        ngx_epoll_init,                  /* init the events */
        ngx_epoll_done,                  /* done the events */
    }
};

ngx_module_t  ngx_epoll_module = {
    NGX_MODULE_V1,
    &ngx_epoll_module_ctx,               /* module context */
    ngx_epoll_commands,                  /* module directives */
    NGX_EVENT_MODULE,                    /* module type */
    NULL,                                /* init master */
    NULL,                                /* init module */
    NULL,                                /* init process */
    NULL,                                /* init thread */
    NULL,                                /* exit thread */
    NULL,                                /* exit process */
    NULL,                                /* exit master */
    NGX_MODULE_V1_PADDING
};


#if (NGX_HAVE_FILE_AIO)

/*
 * We call io_setup(), io_destroy() io_submit(), and io_getevents() directly
 * as syscalls instead of libaio usage, because the library header file
 * supports eventfd() since 0.3.107 version only.
 *
 * Also we do not use eventfd() in glibc, because glibc supports it
 * since 2.8 version and glibc maps two syscalls eventfd() and eventfd2()
 * into single eventfd() function with different number of parameters.
 */

static int
io_setup(u_int nr_reqs, aio_context_t *ctx)
{
    return syscall(SYS_io_setup, nr_reqs, ctx);
}


static int
io_destroy(aio_context_t ctx)
{
    return syscall(SYS_io_destroy, ctx);
}


static int
io_getevents(aio_context_t ctx, long min_nr, long nr, struct io_event *events,
    struct timespec *tmo)
{
    return syscall(SYS_io_getevents, ctx, min_nr, nr, events, tmo);
}


static void
ngx_epoll_aio_init(ngx_cycle_t *cycle, ngx_epoll_conf_t *epcf)
{
    int                 n;
    struct epoll_event  ee;

    ngx_eventfd = syscall(SYS_eventfd, 0);

    if (ngx_eventfd == -1) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "eventfd() failed");
        ngx_file_aio = 0;
        return;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "eventfd: %d", ngx_eventfd);

    n = 1;

    if (ioctl(ngx_eventfd, FIONBIO, &n) == -1) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "ioctl(eventfd, FIONBIO) failed");
        goto failed;
    }

    if (io_setup(epcf->aio_requests, &ngx_aio_ctx) == -1) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "io_setup() failed");
        goto failed;
    }

    ngx_eventfd_event.data = &ngx_eventfd_conn;
    ngx_eventfd_event.handler = ngx_epoll_eventfd_handler;
    ngx_eventfd_event.log = cycle->log;
    ngx_eventfd_event.active = 1;
    ngx_eventfd_conn.fd = ngx_eventfd;
    ngx_eventfd_conn.read = &ngx_eventfd_event;
    ngx_eventfd_conn.log = cycle->log;

    ee.events = EPOLLIN|EPOLLET;
    ee.data.ptr = &ngx_eventfd_conn;

    if (epoll_ctl(ep, EPOLL_CTL_ADD, ngx_eventfd, &ee) != -1) {
        return;
    }

    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                  "epoll_ctl(EPOLL_CTL_ADD, eventfd) failed");

    if (io_destroy(ngx_aio_ctx) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "io_destroy() failed");
    }

failed:

    if (close(ngx_eventfd) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "eventfd close() failed");
    }

    ngx_eventfd = -1;
    ngx_aio_ctx = 0;
    ngx_file_aio = 0;
}

#endif


static ngx_int_t
ngx_epoll_init(ngx_cycle_t *cycle, ngx_msec_t timer)
{
    ngx_epoll_conf_t  *epcf;

    epcf = ngx_event_get_conf(cycle->conf_ctx, ngx_epoll_module);		/* [analy]	获取epoll配置结构 */

    if (ep == -1) {														/* [analysis???why divide 2?]	创建epoll句柄 */
        ep = epoll_create(cycle->connection_n / 2);

        if (ep == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "epoll_create() failed");
            return NGX_ERROR;
        }

#if (NGX_HAVE_FILE_AIO)

        ngx_epoll_aio_init(cycle, epcf);

#endif
    }

    if (nevents < epcf->events) {				/* [analy]	event_list是被监听的句柄的列表，默认最大为512个句柄 */
        if (event_list) {
            ngx_free(event_list);
        }

        event_list = ngx_alloc(sizeof(struct epoll_event) * epcf->events,
                               cycle->log);
        if (event_list == NULL) {
            return NGX_ERROR;
        }
    }

    nevents = epcf->events;

    ngx_io = ngx_os_io;

    ngx_event_actions = ngx_epoll_module_ctx.actions;			/* [analy]	通过宏定义很容易访问到事件处理模块的各个操作 */

#if (NGX_HAVE_CLEAR_EVENT)
    ngx_event_flags = NGX_USE_CLEAR_EVENT					/* [analy]	使用edge triggered */		
#else
    ngx_event_flags = NGX_USE_LEVEL_EVENT					/* [analy]	使用level triggered */
#endif
                      |NGX_USE_GREEDY_EVENT					/* [analy]	io的时候，直到EAGAIN为止 */
                      |NGX_USE_EPOLL_EVENT;					/* [analy]	epoll标志 */

    return NGX_OK;
}


static void
ngx_epoll_done(ngx_cycle_t *cycle)
{
    if (close(ep) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "epoll close() failed");
    }

    ep = -1;

#if (NGX_HAVE_FILE_AIO)

    if (ngx_eventfd != -1) {

        if (io_destroy(ngx_aio_ctx) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "io_destroy() failed");
        }

        if (close(ngx_eventfd) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "eventfd close() failed");
        }

        ngx_eventfd = -1;
    }

    ngx_aio_ctx = 0;

#endif

    ngx_free(event_list);

    event_list = NULL;
    nevents = 0;
}

/*
 * [analy] 通过调用epoll_ctl将事件添加到epoll时间队列里进行监听处理
 *		
 *	参数2： 读写事件
 *	参数3：	添加的事件类型（目前理解是仅包含ET和LT模式）
 *	说明：当添加的事件类型为读事件，将检查该描述符的写事件是否在活跃状态，如果是将使用EPOLL_CTL_MOD添加读事件
 *		  如果对应的事件不存在则使用EPOLL_CTL_ADD重新添加进epoll事件集合中，当添加的事件类型为写事件也同样如此。
 */
static ngx_int_t
ngx_epoll_add_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
	int                  op;
	uint32_t             events, prev;
	ngx_event_t         *e;
	ngx_connection_t    *c;
	struct epoll_event   ee;

	c = ev->data;

	events = (uint32_t) event;

	if (event == NGX_READ_EVENT) {
		e = c->write;
		prev = EPOLLOUT;
#if (NGX_READ_EVENT != EPOLLIN)
		events = EPOLLIN;
#endif

	} else {
		e = c->read;
		prev = EPOLLIN;
#if (NGX_WRITE_EVENT != EPOLLOUT)
		events = EPOLLOUT;
#endif
	}

	if (e->active) {
		op = EPOLL_CTL_MOD;
		events |= prev;

	} else {
		op = EPOLL_CTL_ADD;
	}

	ee.events = events | (uint32_t) flags;
	ee.data.ptr = (void *) ((uintptr_t) c | ev->instance);		/* [analy] ????????? */

	ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
		"epoll add event: fd:%d op:%d ev:%08XD",
		c->fd, op, ee.events);

	if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
		ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
			"epoll_ctl(%d, %d) failed", op, c->fd);
		return NGX_ERROR;
	}

	ev->active = 1;			/* [analy] 将此事件active标志激活 */
#if 0
	ev->oneshot = (flags & NGX_ONESHOT_EVENT) ? 1 : 0;
#endif

	return NGX_OK;
}


static ngx_int_t
ngx_epoll_del_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    int                  op;
    uint32_t             prev;
    ngx_event_t         *e;
    ngx_connection_t    *c;
    struct epoll_event   ee;

    /*
     * when the file descriptor is closed, the epoll automatically deletes
     * it from its queue, so we do not need to delete explicity the event
     * before the closing the file descriptor
     */

    if (flags & NGX_CLOSE_EVENT) {
        ev->active = 0;
        return NGX_OK;
    }

    c = ev->data;

    if (event == NGX_READ_EVENT) {
        e = c->write;
        prev = EPOLLOUT;

    } else {
        e = c->read;
        prev = EPOLLIN;
    }

    if (e->active) {
        op = EPOLL_CTL_MOD;
        ee.events = prev | (uint32_t) flags;
        ee.data.ptr = (void *) ((uintptr_t) c | ev->instance);

    } else {
        op = EPOLL_CTL_DEL;
        ee.events = 0;
        ee.data.ptr = NULL;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "epoll del event: fd:%d op:%d ev:%08XD",
                   c->fd, op, ee.events);

    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }

    ev->active = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_epoll_add_connection(ngx_connection_t *c)
{
    struct epoll_event  ee;

    ee.events = EPOLLIN|EPOLLOUT|EPOLLET;
    ee.data.ptr = (void *) ((uintptr_t) c | c->read->instance);				/* [analy] ????????? */

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "epoll add connection: fd:%d ev:%08XD", c->fd, ee.events);

    if (epoll_ctl(ep, EPOLL_CTL_ADD, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      "epoll_ctl(EPOLL_CTL_ADD, %d) failed", c->fd);
        return NGX_ERROR;
    }

    c->read->active = 1;
    c->write->active = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_epoll_del_connection(ngx_connection_t *c, ngx_uint_t flags)
{
    int                 op;
    struct epoll_event  ee;

    /*
     * when the file descriptor is closed the epoll automatically deletes
     * it from its queue so we do not need to delete explicity the event
     * before the closing the file descriptor
     */

    if (flags & NGX_CLOSE_EVENT) {
        c->read->active = 0;
        c->write->active = 0;
        return NGX_OK;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "epoll del connection: fd:%d", c->fd);

    op = EPOLL_CTL_DEL;
    ee.events = 0;
    ee.data.ptr = NULL;

    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }

    c->read->active = 0;				//	将读写事件active标志都置为非活跃状态
    c->write->active = 0;

    return NGX_OK;
}


/*
	简单了说，就是同一时刻只允许一个nginx worker在自己的epoll中处理监听句柄。它的负载均衡也很简单，当达到最大connection的7/8时，
	本worker不会去试图拿accept锁，也不会去处理新连接，这样其他nginx worker进程就更有机会去处理监听句柄，建立新连接了。而且，由于timeout的设定，
	使得没有拿到锁的worker进程，去拿锁的频繁更高。 
*/

static ngx_int_t
ngx_epoll_process_events(ngx_cycle_t *cycle, ngx_msec_t timer, ngx_uint_t flags)
{
    int                events;
    uint32_t           revents;
    ngx_int_t          instance, i;
    ngx_uint_t         level;
    ngx_err_t          err;
    ngx_event_t       *rev, *wev, **queue;
    ngx_connection_t  *c;

    /* NGX_TIMER_INFINITE == INFTIM */

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "epoll timer: %M", timer);

    events = epoll_wait(ep, event_list, (int) nevents, timer);			//	等待事件发生，超时或被信号中断

    err = (events == -1) ? ngx_errno : 0;								//	epoll_wait返回-1说明发生错误，相应的errno将被设置

	//	epoll_wait()返回后会更新系统缓存时间，如果timer_resolution指令设置后会周期性的来更新缓存时间
    if (flags & NGX_UPDATE_TIME || ngx_event_timer_alarm) {				
        ngx_time_update();
    }

	//	出错
    if (err) {
        if (err == NGX_EINTR) {						//	epoll_wait被信号中断时，检查是否被定时器中断，如果是返回OK

            if (ngx_event_timer_alarm) {
                ngx_event_timer_alarm = 0;
                return NGX_OK;
            }

            level = NGX_LOG_INFO;

        } else {
            level = NGX_LOG_ALERT;
        }

        ngx_log_error(level, cycle->log, err, "epoll_wait() failed");
        return NGX_ERROR;
    }

	//	超时， 返回
    if (events == 0) {							
        if (timer != NGX_TIMER_INFINITE) {
            return NGX_OK;
        }

        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                      "epoll_wait() returned no events without timeout");
        return NGX_ERROR;
    }

    ngx_mutex_lock(ngx_posted_events_mutex);

	//	循环处理所有发生的事件
    for (i = 0; i < events; i++) {					
        c = event_list[i].data.ptr;

        instance = (uintptr_t) c & 1;
        c = (ngx_connection_t *) ((uintptr_t) c & (uintptr_t) ~1);			

        rev = c->read;

		/* 
			fd在当前处理时变成-1，意味着在之前的事件处理时，把当前请求关闭了， 
			即close fd并且当前事件对应的连接已被还回连接池，此时该次事件就不应该处理了，作废掉。 
			其次，如果fd > 0,那么是否本次事件就可以正常处理，就可以认为是一个合法的呢？答案是否定的。 
			这里我们给出一个情景： 
			当前的事件序列是： A ... B ... C ... 
			其中A,B,C是本次epoll上报的其中一些事件，但是他们此时却相互牵扯： 
			A事件是向客户端写的事件，B事件是新连接到来，C事件是A事件中请求建立的upstream连接，此时需要读源数据， 
			然后A事件处理时，由于种种原因将C中upstream的连接关闭了(比如客户端关闭，此时需要同时关闭掉取源连接)，自然 
			C事件中请求对应的连接也被还到连接池(注意，客户端连接与upstream连接使用同一连接池)， 
			而B事件中的请求到来，获取连接池时，刚好拿到了之前C中upstream还回来的连接结构，当前需要处理C事件的时候， 
			c->fd != -1，因为该连接被B事件拿去接收请求了，而rev->instance在B使用时，已经将其值取反了，所以此时C事件epoll中 
			携带的instance就不等于rev->instance了，因此我们也就识别出该stale event，跳过不处理了。 
		*/ 
        if (c->fd == -1 || rev->instance != instance) {

            /*
             * the stale event from a file descriptor
             * that was just closed in this iteration
             */

            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "epoll: stale event %p", c);
            continue;
        }

        revents = event_list[i].events;			/* [analy]	获取发生的事件 */

        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "epoll: fd:%d ev:%04XD d:%p",
                       c->fd, revents, event_list[i].data.ptr);

        if (revents & (EPOLLERR|EPOLLHUP)) {
            ngx_log_debug2(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "epoll_wait() error on fd:%d ev:%04XD",
                           c->fd, revents);
        }

#if 0
        if (revents & ~(EPOLLIN|EPOLLOUT|EPOLLERR|EPOLLHUP)) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          "strange epoll_wait() events fd:%d ev:%04XD",
                          c->fd, revents);
        }
#endif

        if ((revents & (EPOLLERR|EPOLLHUP))
             && (revents & (EPOLLIN|EPOLLOUT)) == 0)
        {
            /*
             * if the error events were returned without EPOLLIN or EPOLLOUT,
             * then add these flags to handle the events at least in one
             * active handler
             */

            revents |= EPOLLIN|EPOLLOUT;
        }

		//	读事件触发并且此事件的read已经加入到epoll事件监控队列中
        if ((revents & EPOLLIN) && rev->active) {

            if ((flags & NGX_POST_THREAD_EVENTS) && !rev->accept) {
                rev->posted_ready = 1;

            } else {
                rev->ready = 1;							//	epoll走这里
            }

			//	如果进程持有accept锁， 对accpet事件和正常读写事件均采用延迟处理；只有获得了accpet锁后才能有NGX_POST_EVENTS标记
            if (flags & NGX_POST_EVENTS) {				
                queue = (ngx_event_t **) (rev->accept ?
                               &ngx_posted_accept_events : &ngx_posted_events);

                ngx_locked_post_event(rev, queue);			//	加入事件到延迟处理事件队列中

            } else {
                rev->handler(rev);							//	此时调用事件注册函数。 i.e. ngx_event_accept， ngx_http_init_request
            }
        }

        wev = c->write;

        if ((revents & EPOLLOUT) && wev->active) {

            if (c->fd == -1 || wev->instance != instance) {

                /*
                 * the stale event from a file descriptor
                 * that was just closed in this iteration
                 */

                ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                               "epoll: stale event %p", c);
                continue;
            }

            if (flags & NGX_POST_THREAD_EVENTS) {
                wev->posted_ready = 1;

            } else {
                wev->ready = 1;					//	epoll走这里
            }

            if (flags & NGX_POST_EVENTS) {
                ngx_locked_post_event(wev, &ngx_posted_events);

            } else {
                wev->handler(wev);
            }
        }
    }

    ngx_mutex_unlock(ngx_posted_events_mutex);

    return NGX_OK;
}


#if (NGX_HAVE_FILE_AIO)

static void
ngx_epoll_eventfd_handler(ngx_event_t *ev)
{
    int               n, events;
    long              i;
    uint64_t          ready;
    ngx_err_t         err;
    ngx_event_t      *e;
    ngx_event_aio_t  *aio;
    struct io_event   event[64];
    struct timespec   ts;

    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ev->log, 0, "eventfd handler");

    n = read(ngx_eventfd, &ready, 8);

    err = ngx_errno;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0, "eventfd: %d", n);

    if (n != 8) {
        if (n == -1) {
            if (err == NGX_EAGAIN) {
                return;
            }

            ngx_log_error(NGX_LOG_ALERT, ev->log, err, "read(eventfd) failed");
            return;
        }

        ngx_log_error(NGX_LOG_ALERT, ev->log, 0,
                      "read(eventfd) returned only %d bytes", n);
        return;
    }

    ts.tv_sec = 0;
    ts.tv_nsec = 0;

    while (ready) {

        events = io_getevents(ngx_aio_ctx, 1, 64, event, &ts);

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                       "io_getevents: %l", events);

        if (events > 0) {
            ready -= events;

            for (i = 0; i < events; i++) {

                ngx_log_debug4(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                               "io_event: %uXL %uXL %L %L",
                                event[i].data, event[i].obj,
                                event[i].res, event[i].res2);

                e = (ngx_event_t *) (uintptr_t) event[i].data;

                e->complete = 1;
                e->active = 0;
                e->ready = 1;

                aio = e->data;
                aio->res = event[i].res;

                ngx_post_event(e, &ngx_posted_events);
            }

            continue;
        }

        if (events == 0) {
            return;
        }

        /* events == -1 */
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                      "io_getevents() failed");
        return;
    }
}

#endif


static void *
ngx_epoll_create_conf(ngx_cycle_t *cycle)
{
    ngx_epoll_conf_t  *epcf;

    epcf = ngx_palloc(cycle->pool, sizeof(ngx_epoll_conf_t));
    if (epcf == NULL) {
        return NULL;
    }

    epcf->events = NGX_CONF_UNSET;
    epcf->aio_requests = NGX_CONF_UNSET;

    return epcf;
}


static char *
ngx_epoll_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_epoll_conf_t *epcf = conf;

    ngx_conf_init_uint_value(epcf->events, 512);
    ngx_conf_init_uint_value(epcf->aio_requests, 32);

    return NGX_CONF_OK;
}
