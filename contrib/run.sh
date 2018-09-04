#!/bin/sh

if [ "$MJ_GRACEFUL" != "on" -a "$MJ_GRACEFUL" != "off" ]; then
	MJ_GRACEFUL=on
fi

if [ "$MJ_BUFFER" = "" ]; then
	MJ_BUFFER=10M
fi

if [ "$MJ_MAX_PIXEL" = "" ]; then
	MJ_MAX_PIXEL=0
fi

if [ "$MJ_DROPON_ALIGN" = "" ]; then
	MJ_DROPON_ALIGN="top left"
fi

if [ "$MJ_DROPON_OFFSET" = "" ]; then
	MJ_DROPON_OFFSET="0 0"
fi

if [ "$MJ_DROPON_FILE" = "" ]; then
	MJ_DROPON_FILE="/usr/local/nginx/conf/dropon.png"
fi

cp /usr/local/nginx/conf/modjpeg.conf.in /usr/local/nginx/conf/modjpeg.conf

sed -i"" "s@%MJ_GRACEFUL%@$MJ_GRACEFUL@g" /usr/local/nginx/conf/modjpeg.conf
sed -i"" "s@%MJ_BUFFER%@$MJ_BUFFER@g" /usr/local/nginx/conf/modjpeg.conf
sed -i"" "s@%MJ_MAX_PIXEL%@$MJ_MAX_PIXEL@g" /usr/local/nginx/conf/modjpeg.conf
sed -i"" "s@%MJ_DROPON_ALIGN%@$MJ_DROPON_ALIGN@g" /usr/local/nginx/conf/modjpeg.conf
sed -i"" "s@%MJ_DROPON_OFFSET%@$MJ_DROPON_OFFSET@g" /usr/local/nginx/conf/modjpeg.conf
sed -i"" "s@%MJ_DROPON_FILE%@$MJ_DROPON_FILE@g" /usr/local/nginx/conf/modjpeg.conf

cat /usr/local/nginx/conf/modjpeg.conf

exec /usr/local/nginx/sbin/nginx -p /usr/local/nginx -c conf/modjpeg.conf
