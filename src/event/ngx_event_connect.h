
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_EVENT_CONNECT_H_INCLUDED_
#define _NGX_EVENT_CONNECT_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#define NGX_PEER_KEEPALIVE           1
#define NGX_PEER_NEXT                2
#define NGX_PEER_FAILED              4


typedef struct ngx_peer_connection_s  ngx_peer_connection_t;

typedef ngx_int_t (*ngx_event_get_peer_pt)(ngx_peer_connection_t *pc,
    void *data);
typedef void (*ngx_event_free_peer_pt)(ngx_peer_connection_t *pc, void *data,
    ngx_uint_t state);
#if (NGX_SSL)

typedef ngx_int_t (*ngx_event_set_peer_session_pt)(ngx_peer_connection_t *pc,
    void *data);
typedef void (*ngx_event_save_peer_session_pt)(ngx_peer_connection_t *pc,
    void *data);
#endif


struct ngx_peer_connection_s {
    ngx_connection_t                *connection;			//	与后端连接使用的connection，连接池中的(ngx_event_connect_peer()函数中设置)

    struct sockaddr                 *sockaddr;				//	ngx_http_upstream_rr_peer_t的sockaddr；	在函数 ngx_http_upstream_get_round_robin_peer() 中设置
    socklen_t                        socklen;				//	ngx_http_upstream_rr_peer_t的socklen；	在函数 ngx_http_upstream_get_round_robin_peer() 中设置
    ngx_str_t                       *name;					//	ngx_http_upstream_rr_peer_t的name；		在函数 ngx_http_upstream_get_round_robin_peer() 中设置

    ngx_uint_t                       tries;					//	tries = 后端服务器IP个数， 在 ngx_http_upstream_init_round_robin_peer() 中设置

    ngx_event_get_peer_pt            get;					//	不同模块采用不同的方式 ngx_http_upstream_get_ip_hash_peer() 和 ngx_http_upstream_get_round_robin_peer()
    ngx_event_free_peer_pt           free;
	void                            *data;					//	保存负载均衡模块使用的数据；存放多个模块 ngx_http_upstream_rr_peer_data_t 、ngx_http_upstream_keepalive_peer_data_t、ngx_http_upstream_rr_peer_data_t

#if (NGX_SSL)
    ngx_event_set_peer_session_pt    set_session;
    ngx_event_save_peer_session_pt   save_session;
#endif

#if (NGX_THREADS)
    ngx_atomic_t                    *lock;
#endif
 
    ngx_addr_t                      *local;					//	向后端服务器发送请求前，绑定本地IP和端口的信息；指令 "proxy_bind" 设置 （ngx_http_upstream_init_request()函数中设置）

    int                              rcvbuf;

    ngx_log_t                       *log;

    unsigned                         cached:1;				//	说明此连接被缓存了（再ngx_http_upstream_get_keepalive_peer（）中设置）

                                     /* ngx_connection_log_error_e */
    unsigned                         log_error:2;
};


ngx_int_t ngx_event_connect_peer(ngx_peer_connection_t *pc);
ngx_int_t ngx_event_get_peer(ngx_peer_connection_t *pc, void *data);


#endif /* _NGX_EVENT_CONNECT_H_INCLUDED_ */
