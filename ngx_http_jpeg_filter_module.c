#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
	ngx_uint_t	max_width;
	ngx_uint_t	max_height;

	ngx_flag_t	enabled;
	ngx_flag_t	optimize;
	ngx_flag_t	progressive;

	size_t		buffer_size;
} ngx_http_modjpeg_loc_conf_t;

typedef struct {
	u_char		*image;
	u_char		*last;

	size_t		length;

	ngx_uint_t	width;
	ngx_uint_t	height;
	ngx_uint_t	max_width;
	ngx_uint_t	max_height;

	ngx_uint_t	phase;
} ngx_http_modjpeg_ctx_t;

static ngx_command_t ngx_http_modjpeg_commands[] = {
	{ ngx_string("modjpeg"),
	  NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	  ngx_conf_set_flag_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_modjpeg_loc_conf_t, enabled),
	  NULL },

	{ ngx_string("modjpeg_max_width"),
	  NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
	  ngx_conf_set_num_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_modjpeg_loc_conf_t, max_width),
	  NULL },

	{ ngx_string("modjpeg_max_height"),
	  NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
	  ngx_conf_set_num_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_modjpeg_loc_conf_t, max_height),
	  NULL },

	{ ngx_string("modjpeg_optimize"),
	  NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	  ngx_conf_set_flag_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_modjpeg_loc_conf_t, optimize),
	  NULL },

	{ ngx_string("modjpeg_progressive"),
	  NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	  ngx_conf_set_flag_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_modjpeg_loc_conf_t, progressive),
	  NULL },

	{ ngx_string("image_filter_buffer"),
	  NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
	  ngx_conf_set_size_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_modjpeg_loc_conf_t, buffer_size),
	  NULL },

	ngx_null_command
};

static ngx_http_module_t  ngx_http_modjpeg_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_modjpeg_filter_init,          /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_modjpeg_create_conf,          /* create location configuration */
    ngx_http_modjpeg_merge_conf            /* merge location configuration */
};

static ngx_int_t ngx_http_modjpeg_header_filter(ngx_http_request_t *r) {
	off_t                          len;
	ngx_http_modjpeg_ctx_t   *ctx;
	ngx_http_modjpeg_conf_t  *conf;

	if (r->headers_out.status == NGX_HTTP_NOT_MODIFIED) {
		return ngx_http_next_header_filter(r);
	}

	ctx = ngx_http_get_module_ctx(r, ngx_http_modjpeg_module);
	if(ctx) {
		ngx_http_set_ctx(r, NULL, ngx_http_modjpeg_module);
		return ngx_http_next_header_filter(r);
	}

	conf = ngx_http_get_module_loc_conf(r, ngx_http_modjpeg_module);

	if(conf->enabled == 0) {
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
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "modjpeg: multipart/x-mixed-replace response");

		return NGX_ERROR;
	}

	ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_modjpeg_ctx_t));
	if (ctx == NULL) {
		return NGX_ERROR;
	}

	ngx_http_set_ctx(r, ctx, ngx_http_modjpeg_module);

	len = r->headers_out.content_length_n;

	if(len != -1 && len > (off_t)conf->buffer_size) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "modjpeg: too big response: %O", len);

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

