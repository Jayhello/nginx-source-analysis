
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct ngx_http_proxy_rewrite_s  ngx_http_proxy_rewrite_t;

typedef ngx_int_t (*ngx_http_proxy_rewrite_pt)(ngx_http_request_t *r,
    ngx_table_elt_t *h, size_t prefix, size_t len,
    ngx_http_proxy_rewrite_t *pr);

struct ngx_http_proxy_rewrite_s {
    ngx_http_proxy_rewrite_pt      handler;

    union {
        ngx_http_complex_value_t   complex;
#if (NGX_PCRE)
        ngx_http_regex_t          *regex;
#endif
    } pattern;

    ngx_http_complex_value_t       replacement;
};


typedef struct {
    ngx_str_t                      key_start;				//	
    ngx_str_t                      schema;					//	"http://" 或 "https://"
    ngx_str_t                      host_header;				//	url中的host					(ngx_http_proxy_set_var()函数中设置)
    ngx_str_t                      port;					//	端口
    ngx_str_t                      uri;						//	url中的uri（ngx_http_proxy_set_vars()函数中设置）
} ngx_http_proxy_vars_t;


typedef struct {
    ngx_http_upstream_conf_t       upstream;

    ngx_array_t                   *flushes;					//	存放指令 "proxy_set_body" 和 "proxy_set_header" 中使用的变量在索引变量数组中的index
    ngx_array_t                   *body_set_len;			//	指令 "proxy_set_body" 使用变量时，将处理变量和常量字符串长度的函数加入到此列表里
    ngx_array_t                   *body_set;				//	指令 "proxy_set_body" 使用变量时，将处理变量和常量字符串函数加入到此列表里
    ngx_array_t                   *headers_set_len;			//	存放 proxy_header 的脚本解析长度数组
    ngx_array_t                   *headers_set;				//	存放 proxy_header 的脚本解析常量字符串或变量处理数组
    ngx_hash_t                     headers_set_hash;		//	存放 proxy_header 的hash表(包括 "proxy_set_header" 和 系统自定义的)

    ngx_array_t                   *headers_source;			//	指令"proxy_set_header"	array of ngx_keyval_t

    ngx_array_t                   *proxy_lengths;			//	指令 "proxy_pass" 使用变量时，将处理变量和常量字符串长度的函数加入到此列表里
    ngx_array_t                   *proxy_values;			//	指令 "proxy_pass" 使用变量时，将处理变量和常量字符串函数加入到此列表里

    ngx_array_t                   *redirects;				//	array of ngx_http_proxy_rewrite_t (ngx_http_proxy_redirect())
    ngx_array_t                   *cookie_domains;
    ngx_array_t                   *cookie_paths;

    ngx_str_t                      body_source;				//	指令 "proxy_set_body " 设定

    ngx_str_t                      method;					//	指令 "proxy_method" 指定发送到后端服务器的发送方式
    ngx_str_t                      location;				//	所属location层的name字段的值 /hellobaidu
    ngx_str_t                      url;						//	指向proxy_pass参数部分例如："http://www.baidu.com"

#if (NGX_HTTP_CACHE)
    ngx_http_complex_value_t       cache_key;				//	存放指令 "proxy_cache_key" 设置的值
#endif

    ngx_http_proxy_vars_t          vars;

    ngx_flag_t                     redirect;							//	ngx_http_proxy_merge_loc_conf()函数中设置

    ngx_uint_t                     http_version;						//	向后端发送请求时，请求头中的HTTP协议版本；指令"proxy_http_version "设置，默认1.0

    ngx_uint_t                     headers_hash_max_size;				//	指令"proxy_headers_hash_max_size"设置
    ngx_uint_t                     headers_hash_bucket_size;			//	指令"proxy_headers_hash_bucket_size"设置
} ngx_http_proxy_loc_conf_t;


typedef struct {
    ngx_http_status_t              status;
    ngx_http_proxy_vars_t          vars;
    size_t                         internal_body_length;

    ngx_uint_t                     state;
    off_t                          size;
    off_t                          length;

    ngx_uint_t                     head;					//	使用的method为HEAD, 在ngx_http_proxy_create_request()中设置					/* unsigned  head:1 */
} ngx_http_proxy_ctx_t;


static ngx_int_t ngx_http_proxy_eval(ngx_http_request_t *r,
    ngx_http_proxy_ctx_t *ctx, ngx_http_proxy_loc_conf_t *plcf);
#if (NGX_HTTP_CACHE)
static ngx_int_t ngx_http_proxy_create_key(ngx_http_request_t *r);
#endif
static ngx_int_t ngx_http_proxy_create_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_proxy_reinit_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_proxy_process_status_line(ngx_http_request_t *r);
static ngx_int_t ngx_http_proxy_process_header(ngx_http_request_t *r);
static ngx_int_t ngx_http_proxy_input_filter_init(void *data);
static ngx_int_t ngx_http_proxy_copy_filter(ngx_event_pipe_t *p,
    ngx_buf_t *buf);
static ngx_int_t ngx_http_proxy_chunked_filter(ngx_event_pipe_t *p,
    ngx_buf_t *buf);
static ngx_int_t ngx_http_proxy_non_buffered_copy_filter(void *data,
    ssize_t bytes);
static ngx_int_t ngx_http_proxy_non_buffered_chunked_filter(void *data,
    ssize_t bytes);
static void ngx_http_proxy_abort_request(ngx_http_request_t *r);
static void ngx_http_proxy_finalize_request(ngx_http_request_t *r,
    ngx_int_t rc);

static ngx_int_t ngx_http_proxy_host_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_proxy_port_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t
    ngx_http_proxy_add_x_forwarded_for_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t
    ngx_http_proxy_internal_body_length_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_proxy_rewrite_redirect(ngx_http_request_t *r,
    ngx_table_elt_t *h, size_t prefix);
static ngx_int_t ngx_http_proxy_rewrite_cookie(ngx_http_request_t *r,
    ngx_table_elt_t *h);
static ngx_int_t ngx_http_proxy_rewrite_cookie_value(ngx_http_request_t *r,
    ngx_table_elt_t *h, u_char *value, ngx_array_t *rewrites);
static ngx_int_t ngx_http_proxy_rewrite(ngx_http_request_t *r,
    ngx_table_elt_t *h, size_t prefix, size_t len, ngx_str_t *replacement);

static ngx_int_t ngx_http_proxy_add_variables(ngx_conf_t *cf);
static void *ngx_http_proxy_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_proxy_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_http_proxy_merge_headers(ngx_conf_t *cf,
    ngx_http_proxy_loc_conf_t *conf, ngx_http_proxy_loc_conf_t *prev);

