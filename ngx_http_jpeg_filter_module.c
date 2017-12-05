#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <libmodjpeg.h>

#define NGX_HTTP_IMAGE_NONE      0
#define NGX_HTTP_IMAGE_JPEG      1

#define NGX_HTTP_IMAGE_START     0
#define NGX_HTTP_IMAGE_READ      1
#define NGX_HTTP_IMAGE_PROCESS   2
#define NGX_HTTP_IMAGE_PASS      3
#define NGX_HTTP_IMAGE_DONE      4

#define NGX_HTTP_IMAGE_BUFFERED  0x08

typedef struct {
	ngx_uint_t	max_width;
	ngx_uint_t	max_height;

	ngx_flag_t	enable;
	ngx_flag_t	optimize;
	ngx_flag_t	progressive;

	size_t		buffer_size;
} ngx_http_jpeg_filter_loc_conf_t;

typedef struct {
	u_char		*in_image;
	u_char		*in_last;

	u_char		*out_image;
	u_char 		*out_last;

	size_t		length;

	ngx_uint_t	width;
	ngx_uint_t	height;
	ngx_uint_t	max_width;
	ngx_uint_t	max_height;

	ngx_uint_t	phase;
} ngx_http_jpeg_filter_ctx_t;

static ngx_command_t ngx_http_jpeg_filter_commands[] = {
	{ ngx_string("jpeg_filter"),
	  NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	  ngx_conf_set_flag_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_jpeg_filter_loc_conf_t, enable),
	  NULL },

	{ ngx_string("jpeg_filter_max_width"),
	  NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
	  ngx_conf_set_num_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_jpeg_filter_loc_conf_t, max_width),
	  NULL },

	{ ngx_string("jpeg_filter_max_height"),
	  NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
	  ngx_conf_set_num_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_jpeg_filter_loc_conf_t, max_height),
	  NULL },

	{ ngx_string("jpeg_filter_optimize"),
	  NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	  ngx_conf_set_flag_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_jpeg_filter_loc_conf_t, optimize),
	  NULL },

	{ ngx_string("jpeg_filter_progressive"),
	  NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	  ngx_conf_set_flag_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_jpeg_filter_loc_conf_t, progressive),
	  NULL },

	{ ngx_string("jpeg_filter_buffer"),
	  NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
	  ngx_conf_set_size_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_jpeg_filter_loc_conf_t, buffer_size),
	  NULL },

	ngx_null_command
};

static ngx_int_t ngx_http_jpeg_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_jpeg_body_filter(ngx_http_request_t *r, ngx_chain_t *in);
static ngx_int_t ngx_http_jpeg_filter_send(ngx_http_request_t *r, ngx_http_jpeg_filter_ctx_t *ctx, ngx_chain_t *in);
static ngx_uint_t ngx_http_jpeg_filter_test(ngx_http_request_t *r, ngx_chain_t *in);
static ngx_int_t ngx_http_jpeg_filter_read(ngx_http_request_t *r, ngx_chain_t *in);
static ngx_buf_t *ngx_http_jpeg_filter_process(ngx_http_request_t *r);
//static ngx_buf_t *ngx_http_jpeg_filter_asis(ngx_http_request_t *r, ngx_http_jpeg_filter_ctx_t *ctx);
static ngx_buf_t *ngx_http_jpeg_filter_modified(ngx_http_request_t *r, ngx_http_jpeg_filter_ctx_t *ctx);
static void ngx_http_jpeg_filter_length(ngx_http_request_t *r, ngx_buf_t *b);
static void *ngx_http_jpeg_filter_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_jpeg_filter_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_http_jpeg_filter_init(ngx_conf_t *cf);
static void ngx_http_jpeg_filter_cleanup(void *data);

static ngx_http_module_t ngx_http_jpeg_filter_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_jpeg_filter_init,          /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_jpeg_filter_create_loc_conf,          /* create location configuration */
    ngx_http_jpeg_filter_merge_loc_conf            /* merge location configuration */
};

