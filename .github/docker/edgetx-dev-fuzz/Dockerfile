FROM ghcr.io/edgetx/edgetx-dev:latest

RUN apt-get update \
  && apt-get install -y --no-install-recommends clang \
  && rm -rf /var/lib/apt/lists/*
