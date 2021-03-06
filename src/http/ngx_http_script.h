
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HTTP_SCRIPT_H_INCLUDED_
#define _NGX_HTTP_SCRIPT_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    u_char                     *ip;					//	指向 ngx_http_rewrite_loc_conf_t->codes
    u_char                     *pos;				//	pos 指向的是复杂变量被展开后的值
	
	//	指向 ngx_http_rewrite_loc_conf_t->stack_size 个数的 ngx_http_variable_value_t 数组中, 
	//	在函数 ngx_http_script_value_code（）中进行赋值
    ngx_http_variable_value_t  *sp;					

    ngx_str_t                   buf;				//	存放复合变量中变量value和常量字符串的值
    ngx_str_t                   line;

    /* the start of the rewritten arguments */
    u_char                     *args;

    unsigned                    flushed:1;			//	是否在缓冲区获取变量的值
    unsigned                    skip:1;				//	？？？？在拷贝的时候会跳过拷贝
    unsigned                    quote:1;
    unsigned                    is_args:1;
    unsigned                    log:1;

    ngx_int_t                   status;
    ngx_http_request_t         *request;			//	关联的request请求
} ngx_http_script_engine_t;


typedef struct {
    ngx_conf_t                 *cf;
    ngx_str_t                  *source;					// 需要compile的字符串

    ngx_array_t               **flushes;				// 存放变量的index	array of ngx_uint_t's point
    ngx_array_t               **lengths;				//	存放变量的value和常量字符串的长度
    ngx_array_t               **values;					//	存放变量的value值和常量字符串的值

    ngx_uint_t                  variables;				// 普通变量的个数，而非其他三种(args变量，$n变量以及常量字符串)  
    ngx_uint_t                  ncaptures;				// 当前处理时，出现的$n变量的最大值，如配置的最大为$3，那么ncaptures就等于3

	/*
	 * 以位移的形式保存$1,$2...$9等变量，即响应位置上置1来表示，主要的作用是为dup_capture准备，
	 * 正是由于这个mask的存在，才比较容易得到是否有重复的$n出现。
	 */
    ngx_uint_t                  captures_mask;
    ngx_uint_t                  size;					// 待compile的字符串中，"常量字符串"的总长度

    void                       *main;					//	??????????

    unsigned                    compile_args:1;			// 是否需要处理请求参数
    unsigned                    complete_lengths:1;		// 是否设置lengths数组的终止符，即NULL
    unsigned                    complete_values:1;		// 是否设置values数组的终止符
    unsigned                    zero:1;					// values数组运行时，得到的字符串是否追加'\0'结尾
    unsigned                    conf_prefix:1;			// 是否在生成的文件名前，追加配置文件的路径前缀（e.g. "/usr/local/nginx/conf/"）
    unsigned                    root_prefix:1;			// 是否在生成的文件名前，追加系统运行的路径前缀（e.g. "/usr/local/nginx/"）
	
	/*
	 * 这个标记位主要在rewrite模块里使用，在ngx_http_rewrite中，
	 * if (sc.variables == 0 && !sc.dup_capture) {
	 *     regex->lengths = NULL;
	 * }
	 * 没有重复的$n，那么regex->lengths被置为NULL，这个设置很关键，在函数
	 * ngx_http_script_regex_start_code中就是通过对regex->lengths的判断，来做不同的处理，
	 * 因为在没有重复的$n的时候，可以通过正则自身的captures机制来获取$n，一旦出现重复的，
	 * 那么pcre正则自身的captures并不能满足我们的要求，我们需要用自己handler来处理。
	 */
    unsigned                    dup_capture:1;
    unsigned                    args:1;					// 待compile的字符串中是否发现了字符“?”
} ngx_http_script_compile_t;


typedef struct {
    ngx_str_t                   value;				//	需要处理的字符串
    ngx_uint_t                 *flushes;			//	处理完的变量index
    void                       *lengths;			//	存放处理value的长度处理函数指针
    void                       *values;				//	存放处理value的处理函数指针
} ngx_http_complex_value_t;


typedef struct {
    ngx_conf_t                 *cf;
    ngx_str_t                  *value;
    ngx_http_complex_value_t   *complex_value;		

    unsigned                    zero:1;
    unsigned                    conf_prefix:1;
    unsigned                    root_prefix:1;
} ngx_http_compile_complex_value_t;


typedef void (*ngx_http_script_code_pt) (ngx_http_script_engine_t *e);
typedef size_t (*ngx_http_script_len_code_pt) (ngx_http_script_engine_t *e);


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   len;			//	单个常量字符串的长度
} ngx_http_script_copy_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   index;
} ngx_http_script_var_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    ngx_http_set_variable_pt    handler;
    uintptr_t                   data;
} ngx_http_script_var_handler_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   n;
} ngx_http_script_copy_capture_code_t;


#if (NGX_PCRE)

