#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <libmodjpeg.h>

#define NGX_HTTP_IMAGE_NONE      0
#define NGX_HTTP_IMAGE_JPEG      1

#define NGX_HTTP_JPEG_FILTER_PHASE_START     0
#define NGX_HTTP_JPEG_FILTER_PHASE_READ      1
#define NGX_HTTP_JPEG_FILTER_PHASE_PROCESS   2
#define NGX_HTTP_JPEG_FILTER_PHASE_DONE      4

#define NGX_HTTP_JPEG_FILTER_UNMODIFIED   0
#define NGX_HTTP_JPEG_FILTER_MODIFIED     1

#define NGX_HTTP_IMAGE_BUFFERED  0x08

#define NGX_HTTP_JPEG_FILTER_TYPE_GRAYSCALE          1
#define NGX_HTTP_JPEG_FILTER_TYPE_PIXELATE           2
#define NGX_HTTP_JPEG_FILTER_TYPE_BRIGHTEN           3
#define NGX_HTTP_JPEG_FILTER_TYPE_DARKEN             4
#define NGX_HTTP_JPEG_FILTER_TYPE_TINTBLUE           5
#define NGX_HTTP_JPEG_FILTER_TYPE_TINTYELLOW         6
#define NGX_HTTP_JPEG_FILTER_TYPE_TINTRED            7
#define NGX_HTTP_JPEG_FILTER_TYPE_TINTGREEN          8
#define NGX_HTTP_JPEG_FILTER_TYPE_DROPON_POSITION    9
#define NGX_HTTP_JPEG_FILTER_TYPE_DROPON_OFFSET     10
#define NGX_HTTP_JPEG_FILTER_TYPE_DROPON            11

typedef struct {
	ngx_uint_t	type;
	void		*element;
} ngx_http_jpeg_filter_element_t;

typedef struct {
	ngx_http_complex_value_t  cv1;
} ngx_http_jpeg_filter_element_cv1_t;

typedef struct {
	ngx_http_complex_value_t 	 cv1;
	ngx_http_complex_value_t 	 cv2;
	mj_dropon_t 			*dropon;
} ngx_http_jpeg_filter_element_dropon_t;

typedef struct {
	ngx_uint_t	max_width;
	ngx_uint_t	max_height;

	ngx_flag_t	enable;
	ngx_flag_t	optimize;
	ngx_flag_t	progressive;
	ngx_flag_t 	graceful;

	ngx_array_t    *filter_elements;

	size_t		buffer_size;
} ngx_http_jpeg_filter_conf_t;

typedef struct {
	u_char		*in_image;
	u_char		*in_last;

	u_char		*out_image;
	u_char 		*out_last;

	size_t		length;

	ngx_uint_t	width;
	ngx_uint_t	height;

	ngx_uint_t	phase;
} ngx_http_jpeg_filter_ctx_t;

static ngx_int_t ngx_http_jpeg_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_jpeg_body_filter(ngx_http_request_t *r, ngx_chain_t *in);

static ngx_int_t ngx_http_jpeg_filter_send(ngx_http_request_t *r, ngx_uint_t image);
static ngx_uint_t ngx_http_jpeg_filter_test(ngx_http_request_t *r, ngx_chain_t *in);
static ngx_int_t ngx_http_jpeg_filter_read(ngx_http_request_t *r, ngx_chain_t *in);
static ngx_int_t ngx_http_jpeg_filter_process(ngx_http_request_t *r);
static void ngx_http_jpeg_filter_cleanup(void *data);

static char *ngx_conf_jpeg_filter_effect(ngx_conf_t *cf, ngx_command_t *cmd, void *c);
static char *ngx_conf_jpeg_filter_dropon(ngx_conf_t *cf, ngx_command_t *cmd, void *c);

static void *ngx_http_jpeg_filter_create_conf(ngx_conf_t *cf);
static char *ngx_http_jpeg_filter_merge_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_http_jpeg_filter_init(ngx_conf_t *cf);