ngx_module_t  ngx_http_jpeg_filter_module = {
	NGX_MODULE_V1,
	&ngx_http_jpeg_filter_module_ctx, /* module context */
	ngx_http_jpeg_filter_commands,   /* module directives */
	NGX_HTTP_MODULE,               /* module type */
	NULL,                          /* init master */
	NULL,                          /* init module */
	NULL,                          /* init process */
	NULL,                          /* init thread */
	NULL,                          /* exit thread */
	NULL,                          /* exit process */
	NULL,                          /* exit master */
	NGX_MODULE_V1_PADDING
};

static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;

static ngx_int_t ngx_http_jpeg_header_filter(ngx_http_request_t *r) {
	off_t                          len;
	ngx_http_jpeg_filter_ctx_t   *ctx;
	ngx_http_jpeg_filter_loc_conf_t  *conf;

	if (r->headers_out.status == NGX_HTTP_NOT_MODIFIED) {
		return ngx_http_next_header_filter(r);
	}

	ctx = ngx_http_get_module_ctx(r, ngx_http_jpeg_filter_module);
	if(ctx) {
		ngx_http_set_ctx(r, NULL, ngx_http_jpeg_filter_module);
		return ngx_http_next_header_filter(r);
	}

	conf = ngx_http_get_module_loc_conf(r, ngx_http_jpeg_filter_module);

	if(conf->enable == 0) {
		return ngx_http_next_header_filter(r);
	}

	if(
		r->headers_out.content_type.len >= sizeof("multipart/x-mixed-replace") - 1 &&
		ngx_strncasecmp(
			r->headers_out.content_type.data,
			(u_char *) "multipart/x-mixed-replace",
			sizeof("multipart/x-mixed-replace") - 1
		) == 0
	) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "jpeg_filter: multipart/x-mixed-replace response");

		return NGX_ERROR;
	}

	ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_jpeg_filter_ctx_t));
	if (ctx == NULL) {
		return NGX_ERROR;
	}

	ngx_http_set_ctx(r, ctx, ngx_http_jpeg_filter_module);

	len = r->headers_out.content_length_n;

	if(len != -1 && len > (off_t)conf->buffer_size) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "jpeg_filter: too big response: %O", len);

		return NGX_HTTP_UNSUPPORTED_MEDIA_TYPE;
	}

	if(len == -1) {
		ctx->length = conf->buffer_size;
	} else {
		ctx->length = (size_t)len;
	}

	if(r->headers_out.refresh) {
		r->headers_out.refresh->hash = 0;
	}

	r->main_filter_need_in_memory = 1;
	r->allow_ranges = 0;

	return NGX_OK;
}

static ngx_int_t ngx_http_jpeg_body_filter(ngx_http_request_t *r, ngx_chain_t *in) {
	ngx_int_t			rc;
	//ngx_str_t			*ct;
	ngx_chain_t			out;
	ngx_http_jpeg_filter_ctx_t	*ctx;
	//ngx_http_jpeg_filter_loc_conf_t	*conf;

	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter");

	if(in == NULL) {
		return ngx_http_next_body_filter(r, in);
	}

	ctx = ngx_http_get_module_ctx(r, ngx_http_jpeg_filter_module);
	if(ctx == NULL) {
		return ngx_http_next_body_filter(r, in);
	}

	switch(ctx->phase) {
	case NGX_HTTP_IMAGE_START:
		if(ngx_http_jpeg_filter_test(r, in) == NGX_HTTP_IMAGE_NONE) {
			return ngx_http_next_body_filter(r, in);
		}

		r->headers_out.content_type.len = sizeof("image/jpeg") - 1;
        	r->headers_out.content_type.data = (u_char *) "image/jpeg";

		ctx->phase = NGX_HTTP_IMAGE_READ;

	case NGX_HTTP_IMAGE_READ:
		rc = ngx_http_jpeg_filter_read(r, in);

		if (rc == NGX_AGAIN) {
			return NGX_OK;
		}

		if (rc == NGX_ERROR) {
			return ngx_http_filter_finalize_request(r, &ngx_http_jpeg_filter_module, NGX_HTTP_UNSUPPORTED_MEDIA_TYPE);
		}

	case NGX_HTTP_IMAGE_PROCESS:
		out.buf = ngx_http_jpeg_filter_process(r);

		if(out.buf == NULL) {
			return ngx_http_filter_finalize_request(r, &ngx_http_jpeg_filter_module, NGX_HTTP_UNSUPPORTED_MEDIA_TYPE);
		}

		out.next = NULL;
		ctx->phase = NGX_HTTP_IMAGE_PASS;

		return ngx_http_jpeg_filter_send(r, ctx, &out);

	case NGX_HTTP_IMAGE_PASS:
		rc = ngx_http_next_body_filter(r, in);

	default:
		rc = ngx_http_next_body_filter(r, NULL);

		/* NGX_ERROR resets any pending data */
		return (rc == NGX_OK) ? NGX_ERROR : rc;
	}
}

