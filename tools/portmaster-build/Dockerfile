# Cross-toolchain build environment for the PortMaster ports (RG-SMP / RG-808).
# Builds armhf + aarch64 binaries linked against the system SDL2 on the device.
#
#   docker build -t rg808build tools/portmaster-build
#   docker run --rm -v ${PWD}:/src rg808build              # -> dist/rgsmp.zip
#   docker run --rm -e MACHINE=808 -v ${PWD}:/src rg808build  # -> dist/rg808.zip
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
ENV MACHINE=smp
# Prefer the script from the mounted repo so build.sh edits do not need an
# image rebuild; the baked copy is a fallback for mounts without it.
CMD ["bash", "-c", "if [ -f /src/tools/portmaster-build/build.sh ]; then exec bash /src/tools/portmaster-build/build.sh; else exec bash /build.sh; fi"]