static ngx_int_t ngx_http_jpeg_filter_get_int_value(ngx_http_request_t *r, ngx_http_jpeg_filter_element_t *fe, ngx_int_t defval);

static ngx_command_t ngx_http_jpeg_filter_commands[] = {
	{ ngx_string("jpeg_filter"),
	  NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	  ngx_conf_set_flag_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_jpeg_filter_conf_t, enable),
	  NULL },

	{ ngx_string("jpeg_filter_max_width"),
	  NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
	  ngx_conf_set_num_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_jpeg_filter_conf_t, max_width),
	  NULL },

	{ ngx_string("jpeg_filter_max_height"),
	  NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
	  ngx_conf_set_num_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_jpeg_filter_conf_t, max_height),
	  NULL },

	{ ngx_string("jpeg_filter_optimize"),
	  NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	  ngx_conf_set_flag_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_jpeg_filter_conf_t, optimize),
	  NULL },

	{ ngx_string("jpeg_filter_progressive"),
	  NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	  ngx_conf_set_flag_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_jpeg_filter_conf_t, progressive),
	  NULL },

	{ ngx_string("jpeg_filter_graceful"),
	  NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	  ngx_conf_set_flag_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_jpeg_filter_conf_t, graceful),
	  NULL },

	{ ngx_string("jpeg_filter_buffer"),
	  NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
	  ngx_conf_set_size_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_jpeg_filter_conf_t, buffer_size),
	  NULL },

	{ ngx_string("jpeg_filter_effect"),
	  NGX_HTTP_LOC_CONF|NGX_CONF_TAKE12,
	  ngx_conf_jpeg_filter_effect,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  0,
	  NULL },

	{ ngx_string("jpeg_filter_dropon"),
	  NGX_HTTP_LOC_CONF|NGX_CONF_TAKE3|NGX_CONF_TAKE4,
	  ngx_conf_jpeg_filter_dropon,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  0,
	  NULL },

	ngx_null_command
};

static ngx_http_module_t ngx_http_jpeg_filter_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_jpeg_filter_init,             /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_jpeg_filter_create_conf,      /* create location configuration */
    ngx_http_jpeg_filter_merge_conf        /* merge location configuration */
};

ngx_module_t  ngx_http_jpeg_filter_module = {
	NGX_MODULE_V1,
	&ngx_http_jpeg_filter_module_ctx,  /* module context */
	ngx_http_jpeg_filter_commands,     /* module directives */
	NGX_HTTP_MODULE,                   /* module type */
	NULL,                              /* init master */
	NULL,                              /* init module */
	NULL,                              /* init process */
	NULL,                              /* init thread */
	NULL,                              /* exit thread */
	NULL,                              /* exit process */
	NULL,                              /* exit master */
	NGX_MODULE_V1_PADDING
};

static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;