static ngx_int_t ngx_http_jpeg_filter_send(ngx_http_request_t *r, ngx_http_jpeg_filter_ctx_t *ctx, ngx_chain_t *in) {
	ngx_int_t  rc;

	rc = ngx_http_next_header_filter(r);

	if(rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
		return NGX_ERROR;
	}

	rc = ngx_http_next_body_filter(r, in);

	if (ctx->phase == NGX_HTTP_IMAGE_DONE) {
		/* NGX_ERROR resets any pending data */
		return (rc == NGX_OK) ? NGX_ERROR : rc;
	}

	return rc;
}

static ngx_uint_t ngx_http_jpeg_filter_test(ngx_http_request_t *r, ngx_chain_t *in) {
	u_char  *p;

	p = in->buf->pos;

	if (in->buf->last - p < 16) {
		return NGX_HTTP_IMAGE_NONE;
	}

	ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter: \"%c%c\"", p[0], p[1]);

	if(p[0] == 0xff && p[1] == 0xd8) { /* JPEG */
		return NGX_HTTP_IMAGE_JPEG;
	}

	return NGX_HTTP_IMAGE_NONE;
}

static ngx_int_t ngx_http_jpeg_filter_read(ngx_http_request_t *r, ngx_chain_t *in) {
	u_char				*p;
	size_t				size, rest;
	ngx_buf_t			*b;
	ngx_chain_t			*cl;
	ngx_http_jpeg_filter_ctx_t	*ctx;

	ctx = ngx_http_get_module_ctx(r, ngx_http_jpeg_filter_module);

	if(ctx->in_image == NULL) {
		ctx->in_image = ngx_palloc(r->pool, ctx->length);
		if (ctx->in_image == NULL) {
			return NGX_ERROR;
		}

		ctx->in_last = ctx->in_image;
	}

	p = ctx->in_last;

	for(cl = in; cl; cl = cl->next) {
		b = cl->buf;
		size = b->last - b->pos;

		ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "image buf: %uz", size);

		rest = ctx->in_image + ctx->length - p;

		if(size > rest) {
			ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "jpeg_filter: too big response");
			return NGX_ERROR;
		}

		p = ngx_cpymem(p, b->pos, size);
		b->pos += size;

		if (b->last_buf) {
			ctx->in_last = p;
			return NGX_OK;
		}
	}

	ctx->in_last = p;
	r->connection->buffered |= NGX_HTTP_IMAGE_BUFFERED;

	return NGX_AGAIN;
}