static ngx_int_t ngx_http_modjpeg_body_filter(ngx_http_request_t *r, ngx_chain_t *in) {
	ngx_int_t			rc;
	ngx_str_t			*ct;
	ngx_chain_t			out;
	ngx_http_image_filter_ctx_t	*ctx;
	ngx_http_image_filter_conf_t	*conf;

	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "modjpeg filter");

	if(in == NULL) {
		return ngx_http_next_body_filter(r, in);
	}

	ctx = ngx_http_get_module_ctx(r, ngx_http_modjpeg_module);
	if(ctx == NULL) {
		return ngx_http_next_body_filter(r, in);
	}

	switch(ctx->phase) {
	case NGX_HTTP_IMAGE_START:
		if(ngx_http_modjpeg_test(r, in) == NGX_ERROR) {
			return ngx_http_next_body_filter(r, in);
		}

		r->headers_out.content_type_len = sizeof("image/jpeg") - 1;
        	r->headers_out.content_type = (u_char *) "image/jpeg";

		ctx->phase = NGX_HTTP_IMAGE_READ;

	case NGX_HTTP_IMAGE_READ:
		rc = ngx_http_modjpeg_read(r, in);

		if (rc == NGX_AGAIN) {
			return NGX_OK;
		}

		if (rc == NGX_ERROR) {
			return ngx_http_filter_finalize_request(r, &ngx_http_modjpeg_module, NGX_HTTP_UNSUPPORTED_MEDIA_TYPE);
		}

	case NGX_HTTP_IMAGE_PROCESS:
		out.buf = ngx_http_modjpeg_process(r);

		if(out.buf == NULL) {
			return ngx_http_filter_finalize_request(r, &ngx_http_modjpeg_module, NGX_HTTP_UNSUPPORTED_MEDIA_TYPE);
		}

		out.next = NULL;
		ctx->phase = NGX_HTTP_IMAGE_PASS;

		return ngx_http_modjpeg_send(r, ctx, &out);

	case NGX_HTTP_IMAGE_PASS:
		rc = ngx_http_next_body_filter(r, in);

	default:
		rc = ngx_http_next_body_filter(r, NULL);

		/* NGX_ERROR resets any pending data */
		return (rc == NGX_OK) ? NGX_ERROR : rc;
	}
}

static ngx_int_t ngx_http_modjpeg_send(ngx_http_request_t *r, ngx_http_modjpeg_ctx_t *ctx, ngx_chain_t *in) {
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

static ngx_uint_t ngx_http_modjpeg_test(ngx_http_request_t *r, ngx_chain_t *in)
{
	u_char  *p;

	p = in->buf->pos;

	if (in->buf->last - p < 16) {
		return NGX_ERROR;
	}

	ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "modjpeg filter: \"%c%c\"", p[0], p[1]);

	if(p[0] == 0xff && p[1] == 0xd8) { /* JPEG */
		return NGX_OK;
	}

	return NGX_ERROR;
}

static ngx_int_t ngx_http_modjpeg_read(ngx_http_request_t *r, ngx_chain_t *in) {
	u_char				*p;
	size_t				size, rest;
	ngx_buf_t			*b;
	ngx_chain_t			*cl;
	ngx_http_image_filter_ctx_t	*ctx;

	ctx = ngx_http_get_module_ctx(r, ngx_http_modjpeg_module);

	if (ctx->image == NULL) {
		ctx->image = ngx_palloc(r->pool, ctx->length);
		if (ctx->image == NULL) {
			return NGX_ERROR;
		}

		ctx->last = ctx->image;
	}

	p = ctx->last;

	for(cl = in; cl; cl = cl->next) {
		b = cl->buf;
		size = b->last - b->pos;

		ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "image buf: %uz", size);

		rest = ctx->image + ctx->length - p;

		if(size > rest) {
			ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "modjpeg filter: too big response");
			return NGX_ERROR;
		}

		p = ngx_cpymem(p, b->pos, size);
		b->pos += size;

		if (b->last_buf) {
			ctx->last = p;
			return NGX_OK;
		}
	}

	ctx->last = p;
	r->connection->buffered |= NGX_HTTP_IMAGE_BUFFERED;

	return NGX_AGAIN;
}

static ngx_buf_t *ngx_http_image_process(ngx_http_request_t *r) {
	return NULL;
}

static void *ngx_http_modjpeg_create_loc_conf(ngx_conf_t *cf) {
	ngx_http_modjpeg_loc_conf_t  *conf;

	conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_modjpeg_loc_conf_t));
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

static char *ngx_http_modjpeg_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child) {
	ngx_http_modjpeg_loc_conf_t *prev = parent;
	ngx_http_modjpeg_loc_conf_t *conf = child;

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

ngx_module_t  ngx_http_modjpeg_module = {
	NGX_MODULE_V1,
	&ngx_http_modjpeg_module_ctx, /* module context */
	ngx_http_modjpeg_commands,   /* module directives */
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

static ngx_int_t ngx_http_modjpeg_filter_init(ngx_conf_t *cf) {
	ngx_http_next_header_filter = ngx_http_top_header_filter;
	ngx_http_top_header_filter = ngx_http_modjpeg_header_filter;

	ngx_http_next_body_filter = ngx_http_top_body_filter;
	ngx_http_top_body_filter = ngx_http_modjpeg_body_filter;

	return NGX_OK;
}
