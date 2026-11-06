# Cross-toolchain build environment for the RG-808 PortMaster port.
# Builds armhf + aarch64 binaries linked against the system SDL2 on the device.
FROM debian:bookworm

RUN dpkg --add-architecture armhf && dpkg --add-architecture arm64 && \
    apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates zip file \
      crossbuild-essential-armhf crossbuild-essential-arm64 \
      libsdl2-dev:armhf libsdl2-dev:arm64 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY build.sh /build.sh
RUN chmod +x /build.sh
CMD ["/build.sh"]