static ngx_int_t ngx_http_jpeg_header_filter(ngx_http_request_t *r) {
	off_t                         len;
	ngx_http_jpeg_filter_ctx_t   *ctx;
	ngx_http_jpeg_filter_conf_t  *conf;

	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter: ngx_http_jpeg_header_filter");

	if (r->headers_out.status == NGX_HTTP_NOT_MODIFIED) {
		return ngx_http_next_header_filter(r);
	}

	/* Check if we already have a context for this request and our module */
	ctx = ngx_http_get_module_ctx(r, ngx_http_jpeg_filter_module);
	if(ctx) {
		/* There is already a context for this filter? Remove it, next! */
		ngx_http_set_ctx(r, NULL, ngx_http_jpeg_filter_module);
		return ngx_http_next_header_filter(r);
	}

	/* Get our configuration */
	conf = ngx_http_get_module_loc_conf(r, ngx_http_jpeg_filter_module);
	if(conf->enable == 0) {
		/* This filter is not enabled. Next! */
		return ngx_http_next_header_filter(r);
	}

	/* Check for multipart/x-mixed-replace. We can't handle this. Next */
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

	/* Allocate space for our context struct, so can store some state */
	ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_jpeg_filter_ctx_t));
	if (ctx == NULL) {
		return NGX_ERROR;
	}

	/* Associate our context struct with the request and module context */
	ngx_http_set_ctx(r, ctx, ngx_http_jpeg_filter_module);

	/* Check for the body length and if we support this. We need to buffer
	 * the whole body and we have an upper limit for how much memory we are
	 * willing to allocate.
	**/
	len = r->headers_out.content_length_n;

	if(len != -1 && len > (off_t)conf->buffer_size) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "jpeg_filter: too big response: %O", len);

		return NGX_HTTP_UNSUPPORTED_MEDIA_TYPE;
	}

	/* In our context for this request, set the length of the body. We need this later
	 * in the body filter to allocate the memory for the buffer that we're going to buffer.
	**/
	if(len == -1) {
		ctx->length = conf->buffer_size;
	} else {
		ctx->length = (size_t)len;
	}

	/* Copied from image_filter. No exact clue what this is doing */
	if(r->headers_out.refresh) {
		r->headers_out.refresh->hash = 0;
	}

	/* Copied from image_filter. No clue what these are doing */
	r->main_filter_need_in_memory = 1;
	r->allow_ranges = 0;

	/*
	 * Do not call the next header filter because we don't know yet
	 * the length of the modified body.
	 */
	return NGX_OK;
}

static ngx_int_t ngx_http_jpeg_body_filter(ngx_http_request_t *r, ngx_chain_t *in) {
	ngx_int_t			 rc;
	ngx_http_jpeg_filter_ctx_t	*ctx;
	ngx_http_jpeg_filter_conf_t	*conf;

	/* Now it's our turn */
	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter: ngx_http_jpeg_body_filter");

	/* Bail out to the next body filter if there's no data */
	if(in == NULL) {
		return ngx_http_next_body_filter(r, in);
	}

	/* Get the configuration for our filter */
	conf = ngx_http_get_module_loc_conf(r, ngx_http_jpeg_filter_module);
	if(conf->enable == 0) {
		/* Our filter is not enabled. Next! */
		return ngx_http_next_body_filter(r, in);
	}

	/* Get our context for this request that we allocated in the header filter */
	ctx = ngx_http_get_module_ctx(r, ngx_http_jpeg_filter_module);
	if(ctx == NULL) {
		/* No context? Next! */
		return ngx_http_next_body_filter(r, in);
	}

	/* Because the body data it most probably split into several chains and this
	 * function will be called more than once, we have to remember in what "phase" we're in
	**/
	switch(ctx->phase) {
	case NGX_HTTP_JPEG_FILTER_PHASE_START:
		/* This is the first time we see some data for our filter */
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter: phase START");

		/* Have a taste of the first bytes of data in order to find out
		 * if this actually we should care about and can handle.
		**/
		if(ngx_http_jpeg_filter_test(r, in) == NGX_HTTP_IMAGE_NONE) {
			/* Invalid data. Next! */
			return ngx_http_next_body_filter(r, in);
		}

        	/* Following calls of this function go directly to the reading phase */
		ctx->phase = NGX_HTTP_JPEG_FILTER_PHASE_READ;

		/* Fall through */

	case NGX_HTTP_JPEG_FILTER_PHASE_READ:
		/* Here we want to read all the data into our buffer */
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter: phase READ");

		rc = ngx_http_jpeg_filter_read(r, in);

		/* If there is more data, return nicely but don't call the next filter, so we will get more data! */
		if(rc == NGX_AGAIN) {
			return NGX_OK;
		}

		/* If there was an error, abort and send some error code */
		if(rc == NGX_ERROR) {
			return ngx_http_filter_finalize_request(r, &ngx_http_jpeg_filter_module, NGX_HTTP_UNSUPPORTED_MEDIA_TYPE);
		}

		/* Fall through (rc == NGX_OK) */

	case NGX_HTTP_JPEG_FILTER_PHASE_PROCESS:
		/* Now that we have all the bytes from the image, we can go on an process it */
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter: phase PROCESS");

		ctx->phase = NGX_HTTP_JPEG_FILTER_PHASE_DONE;

		rc = ngx_http_jpeg_filter_process(r);
		if(rc == NGX_ERROR) {
			/* There was a problem processing the image. Bail out */

			if(conf->graceful == 1) {
				return ngx_http_jpeg_filter_send(r, NGX_HTTP_JPEG_FILTER_UNMODIFIED);
			}
			else {
				return ngx_http_filter_finalize_request(r, &ngx_http_jpeg_filter_module, NGX_HTTP_UNSUPPORTED_MEDIA_TYPE);
			}
		}

		/* Send the modified image */
		return ngx_http_jpeg_filter_send(r, NGX_HTTP_JPEG_FILTER_MODIFIED);

	case NGX_HTTP_JPEG_FILTER_PHASE_DONE:
	default:
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter: phase default (DONE)");

		rc = ngx_http_next_body_filter(r, NULL);

		/* NGX_ERROR resets any pending data */
		return (rc == NGX_OK) ? NGX_ERROR : rc;
	}
}