static ngx_buf_t *ngx_http_jpeg_filter_process(ngx_http_request_t *r) {
	ngx_http_jpeg_filter_ctx_t	*ctx;
	ngx_http_jpeg_filter_loc_conf_t  *conf;
	ngx_pool_cleanup_t            *cln;

	mj_jpeg_t *m;

	ctx = ngx_http_get_module_ctx(r, ngx_http_jpeg_filter_module);

	if(ctx->in_image == NULL) {
		return NULL;
	}

	conf = ngx_http_get_module_loc_conf(r, ngx_http_jpeg_filter_module);

	m = mj_read_jpeg_from_buffer((char *)ctx->in_image, ctx->length);

	int options = 0;

	if(conf->optimize) {
		options |= MJ_OPTION_OPTIMIZE;
	}

	if(conf->progressive) {
		options |= MJ_OPTION_PROGRESSIVE;
	}

	size_t len;

	if(mj_write_jpeg_to_buffer(m, (char **)&ctx->out_image, &len, options) != 0) {
		mj_destroy_jpeg(m);
		return NULL;
	}

	mj_destroy_jpeg(m);

	cln = ngx_pool_cleanup_add(r->pool, 0);
	if(cln == NULL) {
		return NULL;
	}

	cln->handler = ngx_http_jpeg_filter_cleanup;
	cln->data = ctx;

	ctx->out_last = ctx->out_image + len;

	return ngx_http_jpeg_filter_modified(r, ctx);
}
/*
static ngx_buf_t *ngx_http_jpeg_filter_asis(ngx_http_request_t *r, ngx_http_jpeg_filter_ctx_t *ctx) {
	ngx_buf_t  *b;

	b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
	if (b == NULL) {
		return NULL;
	}

	b->pos = ctx->in_image;
	b->last = ctx->in_last;
	b->memory = 1;
	b->last_buf = 1;

	ngx_http_jpeg_filter_length(r, b);

	return b;
}
*/
static ngx_buf_t *ngx_http_jpeg_filter_modified(ngx_http_request_t *r, ngx_http_jpeg_filter_ctx_t *ctx) {
	ngx_buf_t  *b;

	b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
	if (b == NULL) {
		return NULL;
	}

	b->pos = ctx->out_image;
	b->last = ctx->out_last;
	b->memory = 1;
	b->last_buf = 1;

	ngx_http_jpeg_filter_length(r, b);

	return b;
}

static void ngx_http_jpeg_filter_length(ngx_http_request_t *r, ngx_buf_t *b) {
    r->headers_out.content_length_n = b->last - b->pos;

    if (r->headers_out.content_length) {
        r->headers_out.content_length->hash = 0;
    }

    r->headers_out.content_length = NULL;
}

static void ngx_http_jpeg_filter_cleanup(void *data) {
	return;
}

static void *ngx_http_jpeg_filter_create_loc_conf(ngx_conf_t *cf) {
	ngx_http_jpeg_filter_loc_conf_t  *conf;

	conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_jpeg_filter_loc_conf_t));
	if(conf == NULL) {
		return NGX_CONF_ERROR;
	}

	conf->enable = 0;
	conf->optimize = 0;
	conf->progressive = 0;

	conf->max_width = NGX_CONF_UNSET_UINT;
	conf->max_height = NGX_CONF_UNSET_UINT;

	return conf;
}

static char *ngx_http_jpeg_filter_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child) {
	ngx_http_jpeg_filter_loc_conf_t *prev = parent;
	ngx_http_jpeg_filter_loc_conf_t *conf = child;

	ngx_conf_merge_value(conf->enable, prev->enable, 0);
	ngx_conf_merge_value(conf->optimize, prev->optimize, 0);
	ngx_conf_merge_value(conf->progressive, prev->progressive, 0);

	ngx_conf_merge_uint_value(conf->max_width, prev->max_width, 0);
	ngx_conf_merge_uint_value(conf->max_height, prev->max_height, 0);
/*
	if(conf->min_radius < 1) {
		ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "min_radius must be equal or more than 1");
		return NGX_CONF_ERROR;
	}

	if(conf->max_radius < conf->min_radius) {
		ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "max_radius must be equal or more than min_radius");
		return NGX_CONF_ERROR;
	}
*/
	return NGX_CONF_OK;
}

static ngx_int_t ngx_http_jpeg_filter_init(ngx_conf_t *cf) {
	ngx_http_next_header_filter = ngx_http_top_header_filter;
	ngx_http_top_header_filter = ngx_http_jpeg_header_filter;

	ngx_http_next_body_filter = ngx_http_top_body_filter;
	ngx_http_top_body_filter = ngx_http_jpeg_body_filter;

	return NGX_OK;
}
