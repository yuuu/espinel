# ESP-IDF v5.5 (with qemu-xtensa) plus a host build of the Spinel compiler.
# The Spinel tree comes in as the `spinel` build context:
#   docker build --build-context spinel=../spinel -f docker/Dockerfile .
FROM espressif/idf:v5.5.4

# Spinel's host build needs a C compiler, make and curl (all in the image),
# plus the prism gem for the Ruby generator of
# lib/spinel_rt_responds_generated.h; the image's Ruby 3.2 lacks it.
RUN apt-get update && apt-get install -y --no-install-recommends ruby-dev \
 && rm -rf /var/lib/apt/lists/* && gem install --no-document prism -v 1.9.0

# The host checkout's build outputs are left out and rebuilt here; `make deps`
# fetches the vendored parsers if the checkout has not.
COPY --from=spinel . /opt/spinel
RUN cd /opt/spinel && rm -rf build bin spinel lib/*.a packages/*/build \
 && make -C /opt/spinel deps \
 && make -C /opt/spinel -j"$(nproc)" bin/spinel lib/spinel_rt_responds_generated.h \
 && /opt/spinel/bin/spinel --version

ENV SPINEL_DIR=/opt/spinel \
    PATH=/opt/spinel/bin:$PATH
WORKDIR /project