static char *ngx_http_proxy_pass(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_proxy_redirect(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_proxy_cookie_domain(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_proxy_cookie_path(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_proxy_store(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
#if (NGX_HTTP_CACHE)
static char *ngx_http_proxy_cache(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_proxy_cache_key(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
#endif

static char *ngx_http_proxy_lowat_check(ngx_conf_t *cf, void *post, void *data);

static ngx_int_t ngx_http_proxy_rewrite_regex(ngx_conf_t *cf,
    ngx_http_proxy_rewrite_t *pr, ngx_str_t *regex, ngx_uint_t caseless);

#if (NGX_HTTP_SSL)
static ngx_int_t ngx_http_proxy_set_ssl(ngx_conf_t *cf,
    ngx_http_proxy_loc_conf_t *plcf);
#endif
static void ngx_http_proxy_set_vars(ngx_url_t *u, ngx_http_proxy_vars_t *v);


static ngx_conf_post_t  ngx_http_proxy_lowat_post =
    { ngx_http_proxy_lowat_check };


static ngx_conf_bitmask_t  ngx_http_proxy_next_upstream_masks[] = {
    { ngx_string("error"), NGX_HTTP_UPSTREAM_FT_ERROR },
    { ngx_string("timeout"), NGX_HTTP_UPSTREAM_FT_TIMEOUT },
    { ngx_string("invalid_header"), NGX_HTTP_UPSTREAM_FT_INVALID_HEADER },
    { ngx_string("http_500"), NGX_HTTP_UPSTREAM_FT_HTTP_500 },
    { ngx_string("http_502"), NGX_HTTP_UPSTREAM_FT_HTTP_502 },
    { ngx_string("http_503"), NGX_HTTP_UPSTREAM_FT_HTTP_503 },
    { ngx_string("http_504"), NGX_HTTP_UPSTREAM_FT_HTTP_504 },
    { ngx_string("http_404"), NGX_HTTP_UPSTREAM_FT_HTTP_404 },
    { ngx_string("updating"), NGX_HTTP_UPSTREAM_FT_UPDATING },
    { ngx_string("off"), NGX_HTTP_UPSTREAM_FT_OFF },
    { ngx_null_string, 0 }
};


static ngx_conf_enum_t  ngx_http_proxy_http_version[] = {
    { ngx_string("1.0"), NGX_HTTP_VERSION_10 },
    { ngx_string("1.1"), NGX_HTTP_VERSION_11 },
    { ngx_null_string, 0 }
};


ngx_module_t  ngx_http_proxy_module;


static ngx_command_t  ngx_http_proxy_commands[] = {

    { ngx_string("proxy_pass"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_HTTP_LMT_CONF|NGX_CONF_TAKE1,
      ngx_http_proxy_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("proxy_redirect"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE12,
      ngx_http_proxy_redirect,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("proxy_cookie_domain"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE12,
      ngx_http_proxy_cookie_domain,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("proxy_cookie_path"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE12,
      ngx_http_proxy_cookie_path,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("proxy_store"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_proxy_store,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("proxy_store_access"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE123,
      ngx_conf_set_access_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.store_access),
      NULL },

    { ngx_string("proxy_buffering"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.buffering),
      NULL },

    { ngx_string("proxy_ignore_client_abort"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.ignore_client_abort),
      NULL },

    { ngx_string("proxy_bind"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_upstream_bind_set_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.local),
      NULL },

    { ngx_string("proxy_connect_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.connect_timeout),
      NULL },

    { ngx_string("proxy_send_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.send_timeout),
      NULL },

    { ngx_string("proxy_send_lowat"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.send_lowat),
      &ngx_http_proxy_lowat_post },

    { ngx_string("proxy_intercept_errors"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.intercept_errors),
      NULL },

    { ngx_string("proxy_set_header"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_conf_set_keyval_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, headers_source),
      NULL },

    { ngx_string("proxy_headers_hash_max_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, headers_hash_max_size),
      NULL },

    { ngx_string("proxy_headers_hash_bucket_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, headers_hash_bucket_size),
      NULL },

    { ngx_string("proxy_set_body"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, body_source),
      NULL },

    { ngx_string("proxy_method"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, method),
      NULL },

    { ngx_string("proxy_pass_request_headers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.pass_request_headers),
      NULL },

    { ngx_string("proxy_pass_request_body"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.pass_request_body),
      NULL },

    { ngx_string("proxy_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.buffer_size),
      NULL },

    { ngx_string("proxy_read_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.read_timeout),
      NULL },

    { ngx_string("proxy_buffers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_conf_set_bufs_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.bufs),
      NULL },

    { ngx_string("proxy_busy_buffers_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.busy_buffers_size_conf),
      NULL },

#if (NGX_HTTP_CACHE)

    { ngx_string("proxy_cache"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_proxy_cache,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("proxy_cache_key"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_proxy_cache_key,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("proxy_cache_path"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_2MORE,
      ngx_http_file_cache_set_slot,
      0,
      0,
      &ngx_http_proxy_module },

    { ngx_string("proxy_cache_bypass"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_set_predicate_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.cache_bypass),
      NULL },

    { ngx_string("proxy_no_cache"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_set_predicate_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.no_cache),
      NULL },

    { ngx_string("proxy_cache_valid"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_file_cache_valid_set_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.cache_valid),
      NULL },

    { ngx_string("proxy_cache_min_uses"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.cache_min_uses),
      NULL },

    { ngx_string("proxy_cache_use_stale"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.cache_use_stale),
      &ngx_http_proxy_next_upstream_masks },

    { ngx_string("proxy_cache_methods"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.cache_methods),
      &ngx_http_upstream_cache_method_mask },

    { ngx_string("proxy_cache_lock"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.cache_lock),
      NULL },

    { ngx_string("proxy_cache_lock_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.cache_lock_timeout),
      NULL },

#endif

    { ngx_string("proxy_temp_path"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1234,
      ngx_conf_set_path_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.temp_path),
      NULL },

    { ngx_string("proxy_max_temp_file_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.max_temp_file_size_conf),
      NULL },

    { ngx_string("proxy_temp_file_write_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.temp_file_write_size_conf),
      NULL },

    { ngx_string("proxy_next_upstream"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.next_upstream),
      &ngx_http_proxy_next_upstream_masks },

    { ngx_string("proxy_pass_header"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.pass_headers),
      NULL },

    { ngx_string("proxy_hide_header"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.hide_headers),
      NULL },

    { ngx_string("proxy_ignore_headers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.ignore_headers),
      &ngx_http_upstream_ignore_headers_masks },

    { ngx_string("proxy_http_version"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, http_version),
      &ngx_http_proxy_http_version },

#if (NGX_HTTP_SSL)

    { ngx_string("proxy_ssl_session_reuse"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_proxy_loc_conf_t, upstream.ssl_session_reuse),
      NULL },

#endif

      ngx_null_command
};


static ngx_http_module_t  ngx_http_proxy_module_ctx = {
    ngx_http_proxy_add_variables,          /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_proxy_create_loc_conf,        /* create location configuration */
    ngx_http_proxy_merge_loc_conf          /* merge location configuration */
};


ngx_module_t  ngx_http_proxy_module = {
    NGX_MODULE_V1,
    &ngx_http_proxy_module_ctx,            /* module context */
    ngx_http_proxy_commands,               /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static char  ngx_http_proxy_version[] = " HTTP/1.0" CRLF;
static char  ngx_http_proxy_version_11[] = " HTTP/1.1" CRLF;

//	发送到后端服务器的头域
static ngx_keyval_t  ngx_http_proxy_headers[] = {
    { ngx_string("Host"), ngx_string("$proxy_host") },
    { ngx_string("Connection"), ngx_string("close") },
    { ngx_string("Keep-Alive"), ngx_string("") },
    { ngx_string("Expect"), ngx_string("") },
    { ngx_string("Upgrade"), ngx_string("") },
    { ngx_null_string, ngx_null_string }
};

/*
 *	默认不转发后端服务器到客户端的响应头域列表
 */
static ngx_str_t  ngx_http_proxy_hide_headers[] = {
    ngx_string("Date"),
    ngx_string("Server"),
    ngx_string("X-Pad"),
    ngx_string("X-Accel-Expires"),
    ngx_string("X-Accel-Redirect"),
    ngx_string("X-Accel-Limit-Rate"),
    ngx_string("X-Accel-Buffering"),
    ngx_string("X-Accel-Charset"),
    ngx_null_string
};


#if (NGX_HTTP_CACHE)

static ngx_keyval_t  ngx_http_proxy_cache_headers[] = {
    { ngx_string("Host"), ngx_string("$proxy_host") },
    { ngx_string("Connection"), ngx_string("close") },
    { ngx_string("Keep-Alive"), ngx_string("") },
    { ngx_string("Expect"), ngx_string("") },
    { ngx_string("Upgrade"), ngx_string("") },
    { ngx_string("If-Modified-Since"), ngx_string("") },
    { ngx_string("If-Unmodified-Since"), ngx_string("") },
    { ngx_string("If-None-Match"), ngx_string("") },
    { ngx_string("If-Match"), ngx_string("") },
    { ngx_string("Range"), ngx_string("") },
    { ngx_string("If-Range"), ngx_string("") },
    { ngx_null_string, ngx_null_string }
};

#endif


static ngx_http_variable_t  ngx_http_proxy_vars[] = {

    { ngx_string("proxy_host"), NULL, ngx_http_proxy_host_variable, 0,
      NGX_HTTP_VAR_CHANGEABLE|NGX_HTTP_VAR_NOCACHEABLE|NGX_HTTP_VAR_NOHASH, 0 },

    { ngx_string("proxy_port"), NULL, ngx_http_proxy_port_variable, 0,
      NGX_HTTP_VAR_CHANGEABLE|NGX_HTTP_VAR_NOCACHEABLE|NGX_HTTP_VAR_NOHASH, 0 },

    { ngx_string("proxy_add_x_forwarded_for"), NULL,
      ngx_http_proxy_add_x_forwarded_for_variable, 0, NGX_HTTP_VAR_NOHASH, 0 },

#if 0
    { ngx_string("proxy_add_via"), NULL, NULL, 0, NGX_HTTP_VAR_NOHASH, 0 },
#endif

    { ngx_string("proxy_internal_body_length"), NULL,
      ngx_http_proxy_internal_body_length_variable, 0, NGX_HTTP_VAR_NOHASH, 0 },

    { ngx_null_string, NULL, NULL, 0, 0, 0 }
};

//	默认"proxy_temp" 1、2、0
static ngx_path_init_t  ngx_http_proxy_temp_path = {
    ngx_string(NGX_HTTP_PROXY_TEMP_PATH), { 1, 2, 0 }
};


static ngx_int_t
ngx_http_proxy_handler(ngx_http_request_t *r)
{
    ngx_int_t                   rc;
    ngx_http_upstream_t        *u;
    ngx_http_proxy_ctx_t       *ctx;
    ngx_http_proxy_loc_conf_t  *plcf;

	//	1. 在客户端的r->upstream上创建ngx_http_upstream_t
    if (ngx_http_upstream_create(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

	//	2. 申请proxy模块使用的上下文空间并设置到request结构的ctx上
    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_proxy_ctx_t));
    if (ctx == NULL) {
        return NGX_ERROR;
    }

    ngx_http_set_ctx(r, ctx, ngx_http_proxy_module);				//	设置proxy模块的上下文结构

    plcf = ngx_http_get_module_loc_conf(r, ngx_http_proxy_module);

    u = r->upstream;			//	在函数 ngx_http_upstream_create() 中创建

    if (plcf->proxy_lengths == NULL) {
        ctx->vars = plcf->vars;
        u->schema = plcf->vars.schema;
#if (NGX_HTTP_SSL)
        u->ssl = (plcf->upstream.ssl != NULL);
#endif

    } else {
        if (ngx_http_proxy_eval(r, ctx, plcf) != NGX_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
    }

    u->output.tag = (ngx_buf_tag_t) &ngx_http_proxy_module;

    u->conf = &plcf->upstream;										//	将proxy模块的 ngx_http_upstream_conf_t 赋值给运转结构 ngx_http_upstream_t

#if (NGX_HTTP_CACHE)
    u->create_key = ngx_http_proxy_create_key;
#endif

    u->create_request = ngx_http_proxy_create_request;				//	注册-拼装到后端服务器请求的处理函数
    u->reinit_request = ngx_http_proxy_reinit_request;
    u->process_header = ngx_http_proxy_process_status_line;			//	注册-解析响应行处理函数
    u->abort_request = ngx_http_proxy_abort_request;
    u->finalize_request = ngx_http_proxy_finalize_request;
    r->state = 0;

    if (plcf->redirects) {
        u->rewrite_redirect = ngx_http_proxy_rewrite_redirect;
    }

    if (plcf->cookie_domains || plcf->cookie_paths) {
        u->rewrite_cookie = ngx_http_proxy_rewrite_cookie;
    }

    u->buffering = plcf->upstream.buffering;						//	设置buffering，是否缓存后端服务器的响应

    u->pipe = ngx_pcalloc(r->pool, sizeof(ngx_event_pipe_t));		//	创建 ngx_event_pipe_t
    if (u->pipe == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    u->pipe->input_filter = ngx_http_proxy_copy_filter;
    u->pipe->input_ctx = r;

    u->input_filter_init = ngx_http_proxy_input_filter_init;
    u->input_filter = ngx_http_proxy_non_buffered_copy_filter;
    u->input_filter_ctx = r;

    u->accel = 1;

    rc = ngx_http_read_client_request_body(r, ngx_http_upstream_init);

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    return NGX_DONE;
}


static ngx_int_t
ngx_http_proxy_eval(ngx_http_request_t *r, ngx_http_proxy_ctx_t *ctx,
    ngx_http_proxy_loc_conf_t *plcf)
{
    u_char               *p;
    size_t                add;
    u_short               port;
    ngx_str_t             proxy;
    ngx_url_t             url;
    ngx_http_upstream_t  *u;

    if (ngx_http_script_run(r, &proxy, plcf->proxy_lengths->elts, 0,
                            plcf->proxy_values->elts)
        == NULL)
    {
        return NGX_ERROR;
    }

    if (proxy.len > 7
        && ngx_strncasecmp(proxy.data, (u_char *) "http://", 7) == 0)
    {
        add = 7;
        port = 80;

#if (NGX_HTTP_SSL)

    } else if (proxy.len > 8
               && ngx_strncasecmp(proxy.data, (u_char *) "https://", 8) == 0)
    {
        add = 8;
        port = 443;
        r->upstream->ssl = 1;

#endif

    } else {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "invalid URL prefix in \"%V\"", &proxy);
        return NGX_ERROR;
    }

    u = r->upstream;

	//	设置schema
    u->schema.len = add;
    u->schema.data = proxy.data;

    ngx_memzero(&url, sizeof(ngx_url_t));

    url.url.len = proxy.len - add;
    url.url.data = proxy.data + add;
    url.default_port = port;
    url.uri_part = 1;
    url.no_resolve = 1;						//	不解析域名（即不调用gethostbyname）

    if (ngx_parse_url(r->pool, &url) != NGX_OK) {
        if (url.err) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "%s in upstream \"%V\"", url.err, &url.url);
        }

        return NGX_ERROR;
    }

    if (url.uri.len) {
        if (url.uri.data[0] == '?') {
            p = ngx_pnalloc(r->pool, url.uri.len + 1);
            if (p == NULL) {
                return NGX_ERROR;
            }

            *p++ = '/';
            ngx_memcpy(p, url.uri.data, url.uri.len);

            url.uri.len++;
            url.uri.data = p - 1;
        }
    }

	//	重新设置 key_start
    ctx->vars.key_start = u->schema;

    ngx_http_proxy_set_vars(&url, &ctx->vars);

    u->resolved = ngx_pcalloc(r->pool, sizeof(ngx_http_upstream_resolved_t));
    if (u->resolved == NULL) {
        return NGX_ERROR;
    }

    if (url.addrs && url.addrs[0].sockaddr) {
        u->resolved->sockaddr = url.addrs[0].sockaddr;
        u->resolved->socklen = url.addrs[0].socklen;
        u->resolved->naddrs = 1;
        u->resolved->host = url.addrs[0].name;

    } else {
        u->resolved->host = url.host;
        u->resolved->port = (in_port_t) (url.no_port ? port : url.port);
        u->resolved->no_port = url.no_port;
    }

    return NGX_OK;
}


#if (NGX_HTTP_CACHE)

static ngx_int_t
ngx_http_proxy_create_key(ngx_http_request_t *r)
{
    size_t                      len, loc_len;
    u_char                     *p;
    uintptr_t                   escape;
    ngx_str_t                  *key;
    ngx_http_upstream_t        *u;
    ngx_http_proxy_ctx_t       *ctx;
    ngx_http_proxy_loc_conf_t  *plcf;

    u = r->upstream;

    plcf = ngx_http_get_module_loc_conf(r, ngx_http_proxy_module);

    ctx = ngx_http_get_module_ctx(r, ngx_http_proxy_module);

    key = ngx_array_push(&r->cache->keys);
    if (key == NULL) {
        return NGX_ERROR;
    }

	//	检查指令"proxy_cache_key"是否使用了
    if (plcf->cache_key.value.len) {

        if (ngx_http_complex_value(r, &plcf->cache_key, key) != NGX_OK) {
            return NGX_ERROR;
        }

        return NGX_OK;
    }

	//	指令 "proxy_cache_key" 未使用时，添加schema+host、uri到 r->cache->keys 中
    *key = ctx->vars.key_start;

    key = ngx_array_push(&r->cache->keys);
    if (key == NULL) {
        return NGX_ERROR;
    }

    if (plcf->proxy_lengths && ctx->vars.uri.len) {

        *key = ctx->vars.uri;
        u->uri = ctx->vars.uri;

        return NGX_OK;

    } else if (ctx->vars.uri.len == 0 && r->valid_unparsed_uri && r == r->main)
    {
        *key = r->unparsed_uri;
        u->uri = r->unparsed_uri;

        return NGX_OK;
    }

    loc_len = (r->valid_location && ctx->vars.uri.len) ? plcf->location.len : 0;

    if (r->quoted_uri || r->internal) {
        escape = 2 * ngx_escape_uri(NULL, r->uri.data + loc_len,
                                    r->uri.len - loc_len, NGX_ESCAPE_URI);
    } else {
        escape = 0;
    }

    len = ctx->vars.uri.len + r->uri.len - loc_len + escape
          + sizeof("?") - 1 + r->args.len;

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    key->data = p;

    if (r->valid_location) {
        p = ngx_copy(p, ctx->vars.uri.data, ctx->vars.uri.len);
    }

    if (escape) {
        ngx_escape_uri(p, r->uri.data + loc_len,
                       r->uri.len - loc_len, NGX_ESCAPE_URI);
        p += r->uri.len - loc_len + escape;

    } else {
        p = ngx_copy(p, r->uri.data + loc_len, r->uri.len - loc_len);
    }

    if (r->args.len > 0) {
        *p++ = '?';
        p = ngx_copy(p, r->args.data, r->args.len);
    }

    key->len = p - key->data;
    u->uri = *key;

    return NGX_OK;
}

#endif


static ngx_int_t
ngx_http_proxy_create_request(ngx_http_request_t *r)
{
    size_t                        len, uri_len, loc_len, body_len;
    uintptr_t                     escape;
    ngx_buf_t                    *b;
    ngx_str_t                     method;
    ngx_uint_t                    i, unparsed_uri;
    ngx_chain_t                  *cl, *body;
    ngx_list_part_t              *part;
    ngx_table_elt_t              *header;
    ngx_http_upstream_t          *u;
    ngx_http_proxy_ctx_t         *ctx;
    ngx_http_script_code_pt       code;
    ngx_http_script_engine_t      e, le;
    ngx_http_proxy_loc_conf_t    *plcf;
    ngx_http_script_len_code_pt   lcode;

    u = r->upstream;

    plcf = ngx_http_get_module_loc_conf(r, ngx_http_proxy_module);				

    if (u->method.len) {
        /* HEAD was changed to GET to cache response */
        method = u->method;
        method.len++;

    } else if (plcf->method.len) {
        method = plcf->method;

    } else {
        method = r->method_name;		//	获取请求中的method字符串
        method.len++;					//	长度包含空格的大小
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_proxy_module);							//	这里获取在ngx_http_proxy_handler()中设置的ctx

	//	是否使用了"HEAD "请求方法
    if (method.len == 5
        && ngx_strncasecmp(method.data, (u_char *) "HEAD ", 5) == 0)
    {
        ctx->head = 1;
    }

    len = method.len + sizeof(ngx_http_proxy_version) - 1 + sizeof(CRLF) - 1;			//	计算请求行的长度（method + http_version + CRLF）。没有URL的长度

    escape = 0;
    loc_len = 0;
    unparsed_uri = 0;


	//	处理URI????????
    if (plcf->proxy_lengths && ctx->vars.uri.len) {
        uri_len = ctx->vars.uri.len;

    } else if (ctx->vars.uri.len == 0 && r->valid_unparsed_uri && r == r->main)
    {
        unparsed_uri = 1;
        uri_len = r->unparsed_uri.len;

    } else {
        loc_len = (r->valid_location && ctx->vars.uri.len) ?
                      plcf->location.len : 0;

        if (r->quoted_uri || r->space_in_uri || r->internal) {
            escape = 2 * ngx_escape_uri(NULL, r->uri.data + loc_len,
                                        r->uri.len - loc_len, NGX_ESCAPE_URI);
        }

        uri_len = ctx->vars.uri.len + r->uri.len - loc_len + escape
                  + sizeof("?") - 1 + r->args.len;
    }

    if (uri_len == 0) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "zero length URI to proxy");
        return NGX_ERROR;
    }

    len += uri_len;				//	请求行的长度加上 uri 的长度

    ngx_http_script_flush_no_cacheable_variables(r, plcf->flushes);

	//	如果指令"proxy_set_body"使用了变量，将运行此
    if (plcf->body_set_len) {
        le.ip = plcf->body_set_len->elts;
        le.request = r;
        le.flushed = 1;
        body_len = 0;

        while (*(uintptr_t *) le.ip) {
            lcode = *(ngx_http_script_len_code_pt *) le.ip;
            body_len += lcode(&le);
        }

        ctx->internal_body_length = body_len;				//	计算展开后的"proxy_set_body"指令指定的body长度
        len += body_len;									//	加上 body 的长度
    }

    le.ip = plcf->headers_set_len->elts;
    le.request = r;
    le.flushed = 1;

    while (*(uintptr_t *) le.ip) {
        while (*(uintptr_t *) le.ip) {
            lcode = *(ngx_http_script_len_code_pt *) le.ip;
            len += lcode(&le);								//	加上proxy header 的长度
        }
        le.ip += sizeof(uintptr_t);
    }


	/*	计算长度
	 *	指令 "proxy_pass_request_headers" 定义client端的request是否可以通过proxy
	 *	这里将client端的request-header与指令"proxy_set_header"和系统自定义的header比较
	 *	如果没有在"proxy_set_header"和系统自定义表中将向后端服务器直接发送此头 
	 */
    if (plcf->upstream.pass_request_headers) {
        part = &r->headers_in.headers.part;				//	获取client的请求头
        header = part->elts;	

        for (i = 0; /* void */; i++) {

            if (i >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }

                part = part->next;
                header = part->elts;
                i = 0;
            }

            if (ngx_hash_find(&plcf->headers_set_hash, header[i].hash,
                              header[i].lowcase_key, header[i].key.len))
            {
                continue;
            }

            len += header[i].key.len + sizeof(": ") - 1					//	加上client端发送的请求头中的header和value长度
                + header[i].value.len + sizeof(CRLF) - 1;
        }
    }


	//	创建buf
    b = ngx_create_temp_buf(r->pool, len);
    if (b == NULL) {
        return NGX_ERROR;
    }


	//	创建chain
    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = b;


	//	向刚申请的buf中填充请求数据

    /* the request line */
	//	method
    b->last = ngx_copy(b->last, method.data, method.len);

    u->uri.data = b->last;

	//	uri
    if (plcf->proxy_lengths && ctx->vars.uri.len) {
        b->last = ngx_copy(b->last, ctx->vars.uri.data, ctx->vars.uri.len);

    } else if (unparsed_uri) {
        b->last = ngx_copy(b->last, r->unparsed_uri.data, r->unparsed_uri.len);

    } else {
        if (r->valid_location) {
            b->last = ngx_copy(b->last, ctx->vars.uri.data, ctx->vars.uri.len);
        }

        if (escape) {
            ngx_escape_uri(b->last, r->uri.data + loc_len,
                           r->uri.len - loc_len, NGX_ESCAPE_URI);
            b->last += r->uri.len - loc_len + escape;

        } else {
            b->last = ngx_copy(b->last, r->uri.data + loc_len,
                               r->uri.len - loc_len);
        }

        if (r->args.len > 0) {
            *b->last++ = '?';
            b->last = ngx_copy(b->last, r->args.data, r->args.len);
        }
    }

    u->uri.len = b->last - u->uri.data;			//	设置 ngx_http_upstream_t 的uri字段

	//	设置http版本
    if (plcf->http_version == NGX_HTTP_VERSION_11) {
        b->last = ngx_cpymem(b->last, ngx_http_proxy_version_11,
                             sizeof(ngx_http_proxy_version_11) - 1);

    } else {
        b->last = ngx_cpymem(b->last, ngx_http_proxy_version,
                             sizeof(ngx_http_proxy_version) - 1);
    }

    ngx_memzero(&e, sizeof(ngx_http_script_engine_t));

    e.ip = plcf->headers_set->elts;
    e.pos = b->last;						//	此处将 e.pos 指向上边申请的 buf 缓冲区
    e.request = r;
    e.flushed = 1;

    le.ip = plcf->headers_set_len->elts;

    while (*(uintptr_t *) le.ip) {
        lcode = *(ngx_http_script_len_code_pt *) le.ip;

        /* skip the header line name length */
        (void) lcode(&le);

        if (*(ngx_http_script_len_code_pt *) le.ip) {

            for (len = 0; *(uintptr_t *) le.ip; len += lcode(&le)) {				//	计算len长度
                lcode = *(ngx_http_script_len_code_pt *) le.ip;
            }

            e.skip = (len == sizeof(CRLF) - 1) ? 1 : 0;

        } else {
            e.skip = 0;
        }

        le.ip += sizeof(uintptr_t);

        while (*(uintptr_t *) e.ip) {
            code = *(ngx_http_script_code_pt *) e.ip;
            code((ngx_http_script_engine_t *) &e);
        }
        e.ip += sizeof(uintptr_t);
    }

    b->last = e.pos;			//	设置buf的last指针


	//	检查是否允许client端的request header是否可以通过proxy,如果允许将拷贝个别的header
    if (plcf->upstream.pass_request_headers) {
        part = &r->headers_in.headers.part;
        header = part->elts;

        for (i = 0; /* void */; i++) {

            if (i >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }

                part = part->next;
                header = part->elts;
                i = 0;
            }

            if (ngx_hash_find(&plcf->headers_set_hash, header[i].hash,
                              header[i].lowcase_key, header[i].key.len))
            {
                continue;
            }

            b->last = ngx_copy(b->last, header[i].key.data, header[i].key.len);

            *b->last++ = ':'; *b->last++ = ' ';

            b->last = ngx_copy(b->last, header[i].value.data,
                               header[i].value.len);

            *b->last++ = CR; *b->last++ = LF;

            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "http proxy header: \"%V: %V\"",
                           &header[i].key, &header[i].value);
        }
    }


    /* add "\r\n" at the header end */
    *b->last++ = CR; *b->last++ = LF;

	//	拷贝body
    if (plcf->body_set) {
        e.ip = plcf->body_set->elts;
        e.pos = b->last;

        while (*(uintptr_t *) e.ip) {
            code = *(ngx_http_script_code_pt *) e.ip;
            code((ngx_http_script_engine_t *) &e);
        }

        b->last = e.pos;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http proxy header:\n\"%*s\"",
                   (size_t) (b->last - b->pos), b->pos);

	//	如果没有配置中没有指定发送到后端数据的Body，并且"proxy_pass_request_body"指令开启状态
	//	则client端的body数据将通过proxy转发到后端服务器
    if (plcf->body_set == NULL && plcf->upstream.pass_request_body) {

        body = u->request_bufs;			//	此时body指向了client的body部分， ngx_http_upstream_init_request()函数中设置了u->request_bufs，根据client是否有Body数据到来设置
        u->request_bufs = cl;			//	此时 u->request_bufs 指向了在本函数中拼装的buf数据

        while (body) {
            b = ngx_alloc_buf(r->pool);
            if (b == NULL) {
                return NGX_ERROR;
            }

            ngx_memcpy(b, body->buf, sizeof(ngx_buf_t));				//	

            cl->next = ngx_alloc_chain_link(r->pool);
            if (cl->next == NULL) {
                return NGX_ERROR;
            }

            cl = cl->next;
            cl->buf = b;

            body = body->next;
        }

    } else {
        u->request_bufs = cl;
    }

    b->flush = 1;
    cl->next = NULL;

    return NGX_OK;
}


static ngx_int_t
ngx_http_proxy_reinit_request(ngx_http_request_t *r)
{
    ngx_http_proxy_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_proxy_module);

    if (ctx == NULL) {
        return NGX_OK;
    }

    ctx->status.code = 0;
    ctx->status.count = 0;
    ctx->status.start = NULL;
    ctx->status.end = NULL;
    ctx->state = 0;

    r->upstream->process_header = ngx_http_proxy_process_status_line;
    r->upstream->pipe->input_filter = ngx_http_proxy_copy_filter;
    r->upstream->input_filter = ngx_http_proxy_non_buffered_copy_filter;
    r->state = 0;

    return NGX_OK;
}

/* 
 *	[analy]	解析proxy返回的响应状态行，解析完状态行后解析响应头
 */
static ngx_int_t
ngx_http_proxy_process_status_line(ngx_http_request_t *r)
{
    size_t                 len;
    ngx_int_t              rc;
    ngx_http_upstream_t   *u;
    ngx_http_proxy_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_proxy_module);

    if (ctx == NULL) {
        return NGX_ERROR;
    }

    u = r->upstream;

	//	解析后端服务器响应的状态行存放于 ctx->status 结构中
    rc = ngx_http_parse_status_line(r, &u->buffer, &ctx->status);

    if (rc == NGX_AGAIN) {
        return rc;
    }

    if (rc == NGX_ERROR) {

#if (NGX_HTTP_CACHE)

        if (r->cache) {
            r->http_version = NGX_HTTP_VERSION_9;
            return NGX_OK;
        }

#endif

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "upstream sent no valid HTTP/1.0 header");

#if 0
        if (u->accel) {
            return NGX_HTTP_UPSTREAM_INVALID_HEADER;
        }
#endif

        r->http_version = NGX_HTTP_VERSION_9;
        u->state->status = NGX_HTTP_OK;
        u->headers_in.connection_close = 1;

        return NGX_OK;
    }

    if (u->state) {
        u->state->status = ctx->status.code;									//	设置后端服务器响应的状态码
    }

    u->headers_in.status_n = ctx->status.code;									//	设置响应状态码

    len = ctx->status.end - ctx->status.start;									//	以下拷贝后端服务器响应状态码的字符串
    u->headers_in.status_line.len = len;

    u->headers_in.status_line.data = ngx_pnalloc(r->pool, len);
    if (u->headers_in.status_line.data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(u->headers_in.status_line.data, ctx->status.start, len);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http proxy status %ui \"%V\"",
                   u->headers_in.status_n, &u->headers_in.status_line);

    if (ctx->status.http_version < NGX_HTTP_VERSION_11) {						//	后端服务器的响应http版本低于1.1，将关闭连接
        u->headers_in.connection_close = 1;
    }

    u->process_header = ngx_http_proxy_process_header;

    return ngx_http_proxy_process_header(r);									//	处理后端反馈的响应头
}

/* 
 *	[analy]	解析proxy返回的响应header
 */
static ngx_int_t
ngx_http_proxy_process_header(ngx_http_request_t *r)
{
    ngx_int_t                       rc;
    ngx_table_elt_t                *h;
    ngx_http_upstream_t            *u;
    ngx_http_proxy_ctx_t           *ctx;
    ngx_http_upstream_header_t     *hh;
    ngx_http_upstream_main_conf_t  *umcf;

    umcf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);

    for ( ;; ) {

        rc = ngx_http_parse_header_line(r, &r->upstream->buffer, 1);

        if (rc == NGX_OK) {				//	响应头中一行解析完毕

            /* a header line has been parsed successfully */

            h = ngx_list_push(&r->upstream->headers_in.headers);
            if (h == NULL) {
                return NGX_ERROR;
            }

            h->hash = r->header_hash;

            h->key.len = r->header_name_end - r->header_name_start;			//	header name
            h->value.len = r->header_end - r->header_start;					//	header value

            h->key.data = ngx_pnalloc(r->pool,
                               h->key.len + 1 + h->value.len + 1 + h->key.len);
            if (h->key.data == NULL) {
                return NGX_ERROR;
            }

            h->value.data = h->key.data + h->key.len + 1;
            h->lowcase_key = h->key.data + h->key.len + 1 + h->value.len + 1;

            ngx_memcpy(h->key.data, r->header_name_start, h->key.len);						//	增加结束符
            h->key.data[h->key.len] = '\0';
            ngx_memcpy(h->value.data, r->header_start, h->value.len);
            h->value.data[h->value.len] = '\0';

            if (h->key.len == r->lowcase_index) {
                ngx_memcpy(h->lowcase_key, r->lowcase_header, h->key.len);

            } else {
                ngx_strlow(h->lowcase_key, h->key.data, h->key.len);
            }

            hh = ngx_hash_find(&umcf->headers_in_hash, h->hash,			//	在 ngx_http_upstream_headers_in 静态数组的hash表中查找，找到后调用对应的handler
                               h->lowcase_key, h->key.len);

            if (hh && hh->handler(r, h, hh->offset) != NGX_OK) {
                return NGX_ERROR;
            }

            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "http proxy header: \"%V: %V\"",
                           &h->key, &h->value);

            continue;
        }

        if (rc == NGX_HTTP_PARSE_HEADER_DONE) {			//	响应头解析完毕

            /* a whole header has been parsed successfully */

            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "http proxy header done");

            /*
             * if no "Server" and "Date" in header line,
             * then add the special empty headers
             */

            if (r->upstream->headers_in.server == NULL) {
                h = ngx_list_push(&r->upstream->headers_in.headers);
                if (h == NULL) {
                    return NGX_ERROR;
                }

                h->hash = ngx_hash(ngx_hash(ngx_hash(ngx_hash(
                                    ngx_hash('s', 'e'), 'r'), 'v'), 'e'), 'r');

                ngx_str_set(&h->key, "Server");
                ngx_str_null(&h->value);
                h->lowcase_key = (u_char *) "server";
            }

            if (r->upstream->headers_in.date == NULL) {
                h = ngx_list_push(&r->upstream->headers_in.headers);
                if (h == NULL) {
                    return NGX_ERROR;
                }

                h->hash = ngx_hash(ngx_hash(ngx_hash('d', 'a'), 't'), 'e');

                ngx_str_set(&h->key, "Date");
                ngx_str_null(&h->value);
                h->lowcase_key = (u_char *) "date";
            }

            /* clear content length if response is chunked */

            u = r->upstream;

            if (u->headers_in.chunked) {
                u->headers_in.content_length_n = -1;
            }

            /*
             * set u->keepalive if response has no body; this allows to keep
             * connections alive in case of r->header_only or X-Accel-Redirect
             */

            ctx = ngx_http_get_module_ctx(r, ngx_http_proxy_module);

            if (u->headers_in.status_n == NGX_HTTP_NO_CONTENT
                || u->headers_in.status_n == NGX_HTTP_NOT_MODIFIED
                || ctx->head
                || (!u->headers_in.chunked
                    && u->headers_in.content_length_n == 0))
            {
                u->keepalive = !u->headers_in.connection_close;
            }

            return NGX_OK;
        }

        if (rc == NGX_AGAIN) {
            return NGX_AGAIN;
        }

        /* there was error while a header line parsing */

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "upstream sent invalid header");

        return NGX_HTTP_UPSTREAM_INVALID_HEADER;
    }	//	end for
}


static ngx_int_t
ngx_http_proxy_input_filter_init(void *data)
{
    ngx_http_request_t    *r = data;
    ngx_http_upstream_t   *u;
    ngx_http_proxy_ctx_t  *ctx;

    u = r->upstream;
    ctx = ngx_http_get_module_ctx(r, ngx_http_proxy_module);

    ngx_log_debug4(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http proxy filter init s:%d h:%d c:%d l:%O",
                   u->headers_in.status_n, ctx->head, u->headers_in.chunked,
                   u->headers_in.content_length_n);

    /* as per RFC2616, 4.4 Message Length */

    if (u->headers_in.status_n == NGX_HTTP_NO_CONTENT
        || u->headers_in.status_n == NGX_HTTP_NOT_MODIFIED
        || ctx->head)
    {
        /* 1xx, 204, and 304 and replies to HEAD requests */
        /* no 1xx since we don't send Expect and Upgrade */

        u->pipe->length = 0;
        u->length = 0;
        u->keepalive = !u->headers_in.connection_close;

    } else if (u->headers_in.chunked) {								//	后端服务器使用的是chunked编码
        /* chunked */

        u->pipe->input_filter = ngx_http_proxy_chunked_filter;
        u->pipe->length = 3; /* "0" LF LF */

        u->input_filter = ngx_http_proxy_non_buffered_chunked_filter;
        u->length = -1;

    } else if (u->headers_in.content_length_n == 0) {
        /* empty body: special case as filter won't be called */

        u->pipe->length = 0;
        u->length = 0;
        u->keepalive = !u->headers_in.connection_close;

    } else {
        /* content length or connection close */

        u->pipe->length = u->headers_in.content_length_n;
        u->length = u->headers_in.content_length_n;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_proxy_copy_filter(ngx_event_pipe_t *p, ngx_buf_t *buf)
{
    ngx_buf_t           *b;
    ngx_chain_t         *cl;
    ngx_http_request_t  *r;

    if (buf->pos == buf->last) {			//	pos等于last 说明数据已经解析或发送完毕
        return NGX_OK;
    }

	/*	p->free中有空闲的chain时，取出一个空闲的chain并将它放入pool->chain链表中，
	 */
    if (p->free) {							//	ngx_event_pipe_write_to_downstream（）函数中已发送到客户端的数据会挂载到free上。
        cl = p->free;
        b = cl->buf;
        p->free = cl->next;
        ngx_free_chain(p->pool, cl);

    } else {

		//	申请一个buf
        b = ngx_alloc_buf(p->pool);
        if (b == NULL) {
            return NGX_ERROR;
        }
    }

	//	拷贝参数buf的管理结构到申请的buf上，他们共享数据内存空间
    ngx_memcpy(b, buf, sizeof(ngx_buf_t));
    b->shadow = buf;								//	两个buf相互关联，成为对方的影子，新申请的buf是参数buf的影子
    b->tag = p->tag;								//	模块标记
    b->last_shadow = 1;								//	暂时理解为“影子缓冲”???????????
    b->recycled = 1;								//	循环使用
    buf->shadow = b;

    cl = ngx_alloc_chain_link(p->pool);				//	由于当p->free不为空时，会将其中的空闲chain扔到p->chain中，所以在上边拷贝和赋值时针对新获取的chain->buf设置的
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = b;
    cl->next = NULL;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0, "input buf #%d", b->num);

    if (p->in) {
        *p->last_in = cl;
    } else {
        p->in = cl;
    }

    p->last_in = &cl->next;		//	指向p->in链表中最后一个节点

    if (p->length == -1) {		//	why????
        return NGX_OK;
    }

	/*	后端服务器body部分剩余大小
		当缓冲区内的数据已经被发送到客户端时，buf的pos和last标记相等 */
    p->length -= b->last - b->pos;			

	//	p->length等于0，说明后端服务器解析全部接收完毕
    if (p->length == 0) {
        r = p->input_ctx;
        p->upstream_done = 1;
        r->upstream->keepalive = !r->upstream->headers_in.connection_close;

    } else if (p->length < 0) {
        r = p->input_ctx;
        p->upstream_done = 1;

        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "upstream sent too much data");
    }

    return NGX_OK;
}


static ngx_inline ngx_int_t
ngx_http_proxy_parse_chunked(ngx_http_request_t *r, ngx_buf_t *buf)
{
    u_char                *pos, ch, c;
    ngx_int_t              rc;
    ngx_http_proxy_ctx_t  *ctx;
    enum {
        sw_chunk_start = 0,
        sw_chunk_size,
        sw_chunk_extension,
        sw_chunk_extension_almost_done,
        sw_chunk_data,
        sw_after_data,
        sw_after_data_almost_done,
        sw_last_chunk_extension,
        sw_last_chunk_extension_almost_done,
        sw_trailer,
        sw_trailer_almost_done,
        sw_trailer_header,
        sw_trailer_header_almost_done
    } state;

    ctx = ngx_http_get_module_ctx(r, ngx_http_proxy_module);
    state = ctx->state;

    if (state == sw_chunk_data && ctx->size == 0) {
        state = sw_after_data;
    }

    rc = NGX_AGAIN;

    for (pos = buf->pos; pos < buf->last; pos++) {

        ch = *pos;

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http proxy chunked byte: %02Xd s:%d", ch, state);

        switch (state) {

        case sw_chunk_start:
            if (ch >= '0' && ch <= '9') {
                state = sw_chunk_size;
                ctx->size = ch - '0';
                break;
            }

            c = (u_char) (ch | 0x20);

            if (c >= 'a' && c <= 'f') {
                state = sw_chunk_size;
                ctx->size = c - 'a' + 10;
                break;
            }

            goto invalid;

        case sw_chunk_size:
            if (ch >= '0' && ch <= '9') {
                ctx->size = ctx->size * 16 + (ch - '0');
                break;
            }

            c = (u_char) (ch | 0x20);

            if (c >= 'a' && c <= 'f') {
                ctx->size = ctx->size * 16 + (c - 'a' + 10);
                break;
            }

            if (ctx->size == 0) {

                switch (ch) {
                case CR:
                    state = sw_last_chunk_extension_almost_done;
                    break;
                case LF:
                    state = sw_trailer;
                    break;
                case ';':
                case ' ':
                case '\t':
                    state = sw_last_chunk_extension;
                    break;
                default:
                    goto invalid;
                }

                break;
            }

            switch (ch) {
            case CR:
                state = sw_chunk_extension_almost_done;
                break;
            case LF:
                state = sw_chunk_data;
                break;
            case ';':
            case ' ':
            case '\t':
                state = sw_chunk_extension;
                break;
            default:
                goto invalid;
            }

            break;

        case sw_chunk_extension:
            switch (ch) {
            case CR:
                state = sw_chunk_extension_almost_done;
                break;
            case LF:
                state = sw_chunk_data;
            }
            break;

        case sw_chunk_extension_almost_done:
            if (ch == LF) {
                state = sw_chunk_data;
                break;
            }
            goto invalid;

        case sw_chunk_data:
            rc = NGX_OK;
            goto data;

        case sw_after_data:
            switch (ch) {
            case CR:
                state = sw_after_data_almost_done;
                break;
            case LF:
                state = sw_chunk_start;
            }
            break;

        case sw_after_data_almost_done:
            if (ch == LF) {
                state = sw_chunk_start;
                break;
            }
            goto invalid;

        case sw_last_chunk_extension:
            switch (ch) {
            case CR:
                state = sw_last_chunk_extension_almost_done;
                break;
            case LF:
                state = sw_trailer;
            }
            break;

        case sw_last_chunk_extension_almost_done:
            if (ch == LF) {
                state = sw_trailer;
                break;
            }
            goto invalid;

        case sw_trailer:
            switch (ch) {
            case CR:
                state = sw_trailer_almost_done;
                break;
            case LF:
                goto done;
            default:
                state = sw_trailer_header;
            }
            break;

        case sw_trailer_almost_done:
            if (ch == LF) {
                goto done;
            }
            goto invalid;

        case sw_trailer_header:
            switch (ch) {
            case CR:
                state = sw_trailer_header_almost_done;
                break;
            case LF:
                state = sw_trailer;
            }
            break;

        case sw_trailer_header_almost_done:
            if (ch == LF) {
                state = sw_trailer;
                break;
            }
            goto invalid;

        }
    }

data:

    ctx->state = state;
    buf->pos = pos;

    switch (state) {

    case sw_chunk_start:
        ctx->length = 3 /* "0" LF LF */;
        break;
    case sw_chunk_size:
        ctx->length = 2 /* LF LF */
                      + (ctx->size ? ctx->size + 4 /* LF "0" LF LF */ : 0);
        break;
    case sw_chunk_extension:
    case sw_chunk_extension_almost_done:
        ctx->length = 1 /* LF */ + ctx->size + 4 /* LF "0" LF LF */;
        break;
    case sw_chunk_data:
        ctx->length = ctx->size + 4 /* LF "0" LF LF */;
        break;
    case sw_after_data:
    case sw_after_data_almost_done:
        ctx->length = 4 /* LF "0" LF LF */;
        break;
    case sw_last_chunk_extension:
    case sw_last_chunk_extension_almost_done:
        ctx->length = 2 /* LF LF */;
        break;
    case sw_trailer:
    case sw_trailer_almost_done:
        ctx->length = 1 /* LF */;
        break;
    case sw_trailer_header:
    case sw_trailer_header_almost_done:
        ctx->length = 2 /* LF LF */;
        break;

    }

    return rc;

done:

    return NGX_DONE;

invalid:

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_proxy_chunked_filter(ngx_event_pipe_t *p, ngx_buf_t *buf)
{
    ngx_int_t              rc;
    ngx_buf_t             *b, **prev;
    ngx_chain_t           *cl;
    ngx_http_request_t    *r;
    ngx_http_proxy_ctx_t  *ctx;

    if (buf->pos == buf->last) {
        return NGX_OK;
    }

    r = p->input_ctx;
    ctx = ngx_http_get_module_ctx(r, ngx_http_proxy_module);

    b = NULL;
    prev = &buf->shadow;

    for ( ;; ) {

        rc = ngx_http_proxy_parse_chunked(r, buf);

        if (rc == NGX_OK) {

            /* a chunk has been parsed successfully */

            if (p->free) {
                cl = p->free;
                b = cl->buf;
                p->free = cl->next;
                ngx_free_chain(p->pool, cl);

            } else {
                b = ngx_alloc_buf(p->pool);
                if (b == NULL) {
                    return NGX_ERROR;
                }
            }

            ngx_memzero(b, sizeof(ngx_buf_t));

            b->pos = buf->pos;
            b->start = buf->start;
            b->end = buf->end;
            b->tag = p->tag;
            b->temporary = 1;
            b->recycled = 1;

            *prev = b;
            prev = &b->shadow;

            cl = ngx_alloc_chain_link(p->pool);
            if (cl == NULL) {
                return NGX_ERROR;
            }

            cl->buf = b;
            cl->next = NULL;

            if (p->in) {
                *p->last_in = cl;
            } else {
                p->in = cl;
            }
            p->last_in = &cl->next;

            /* STUB */ b->num = buf->num;

            ngx_log_debug2(NGX_LOG_DEBUG_EVENT, p->log, 0,
                           "input buf #%d %p", b->num, b->pos);

            if (buf->last - buf->pos >= ctx->size) {

                buf->pos += ctx->size;
                b->last = buf->pos;
                ctx->size = 0;

                continue;
            }

            ctx->size -= buf->last - buf->pos;
            buf->pos = buf->last;
            b->last = buf->last;

            continue;
        }

        if (rc == NGX_DONE) {

            /* a whole response has been parsed successfully */

            p->upstream_done = 1;
            r->upstream->keepalive = !r->upstream->headers_in.connection_close;

            break;
        }

        if (rc == NGX_AGAIN) {

            /* set p->length, minimal amount of data we want to see */

            p->length = ctx->length;

            break;
        }

        /* invalid response */

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "upstream sent invalid chunked response");

        return NGX_ERROR;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http proxy chunked state %d, length %d",
                   ctx->state, p->length);

    if (b) {
        b->shadow = buf;
        b->last_shadow = 1;

        ngx_log_debug2(NGX_LOG_DEBUG_EVENT, p->log, 0,
                       "input buf %p %z", b->pos, b->last - b->pos);

        return NGX_OK;
    }

    /* there is no data record in the buf, add it to free chain */

    if (ngx_event_pipe_add_free_buf(p, buf) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

//	
static ngx_int_t
ngx_http_proxy_non_buffered_copy_filter(void *data, ssize_t bytes)
{
    ngx_http_request_t   *r = data;

    ngx_buf_t            *b;
    ngx_chain_t          *cl, **ll;
    ngx_http_upstream_t  *u;

    u = r->upstream;

	//	遍历 u->out_bufs, 找到尾结点， ll指向尾结点
    for (cl = u->out_bufs, ll = &u->out_bufs; cl; cl = cl->next) {
        ll = &cl->next;
    }

    cl = ngx_chain_get_free_buf(r->pool, &u->free_bufs);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    *ll = cl;				//	将申请的chain挂接到 u->out_bufs 的尾结点

    cl->buf->flush = 1;
    cl->buf->memory = 1;

    b = &u->buffer;			//	后端服务器反馈的数据

    cl->buf->pos = b->last;				//	由于在外边已经reset缓冲区
    b->last += bytes;					//	这里将缓冲区的last指针向后移动bytes字节
    cl->buf->last = b->last;			//	设置缓冲去的laset指针
    cl->buf->tag = u->output.tag;		//	设置缓冲模块标记

    if (u->length == -1) {
        return NGX_OK;
    }

    u->length -= bytes;					//	???

    if (u->length == 0) {				//	???
        u->keepalive = !u->headers_in.connection_close;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_proxy_non_buffered_chunked_filter(void *data, ssize_t bytes)
{
    ngx_http_request_t   *r = data;

    ngx_int_t              rc;
    ngx_buf_t             *b, *buf;
    ngx_chain_t           *cl, **ll;
    ngx_http_upstream_t   *u;
    ngx_http_proxy_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_proxy_module);
    u = r->upstream;
    buf = &u->buffer;

    buf->pos = buf->last;
    buf->last += bytes;

    for (cl = u->out_bufs, ll = &u->out_bufs; cl; cl = cl->next) {
        ll = &cl->next;
    }

    for ( ;; ) {

        rc = ngx_http_proxy_parse_chunked(r, buf);

        if (rc == NGX_OK) {

            /* a chunk has been parsed successfully */

            cl = ngx_chain_get_free_buf(r->pool, &u->free_bufs);
            if (cl == NULL) {
                return NGX_ERROR;
            }

            *ll = cl;
            ll = &cl->next;

            b = cl->buf;

            b->flush = 1;
            b->memory = 1;

            b->pos = buf->pos;
            b->tag = u->output.tag;

            if (buf->last - buf->pos >= ctx->size) {
                buf->pos += ctx->size;
                b->last = buf->pos;
                ctx->size = 0;

            } else {
                ctx->size -= buf->last - buf->pos;
                buf->pos = buf->last;
                b->last = buf->last;
            }

            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "http proxy out buf %p %z",
                           b->pos, b->last - b->pos);

            continue;
        }

        if (rc == NGX_DONE) {

            /* a whole response has been parsed successfully */

            u->keepalive = !u->headers_in.connection_close;
            u->length = 0;

            break;
        }

        if (rc == NGX_AGAIN) {
            break;
        }

        /* invalid response */

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "upstream sent invalid chunked response");

        return NGX_ERROR;
    }

    /* provide continuous buffer for subrequests in memory */

    if (r->subrequest_in_memory) {

        cl = u->out_bufs;

        if (cl) {
            buf->pos = cl->buf->pos;
        }

        buf->last = buf->pos;

        for (cl = u->out_bufs; cl; cl = cl->next) {
            ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "http proxy in memory %p-%p %uz",
                           cl->buf->pos, cl->buf->last, ngx_buf_size(cl->buf));

            if (buf->last == cl->buf->pos) {
                buf->last = cl->buf->last;
                continue;
            }

            buf->last = ngx_movemem(buf->last, cl->buf->pos,
                                    cl->buf->last - cl->buf->pos);

            cl->buf->pos = buf->last - (cl->buf->last - cl->buf->pos);
            cl->buf->last = buf->last;
        }
    }

    return NGX_OK;
}


static void
ngx_http_proxy_abort_request(ngx_http_request_t *r)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "abort http proxy request");

    return;
}


static void
ngx_http_proxy_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "finalize http proxy request");

    return;
}


static ngx_int_t
ngx_http_proxy_host_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_http_proxy_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_proxy_module);

    if (ctx == NULL) {
        v->not_found = 1;
        return NGX_OK;
    }

    v->len = ctx->vars.host_header.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = ctx->vars.host_header.data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_proxy_port_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_http_proxy_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_proxy_module);

    if (ctx == NULL) {
        v->not_found = 1;
        return NGX_OK;
    }

    v->len = ctx->vars.port.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = ctx->vars.port.data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_proxy_add_x_forwarded_for_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char  *p;

    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    if (r->headers_in.x_forwarded_for == NULL) {
        v->len = r->connection->addr_text.len;
        v->data = r->connection->addr_text.data;
        return NGX_OK;
    }

    v->len = r->headers_in.x_forwarded_for->value.len
             + sizeof(", ") - 1 + r->connection->addr_text.len;

    p = ngx_pnalloc(r->pool, v->len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    v->data = p;

    p = ngx_copy(p, r->headers_in.x_forwarded_for->value.data,
                 r->headers_in.x_forwarded_for->value.len);

    *p++ = ','; *p++ = ' ';

    ngx_memcpy(p, r->connection->addr_text.data, r->connection->addr_text.len);

    return NGX_OK;
}


static ngx_int_t
ngx_http_proxy_internal_body_length_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_http_proxy_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_proxy_module);

    if (ctx == NULL) {
        v->not_found = 1;
        return NGX_OK;
    }

    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    v->data = ngx_pnalloc(r->connection->pool, NGX_SIZE_T_LEN);

    if (v->data == NULL) {
        return NGX_ERROR;
    }

    v->len = ngx_sprintf(v->data, "%uz", ctx->internal_body_length) - v->data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_proxy_rewrite_redirect(ngx_http_request_t *r, ngx_table_elt_t *h,
    size_t prefix)
{
    size_t                      len;
    ngx_int_t                   rc;
    ngx_uint_t                  i;
    ngx_http_proxy_rewrite_t   *pr;
    ngx_http_proxy_loc_conf_t  *plcf;

    plcf = ngx_http_get_module_loc_conf(r, ngx_http_proxy_module);

    pr = plcf->redirects->elts;

    if (pr == NULL) {
        return NGX_DECLINED;
    }

    len = h->value.len - prefix;

    for (i = 0; i < plcf->redirects->nelts; i++) {
        rc = pr[i].handler(r, h, prefix, len, &pr[i]);

        if (rc != NGX_DECLINED) {
            return rc;
        }
    }

    return NGX_DECLINED;
}


static ngx_int_t
ngx_http_proxy_rewrite_cookie(ngx_http_request_t *r, ngx_table_elt_t *h)
{
    size_t                      prefix;
    u_char                     *p;
    ngx_int_t                   rc, rv;
    ngx_http_proxy_loc_conf_t  *plcf;

    p = (u_char *) ngx_strchr(h->value.data, ';');
    if (p == NULL) {
        return NGX_DECLINED;
    }

    prefix = p + 1 - h->value.data;

    rv = NGX_DECLINED;

    plcf = ngx_http_get_module_loc_conf(r, ngx_http_proxy_module);

    if (plcf->cookie_domains) {
        p = ngx_strcasestrn(h->value.data + prefix, "domain=", 7 - 1);

        if (p) {
            rc = ngx_http_proxy_rewrite_cookie_value(r, h, p + 7,
                                                     plcf->cookie_domains);
            if (rc == NGX_ERROR) {
                return NGX_ERROR;
            }

            if (rc != NGX_DECLINED) {
                rv = rc;
            }
        }
    }

    if (plcf->cookie_paths) {
        p = ngx_strcasestrn(h->value.data + prefix, "path=", 5 - 1);

        if (p) {
            rc = ngx_http_proxy_rewrite_cookie_value(r, h, p + 5,
                                                     plcf->cookie_paths);
            if (rc == NGX_ERROR) {
                return NGX_ERROR;
            }

            if (rc != NGX_DECLINED) {
                rv = rc;
            }
        }
    }

    return rv;
}


static ngx_int_t
ngx_http_proxy_rewrite_cookie_value(ngx_http_request_t *r, ngx_table_elt_t *h,
    u_char *value, ngx_array_t *rewrites)
{
    size_t                     len, prefix;
    u_char                    *p;
    ngx_int_t                  rc;
    ngx_uint_t                 i;
    ngx_http_proxy_rewrite_t  *pr;

    prefix = value - h->value.data;

    p = (u_char *) ngx_strchr(value, ';');

    len = p ? (size_t) (p - value) : (h->value.len - prefix);

    pr = rewrites->elts;

    for (i = 0; i < rewrites->nelts; i++) {
        rc = pr[i].handler(r, h, prefix, len, &pr[i]);

        if (rc != NGX_DECLINED) {
            return rc;
        }
    }

    return NGX_DECLINED;
}


static ngx_int_t
ngx_http_proxy_rewrite_complex_handler(ngx_http_request_t *r,
    ngx_table_elt_t *h, size_t prefix, size_t len, ngx_http_proxy_rewrite_t *pr)
{
    ngx_str_t  pattern, replacement;

    if (ngx_http_complex_value(r, &pr->pattern.complex, &pattern) != NGX_OK) {
        return NGX_ERROR;
    }

    if (pattern.len > len
        || ngx_rstrncmp(h->value.data + prefix, pattern.data,
                        pattern.len) != 0)
    {
        return NGX_DECLINED;
    }

    if (ngx_http_complex_value(r, &pr->replacement, &replacement) != NGX_OK) {
        return NGX_ERROR;
    }

    return ngx_http_proxy_rewrite(r, h, prefix, pattern.len, &replacement);
}


#if (NGX_PCRE)

static ngx_int_t
ngx_http_proxy_rewrite_regex_handler(ngx_http_request_t *r, ngx_table_elt_t *h,
    size_t prefix, size_t len, ngx_http_proxy_rewrite_t *pr)
{
    ngx_str_t  pattern, replacement;

    pattern.len = len;
    pattern.data = h->value.data + prefix;

    if (ngx_http_regex_exec(r, pr->pattern.regex, &pattern) != NGX_OK) {
        return NGX_DECLINED;
    }

    if (ngx_http_complex_value(r, &pr->replacement, &replacement) != NGX_OK) {
        return NGX_ERROR;
    }

    if (prefix == 0 && h->value.len == len) {
        h->value = replacement;
        return NGX_OK;
    }

    return ngx_http_proxy_rewrite(r, h, prefix, len, &replacement);
}

#endif


static ngx_int_t
ngx_http_proxy_rewrite_domain_handler(ngx_http_request_t *r,
    ngx_table_elt_t *h, size_t prefix, size_t len, ngx_http_proxy_rewrite_t *pr)
{
    u_char     *p;
    ngx_str_t   pattern, replacement;

    if (ngx_http_complex_value(r, &pr->pattern.complex, &pattern) != NGX_OK) {
        return NGX_ERROR;
    }

    p = h->value.data + prefix;

    if (p[0] == '.') {
        p++;
        prefix++;
        len--;
    }

    if (pattern.len != len || ngx_rstrncasecmp(pattern.data, p, len) != 0) {
        return NGX_DECLINED;
    }

    if (ngx_http_complex_value(r, &pr->replacement, &replacement) != NGX_OK) {
        return NGX_ERROR;
    }

    return ngx_http_proxy_rewrite(r, h, prefix, len, &replacement);
}


static ngx_int_t
ngx_http_proxy_rewrite(ngx_http_request_t *r, ngx_table_elt_t *h, size_t prefix,
    size_t len, ngx_str_t *replacement)
{
    u_char  *p, *data;
    size_t   new_len;

    new_len = replacement->len + h->value.len - len;

    if (replacement->len > len) {

        data = ngx_pnalloc(r->pool, new_len);
        if (data == NULL) {
            return NGX_ERROR;
        }

        p = ngx_copy(data, h->value.data, prefix);
        p = ngx_copy(p, replacement->data, replacement->len);

        ngx_memcpy(p, h->value.data + prefix + len,
                   h->value.len - len - prefix);

        h->value.data = data;

    } else {
        p = ngx_copy(h->value.data + prefix, replacement->data,
                     replacement->len);

        ngx_memmove(p, h->value.data + prefix + len,
                    h->value.len - len - prefix);
    }

    h->value.len = new_len;

    return NGX_OK;
}

/* 
 *	[analy]	添加proxy模块中的变量到hash过的变量数组（cmcf->variables_keys.keys）
 */
static ngx_int_t
ngx_http_proxy_add_variables(ngx_conf_t *cf)
{
    ngx_http_variable_t  *var, *v;

    for (v = ngx_http_proxy_vars; v->name.len; v++) {
        var = ngx_http_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}


static void *
ngx_http_proxy_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_proxy_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_proxy_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->upstream.bufs.num = 0;
     *     conf->upstream.ignore_headers = 0;
     *     conf->upstream.next_upstream = 0;
     *     conf->upstream.cache_use_stale = 0;
     *     conf->upstream.cache_methods = 0;
     *     conf->upstream.temp_path = NULL;
     *     conf->upstream.hide_headers_hash = { NULL, 0 };
     *     conf->upstream.uri = { 0, NULL };
     *     conf->upstream.location = NULL;
     *     conf->upstream.store_lengths = NULL;
     *     conf->upstream.store_values = NULL;
     *
     *     conf->method = NULL;
     *     conf->headers_source = NULL;
     *     conf->headers_set_len = NULL;
     *     conf->headers_set = NULL;
     *     conf->headers_set_hash = NULL;
     *     conf->body_set_len = NULL;
     *     conf->body_set = NULL;
     *     conf->body_source = { 0, NULL };
     *     conf->redirects = NULL;
     */

    conf->upstream.store = NGX_CONF_UNSET;
    conf->upstream.store_access = NGX_CONF_UNSET_UINT;
    conf->upstream.buffering = NGX_CONF_UNSET;
    conf->upstream.ignore_client_abort = NGX_CONF_UNSET;

    conf->upstream.connect_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.send_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.read_timeout = NGX_CONF_UNSET_MSEC;

    conf->upstream.send_lowat = NGX_CONF_UNSET_SIZE;
    conf->upstream.buffer_size = NGX_CONF_UNSET_SIZE;

    conf->upstream.busy_buffers_size_conf = NGX_CONF_UNSET_SIZE;
    conf->upstream.max_temp_file_size_conf = NGX_CONF_UNSET_SIZE;
    conf->upstream.temp_file_write_size_conf = NGX_CONF_UNSET_SIZE;

    conf->upstream.pass_request_headers = NGX_CONF_UNSET;
    conf->upstream.pass_request_body = NGX_CONF_UNSET;

#if (NGX_HTTP_CACHE)
    conf->upstream.cache = NGX_CONF_UNSET_PTR;
    conf->upstream.cache_min_uses = NGX_CONF_UNSET_UINT;
    conf->upstream.cache_bypass = NGX_CONF_UNSET_PTR;
    conf->upstream.no_cache = NGX_CONF_UNSET_PTR;
    conf->upstream.cache_valid = NGX_CONF_UNSET_PTR;
    conf->upstream.cache_lock = NGX_CONF_UNSET;
    conf->upstream.cache_lock_timeout = NGX_CONF_UNSET_MSEC;
#endif

    conf->upstream.hide_headers = NGX_CONF_UNSET_PTR;
    conf->upstream.pass_headers = NGX_CONF_UNSET_PTR;

    conf->upstream.intercept_errors = NGX_CONF_UNSET;
#if (NGX_HTTP_SSL)
    conf->upstream.ssl_session_reuse = NGX_CONF_UNSET;
#endif

    /* "proxy_cyclic_temp_file" is disabled */
    conf->upstream.cyclic_temp_file = 0;

    conf->redirect = NGX_CONF_UNSET;
    conf->upstream.change_buffering = 1;

    conf->cookie_domains = NGX_CONF_UNSET_PTR;
    conf->cookie_paths = NGX_CONF_UNSET_PTR;

    conf->http_version = NGX_CONF_UNSET_UINT;

    conf->headers_hash_max_size = NGX_CONF_UNSET_UINT;
    conf->headers_hash_bucket_size = NGX_CONF_UNSET_UINT;

    ngx_str_set(&conf->upstream.module, "proxy");

    return conf;
}


static char *
ngx_http_proxy_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_proxy_loc_conf_t *prev = parent;
    ngx_http_proxy_loc_conf_t *conf = child;

    u_char                     *p;
    size_t                      size;
    ngx_hash_init_t             hash;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_proxy_rewrite_t   *pr;
    ngx_http_script_compile_t   sc;

    if (conf->upstream.store != 0) {
        ngx_conf_merge_value(conf->upstream.store,
                              prev->upstream.store, 0);

        if (conf->upstream.store_lengths == NULL) {
            conf->upstream.store_lengths = prev->upstream.store_lengths;
            conf->upstream.store_values = prev->upstream.store_values;
        }
    }

    ngx_conf_merge_uint_value(conf->upstream.store_access,
                              prev->upstream.store_access, 0600);

    ngx_conf_merge_value(conf->upstream.buffering,
                              prev->upstream.buffering, 1);

    ngx_conf_merge_value(conf->upstream.ignore_client_abort,
                              prev->upstream.ignore_client_abort, 0);

    ngx_conf_merge_msec_value(conf->upstream.connect_timeout,
                              prev->upstream.connect_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.send_timeout,
                              prev->upstream.send_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.read_timeout,
                              prev->upstream.read_timeout, 60000);

    ngx_conf_merge_size_value(conf->upstream.send_lowat,
                              prev->upstream.send_lowat, 0);

    ngx_conf_merge_size_value(conf->upstream.buffer_size,
                              prev->upstream.buffer_size,
                              (size_t) ngx_pagesize);

    ngx_conf_merge_bufs_value(conf->upstream.bufs, prev->upstream.bufs,
                              8, ngx_pagesize);

    if (conf->upstream.bufs.num < 2) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "there must be at least 2 \"proxy_buffers\"");
        return NGX_CONF_ERROR;
    }


    size = conf->upstream.buffer_size;					//	接收后端服务器反馈的缓冲区大小
    if (size < conf->upstream.bufs.size) {
        size = conf->upstream.bufs.size;
    }


    ngx_conf_merge_size_value(conf->upstream.busy_buffers_size_conf,
                              prev->upstream.busy_buffers_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream.busy_buffers_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream.busy_buffers_size = 2 * size;
    } else {
        conf->upstream.busy_buffers_size =
                                         conf->upstream.busy_buffers_size_conf;
    }

    if (conf->upstream.busy_buffers_size < size) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"proxy_busy_buffers_size\" must be equal or bigger than "
             "maximum of the value of \"proxy_buffer_size\" and "
             "one of the \"proxy_buffers\"");

        return NGX_CONF_ERROR;
    }

    if (conf->upstream.busy_buffers_size
        > (conf->upstream.bufs.num - 1) * conf->upstream.bufs.size)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"proxy_busy_buffers_size\" must be less than "
             "the size of all \"proxy_buffers\" minus one buffer");

        return NGX_CONF_ERROR;
    }


    ngx_conf_merge_size_value(conf->upstream.temp_file_write_size_conf,
                              prev->upstream.temp_file_write_size_conf,
                              NGX_CONF_UNSET_SIZE);

	//	
    if (conf->upstream.temp_file_write_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream.temp_file_write_size = 2 * size;
    } else {
        conf->upstream.temp_file_write_size =
                                      conf->upstream.temp_file_write_size_conf;
    }

    if (conf->upstream.temp_file_write_size < size) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"proxy_temp_file_write_size\" must be equal or bigger than "
             "maximum of the value of \"proxy_buffer_size\" and "
             "one of the \"proxy_buffers\"");

        return NGX_CONF_ERROR;
    }

    ngx_conf_merge_size_value(conf->upstream.max_temp_file_size_conf,
                              prev->upstream.max_temp_file_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream.max_temp_file_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream.max_temp_file_size = 1024 * 1024 * 1024;
    } else {
        conf->upstream.max_temp_file_size =
                                        conf->upstream.max_temp_file_size_conf;
    }

    if (conf->upstream.max_temp_file_size != 0
        && conf->upstream.max_temp_file_size < size)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"proxy_max_temp_file_size\" must be equal to zero to disable "
             "the temporary files usage or must be equal or bigger than "
             "maximum of the value of \"proxy_buffer_size\" and "
             "one of the \"proxy_buffers\"");

        return NGX_CONF_ERROR;
    }


    ngx_conf_merge_bitmask_value(conf->upstream.ignore_headers,
                              prev->upstream.ignore_headers,
                              NGX_CONF_BITMASK_SET);


    ngx_conf_merge_bitmask_value(conf->upstream.next_upstream,
                              prev->upstream.next_upstream,
                              (NGX_CONF_BITMASK_SET
                               |NGX_HTTP_UPSTREAM_FT_ERROR
                               |NGX_HTTP_UPSTREAM_FT_TIMEOUT));

    if (conf->upstream.next_upstream & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.next_upstream = NGX_CONF_BITMASK_SET
                                       |NGX_HTTP_UPSTREAM_FT_OFF;
    }

    if (ngx_conf_merge_path_value(cf, &conf->upstream.temp_path,
                              prev->upstream.temp_path,
                              &ngx_http_proxy_temp_path)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }


#if (NGX_HTTP_CACHE)

    ngx_conf_merge_ptr_value(conf->upstream.cache,
                              prev->upstream.cache, NULL);

    if (conf->upstream.cache && conf->upstream.cache->data == NULL) {
        ngx_shm_zone_t  *shm_zone;

        shm_zone = conf->upstream.cache;

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"proxy_cache\" zone \"%V\" is unknown",
                           &shm_zone->shm.name);

        return NGX_CONF_ERROR;
    }

    ngx_conf_merge_uint_value(conf->upstream.cache_min_uses,
                              prev->upstream.cache_min_uses, 1);

    ngx_conf_merge_bitmask_value(conf->upstream.cache_use_stale,
                              prev->upstream.cache_use_stale,
                              (NGX_CONF_BITMASK_SET
                               |NGX_HTTP_UPSTREAM_FT_OFF));

    if (conf->upstream.cache_use_stale & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.cache_use_stale = NGX_CONF_BITMASK_SET
                                         |NGX_HTTP_UPSTREAM_FT_OFF;
    }

    if (conf->upstream.cache_use_stale & NGX_HTTP_UPSTREAM_FT_ERROR) {
        conf->upstream.cache_use_stale |= NGX_HTTP_UPSTREAM_FT_NOLIVE;
    }

    if (conf->upstream.cache_methods == 0) {
        conf->upstream.cache_methods = prev->upstream.cache_methods;
    }

    conf->upstream.cache_methods |= NGX_HTTP_GET|NGX_HTTP_HEAD;

    ngx_conf_merge_ptr_value(conf->upstream.cache_bypass,
                             prev->upstream.cache_bypass, NULL);

    ngx_conf_merge_ptr_value(conf->upstream.no_cache,
                             prev->upstream.no_cache, NULL);

    if (conf->upstream.no_cache && conf->upstream.cache_bypass == NULL) {
        ngx_log_error(NGX_LOG_WARN, cf->log, 0,
             "\"proxy_no_cache\" functionality has been changed in 0.8.46, "
             "now it should be used together with \"proxy_cache_bypass\"");
    }

    ngx_conf_merge_ptr_value(conf->upstream.cache_valid,
                             prev->upstream.cache_valid, NULL);

    if (conf->cache_key.value.data == NULL) {
        conf->cache_key = prev->cache_key;
    }

    ngx_conf_merge_value(conf->upstream.cache_lock,
                              prev->upstream.cache_lock, 0);

    ngx_conf_merge_msec_value(conf->upstream.cache_lock_timeout,
                              prev->upstream.cache_lock_timeout, 5000);