typedef struct {
    ngx_http_script_code_pt     code;
    ngx_http_regex_t           *regex;
    ngx_array_t                *lengths;
    uintptr_t                   size;
    uintptr_t                   status;
    uintptr_t                   next;

    uintptr_t                   test:1;
    uintptr_t                   negative_test:1;
    uintptr_t                   uri:1;
    uintptr_t                   args:1;

    /* add the r->args to the new arguments */
    uintptr_t                   add_args:1;

    uintptr_t                   redirect:1;				//	rewrite的flag指令后配置了redirect或permanent时，会将此字段设置为1，还有一种情况TODO在分析
    uintptr_t                   break_cycle:1;			//	rewrite的flag指令配置了break时，会将此字段设置为1

    ngx_str_t                   name;
} ngx_http_script_regex_code_t;


typedef struct {
    ngx_http_script_code_pt     code;

    uintptr_t                   uri:1;
    uintptr_t                   args:1;

    /* add the r->args to the new arguments */
    uintptr_t                   add_args:1;

    uintptr_t                   redirect:1;
} ngx_http_script_regex_end_code_t;

#endif


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   conf_prefix;
} ngx_http_script_full_name_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   status;
    ngx_http_complex_value_t    text;
} ngx_http_script_return_code_t;


typedef enum {
    ngx_http_script_file_plain = 0,
    ngx_http_script_file_not_plain,
    ngx_http_script_file_dir,
    ngx_http_script_file_not_dir,
    ngx_http_script_file_exists,
    ngx_http_script_file_not_exists,
    ngx_http_script_file_exec,
    ngx_http_script_file_not_exec
} ngx_http_script_file_op_e;


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   op;					//	对应 ngx_http_script_file_op_e 枚举中的一个变量
} ngx_http_script_file_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   next;				//	存放偏移量
    void                      **loc_conf;			//	指向上层的location的配置
} ngx_http_script_if_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    ngx_array_t                *lengths;
} ngx_http_script_complex_value_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   value;
    uintptr_t                   text_len;				//	保存变量的value的长度
    uintptr_t                   text_data;				//	保存变量的name的data地址
} ngx_http_script_value_code_t;


void ngx_http_script_flush_complex_value(ngx_http_request_t *r,
    ngx_http_complex_value_t *val);
ngx_int_t ngx_http_complex_value(ngx_http_request_t *r,
    ngx_http_complex_value_t *val, ngx_str_t *value);
ngx_int_t ngx_http_compile_complex_value(ngx_http_compile_complex_value_t *ccv);
char *ngx_http_set_complex_value_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


ngx_int_t ngx_http_test_predicates(ngx_http_request_t *r,
    ngx_array_t *predicates);
char *ngx_http_set_predicate_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

ngx_uint_t ngx_http_script_variables_count(ngx_str_t *value);
ngx_int_t ngx_http_script_compile(ngx_http_script_compile_t *sc);
u_char *ngx_http_script_run(ngx_http_request_t *r, ngx_str_t *value,
    void *code_lengths, size_t reserved, void *code_values);
void ngx_http_script_flush_no_cacheable_variables(ngx_http_request_t *r,
    ngx_array_t *indices);

void *ngx_http_script_start_code(ngx_pool_t *pool, ngx_array_t **codes,
    size_t size);
void *ngx_http_script_add_code(ngx_array_t *codes, size_t size, void *code);

size_t ngx_http_script_copy_len_code(ngx_http_script_engine_t *e);
void ngx_http_script_copy_code(ngx_http_script_engine_t *e);
size_t ngx_http_script_copy_var_len_code(ngx_http_script_engine_t *e);
void ngx_http_script_copy_var_code(ngx_http_script_engine_t *e);
size_t ngx_http_script_copy_capture_len_code(ngx_http_script_engine_t *e);
void ngx_http_script_copy_capture_code(ngx_http_script_engine_t *e);
size_t ngx_http_script_mark_args_code(ngx_http_script_engine_t *e);
void ngx_http_script_start_args_code(ngx_http_script_engine_t *e);
#if (NGX_PCRE)
void ngx_http_script_regex_start_code(ngx_http_script_engine_t *e);
void ngx_http_script_regex_end_code(ngx_http_script_engine_t *e);
#endif
void ngx_http_script_return_code(ngx_http_script_engine_t *e);
void ngx_http_script_break_code(ngx_http_script_engine_t *e);
void ngx_http_script_if_code(ngx_http_script_engine_t *e);
void ngx_http_script_equal_code(ngx_http_script_engine_t *e);
void ngx_http_script_not_equal_code(ngx_http_script_engine_t *e);
void ngx_http_script_file_code(ngx_http_script_engine_t *e);
void ngx_http_script_complex_value_code(ngx_http_script_engine_t *e);
void ngx_http_script_value_code(ngx_http_script_engine_t *e);
void ngx_http_script_set_var_code(ngx_http_script_engine_t *e);
void ngx_http_script_var_set_handler_code(ngx_http_script_engine_t *e);
void ngx_http_script_var_code(ngx_http_script_engine_t *e);
void ngx_http_script_nop_code(ngx_http_script_engine_t *e);


#endif /* _NGX_HTTP_SCRIPT_H_INCLUDED_ */
