# Dockerfile for Management Panel Server
# 
# SETUP: Before building, run once on host:
#   git clone --depth 1 --recurse-submodules https://github.com/paullouisageneau/libdatachannel.git deps/libdatachannel
#
# Then build with:
#   docker build -t management_panel .

# ---------- Build stage ----------
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install build tools & dependencies
RUN apt-get update && apt-get install -y \
    build-essential cmake ninja-build \
    libenet-dev libsqlite3-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy libdatachannel source (pre-cloned on host to deps/libdatachannel)
COPY deps/libdatachannel /opt/libdatachannel

# Build and install libdatachannel
RUN cmake -S /opt/libdatachannel -B /opt/libdatachannel/build \
        -DCMAKE_BUILD_TYPE=Release \
        -DNO_EXAMPLES=ON \
        -DNO_TESTS=ON \
        -DUSE_GNUTLS=OFF \
        -DUSE_MBEDTLS=OFF \
        -DUSE_LZ4=OFF && \
    cmake --build /opt/libdatachannel/build --parallel $(nproc) && \
    cmake --install /opt/libdatachannel/build && \
    rm -rf /opt/libdatachannel

# Copy source code
COPY . /src
WORKDIR /src

# Build server executable (Linux path: uses find_path/find_library for datachannel)
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --target server

# ---------- Runtime stage ----------
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libenet7 libsqlite3-0 libssl3 \
    && rm -rf /var/lib/apt/lists/*

# Copy built shared libraries (libdatachannel.so etc.) from builder
COPY --from=builder /usr/local/lib/ /usr/local/lib/
RUN ldconfig

# Copy server binary from builder
COPY --from=builder /src/build/server /usr/local/bin/management_server

# Expose ports
EXPOSE 7777/udp
# ENet (text chat)
EXPOSE 8080/tcp
# HTTP signaling (WebRTC)

# Entry point
CMD ["/usr/local/bin/management_server"]
