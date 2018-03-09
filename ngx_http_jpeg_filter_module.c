/*
 * Copyright (c) Ingo Oppermann
 *
 * JPEG filter module using libmodjpeg (github.com/ioppermann/libmodjpeg)
 *
 * This module is heavily inspired by the image filter module with
 * insights from
 *
 *    "Emiller’s Guide To Nginx Module Development"
 *    https://www.evanmiller.org/nginx-modules-guide.html
 *
 * and the
 *
 *    nginx development guide
 *    https://nginx.org/en/docs/dev/development_guide.html.
 *
 * Directives:
 *
 * jpeg_filter on|off
 * Default: off
 * Context: location
 *
 * jpeg_filter_max_width width
 * Default: 0 (not limited)
 * Context: http, server, location
 *
 * jpeg_filter_max_height height
 * Default: 0 (not limited)
 * Context: http, server, location
 *
 * jpeg_filter_optimize on|off
 * Default: off
 * Context: http, server, location
 *
 * jpeg_filter_progressive on|off
 * Default: off
 * Context: http, server, location
 *
 * jpeg_filter_arithmetric on|off
 * Default: off
 * Context: http, server, location
 *
 * jpeg_filter_graceful on|off
 * Default: off
 * Context: http, server, location
 *
 * jpeg_filter_buffer size
 * Default: 2M
 * Context: http, server, location
 *
 * jpeg_filter_effect grayscale|pixelate
 * jpeg_filter_effect darken|brighten value
 * jpeg_filter_effect tintblue|tintyellow|tintred|tintgreen value
 * Default: -
 * Context: location
 *
 * jpeg_filter_dropon_align top|center|bottom left|center|right
 * Default: center center
 * Context: location
 *
 * jpeg_filter_dropon_offset vertical horizontal
 * Default: 0 0
 * Context: location
 *
 * jpeg_filter_dropon image
 * jpeg_filter_dropon image mask
 * Default: -
 * Context: location
 *
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <libmodjpeg.h>

#define NGX_HTTP_IMAGE_NONE      0
#define NGX_HTTP_IMAGE_JPEG      1

#define NGX_HTTP_IMAGE_BUFFERED  0x08

/* Phases of the body filter */
#define NGX_HTTP_JPEG_FILTER_PHASE_START          0
#define NGX_HTTP_JPEG_FILTER_PHASE_READ           1
#define NGX_HTTP_JPEG_FILTER_PHASE_PROCESS        2
#define NGX_HTTP_JPEG_FILTER_PHASE_PASS           3
#define NGX_HTTP_JPEG_FILTER_PHASE_DONE           4

#define NGX_HTTP_JPEG_FILTER_UNMODIFIED           0
#define NGX_HTTP_JPEG_FILTER_MODIFIED             1

/* Types for the filter elements */
#define NGX_HTTP_JPEG_FILTER_TYPE_EFFECT1         1
#define NGX_HTTP_JPEG_FILTER_TYPE_EFFECT2         2
#define NGX_HTTP_JPEG_FILTER_TYPE_DROPON_ALIGN    3
#define NGX_HTTP_JPEG_FILTER_TYPE_DROPON_OFFSET   4
#define NGX_HTTP_JPEG_FILTER_TYPE_DROPON          5

#define NGX_HTTP_JPEG_FILTER_BUFFER_SIZE          2 * 1024 * 1024

/* Configuration of the elements in the processing chain */
typedef struct {
	ngx_uint_t	          type;     /* Type of filter element */
	ngx_http_complex_value_t  cv1;      /* First complex value. Depends on the type if it is used */
	ngx_http_complex_value_t  cv2;      /* Second complex value. Depends on the type if it is used */
	mj_dropon_t              *dropon;   /* libmodjpeg dropon type. Depends on the type if it is used */
} ngx_http_jpeg_filter_element_t;

