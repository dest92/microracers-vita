# Runtime para el release "continuous" de Vita3K (Vita3K/Vita3K, Linux x86_64).
#
# Por que un contenedor:
#   El binario oficial lo construye CI en Ubuntu 26.04 y exige glibc 2.43
#   (y Qt 6.11 en el zip `ubuntu-latest`). El host (Zorin 18.1 / Ubuntu 24.04)
#   tiene glibc 2.39 y Qt 6.4, asi que el binario NO corre nativo.
#   Esta imagen replica el runner de CI (ubuntu:26.04) y aporta el resto de
#   librerias base; la AppImage ya trae Qt/FFmpeg adentro.
#
# Uso: se construye desde hotwheels/tools/vita3k/vita3k.sh (subcomando setup).
FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl unzip \
    libgl1 libegl1 libglx0 libopengl0 libglu1-mesa \
    libice6 libsm6 libx11-6 libx11-xcb1 libxext6 \
    libfontconfig1 libfreetype6 libwayland-client0 libxcb1 \
    libxkbcommon0 libxkbcommon-x11-0 libdbus-1-3 \
    libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 \
    libxcb-shape0 libxcb-shm0 libxcb-randr0 libxcb-xfixes0 libxcb-xinerama0 \
    libxcb-sync1 libxcb-xkb1 libxcb-cursor0 libxcb-xinput0 libxcb-present0 \
    libxcb-glx0 libxcb-dri3-0 libxcb-dri2-0 \
    libxi6 libxtst6 libxrender1 libxrandr2 libxfixes3 libxcursor1 \
    libvulkan1 mesa-vulkan-drivers \
    libasound2t64 libpulse0 \
    && rm -rf /var/lib/apt/lists/*