/* Send the data to the next header and body filter */
static ngx_int_t ngx_http_jpeg_filter_send(ngx_http_request_t *r, ngx_uint_t image) {
	ngx_buf_t                   *b;
	ngx_chain_t                  out;
	ngx_int_t                    rc;
	ngx_http_jpeg_filter_ctx_t  *ctx;

	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter: ngx_http_jpeg_filter_send");

	ctx = ngx_http_get_module_ctx(r, ngx_http_jpeg_filter_module);

	/* Allocate memory for a buffer */
	b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
	if(b == NULL) {
		return NGX_ERROR;
	}

	if(image == NGX_HTTP_JPEG_FILTER_MODIFIED) {
		b->pos = ctx->out_image;
		b->last = ctx->out_last;
	}
	else {
		b->pos = ctx->in_image;
		b->last = ctx->in_last;
	}

	b->memory = 1;
	b->last_buf = 1;

	out.buf = b;
	out.next = NULL;

	/* Set the content type. However, this should be already the case, but better be sure */
	r->headers_out.content_type.len = sizeof("image/jpeg") - 1;
	r->headers_out.content_type.data = (u_char *) "image/jpeg";

	/* The content length must be adjusted */
	r->headers_out.content_length_n = b->last - b->pos;

	if(r->headers_out.content_length) {
		r->headers_out.content_length->hash = 0;
	}

	r->headers_out.content_length = NULL;

	/* Now that we are done and we know the final size of the modified body
	 * we can proceed to the next header filter.
	 */
	rc = ngx_http_next_header_filter(r);

	/* Bail out if something went wrong */
	if(rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
		return NGX_ERROR;
	}

	/* Push the modified body to the next body filter */
	return ngx_http_next_body_filter(r, &out);
}

/* Test the incoming data if we can and should handle it */
static ngx_uint_t ngx_http_jpeg_filter_test(ngx_http_request_t *r, ngx_chain_t *in) {
	u_char  *p;

	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter: ngx_http_jpeg_filter_test");

	/* Checking if we have enough data available such that we can
	 * decide if we can and should handle it.
	 **/
	p = in->buf->pos;

	if(in->buf->last - p < 16) {
		return NGX_HTTP_IMAGE_NONE;
	}

	ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter: \"%02xd%02xd\"", p[0], p[1]);

	/* Check for JPEG signature */
	if(p[0] == 0xff && p[1] == 0xd8) { /* JPEG */
		return NGX_HTTP_IMAGE_JPEG;
	}

	return NGX_HTTP_IMAGE_NONE;
}