typedef struct {
	ngx_uint_t	max_width;          /* Max. allowed image width */
	ngx_uint_t	max_height;         /* Max. allowed image height */

	ngx_flag_t	enable;             /* Whether the module is enabled */
	ngx_flag_t	optimize;           /* Whether to optimize the Huffman tables in the resulting JPEG */
	ngx_flag_t	progressive;        /* Whether the resulting JPEG should stored in progressive mode */
	ngx_flag_t      arithmetric;        /* Whether to use arithmetric coding in the resulting JPEG */
	ngx_flag_t 	graceful;           /* Whether the unmodified image should be sent if processing fails */

	ngx_array_t    *filter_elements;    /* Processing chain */

	size_t		buffer_size;        /* Max. allowed size of the body */
} ngx_http_jpeg_filter_conf_t;

typedef struct {
	u_char		*in_image;          /* Holds the original image */
	u_char		*in_last;           /* Pointer to the end of in_image */

	u_char		*out_image;         /* Holds the final processed image */
	u_char 		*out_last;          /* Pointer to the end of out_image */

	size_t		length;             /* Size of the original image */

	ngx_uint_t	width;              /* Width of the original image */
	ngx_uint_t	height;             /* Height of the original image */

	ngx_uint_t	phase;              /* The current phase the module is in */
	ngx_uint_t      skip;               /* Skip the processing of the body */
} ngx_http_jpeg_filter_ctx_t;

/* The filter functions */
static ngx_int_t ngx_http_jpeg_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_jpeg_body_filter(ngx_http_request_t *r, ngx_chain_t *in);

/* Helper for the filter functions */
static ngx_int_t ngx_http_jpeg_filter_send(ngx_http_request_t *r, ngx_uint_t image);
static ngx_uint_t ngx_http_jpeg_filter_test(ngx_http_request_t *r, ngx_chain_t *in);
static ngx_int_t ngx_http_jpeg_filter_read(ngx_http_request_t *r, ngx_chain_t *in);
static ngx_int_t ngx_http_jpeg_filter_process(ngx_http_request_t *r);
static void ngx_http_jpeg_filter_cleanup(void *data);

/* Handling the configuration directives for the effects and dropon */
static char *ngx_conf_jpeg_filter_effect(ngx_conf_t *cf, ngx_command_t *cmd, void *c);
static char *ngx_conf_jpeg_filter_dropon(ngx_conf_t *cf, ngx_command_t *cmd, void *c);

/* Configuration functions */
static void *ngx_http_jpeg_filter_create_conf(ngx_conf_t *cf);
static char *ngx_http_jpeg_filter_merge_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_http_jpeg_filter_init(ngx_conf_t *cf);
static void ngx_http_jpeg_filter_conf_cleanup(void *data);

/* Helper functions for complex values */
static ngx_int_t ngx_http_jpeg_filter_get_int_value(ngx_http_request_t *r, ngx_http_complex_value_t *cv, ngx_int_t defval);
static ngx_int_t ngx_http_jpeg_filter_get_string_value(ngx_http_request_t *r, ngx_http_complex_value_t *cv, ngx_str_t *val);