#endif

    if (conf->method.len == 0) {
        conf->method = prev->method;

    } else {
        conf->method.data[conf->method.len] = ' ';
        conf->method.len++;
    }

    ngx_conf_merge_value(conf->upstream.pass_request_headers,
                              prev->upstream.pass_request_headers, 1);
    ngx_conf_merge_value(conf->upstream.pass_request_body,
                              prev->upstream.pass_request_body, 1);

    ngx_conf_merge_value(conf->upstream.intercept_errors,
                              prev->upstream.intercept_errors, 0);

#if (NGX_HTTP_SSL)
    ngx_conf_merge_value(conf->upstream.ssl_session_reuse,
                              prev->upstream.ssl_session_reuse, 1);
#endif

    ngx_conf_merge_value(conf->redirect, prev->redirect, 1);

    if (conf->redirect) {

        if (conf->redirects == NULL) {
            conf->redirects = prev->redirects;
        }

        if (conf->redirects == NULL && conf->url.data) {

            conf->redirects = ngx_array_create(cf->pool, 1,
                                             sizeof(ngx_http_proxy_rewrite_t));
            if (conf->redirects == NULL) {
                return NGX_CONF_ERROR;
            }

            pr = ngx_array_push(conf->redirects);
            if (pr == NULL) {
                return NGX_CONF_ERROR;
            }

            ngx_memzero(&pr->pattern.complex,
                        sizeof(ngx_http_complex_value_t));

            ngx_memzero(&pr->replacement, sizeof(ngx_http_complex_value_t));

            pr->handler = ngx_http_proxy_rewrite_complex_handler;

            if (conf->vars.uri.len) {
                pr->pattern.complex.value = conf->url;
                pr->replacement.value = conf->location;

            } else {
                pr->pattern.complex.value.len = conf->url.len
                                                + sizeof("/") - 1;

                p = ngx_pnalloc(cf->pool, pr->pattern.complex.value.len);
                if (p == NULL) {
                    return NGX_CONF_ERROR;
                }

                pr->pattern.complex.value.data = p;

                p = ngx_cpymem(p, conf->url.data, conf->url.len);
                *p = '/';

                ngx_str_set(&pr->replacement.value, "/");
            }
        }
    }

    ngx_conf_merge_ptr_value(conf->cookie_domains, prev->cookie_domains, NULL);

    ngx_conf_merge_ptr_value(conf->cookie_paths, prev->cookie_paths, NULL);

