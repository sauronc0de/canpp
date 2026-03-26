FROM ubuntu:24.04

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

###############################################################################
# Build arguments
###############################################################################
ARG WORKSPACE_DIR=/workspaces/project
ARG PROJECT_NAME=project
ARG GO_VERSION=1.24.2
ARG GENTLE_AI_VERSION=1.7.22
ARG USERNAME=sauroncode
ARG USER_UID=1000
ARG USER_GID=1000

###############################################################################
# Base system and development environment
###############################################################################
ENV DEBIAN_FRONTEND=noninteractive

RUN set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
    ca-certificates \
    sudo \
    curl \
    git \
    tar \
    xutils-dev \
    build-essential \
    gcc \
    g++ \
    gcc-multilib \
    g++-multilib \
    cmake \
    ninja-build \
    mingw-w64 \
    gdb \
    clang \
    clang-format \
    clang-tidy \
    clangd \
    clang-tools \
    lldb \
    lld \
    llvm-dev \
    libclang-dev \
    libclang-cpp-dev \
    libffi-dev \
    libtinfo-dev \
    zlib1g-dev \
    libxml2-dev \
    libyaml-cpp-dev \
    libboost-all-dev \
    libglew-dev \
    libsdl2-dev \
    libsdl2-gfx-dev \
    libsdl2-image-dev \
    libsdl2-mixer-dev \
    libsdl2-net-dev \
    libsdl2-ttf-dev \
    lua5.4 \
    liblua5.4-dev \
    libreadline-dev \
    libncurses-dev \
    openjdk-17-jdk \
    graphviz \
    nodejs \
    npm; \
    rm -rf /var/lib/apt/lists/*

###############################################################################
# User and permissions configuration
###############################################################################
RUN set -eux; \
    if ! id -u "${USERNAME}" >/dev/null 2>&1; then \
    if getent passwd "${USER_UID}" >/dev/null; then \
    EXISTING_USER_BY_UID="$(getent passwd "${USER_UID}" | cut -d: -f1)"; \
    usermod -l "${USERNAME}" "${EXISTING_USER_BY_UID}"; \
    usermod -d "/home/${USERNAME}" -m "${USERNAME}"; \
    else \
    if getent group "${USER_GID}" >/dev/null; then \
    EXISTING_GROUP="$(getent group "${USER_GID}" | cut -d: -f1)"; \
    else \
    groupadd --gid "${USER_GID}" "${USERNAME}"; \
    EXISTING_GROUP="${USERNAME}"; \
    fi; \
    useradd --uid "${USER_UID}" --gid "${EXISTING_GROUP}" -m "${USERNAME}"; \
    fi; \
    fi; \
    echo "${USERNAME}:1234" | chpasswd; \
    usermod -aG sudo,plugdev,dialout "${USERNAME}"; \
    grep -q '^%sudo ALL=(ALL) NOPASSWD: ALL$' /etc/sudoers || \
    echo '%sudo ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers
###############################################################################
# Environment variables and workspace
###############################################################################
ENV WORKSPACE_DIR=${WORKSPACE_DIR}
ENV PROJECT_NAME=${PROJECT_NAME}
ENV XDG_RUNTIME_DIR=/tmp
ENV PATH="${PATH}:${WORKSPACE_DIR}/tools/tasks:${WORKSPACE_DIR}/tools/programs"

WORKDIR ${WORKSPACE_DIR}

###############################################################################
# Go installation
###############################################################################
RUN set -eux; \
    cd /tmp; \
    curl -fsSLO "https://go.dev/dl/go${GO_VERSION}.linux-amd64.tar.gz"; \
    rm -rf /usr/local/go; \
    tar -C /usr/local -xzf "go${GO_VERSION}.linux-amd64.tar.gz"; \
    rm -f "go${GO_VERSION}.linux-amd64.tar.gz"

ENV PATH="/usr/local/go/bin:${PATH}"

###############################################################################
# AI support - Gentle AI
###############################################################################
RUN set -eux; \
    cd /tmp; \
    curl -fsSLO "https://github.com/Gentleman-Programming/gentle-ai/releases/download/v${GENTLE_AI_VERSION}/gentle-ai_${GENTLE_AI_VERSION}_linux_amd64.tar.gz"; \
    tar -xzf "gentle-ai_${GENTLE_AI_VERSION}_linux_amd64.tar.gz"; \
    install -m 0755 gentle-ai /usr/local/bin/gentle-ai; \
    rm -f "gentle-ai_${GENTLE_AI_VERSION}_linux_amd64.tar.gz" gentle-ai

###############################################################################
# User-level AI tooling
###############################################################################
USER ${USERNAME}
ENV HOME=/home/${USERNAME}
ENV PATH="${HOME}/.local/bin:${HOME}/go/bin:/usr/local/go/bin:${PATH}"

RUN curl -fsSL https://opencode.ai/install | bash

RUN gentle-ai install \
    --preset full-gentleman \
    --agents opencode \
    --persona neutral