/* Read several buffer chains and store the data in a buffer */
static ngx_int_t ngx_http_jpeg_filter_read(ngx_http_request_t *r, ngx_chain_t *in) {
	u_char				*p;
	size_t				size, rest;
	ngx_buf_t			*b;
	ngx_chain_t			*cl;
	ngx_http_jpeg_filter_ctx_t	*ctx;

	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter: ngx_http_jpeg_filter_read");

	/* Get our context for this request */
	ctx = ngx_http_get_module_ctx(r, ngx_http_jpeg_filter_module);

	/* If we didn't allocate yet memory for the image, we do it now */
	if(ctx->in_image == NULL) {
		/* We found out the size of the buffer in the header filter */
		ctx->in_image = ngx_palloc(r->pool, ctx->length);
		if (ctx->in_image == NULL) {
			return NGX_ERROR;
		}

		ctx->in_last = ctx->in_image;
	}

	p = ctx->in_last;

	/* Copying the data from the buffer chain into out buffer */
	for(cl = in; cl; cl = cl->next) {
		b = cl->buf;
		size = b->last - b->pos;

		ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter buf: %uz", size);

		rest = ctx->in_image + ctx->length - p;

		if(size > rest) {
			ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "jpeg_filter: too big response");
			return NGX_ERROR;
		}

		p = ngx_cpymem(p, b->pos, size);
		b->pos += size;

		if (b->last_buf) {
			ctx->in_last = p;

			/* If this was the last buffer chain, we're done */
			return NGX_OK;
		}
	}

	ctx->in_last = p;

	/* This should be probably done, but no clue what it actually means */
	r->connection->buffered |= NGX_HTTP_IMAGE_BUFFERED;

	/* This wasn't the las buffer chain. Tell the caller that we're expecting more */
	return NGX_AGAIN;
}

/* Process the image */
static ngx_int_t ngx_http_jpeg_filter_process(ngx_http_request_t *r) {
	ngx_http_jpeg_filter_ctx_t   *ctx;
	ngx_http_jpeg_filter_conf_t  *conf;
	ngx_pool_cleanup_t           *cln;

	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter: ngx_http_jpeg_filter_process");

	mj_jpeg_t *m;

	/* Get our context for this request */
	ctx = ngx_http_get_module_ctx(r, ngx_http_jpeg_filter_module);
	if(ctx->in_image == NULL) {
		/* No data available. Bail out */
		return NGX_ERROR;
	}

	/* Get out module configuration so we know what we actually have to do */
	conf = ngx_http_get_module_loc_conf(r, ngx_http_jpeg_filter_module);

	/* Read in the image */
	m = mj_read_jpeg_from_buffer((char *)ctx->in_image, ctx->length);
	if(m == NULL) {
		return NGX_ERROR;
	}

	ngx_http_jpeg_filter_element_t *felts = conf->filter_elements->elts;
	ngx_uint_t i;
	ngx_int_t n;

	for(i = 0; i < conf->filter_elements->nelts; i++) {
		switch(felts[i].type) {
			case NGX_HTTP_JPEG_FILTER_TYPE_GRAYSCALE:
				mj_effect_grayscale(m);
				break;
			case NGX_HTTP_JPEG_FILTER_TYPE_PIXELATE:
				mj_effect_pixelate(m);
				break;
			case NGX_HTTP_JPEG_FILTER_TYPE_BRIGHTEN:
				n = ngx_http_jpeg_filter_get_int_value(r, &felts[i], 0);
				if(n < 0) {
					n = 0;
				}

				mj_effect_luminance(m, n);
				break;
			case NGX_HTTP_JPEG_FILTER_TYPE_DARKEN:
				n = ngx_http_jpeg_filter_get_int_value(r, &felts[i], 0);
				if(n < 0) {
					n = 0;
				}

				mj_effect_luminance(m, -n);
				break;
			case NGX_HTTP_JPEG_FILTER_TYPE_TINTBLUE:
				n = ngx_http_jpeg_filter_get_int_value(r, &felts[i], 0);
				if(n < 0) {
					n = 0;
				}

				mj_effect_tint(m, n, 0);
				break;
			case NGX_HTTP_JPEG_FILTER_TYPE_TINTYELLOW:
				n = ngx_http_jpeg_filter_get_int_value(r, &felts[i], 0);
				if(n < 0) {
					n = 0;
				}

				mj_effect_tint(m, -n, 0);
				break;
			case NGX_HTTP_JPEG_FILTER_TYPE_TINTRED:
				n = ngx_http_jpeg_filter_get_int_value(r, &felts[i], 0);
				if(n < 0) {
					n = 0;
				}

				mj_effect_tint(m, 0, n);
				break;
			case NGX_HTTP_JPEG_FILTER_TYPE_TINTGREEN:
				n = ngx_http_jpeg_filter_get_int_value(r, &felts[i], 0);
				if(n < 0) {
					n = 0;
				}

				mj_effect_tint(m, 0, -n);
				break;
			default:
				break;
		}
	}
	
	/* Apply the options */
	int options = 0;

	if(conf->optimize) {
		options |= MJ_OPTION_OPTIMIZE;
	}

	if(conf->progressive) {
		options |= MJ_OPTION_PROGRESSIVE;
	}

	/* Write the modified image to a new buffer */

	size_t len;

	if(mj_write_jpeg_to_buffer(m, (char **)&ctx->out_image, &len, options) != 0) {
		mj_destroy_jpeg(m);
		return NGX_ERROR;
	}

	ctx->out_last = ctx->out_image + len;

	/* Destroy the modified image */
	mj_destroy_jpeg(m);

	/* Add a cleanup routine for the allocated buffer that holds
	 * the modified image. We can only destroy it safely after it has been send.
	 **/
	cln = ngx_pool_cleanup_add(r->pool, 0);
	if(cln == NULL) {
		return NGX_ERROR;
	}

	cln->handler = ngx_http_jpeg_filter_cleanup;
	cln->data = ctx;

	return NGX_OK;
}

