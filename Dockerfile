# Use a standard Ubuntu base image suitable for C++ builds
FROM ubuntu:22.04

# Avoid prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive
ENV VCPKG_ROOT=/app/vcpkg
ENV VCPKG_DEFAULT_BINARY_CACHE=/app/vcpkg_installed
ENV CMAKE_BUILD_PARALLEL_LEVEL=2
ENV VCPKG_FORCE_SYSTEM_BINARIES=1
ENV REDIS_HOST=localhost
ENV REDIS_PORT=6379

# Install necessary build tools and libraries
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libgtest-dev \
    libgmock-dev \
    libssl-dev \
    git \
    ninja-build \
    python3 \
    curl \
    wget \
    zip \
    unzip \
    tar \
    ca-certificates \
    pkg-config \
    libhiredis-dev \
    redis-server \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

# Create a non-root user first
RUN useradd -m -s /bin/bash builder && \
    mkdir -p /app && \
    chown -R builder:builder /app

# Configure Redis
RUN sed -i 's/bind 127.0.0.1/bind 0.0.0.0/' /etc/redis/redis.conf && \
    sed -i 's/protected-mode yes/protected-mode no/' /etc/redis/redis.conf

# Configure Redis for non-root usage after user creation
RUN chown -R builder:builder /etc/redis /var/log/redis /var/lib/redis && \
    chmod 755 /etc/redis /var/log/redis /var/lib/redis

# Set working directory and switch to non-root user
WORKDIR /app
USER builder

# Copy the project files
COPY --chown=builder:builder . /app/

# Copy the config file
COPY backendify.config /app/backendify.config

# Switch to root for vcpkg installation
USER root

# Create vcpkg directories and set permissions
RUN mkdir -p ${VCPKG_DEFAULT_BINARY_CACHE} && \
    mkdir -p ${VCPKG_ROOT} && \
    rm -rf ${VCPKG_ROOT}/* ${VCPKG_ROOT}/.* 2>/dev/null || true && \
    chown -R builder:builder ${VCPKG_ROOT} ${VCPKG_DEFAULT_BINARY_CACHE} && \
    chmod -R 755 ${VCPKG_ROOT} ${VCPKG_DEFAULT_BINARY_CACHE}

# Clone vcpkg and bootstrap it as builder
USER builder
RUN cd ${VCPKG_ROOT} && \
    git clone https://github.com/microsoft/vcpkg.git . && \
    ./bootstrap-vcpkg.sh --disableMetrics && \
    ls -la && \
    ./vcpkg version

# Switch to root to create symlink
USER root
RUN ls -la ${VCPKG_ROOT} && \
    test -f "${VCPKG_ROOT}/vcpkg" && \
    ln -sf ${VCPKG_ROOT}/vcpkg /usr/local/bin/vcpkg && \
    chmod 755 /usr/local/bin/vcpkg

# Switch back to builder user and verify vcpkg installation
USER builder
RUN command -v vcpkg && \
    vcpkg version

# Install dependencies using vcpkg (manifest mode)
USER builder
RUN cd /app && \
    vcpkg install \
        --triplet x64-linux \
        --clean-after-build \
    && vcpkg integrate install

# Install sudo and configure builder user
USER root
RUN apt-get update && \
    apt-get install -y sudo && \
    rm -rf /var/lib/apt/lists/* && \
    echo "builder ALL=(ALL) NOPASSWD: /usr/bin/service" >> /etc/sudoers

# Create a better entrypoint script
RUN echo '#!/bin/bash\n\
sudo service redis-server start\n\
exec /app/build/backendify "$@"' > /app/entrypoint.sh && \
    chmod +x /app/entrypoint.sh && \
    chown builder:builder /app/entrypoint.sh

# Copy entrypoint script
COPY --chown=builder:builder entrypoint.sh /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

# Switch back to builder for runtime
USER builder

# Build the project with explicit compiler and build tool paths
RUN mkdir -p build \
    && cd build \
    && rm -rf CMakeCache.txt CMakeFiles \
    && cmake .. \
        -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DVCPKG_TARGET_TRIPLET=x64-linux \
        -DCMAKE_MAKE_PROGRAM=/usr/bin/ninja \
        -DCMAKE_C_COMPILER=/usr/bin/gcc \
        -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
        -G Ninja \
    && cmake --build . --config Release

# Expose ports
EXPOSE 8080

# Set the entrypoint and default command
ENTRYPOINT ["/app/entrypoint.sh"]
CMD ["./backendify"]