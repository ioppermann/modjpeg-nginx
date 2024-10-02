#!/bin/sh

# Script for building Docker images

function build_native() {
  docker build \
    -f Dockerfile \
    -t ioppermann/modjpeg-nginx:latest .
}

function build_multiplatform() {
  docker buildx build \
    --push \
    --platform linux/amd64,linux/arm64 \
    -f Dockerfile \
    -t ioppermann/modjpeg-nginx:latest .
}

main() {
  if [[ $# == 0 ]]; then
    echo "Options available: native, multi"
    exit 0
  else
    if [[ $1 == "native" ]]; then
      build_native
    elif [[ $1 == "multi" ]]; then
      build_multiplatform
    fi
  fi
}

main $@

exit 0