static ngx_int_t ngx_http_jpeg_filter_get_int_value(ngx_http_request_t *r, ngx_http_jpeg_filter_element_t *fe, ngx_int_t defval) {
	ngx_http_jpeg_filter_element_cv1_t *fecv1;
	ngx_str_t val;
	ngx_int_t n;

	fecv1 = (ngx_http_jpeg_filter_element_cv1_t *)fe->element;

	if(ngx_http_complex_value(r, &fecv1->cv1, &val) != NGX_OK) {
		return defval;
	}

	n = ngx_atoi(val.data, val.len);

	return n;
}

/* Cleanup after we did out job */
static void ngx_http_jpeg_filter_cleanup(void *data) {
	ngx_http_jpeg_filter_ctx_t *ctx = data;

	if(ctx->out_image != NULL) {
		free(ctx->out_image);
	}

	return;
}

static char *ngx_conf_jpeg_filter_effect(ngx_conf_t *cf, ngx_command_t *cmd, void *c) {
	ngx_http_jpeg_filter_conf_t *conf = c;

	ngx_str_t                         *value;
	ngx_http_compile_complex_value_t   ccv;

	ngx_http_jpeg_filter_element_t     *fe;
	ngx_http_jpeg_filter_element_cv1_t *fecv1;

	fprintf(stderr, "jpeg_filter: ngx_conf_jpeg_filter_effect\n");

	value = cf->args->elts;

	if(conf->filter_elements == NULL) {
		conf->filter_elements = ngx_array_create(cf->pool, 10, sizeof(ngx_http_jpeg_filter_element_t));
		if(conf->filter_elements == NULL) {
			return NGX_CONF_ERROR;
		}
	}

	fe = (ngx_http_jpeg_filter_element_t *)ngx_array_push(conf->filter_elements);
	if(fe == NULL) {
		return NGX_CONF_ERROR;
	}

	ngx_memzero(fe, sizeof(ngx_http_jpeg_filter_element_t));

	if(cf->args->nelts == 2) {
		if(ngx_strcmp(value[1].data, "grayscale") == 0) {
			fe->type = NGX_HTTP_JPEG_FILTER_TYPE_GRAYSCALE;
		}
		else if(ngx_strcmp(value[1].data, "pixelate") == 0) {
			fe->type = NGX_HTTP_JPEG_FILTER_TYPE_PIXELATE;
		}
		else {
			return NGX_CONF_ERROR;
		}
	}
	else if(cf->args->nelts == 3) {
		if(ngx_strcmp(value[1].data, "brighten") == 0) {
			fe->type = NGX_HTTP_JPEG_FILTER_TYPE_BRIGHTEN;
		}
		else if(ngx_strcmp(value[1].data, "darken") == 0) {
			fe->type = NGX_HTTP_JPEG_FILTER_TYPE_DARKEN;
		}
		else if(ngx_strcmp(value[1].data, "tintblue") == 0) {
			fe->type = NGX_HTTP_JPEG_FILTER_TYPE_TINTBLUE;
		}
		else if(ngx_strcmp(value[1].data, "tintyellow") == 0) {
			fe->type = NGX_HTTP_JPEG_FILTER_TYPE_TINTYELLOW;
		}
		else if(ngx_strcmp(value[1].data, "tintred") == 0) {
			fe->type = NGX_HTTP_JPEG_FILTER_TYPE_TINTRED;
		}
		else if(ngx_strcmp(value[1].data, "tintgreen") == 0) {
			fe->type = NGX_HTTP_JPEG_FILTER_TYPE_TINTGREEN;
		}
		else {
			return NGX_CONF_ERROR;
		}

		fecv1 = ngx_pcalloc(cf->pool, sizeof(ngx_http_jpeg_filter_element_cv1_t));
		if(fecv1 == NULL) {
			return NGX_CONF_ERROR;
		}

		ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

		ccv.cf = cf;
		ccv.value = &value[2];
		ccv.complex_value = &fecv1->cv1;
		ccv.zero = 1;

		if(ngx_http_compile_complex_value(&ccv) != NGX_OK) {
			return NGX_CONF_ERROR;
		}

		fe->element = fecv1;
	}

	return NGX_CONF_OK;
}

