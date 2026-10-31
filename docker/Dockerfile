# ESP-IDF v5.5 (with qemu-xtensa) plus a host build of the Spinel compiler.
# The Spinel tree comes in as the `spinel` build context:
#   docker build --build-context spinel=../spinel -f docker/Dockerfile .
FROM espressif/idf:v5.5.4

# Spinel's host build needs only a C compiler, make and curl (all in the
# image); the host checkout's build outputs are left out and rebuilt here.
COPY --from=spinel . /opt/spinel
RUN cd /opt/spinel && rm -rf build bin spinel lib/*.a packages/*/build \
 && make -C /opt/spinel -j"$(nproc)" bin/spinel && /opt/spinel/bin/spinel --version

ENV SPINEL_DIR=/opt/spinel \
    PATH=/opt/spinel/bin:$PATH
WORKDIR /project