/* Configuration directives */
static ngx_command_t ngx_http_jpeg_filter_commands[] = {
	{ ngx_string("jpeg_filter"),
	  NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	  ngx_conf_set_flag_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_jpeg_filter_conf_t, enable),
	  NULL },

	{ ngx_string("jpeg_filter_max_width"),
	  NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
	  ngx_conf_set_num_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_jpeg_filter_conf_t, max_width),
	  NULL },

	{ ngx_string("jpeg_filter_max_height"),
	  NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
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

	{ ngx_string("jpeg_filter_arithmetric"),
	  NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	  ngx_conf_set_flag_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_jpeg_filter_conf_t, arithmetric),
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

	{ ngx_string("jpeg_filter_dropon_align"),
	  NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
	  ngx_conf_jpeg_filter_dropon,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  0,
	  NULL },

	{ ngx_string("jpeg_filter_dropon_offset"),
	  NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
	  ngx_conf_jpeg_filter_dropon,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  0,
	  NULL },

	{ ngx_string("jpeg_filter_dropon"),
	  NGX_HTTP_LOC_CONF|NGX_CONF_TAKE12,
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

	if(r->headers_out.status == NGX_HTTP_NOT_MODIFIED) {
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

	/*
	 * Check for the body length and if we support this. We need to buffer
	 * the whole body and we have an upper limit for how much memory we are
	 * willing to allocate.
	 */
	len = r->headers_out.content_length_n;

	if(len != -1 && len > (off_t)conf->buffer_size) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "jpeg_filter: too big response: %O", len);

		if(conf->graceful == 1) {
			ctx->skip = 1;
			return ngx_http_next_header_filter(r);
		}

		return NGX_HTTP_UNSUPPORTED_MEDIA_TYPE;
	}

	/*
	 * In our context for this request, set the length of the body. We need this later
	 * in the body filter to allocate the memory for the buffer that we're going to buffer.
	 */
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
	 * the length of the modified body or if we like the original body.
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

	if(ctx->skip == 1) {
		/* The header filter tells us to skip the processing of the body */
		return ngx_http_next_body_filter(r, in);
	}

	/*
	 * Because the body data it most probably split into several chains and this
	 * function will be called more than once, we have to keep track in what "phase" we're in
	 */
	switch(ctx->phase) {
	case NGX_HTTP_JPEG_FILTER_PHASE_START:
		/* This is the first time we see some data for our filter */
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter: phase START");

		/*
		 * Have a taste of the first bytes of data in order to find out
		 * if this actually something we should care about and can handle.
		 */
		if(ngx_http_jpeg_filter_test(r, in) == NGX_HTTP_IMAGE_NONE) {
			/* No image data. Send the header and pass on the data */
			ctx->phase = NGX_HTTP_JPEG_FILTER_PHASE_PASS;

			/* Proceed to the next header filter as well because
			 * we were holding it back so far.
			 */
			ngx_http_next_header_filter(r);
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
			return ngx_http_filter_finalize_request(r, &ngx_http_jpeg_filter_module, NGX_HTTP_INTERNAL_SERVER_ERROR);
		}

		/* Fall through (rc == NGX_OK) */

	case NGX_HTTP_JPEG_FILTER_PHASE_PROCESS:
		/* Now that we have all the bytes from the image, we can go on an process it */
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter: phase PROCESS");

		/* What ever comes after will be passed through */
		ctx->phase = NGX_HTTP_JPEG_FILTER_PHASE_PASS;

		rc = ngx_http_jpeg_filter_process(r);
		if(rc == NGX_ERROR) {
			/* There was a problem processing the image. Either send the original image or an error */

			if(conf->graceful == 1) {
				/* Send the original image */
				return ngx_http_jpeg_filter_send(r, NGX_HTTP_JPEG_FILTER_UNMODIFIED);
			}
			else {
				return ngx_http_filter_finalize_request(r, &ngx_http_jpeg_filter_module, NGX_HTTP_UNSUPPORTED_MEDIA_TYPE);
			}
		}

		/* Send the modified image */
		return ngx_http_jpeg_filter_send(r, NGX_HTTP_JPEG_FILTER_MODIFIED);

	case NGX_HTTP_JPEG_FILTER_PHASE_PASS:
		return ngx_http_next_body_filter(r, in);

	case NGX_HTTP_JPEG_FILTER_PHASE_DONE:
	default:
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "jpeg_filter: phase default (DONE)");

		rc = ngx_http_next_body_filter(r, NULL);

		/* NGX_ERROR resets any pending data */
		return (rc == NGX_OK) ? NGX_ERROR : rc;
	}

	return NGX_OK;
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

	/* No clue what is happening here. Copied from image filter module */
	if(r->headers_out.content_length) {
		r->headers_out.content_length->hash = 0;
	}

	r->headers_out.content_length = NULL;

	/*
	 * Now that we are done and we know the final size of the modified body
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

	/*
	 * Checking if we have enough data available such that we can
	 * decide if we can and should handle it.
	 */
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

	/* No clue that this actually does. Copied from image filter module */
	r->connection->buffered &= ~NGX_HTTP_IMAGE_BUFFERED;

	/* Get our context for this request */
	ctx = ngx_http_get_module_ctx(r, ngx_http_jpeg_filter_module);
	if(ctx->in_image == NULL) {
		/* No data available. Bail out */
		return NGX_ERROR;
	}

	/* Get out module configuration so we know what we actually have to do */
	conf = ngx_http_get_module_loc_conf(r, ngx_http_jpeg_filter_module);

	/* Read the image */
	mj_jpeg_t *m = mj_read_jpeg_from_buffer((char *)ctx->in_image, ctx->length);
	if(m == NULL) {
		return NGX_ERROR;
	}

	if(
		(conf->max_width != 0 && (ngx_uint_t)m->width > conf->max_width) ||
		(conf->max_height != 0 && (ngx_uint_t)m->height > conf->max_height)
	) {
		mj_destroy_jpeg(m);
		return NGX_ERROR;
	}

	ngx_http_jpeg_filter_element_t *felts = conf->filter_elements->elts;
	ngx_uint_t i;
	ngx_int_t n, align = 0, offset_x = 0, offset_y = 0;
	ngx_str_t val;

	/* Go through the processing chain */
	for(i = 0; i < conf->filter_elements->nelts; i++) {
		switch(felts[i].type) {
			case NGX_HTTP_JPEG_FILTER_TYPE_EFFECT1:
				ngx_http_jpeg_filter_get_string_value(r, &felts[i].cv1, &val);

				if(ngx_strcmp(val.data, "grayscale") == 0) {
					mj_effect_grayscale(m);
				}
				else if(ngx_strcmp(val.data, "pixelate") == 0) {
					mj_effect_pixelate(m);
				}
				else {
					ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "jpeg_filter: invalid effect \"%s\"", val.data);
				}

				break;
			case NGX_HTTP_JPEG_FILTER_TYPE_EFFECT2:
				ngx_http_jpeg_filter_get_string_value(r, &felts[i].cv1, &val);

				n = ngx_http_jpeg_filter_get_int_value(r, &felts[i].cv2, 0);
				if(n < 0) {
					n = 0;
				}

				if(ngx_strcmp(val.data, "brighten") == 0) {
					mj_effect_luminance(m, n);
				}
				else if(ngx_strcmp(val.data, "darken") == 0) {
					mj_effect_luminance(m, -n);
				}
				else if(ngx_strcmp(val.data, "tintblue") == 0) {
					mj_effect_tint(m, n, 0);
				}
				else if(ngx_strcmp(val.data, "tintyellow") == 0) {
					mj_effect_tint(m, -n, 0);
				}
				else if(ngx_strcmp(val.data, "tintred") == 0) {
					mj_effect_tint(m, 0, n);
				}
				else if(ngx_strcmp(val.data, "tintgreen") == 0) {
					mj_effect_tint(m, 0, -n);
				}
				else {
					ngx_log_error(NGX_LOG_WARN, r->connection->log, 0, "jpeg_filter: invalid effect \"%s\"", val.data);
				}

				break;
			case NGX_HTTP_JPEG_FILTER_TYPE_DROPON_ALIGN:
				align = 0;

				ngx_http_jpeg_filter_get_string_value(r, &felts[i].cv1, &val);

				if(ngx_strcmp(val.data, "top") == 0) {
					align |= MJ_ALIGN_TOP;
				}
				else if(ngx_strcmp(val.data, "bottom") == 0) {
					align |= MJ_ALIGN_BOTTOM;
				}
				else if(ngx_strcmp(val.data, "center") == 0) {
					align |= MJ_ALIGN_CENTER;
				}
				else {
					ngx_log_error(NGX_LOG_WARN, r->connection->log, 0, "jpeg_filter: invalid alignment \"%s\"", val.data);
				}

				ngx_http_jpeg_filter_get_string_value(r, &felts[i].cv2, &val);

				if(ngx_strcmp(val.data, "left") == 0) {
					align |= MJ_ALIGN_LEFT;
				}
				else if(ngx_strcmp(val.data, "right") == 0) {
					align |= MJ_ALIGN_RIGHT;
				}
				else if(ngx_strcmp(val.data, "center") == 0) {
					align |= MJ_ALIGN_CENTER;
				}
				else {
					ngx_log_error(NGX_LOG_WARN, r->connection->log, 0, "jpeg_filter: invalid alignment \"%s\"", val.data);
				}

				break;
			case NGX_HTTP_JPEG_FILTER_TYPE_DROPON_OFFSET:
				offset_y = ngx_http_jpeg_filter_get_int_value(r, &felts[i].cv1, offset_y);
				offset_x = ngx_http_jpeg_filter_get_int_value(r, &felts[i].cv2, offset_x);

				break;
			case NGX_HTTP_JPEG_FILTER_TYPE_DROPON:
				mj_compose(m, felts[i].dropon, align, offset_x, offset_y);

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

	if(conf->arithmetric) {
		options |= MJ_OPTION_ARITHMETRIC;
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

	/*
	 * Add a cleanup routine for the allocated buffer that holds
	 * the modified image. We can only destroy it safely after it has been send.
	 */
	cln = ngx_pool_cleanup_add(r->pool, 0);
	if(cln == NULL) {
		return NGX_ERROR;
	}

	cln->handler = ngx_http_jpeg_filter_cleanup;
	cln->data = ctx;

	return NGX_OK;
}

/* Interpret a complex value as an int */
static ngx_int_t ngx_http_jpeg_filter_get_int_value(ngx_http_request_t *r, ngx_http_complex_value_t *cv, ngx_int_t defval) {
	ngx_str_t val;
	ngx_int_t n;

	if(ngx_http_complex_value(r, cv, &val) != NGX_OK) {
		return defval;
	}

	n = ngx_atoi(val.data, val.len);

	return n;
}

/* Get the complex value as a string */
static ngx_int_t ngx_http_jpeg_filter_get_string_value(ngx_http_request_t *r, ngx_http_complex_value_t *cv, ngx_str_t *val) {
	return ngx_http_complex_value(r, cv, val);
}

/* Cleanup after the request finished */
static void ngx_http_jpeg_filter_cleanup(void *data) {
	ngx_http_jpeg_filter_ctx_t *ctx = data;

	if(ctx->out_image != NULL) {
		free(ctx->out_image);
	}

	return;
}

/* Process the "jpeg_filter_effect" configuration directives */
static char *ngx_conf_jpeg_filter_effect(ngx_conf_t *cf, ngx_command_t *cmd, void *c) {
	ngx_http_jpeg_filter_conf_t *conf = c;

	ngx_str_t                         *value;
	ngx_http_compile_complex_value_t   ccv;
	ngx_http_jpeg_filter_element_t    *fe;

	ngx_log_debug0(NGX_LOG_DEBUG_CORE, cf->log, 0, "jpeg_filter: ngx_conf_jpeg_filter_effect");

	value = cf->args->elts;

	/* Initialize the processing chain */
	if(conf->filter_elements == NULL) {
		conf->filter_elements = ngx_array_create(cf->pool, 10, sizeof(ngx_http_jpeg_filter_element_t));
		if(conf->filter_elements == NULL) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "jpeg_filter: failed to create filter chain");
			return NGX_CONF_ERROR;
		}
	}

	/* Add a new element to the processing chain */
	fe = (ngx_http_jpeg_filter_element_t *)ngx_array_push(conf->filter_elements);
	if(fe == NULL) {
		ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "jpeg_filter: failed to add new filter to filter chain for \"%s\"", value[0].data);
		return NGX_CONF_ERROR;
	}

	ngx_memzero(fe, sizeof(ngx_http_jpeg_filter_element_t));

	if(cf->args->nelts == 2) {
		fe->type = NGX_HTTP_JPEG_FILTER_TYPE_EFFECT1;

		/* Get the effect name as complex value */
		ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

		ccv.cf = cf;
		ccv.value = &value[1];
		ccv.complex_value = &fe->cv1;
		ccv.zero = 1;

		if(ngx_http_compile_complex_value(&ccv) != NGX_OK) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "jpeg_filter: failed to compile complex value \"%s %s\"", value[0].data, value[1].data);
			return NGX_CONF_ERROR;
		}
	}
	else if(cf->args->nelts == 3) {
		fe->type = NGX_HTTP_JPEG_FILTER_TYPE_EFFECT2;

		/* Get the effect name as complex value */
		ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

		ccv.cf = cf;
		ccv.value = &value[1];
		ccv.complex_value = &fe->cv1;
		ccv.zero = 1;

		if(ngx_http_compile_complex_value(&ccv) != NGX_OK) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "jpeg_filter: failed to compile complex value for \"%s %s %s\"", value[0].data, value[1].data, value[2].data);
			return NGX_CONF_ERROR;
		}

		/* Get the effect value as complex value */
		ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

		ccv.cf = cf;
		ccv.value = &value[2];
		ccv.complex_value = &fe->cv2;
		ccv.zero = 1;

		if(ngx_http_compile_complex_value(&ccv) != NGX_OK) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "jpeg_filter: failed to compile complex value for \"%s %s %s\"", value[0].data, value[1].data, value[2].data);
			return NGX_CONF_ERROR;
		}
	}

	return NGX_CONF_OK;
}

