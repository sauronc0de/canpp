FROM ubuntu:22.04
RUN apt-get update -y
RUN apt-get upgrade -y
RUN apt-get install -y curl git

###############################################################################
# Install C++ development environment 
###############################################################################
# C++ environment and SDL2
RUN apt-get install -y \
    gcc \
    g++ \
    gcc-multilib \
    g++-multilib \
    build-essential \
    xutils-dev \
    libsdl2-dev \
    libsdl2-gfx-dev \
    libsdl2-image-dev \
    libsdl2-mixer-dev \
    libsdl2-net-dev \
    libsdl2-ttf-dev \
    libreadline6-dev \
    libncurses5-dev \
    mingw-w64 \
    cmake

# Add Ninja
RUN apt-get install -y ninja-build

# Add Lua
RUN apt-get install -y lua5.4 liblua5.4-dev

# Add clang compilers 
RUN apt-get install -y clang gdb

# Utilities
RUN apt-get install -y clang-format clangd lldb clang-tidy

# C++ style checker dependencies
RUN apt install -y libyaml-cpp0.7

# Install glew to work with OpenGL with VAOs, Shaders, ...
RUN apt-get install -y libglew-dev

# Plant to UML requirements
RUN apt-get install -y openjdk-17-jdk graphviz

# Install boost libraries
RUN apt-get install -y libboost-all-dev

# Install lld (Something to build link faster)
RUN apt-get install -y lld

###############################################################################
# Cache configuration for faster builds
###############################################################################

# syntax=docker/dockerfile:1.6
RUN apt-get update && apt-get install -y ccache
RUN update-ccache-symlinks && ccache --set-config=cache_dir=/ccache --max-size=8G

###############################################################################
# User and permissions configuration
###############################################################################

# Install sudo and create a user
RUN apt-get install -y sudo
# Create a non-root user 'sauroncode' with password '1234', grant sudo access without password
RUN useradd -m sauroncode && echo "sauroncode:1234" | chpasswd && adduser sauroncode sudo
RUN echo "%sudo ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

# Add user 'sauroncode' to plugdev and dialout groups for USB access
RUN usermod -aG plugdev sauroncode && usermod -aG dialout sauroncode

# Switch to 'sauroncode' user
USER sauroncode

###############################################################################
# Environment variables and PATH configuration
###############################################################################

# Set the working directory in the container
ARG WORKSPACE_DIR
ENV WORKSPACE_DIR=${WORKSPACE_DIR}

ARG PROJECT_NAME
ENV PROJECT_NAME=${PROJECT_NAME}
WORKDIR ${WORKSPACE_DIR}

# Add directories on PATH variable
ENV PATH="$PATH:$WORKSPACE_DIR/tools/tasks"
ENV PATH="$PATH:$WORKSPACE_DIR/tools/programs"

ENV XDG_RUNTIME_DIR=/tmp

###############################################################################
# Install AI support with OpenCode and AI Gentle Stack
###############################################################################

# Need root for apt installs and global binary installation
USER root

# Need root for apt installs and global binary installation
USER root

# Install extra dependencies required by AI Gentle Stack
RUN apt-get update && apt-get install -y \
    curl \
    nodejs \
    npm \
    ca-certificates \
    tar \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Install Go 1.24.x manually because Ubuntu 22.04 ships an older version
ARG GO_VERSION=1.24.2
RUN set -eux; \
    cd /tmp; \
    curl -fsSLO "https://go.dev/dl/go${GO_VERSION}.linux-amd64.tar.gz"; \
    rm -rf /usr/local/go; \
    tar -C /usr/local -xzf "go${GO_VERSION}.linux-amd64.tar.gz"; \
    rm -f "go${GO_VERSION}.linux-amd64.tar.gz"

ENV PATH="/usr/local/go/bin:${PATH}"

# Install AI Gentle Stack from the GitHub release tarball
ARG GENTLE_AI_VERSION=1.7.22
RUN set -eux; \
    cd /tmp; \
    curl -fsSLO "https://github.com/Gentleman-Programming/gentle-ai/releases/download/v${GENTLE_AI_VERSION}/gentle-ai_${GENTLE_AI_VERSION}_linux_amd64.tar.gz"; \
    tar -xzf "gentle-ai_${GENTLE_AI_VERSION}_linux_amd64.tar.gz"; \
    install -m 0755 gentle-ai /usr/local/bin/gentle-ai; \
    rm -f "gentle-ai_${GENTLE_AI_VERSION}_linux_amd64.tar.gz" gentle-ai

# Switch to container user so AI tooling installs in the correct user paths
USER sauroncode
ENV HOME=/home/sauroncode

# Ensure user-local binaries are on PATH
ENV PATH="/home/sauroncode/.local/bin:/home/sauroncode/go/bin:${PATH}"

# Install OpenCode 
RUN curl -fsSL https://opencode.ai/install | bash

# Execute AI Gentle Stack install command
RUN gentle-ai install \
    --preset full-gentleman \
    --agents opencode \
    --persona neutral