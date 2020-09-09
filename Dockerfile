FROM alpine:latest as nginx

ENV \
	NGINX_VERSION=1.19.2 \
	LIBMODJPEG_VERSION=1.0.2

RUN \
	export buildDeps=" \
        binutils \
        wget \
        coreutils \
        gcc \
        g++ \
        make \
        cmake \
        pcre-dev \
        zlib-dev \
        jpeg-dev \
        libpng-dev \
        " && \
	apk add --update ${buildDeps}

RUN \
	mkdir /dist && cd /dist && \
	wget "https://github.com/ioppermann/libmodjpeg/archive/v${LIBMODJPEG_VERSION}.tar.gz" && \
	tar -xzvf "v${LIBMODJPEG_VERSION}.tar.gz" && \
	cd libmodjpeg-${LIBMODJPEG_VERSION} && \
	cmake . && \
	make -j$(nproc) && \
	make install && \
	rm -rf ${DIR}

ADD config /dist/modjpeg-nginx/config
ADD ngx_http_jpeg_filter_module.c /dist/modjpeg-nginx/ngx_http_jpeg_filter_module.c

RUN \
	cd /dist && \
	wget "http://nginx.org/download/nginx-${NGINX_VERSION}.tar.gz" && \
	tar -xzvf "nginx-${NGINX_VERSION}.tar.gz" && \
	cd nginx-${NGINX_VERSION} && \
	./configure --prefix=/usr/local/nginx --add-module=/dist/modjpeg-nginx && \
	make -j$(nproc) && \
	make install && \
	rm -rf ${DIR}

FROM alpine:latest

COPY --from=nginx /usr/local/nginx /usr/local/nginx
COPY --from=nginx /usr/local/lib /usr/local/lib
COPY --from=nginx /usr/lib /usr/lib

ADD contrib/modjpeg.conf.in /usr/local/nginx/conf/modjpeg.conf.in
ADD contrib/dropon.png /usr/local/nginx/conf/dropon.png
ADD contrib/run.sh /usr/local/nginx/bin/run.sh

EXPOSE 80/tcp
VOLUME ["/images"]

CMD ["/usr/local/nginx/bin/run.sh"]
