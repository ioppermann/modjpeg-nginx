FROM alpine:latest as nginx

ENV \
	NGINX_VERSION=1.15.1 \
	LIBMODJPEG_VERSION=1.0.2

ADD config /tmp/modjpeg-nginx/config
ADD ngx_http_jpeg_filter_module.c /tmp/modjpeg-nginx/ngx_http_jpeg_filter_module.c

RUN \
	export buildDeps=" \
        binutils \
        curl \
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
	export MAKEFLAGS="-j$(($(grep -c ^processor /proc/cpuinfo) + 1))" && \
	apk add --update ${buildDeps}

RUN \
	DIR=$(mktemp -d) && cd ${DIR} && \
	curl -LOks "https://github.com/ioppermann/libmodjpeg/archive/v${LIBMODJPEG_VERSION}.tar.gz" && \
	tar --strip-components 1 -xzvf "v${LIBMODJPEG_VERSION}.tar.gz" && \
	cmake . && \
	make install && \
	rm -rf ${DIR}
RUN \
	DIR=$(mktemp -d) && cd ${DIR} && \
	curl -LOks "http://nginx.org/download/nginx-${NGINX_VERSION}.tar.gz" && \
	tar -xzvf "nginx-${NGINX_VERSION}.tar.gz" && \
	cd nginx-${NGINX_VERSION} && \
	./configure --prefix=/usr/local/nginx --add-module=/tmp/modjpeg-nginx && \
	make && \
	make install && \
	rm -rf ${DIR}

FROM alpine:latest

COPY --from=nginx /usr/local/nginx /usr/local/nginx
COPY --from=nginx /usr/local/lib /usr/local/lib
COPY --from=nginx /usr/lib /usr/lib

ADD contrib/modjpeg.conf /usr/local/nginx/conf/modjpeg.conf
ADD contrib/dropon.png /usr/local/nginx/conf/dropon.png

CMD ["/usr/local/nginx/sbin/nginx", "-p", "/usr/local/nginx/", "-c", "conf/modjpeg.conf"]