static char *ngx_conf_jpeg_filter_dropon(ngx_conf_t *cf, ngx_command_t *cmd, void *c) {
	return NGX_CONF_OK;
}

static void *ngx_http_jpeg_filter_create_conf(ngx_conf_t *cf) {
	ngx_http_jpeg_filter_conf_t  *conf;

	conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_jpeg_filter_conf_t));
	if(conf == NULL) {
		return NGX_CONF_ERROR;
	}

	conf->enable = NGX_CONF_UNSET;
	conf->optimize = NGX_CONF_UNSET;
	conf->progressive = NGX_CONF_UNSET;
	conf->graceful = NGX_CONF_UNSET;

	conf->max_width = NGX_CONF_UNSET_UINT;
	conf->max_height = NGX_CONF_UNSET_UINT;

	conf->buffer_size = NGX_CONF_UNSET_SIZE;

	return conf;
}

static char *ngx_http_jpeg_filter_merge_conf(ngx_conf_t *cf, void *parent, void *child) {
	ngx_http_jpeg_filter_conf_t *prev = parent;
	ngx_http_jpeg_filter_conf_t *conf = child;

	ngx_conf_merge_value(conf->enable, prev->enable, 0);
	ngx_conf_merge_value(conf->optimize, prev->optimize, 0);
	ngx_conf_merge_value(conf->progressive, prev->progressive, 0);
	ngx_conf_merge_value(conf->graceful, prev->graceful, 0);

	ngx_conf_merge_uint_value(conf->max_width, prev->max_width, 0);
	ngx_conf_merge_uint_value(conf->max_height, prev->max_height, 0);

	ngx_conf_merge_size_value(conf->buffer_size, prev->buffer_size, 2 * 1024 * 1024);
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