#if (NGX_HTTP_SSL)
    if (conf->upstream.ssl == NULL) {
        conf->upstream.ssl = prev->upstream.ssl;
    }
#endif

    ngx_conf_merge_uint_value(conf->http_version, prev->http_version,
                              NGX_HTTP_VERSION_10);

    ngx_conf_merge_uint_value(conf->headers_hash_max_size,
                              prev->headers_hash_max_size, 512);

    ngx_conf_merge_uint_value(conf->headers_hash_bucket_size,
                              prev->headers_hash_bucket_size, 64);

    conf->headers_hash_bucket_size = ngx_align(conf->headers_hash_bucket_size,
                                               ngx_cacheline_size);

    hash.max_size = conf->headers_hash_max_size;
    hash.bucket_size = conf->headers_hash_bucket_size;
    hash.name = "proxy_headers_hash";

	//	生成 conf->hide_headers_hash
    if (ngx_http_upstream_hide_headers_hash(cf, &conf->upstream,
            &prev->upstream, ngx_http_proxy_hide_headers, &hash)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (conf->upstream.upstream == NULL) {
        conf->upstream.upstream = prev->upstream.upstream;
        conf->vars = prev->vars;
    }

    if (conf->proxy_lengths == NULL) {
        conf->proxy_lengths = prev->proxy_lengths;
        conf->proxy_values = prev->proxy_values;
    }

    if (conf->upstream.upstream || conf->proxy_lengths) {
        clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
        if (clcf->handler == NULL && clcf->lmt_excpt) {
            clcf->handler = ngx_http_proxy_handler;
            conf->location = prev->location;
        }
    }

    if (conf->body_source.data == NULL) {
        conf->body_source = prev->body_source;
        conf->body_set_len = prev->body_set_len;
        conf->body_set = prev->body_set;
    }

	//	使用 "proxy_set_body" 指令
    if (conf->body_source.data && conf->body_set_len == NULL) {

        ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

        sc.cf = cf;
        sc.source = &conf->body_source;
        sc.flushes = &conf->flushes;
        sc.lengths = &conf->body_set_len;
        sc.values = &conf->body_set;
        sc.complete_lengths = 1;
        sc.complete_values = 1;

        if (ngx_http_script_compile(&sc) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

    if (ngx_http_proxy_merge_headers(cf, conf, prev) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_proxy_merge_headers(ngx_conf_t *cf, ngx_http_proxy_loc_conf_t *conf,
    ngx_http_proxy_loc_conf_t *prev)
{
    u_char                       *p;
    size_t                        size;
    uintptr_t                    *code;
    ngx_uint_t                    i;
    ngx_array_t                   headers_names, headers_merged;
    ngx_keyval_t                 *src, *s, *h;
    ngx_hash_key_t               *hk;
    ngx_hash_init_t               hash;
    ngx_http_script_compile_t     sc;
    ngx_http_script_copy_code_t  *copy;

    if (conf->headers_source == NULL) {
        conf->flushes = prev->flushes;
        conf->headers_set_len = prev->headers_set_len;
        conf->headers_set = prev->headers_set;
        conf->headers_set_hash = prev->headers_set_hash;
        conf->headers_source = prev->headers_source;
    }

    if (conf->headers_set_hash.buckets
        && ((conf->body_source.data == NULL)
            == (prev->body_source.data == NULL))
#if (NGX_HTTP_CACHE)
        && ((conf->upstream.cache == NULL) == (prev->upstream.cache == NULL))
#endif
       )
    {
        return NGX_OK;
    }


	//	1. 初始化headers_names数组为4个元素的ngx_hash_key_t， 用于初始化hash表headers_set_hash
    if (ngx_array_init(&headers_names, cf->temp_pool, 4, sizeof(ngx_hash_key_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

	//	2. 初始化headers_merged数组为4个元素的ngx_keyval_t，用于存放proxy_header的
    if (ngx_array_init(&headers_merged, cf->temp_pool, 4, sizeof(ngx_keyval_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

	//	headers_source未申请空间时，申请4个ngx_keyval_t
    if (conf->headers_source == NULL) {
        conf->headers_source = ngx_array_create(cf->pool, 4,
                                                sizeof(ngx_keyval_t));
        if (conf->headers_source == NULL) {
            return NGX_ERROR;
        }
    }

	//	
    conf->headers_set_len = ngx_array_create(cf->pool, 64, 1);
    if (conf->headers_set_len == NULL) {
        return NGX_ERROR;
    }

    conf->headers_set = ngx_array_create(cf->pool, 512, 1);
    if (conf->headers_set == NULL) {
        return NGX_ERROR;
    }


#if (NGX_HTTP_CACHE)

    h = conf->upstream.cache ? ngx_http_proxy_cache_headers:
                               ngx_http_proxy_headers;
#else

    h = ngx_http_proxy_headers;

#endif

	//	指令"proxy_set_header" - "允许将发送到被代理服务器的请求头重新定义或者增加一些字段"
	//	如果指令"proxy_set_header"设置了相应的头，则将这些头存放在 headers_merged 数组中
    src = conf->headers_source->elts;
    for (i = 0; i < conf->headers_source->nelts; i++) {

        s = ngx_array_push(&headers_merged);
        if (s == NULL) {
            return NGX_ERROR;
        }

        *s = src[i];
    }

	/*	
	 *	指令"proxy_set_header"设置的头域如果在系统已定义的列表中（ngx_http_proxy_headers数组）, 将忽略系统定义的头域
	 *	检查系统预定义的proxy_header（ngx_http_proxy_headers数组）是否与指令"proxy_set_header"设置的头部域相同，
	 *	如果不相同将增加系统定义的proxy_header到 headers_merged 数组中 
	 */
    while (h->key.len) {

        src = headers_merged.elts;
        for (i = 0; i < headers_merged.nelts; i++) {
            if (ngx_strcasecmp(h->key.data, src[i].key.data) == 0) {
                goto next;
            }
        }

        s = ngx_array_push(&headers_merged);
        if (s == NULL) {
            return NGX_ERROR;
        }

        *s = *h;

    next:

        h++;
    }

	//	指令 "proxy_set_body " 设置了proxy_body，将在headers_merged中加入"Content-Length"字段
    if (conf->body_source.data) {
        s = ngx_array_push(&headers_merged);
        if (s == NULL) {
            return NGX_ERROR;
        }

        ngx_str_set(&s->key, "Content-Length");
        ngx_str_set(&s->value, "$proxy_internal_body_length");
    }


    src = headers_merged.elts;
    for (i = 0; i < headers_merged.nelts; i++) {

        hk = ngx_array_push(&headers_names);
        if (hk == NULL) {
            return NGX_ERROR;
        }

        hk->key = src[i].key;
        hk->key_hash = ngx_hash_key_lc(src[i].key.data, src[i].key.len);
        hk->value = (void *) 1;

        if (src[i].value.len == 0) {
            continue;
        }

		//	检查header的value使用了变量
        if (ngx_http_script_variables_count(&src[i].value) == 0) {
			
			//	用于设置常量字符串
            copy = ngx_array_push_n(conf->headers_set_len,
                                    sizeof(ngx_http_script_copy_code_t));
            if (copy == NULL) {
                return NGX_ERROR;
            }

            copy->code = (ngx_http_script_code_pt)
                                                 ngx_http_script_copy_len_code;
            copy->len = src[i].key.len + sizeof(": ") - 1
                        + src[i].value.len + sizeof(CRLF) - 1;


            size = (sizeof(ngx_http_script_copy_code_t)					//	ngx_http_script_copy_code_t结构体
                       + src[i].key.len + sizeof(": ") - 1				//	key
                       + src[i].value.len + sizeof(CRLF) - 1			//	value
                       + sizeof(uintptr_t) - 1)							//	按4字节对齐
                    & ~(sizeof(uintptr_t) - 1);

            copy = ngx_array_push_n(conf->headers_set, size);
            if (copy == NULL) {
                return NGX_ERROR;
            }

            copy->code = ngx_http_script_copy_code;
            copy->len = src[i].key.len + sizeof(": ") - 1
                        + src[i].value.len + sizeof(CRLF) - 1;

            p = (u_char *) copy + sizeof(ngx_http_script_copy_code_t);

            p = ngx_cpymem(p, src[i].key.data, src[i].key.len);
            *p++ = ':'; *p++ = ' ';
            p = ngx_cpymem(p, src[i].value.data, src[i].value.len);
            *p++ = CR; *p = LF;

        } else {

			//	value使用了变量情况下，先将key部分压入脚本引擎队列中
            copy = ngx_array_push_n(conf->headers_set_len,
                                    sizeof(ngx_http_script_copy_code_t));
            if (copy == NULL) {
                return NGX_ERROR;
            }

            copy->code = (ngx_http_script_code_pt)
                                                 ngx_http_script_copy_len_code;
            copy->len = src[i].key.len + sizeof(": ") - 1;


            size = (sizeof(ngx_http_script_copy_code_t)
                    + src[i].key.len + sizeof(": ") - 1 + sizeof(uintptr_t) - 1)
                    & ~(sizeof(uintptr_t) - 1);

            copy = ngx_array_push_n(conf->headers_set, size);
            if (copy == NULL) {
                return NGX_ERROR;
            }

            copy->code = ngx_http_script_copy_code;
            copy->len = src[i].key.len + sizeof(": ") - 1;

            p = (u_char *) copy + sizeof(ngx_http_script_copy_code_t);
            p = ngx_cpymem(p, src[i].key.data, src[i].key.len);
            *p++ = ':'; *p = ' ';


            ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

            sc.cf = cf;
            sc.source = &src[i].value;
            sc.flushes = &conf->flushes;
            sc.lengths = &conf->headers_set_len;
            sc.values = &conf->headers_set;

            if (ngx_http_script_compile(&sc) != NGX_OK) {
                return NGX_ERROR;
            }

			//	以下设置CRLF
            copy = ngx_array_push_n(conf->headers_set_len,
                                    sizeof(ngx_http_script_copy_code_t));
            if (copy == NULL) {
                return NGX_ERROR;
            }

            copy->code = (ngx_http_script_code_pt)
                                                 ngx_http_script_copy_len_code;
            copy->len = sizeof(CRLF) - 1;


            size = (sizeof(ngx_http_script_copy_code_t)
                    + sizeof(CRLF) - 1 + sizeof(uintptr_t) - 1)
                    & ~(sizeof(uintptr_t) - 1);

            copy = ngx_array_push_n(conf->headers_set, size);
            if (copy == NULL) {
                return NGX_ERROR;
            }

            copy->code = ngx_http_script_copy_code;
            copy->len = sizeof(CRLF) - 1;

            p = (u_char *) copy + sizeof(ngx_http_script_copy_code_t);
            *p++ = CR; *p = LF;
        }

        code = ngx_array_push_n(conf->headers_set_len, sizeof(uintptr_t));
        if (code == NULL) {
            return NGX_ERROR;
        }

        *code = (uintptr_t) NULL;

        code = ngx_array_push_n(conf->headers_set, sizeof(uintptr_t));
        if (code == NULL) {
            return NGX_ERROR;
        }

        *code = (uintptr_t) NULL;
    }

    code = ngx_array_push_n(conf->headers_set_len, sizeof(uintptr_t));
    if (code == NULL) {
        return NGX_ERROR;
    }

    *code = (uintptr_t) NULL;


    hash.hash = &conf->headers_set_hash;
    hash.key = ngx_hash_key_lc;
    hash.max_size = conf->headers_hash_max_size;
    hash.bucket_size = conf->headers_hash_bucket_size;
    hash.name = "proxy_headers_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;

    return ngx_hash_init(&hash, headers_names.elts, headers_names.nelts);
}

/* 
 *	[analy]			
	 ngx_http_upstream_module模块在location层的配置被修改，
	 向 ngx_http_upstream_main_conf_t->upstreams数组中添加一个ngx_http_upstream_srv_conf_t配置

	 plcf->upstream.upstream指向的是ngx_http_upstream_main_conf_t->upstreams数组中的第一个元素
	 plcf->vars
	 plcf->location
	 plcf->url
 */
static char *
ngx_http_proxy_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_proxy_loc_conf_t *plcf = conf;

    size_t                      add;
    u_short                     port;
    ngx_str_t                  *value, *url;
    ngx_url_t                   u;
    ngx_uint_t                  n;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_script_compile_t   sc;

    if (plcf->upstream.upstream || plcf->proxy_lengths) {			//	检查在location中是否已经声明指令 "proxy_pass", 此指令不允许重复声明在同一个Location中
        return "is duplicate";
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_http_proxy_handler;							//	设置handler后，content phase阶段的其他handler将不会被执行

    if (clcf->name.data[clcf->name.len - 1] == '/') {				//	proxy_pass 指定的地址最后以"/"结尾的
        clcf->auto_redirect = 1;
    }

    value = cf->args->elts;

    url = &value[1];	//	获取url
	
    n = ngx_http_script_variables_count(url);						

    if (n) {

        ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

        sc.cf = cf;
        sc.source = url;
        sc.lengths = &plcf->proxy_lengths;
        sc.values = &plcf->proxy_values;
        sc.variables = n;
        sc.complete_lengths = 1;
        sc.complete_values = 1;

        if (ngx_http_script_compile(&sc) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

#if (NGX_HTTP_SSL)
        if (ngx_http_proxy_set_ssl(cf, plcf) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
#endif

        return NGX_CONF_OK;
    }

/****************		未使用变量		****************/

	//	检查URL的格式，只能是“http://”和“https://”
    if (ngx_strncasecmp(url->data, (u_char *) "http://", 7) == 0) {
        add = 7;
        port = 80;

    } else if (ngx_strncasecmp(url->data, (u_char *) "https://", 8) == 0) {

#if (NGX_HTTP_SSL)
        if (ngx_http_proxy_set_ssl(cf, plcf) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        add = 8;
        port = 443;
#else
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "https protocol requires SSL support");
        return NGX_CONF_ERROR;
#endif

    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid URL prefix");
        return NGX_CONF_ERROR;
    }

    ngx_memzero(&u, sizeof(ngx_url_t));

    u.url.len = url->len - add;				//	获取proxy_pass 配置中去掉http://或https://之后的部分
    u.url.data = url->data + add;
    u.default_port = port;					//	http://的端口是80，https;//端口是443
    u.uri_part = 1;
    u.no_resolve = 1;						//	设置不解析域名， 在函数 ngx_http_upstream_init_round_robin（）中会对指令 “proxy_pass”指定的参数（host或IP地址）进行解析

    plcf->upstream.upstream = ngx_http_upstream_add(cf, &u, 0);
    if (plcf->upstream.upstream == NULL) {
        return NGX_CONF_ERROR;
    }

	//	设置proxy模块使用的上下文schema
    plcf->vars.schema.len = add;				//	7
    plcf->vars.schema.data = url->data;			//	按照变量add的长度截取字符串； 结果值等于"http://"
    plcf->vars.key_start = plcf->vars.schema;	/*	key_start的长度会在ngx_http_proxy_set_vars中添加上host的长度，最终key_start指向整个url（http://www.baidu.com/ABC/）
													貌似key_start未计算uri的长度 */

	//	设置proxy使用的变量值
    ngx_http_proxy_set_vars(&u, &plcf->vars);	

    plcf->location = clcf->name;

    if (clcf->named
#if (NGX_PCRE)
        || clcf->regex
#endif
        || clcf->noname)
    {
        if (plcf->vars.uri.len) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "\"proxy_pass\" may not have URI part in "
                               "location given by regular expression, "
                               "or inside named location, "
                               "or inside the \"if\" statement, "
                               "or inside the \"limit_except\" block");
            return NGX_CONF_ERROR;
        }

        plcf->location.len = 0;
    }

    plcf->url = *url;

    return NGX_CONF_OK;
}


static char *
ngx_http_proxy_redirect(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_proxy_loc_conf_t *plcf = conf;

    u_char                            *p;
    ngx_str_t                         *value;
    ngx_http_proxy_rewrite_t          *pr;
    ngx_http_compile_complex_value_t   ccv;

    if (plcf->redirect == 0) {				//	plcf->redirect 默认1
        return NGX_CONF_OK;
    }

    plcf->redirect = 1;

    value = cf->args->elts;

    if (cf->args->nelts == 2) {
        if (ngx_strcmp(value[1].data, "off") == 0) {
            plcf->redirect = 0;
            plcf->redirects = NULL;
            return NGX_CONF_OK;
        }

        if (ngx_strcmp(value[1].data, "false") == 0) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0,
                           "invalid parameter \"false\", use \"off\" instead");
            plcf->redirect = 0;
            plcf->redirects = NULL;
            return NGX_CONF_OK;
        }

        if (ngx_strcmp(value[1].data, "default") != 0) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid parameter \"%V\"", &value[1]);
            return NGX_CONF_ERROR;
        }
    }

    if (plcf->redirects == NULL) {
        plcf->redirects = ngx_array_create(cf->pool, 1,
                                           sizeof(ngx_http_proxy_rewrite_t));
        if (plcf->redirects == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    pr = ngx_array_push(plcf->redirects);
    if (pr == NULL) {
        return NGX_CONF_ERROR;
    }

    if (ngx_strcmp(value[1].data, "default") == 0) {
        if (plcf->proxy_lengths) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "\"proxy_redirect default\" may not be used "
                               "with \"proxy_pass\" directive with variables");
            return NGX_CONF_ERROR;
        }

        if (plcf->url.data == NULL) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "\"proxy_redirect default\" must go "
                               "after the \"proxy_pass\" directive");
            return NGX_CONF_ERROR;
        }

        pr->handler = ngx_http_proxy_rewrite_complex_handler;

        ngx_memzero(&pr->pattern.complex, sizeof(ngx_http_complex_value_t));

        ngx_memzero(&pr->replacement, sizeof(ngx_http_complex_value_t));

        if (plcf->vars.uri.len) {
            pr->pattern.complex.value = plcf->url;
            pr->replacement.value = plcf->location;

        } else {
            pr->pattern.complex.value.len = plcf->url.len + sizeof("/") - 1;

            p = ngx_pnalloc(cf->pool, pr->pattern.complex.value.len);
            if (p == NULL) {
                return NGX_CONF_ERROR;
            }

            pr->pattern.complex.value.data = p;

            p = ngx_cpymem(p, plcf->url.data, plcf->url.len);
            *p = '/';

            ngx_str_set(&pr->replacement.value, "/");
        }

        return NGX_CONF_OK;
    }


    if (value[1].data[0] == '~') {
        value[1].len--;
        value[1].data++;

        if (value[1].data[0] == '*') {
            value[1].len--;
            value[1].data++;

            if (ngx_http_proxy_rewrite_regex(cf, pr, &value[1], 1) != NGX_OK) {
                return NGX_CONF_ERROR;
            }

        } else {
            if (ngx_http_proxy_rewrite_regex(cf, pr, &value[1], 0) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }

    } else {

        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

        ccv.cf = cf;
        ccv.value = &value[1];
        ccv.complex_value = &pr->pattern.complex;

        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        pr->handler = ngx_http_proxy_rewrite_complex_handler;
    }


    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[2];
    ccv.complex_value = &pr->replacement;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_proxy_cookie_domain(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_proxy_loc_conf_t *plcf = conf;

    ngx_str_t                         *value;
    ngx_http_proxy_rewrite_t          *pr;
    ngx_http_compile_complex_value_t   ccv;

    if (plcf->cookie_domains == NULL) {
        return NGX_CONF_OK;
    }

    value = cf->args->elts;

    if (cf->args->nelts == 2) {

        if (ngx_strcmp(value[1].data, "off") == 0) {
            plcf->cookie_domains = NULL;
            return NGX_CONF_OK;
        }

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid parameter \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    if (plcf->cookie_domains == NGX_CONF_UNSET_PTR) {
        plcf->cookie_domains = ngx_array_create(cf->pool, 1,
                                     sizeof(ngx_http_proxy_rewrite_t));
        if (plcf->cookie_domains == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    pr = ngx_array_push(plcf->cookie_domains);
    if (pr == NULL) {
        return NGX_CONF_ERROR;
    }

    if (value[1].data[0] == '~') {
        value[1].len--;
        value[1].data++;

        if (ngx_http_proxy_rewrite_regex(cf, pr, &value[1], 1) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

    } else {

        if (value[1].data[0] == '.') {
            value[1].len--;
            value[1].data++;
        }

        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

        ccv.cf = cf;
        ccv.value = &value[1];
        ccv.complex_value = &pr->pattern.complex;

        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        pr->handler = ngx_http_proxy_rewrite_domain_handler;

        if (value[2].data[0] == '.') {
            value[2].len--;
            value[2].data++;
        }
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[2];
    ccv.complex_value = &pr->replacement;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_proxy_cookie_path(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_proxy_loc_conf_t *plcf = conf;

    ngx_str_t                         *value;
    ngx_http_proxy_rewrite_t          *pr;
    ngx_http_compile_complex_value_t   ccv;

    if (plcf->cookie_paths == NULL) {
        return NGX_CONF_OK;
    }

    value = cf->args->elts;

    if (cf->args->nelts == 2) {

        if (ngx_strcmp(value[1].data, "off") == 0) {
            plcf->cookie_paths = NULL;
            return NGX_CONF_OK;
        }

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid parameter \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    if (plcf->cookie_paths == NGX_CONF_UNSET_PTR) {
        plcf->cookie_paths = ngx_array_create(cf->pool, 1,
                                     sizeof(ngx_http_proxy_rewrite_t));
        if (plcf->cookie_paths == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    pr = ngx_array_push(plcf->cookie_paths);
    if (pr == NULL) {
        return NGX_CONF_ERROR;
    }

    if (value[1].data[0] == '~') {
        value[1].len--;
        value[1].data++;

        if (value[1].data[0] == '*') {
            value[1].len--;
            value[1].data++;

            if (ngx_http_proxy_rewrite_regex(cf, pr, &value[1], 1) != NGX_OK) {
                return NGX_CONF_ERROR;
            }

        } else {
            if (ngx_http_proxy_rewrite_regex(cf, pr, &value[1], 0) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }

    } else {

        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

        ccv.cf = cf;
        ccv.value = &value[1];
        ccv.complex_value = &pr->pattern.complex;

        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        pr->handler = ngx_http_proxy_rewrite_complex_handler;
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[2];
    ccv.complex_value = &pr->replacement;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_proxy_rewrite_regex(ngx_conf_t *cf, ngx_http_proxy_rewrite_t *pr,
    ngx_str_t *regex, ngx_uint_t caseless)
{
#if (NGX_PCRE)
    u_char               errstr[NGX_MAX_CONF_ERRSTR];
    ngx_regex_compile_t  rc;

    ngx_memzero(&rc, sizeof(ngx_regex_compile_t));

    rc.pattern = *regex;
    rc.err.len = NGX_MAX_CONF_ERRSTR;
    rc.err.data = errstr;

    if (caseless) {
        rc.options = NGX_REGEX_CASELESS;
    }

    pr->pattern.regex = ngx_http_regex_compile(cf, &rc);
    if (pr->pattern.regex == NULL) {
        return NGX_ERROR;
    }

    pr->handler = ngx_http_proxy_rewrite_regex_handler;

    return NGX_OK;

#else

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "using regex \"%V\" requires PCRE library", regex);
    return NGX_ERROR;

#endif
}


static char *
ngx_http_proxy_store(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_proxy_loc_conf_t *plcf = conf;

    ngx_str_t                  *value;
    ngx_http_script_compile_t   sc;

    if (plcf->upstream.store != NGX_CONF_UNSET
        || plcf->upstream.store_lengths)
    {
        return "is duplicate";
    }

    value = cf->args->elts;

	//	指令"proxy_store"设置off
    if (ngx_strcmp(value[1].data, "off") == 0) {
        plcf->upstream.store = 0;
        return NGX_CONF_OK;
    }

#if (NGX_HTTP_CACHE)

	//	检查是否开启了"proxy_cache"， "proxy_cache"与"proxy_store"是互斥的
    if (plcf->upstream.cache != NGX_CONF_UNSET_PTR
        && plcf->upstream.cache != NULL)
    {
        return "is incompatible with \"proxy_cache\"";
    }

#endif


	//	指令"proxy_store"设置on
    if (ngx_strcmp(value[1].data, "on") == 0) {
        plcf->upstream.store = 1;
        return NGX_CONF_OK;
    }

    /* include the terminating '\0' into script */
    value[1].len++;

    ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

    sc.cf = cf;
    sc.source = &value[1];
    sc.lengths = &plcf->upstream.store_lengths;
    sc.values = &plcf->upstream.store_values;
    sc.variables = ngx_http_script_variables_count(&value[1]);
    sc.complete_lengths = 1;
    sc.complete_values = 1;

    if (ngx_http_script_compile(&sc) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


#if (NGX_HTTP_CACHE)

static char *
ngx_http_proxy_cache(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_proxy_loc_conf_t *plcf = conf;

    ngx_str_t  *value;

    value = cf->args->elts;

    if (plcf->upstream.cache != NGX_CONF_UNSET_PTR) {
        return "is duplicate";
    }

    if (ngx_strcmp(value[1].data, "off") == 0) {
        plcf->upstream.cache = NULL;
        return NGX_CONF_OK;
    }

	//	与指令"proxy_store"不兼容
    if (plcf->upstream.store > 0 || plcf->upstream.store_lengths) {
        return "is incompatible with \"proxy_store\"";
    }

	//	申请共享内存结构
    plcf->upstream.cache = ngx_shared_memory_add(cf, &value[1], 0,
                                                 &ngx_http_proxy_module);
    if (plcf->upstream.cache == NULL) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_proxy_cache_key(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_proxy_loc_conf_t *plcf = conf;

    ngx_str_t                         *value;
    ngx_http_compile_complex_value_t   ccv;

    value = cf->args->elts;

    if (plcf->cache_key.value.len) {
        return "is duplicate";
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[1];
    ccv.complex_value = &plcf->cache_key;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

#endif


static char *
ngx_http_proxy_lowat_check(ngx_conf_t *cf, void *post, void *data)
{
#if (NGX_FREEBSD)
    ssize_t *np = data;

    if ((u_long) *np >= ngx_freebsd_net_inet_tcp_sendspace) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"proxy_send_lowat\" must be less than %d "
                           "(sysctl net.inet.tcp.sendspace)",
                           ngx_freebsd_net_inet_tcp_sendspace);

        return NGX_CONF_ERROR;
    }

#elif !(NGX_HAVE_SO_SNDLOWAT)
    ssize_t *np = data;

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"proxy_send_lowat\" is not supported, ignored");

    *np = 0;

#endif

    return NGX_CONF_OK;
}


#if (NGX_HTTP_SSL)

static ngx_int_t
ngx_http_proxy_set_ssl(ngx_conf_t *cf, ngx_http_proxy_loc_conf_t *plcf)
{
    ngx_pool_cleanup_t  *cln;

    plcf->upstream.ssl = ngx_pcalloc(cf->pool, sizeof(ngx_ssl_t));
    if (plcf->upstream.ssl == NULL) {
        return NGX_ERROR;
    }

    plcf->upstream.ssl->log = cf->log;

    if (ngx_ssl_create(plcf->upstream.ssl,
                       NGX_SSL_SSLv2|NGX_SSL_SSLv3|NGX_SSL_TLSv1
                                    |NGX_SSL_TLSv1_1|NGX_SSL_TLSv1_2,
                       NULL)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    cln = ngx_pool_cleanup_add(cf->pool, 0);
    if (cln == NULL) {
        return NGX_ERROR;
    }

    cln->handler = ngx_ssl_cleanup_ctx;
    cln->data = plcf->upstream.ssl;

    return NGX_OK;
}

#endif


static void
ngx_http_proxy_set_vars(ngx_url_t *u, ngx_http_proxy_vars_t *v)
{
    if (u->family != AF_UNIX) {

		//	url中没有设置端口，或设置的端口与默认端口不一致
        if (u->no_port || u->port == u->default_port) {

			//	设置host
            v->host_header = u->host;

			//	设置端口
            if (u->default_port == 80) {
                ngx_str_set(&v->port, "80");

            } else {
                ngx_str_set(&v->port, "443");
            }

        } else {
            v->host_header.len = u->host.len + 1 + u->port_text.len;
            v->host_header.data = u->host.data;
            v->port = u->port_text;
        }

		//	v->key_start的	len=schema+host
        v->key_start.len += v->host_header.len;

    } else {
        ngx_str_set(&v->host_header, "localhost");
        ngx_str_null(&v->port);
        v->key_start.len += sizeof("unix:") - 1 + u->host.len + 1;
    }

    v->uri = u->uri;			//	设置uri
}
