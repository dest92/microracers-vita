# Imagen tier 1 (qemu-arm) de hotwheels-host: cross ARMv7 hard-float + qemu-user.
# El host no tiene sudo; todo el toolchain ARM Linux vive acá.
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      g++-arm-linux-gnueabihf gcc-arm-linux-gnueabihf qemu-user make python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