/* Process the "jpeg_filter_dropon*" configuration directives */
static char *ngx_conf_jpeg_filter_dropon(ngx_conf_t *cf, ngx_command_t *cmd, void *c) {
	ngx_http_jpeg_filter_conf_t *conf = c;

	ngx_str_t                         *value;
	ngx_http_compile_complex_value_t   ccv;
	ngx_http_jpeg_filter_element_t    *fe;

	ngx_log_debug0(NGX_LOG_DEBUG_CORE, cf->log, 0, "jpeg_filter: ngx_conf_jpeg_filter_dropon");

	value = cf->args->elts;

	/* Initialize the processing chain */
	if(conf->filter_elements == NULL) {
		conf->filter_elements = ngx_array_create(cf->pool, 10, sizeof(ngx_http_jpeg_filter_element_t));
		if(conf->filter_elements == NULL) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "jpeg_filter: failed to create filter chain");
			return NGX_CONF_ERROR;
		}
	}

	/* Add a new element to the processing chain */
	fe = (ngx_http_jpeg_filter_element_t *)ngx_array_push(conf->filter_elements);
	if(fe == NULL) {
		ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "jpeg_filter: failed to add new filter to filter chain (%s)", value[0].data);
		return NGX_CONF_ERROR;
	}

	ngx_memzero(fe, sizeof(ngx_http_jpeg_filter_element_t));

	if(ngx_strcmp(value[0].data, "jpeg_filter_dropon_align") == 0) {
		fe->type = NGX_HTTP_JPEG_FILTER_TYPE_DROPON_ALIGN;

		/* Vertical alignment (top, bottom, center) */
		ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

		ccv.cf = cf;
		ccv.value = &value[1];
		ccv.complex_value = &fe->cv1;
		ccv.zero = 1;

		if(ngx_http_compile_complex_value(&ccv) != NGX_OK) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "jpeg_filter: failed to compile complex value for \"%s %s %s\"", value[0].data, value[1].data, value[2].data);
			return NGX_CONF_ERROR;
		}

		/* Horizontal alignment (left, right, center) */
		ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

		ccv.cf = cf;
		ccv.value = &value[2];
		ccv.complex_value = &fe->cv2;
		ccv.zero = 1;

		if(ngx_http_compile_complex_value(&ccv) != NGX_OK) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "jpeg_filter: failed to compile complex value for \"%s %s %s\"", value[0].data, value[1].data, value[2].data);
			return NGX_CONF_ERROR;
		}
	}
	else if(ngx_strcmp(value[0].data, "jpeg_filter_dropon_offset") == 0) {
		fe->type = NGX_HTTP_JPEG_FILTER_TYPE_DROPON_OFFSET;

		/* Vertical offset */
		ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

		ccv.cf = cf;
		ccv.value = &value[1];
		ccv.complex_value = &fe->cv1;
		ccv.zero = 1;

		if(ngx_http_compile_complex_value(&ccv) != NGX_OK) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "jpeg_filter: failed to compile complex value for \"%s %s %s\"", value[0].data, value[1].data, value[2].data);
			return NGX_CONF_ERROR;
		}

		/* Horizontal offset */
		ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

		ccv.cf = cf;
		ccv.value = &value[2];
		ccv.complex_value = &fe->cv2;
		ccv.zero = 1;

		if(ngx_http_compile_complex_value(&ccv) != NGX_OK) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "jpeg_filter: failed to compile complex value for \"%s %s %s\"", value[0].data, value[1].data, value[2].data);
			return NGX_CONF_ERROR;
		}
	}
	else if(ngx_strcmp(value[0].data, "jpeg_filter_dropon") == 0) {
		fe->type = NGX_HTTP_JPEG_FILTER_TYPE_DROPON;

		if(cf->args->nelts == 2) {
			/* Dropon without a mask */
			fe->dropon = mj_read_dropon_from_jpeg((char *)value[1].data, NULL, MJ_BLEND_FULL);
			if(fe->dropon == NULL) {
				ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "jpeg_filter_dropon could not load the file \"%s\"", value[1].data);
				return NGX_CONF_ERROR;
			}
		}
		else if(cf->args->nelts == 3) {
			/* Dropon with a mask */
			fe->dropon = mj_read_dropon_from_jpeg((char *)value[1].data, (char *)value[2].data, MJ_BLEND_FULL);
			if(fe->dropon == NULL) {
				ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "jpeg_filter_dropon could not load the file \"%s\" or \"%s\"", value[1].data, value[2].data);
				return NGX_CONF_ERROR;
			}
		}

		/* Add a cleanup routine for the allocated dropon */
		ngx_pool_cleanup_t *cln;

		cln = ngx_pool_cleanup_add(cf->pool, 0);
		if(cln == NULL) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "failed to add cleanup routine for dropon");
			return NGX_CONF_ERROR;
		}

		cln->handler = ngx_http_jpeg_filter_conf_cleanup;
		cln->data = fe->dropon;
	}

	return NGX_CONF_OK;
}

/* Cleanup stuff was allocated without a pool during configuration */
static void ngx_http_jpeg_filter_conf_cleanup(void *data) {
	mj_dropon_t *d = (mj_dropon_t *)data;

	mj_destroy_dropon(d);

	return;
}

static void *ngx_http_jpeg_filter_create_conf(ngx_conf_t *cf) {
	ngx_http_jpeg_filter_conf_t  *conf;

	conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_jpeg_filter_conf_t));
	if(conf == NULL) {
		ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "jpeg_filter: failed to allocate memory for filter config");
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

	ngx_conf_merge_size_value(conf->buffer_size, prev->buffer_size, NGX_HTTP_JPEG_FILTER_BUFFER_SIZE);

	return NGX_CONF_OK;
}

static ngx_int_t ngx_http_jpeg_filter_init(ngx_conf_t *cf) {
	ngx_http_next_header_filter = ngx_http_top_header_filter;
	ngx_http_top_header_filter = ngx_http_jpeg_header_filter;

	ngx_http_next_body_filter = ngx_http_top_body_filter;
	ngx_http_top_body_filter = ngx_http_jpeg_body_filter;

	return NGX_OK;
}
