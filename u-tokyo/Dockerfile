FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update
RUN apt-get install -y meson build-essential cmake libnuma-dev
RUN ldconfig -v
RUN mkdir -p /opt/tmp

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get upgrade --yes && \
    DEBIAN_FRONTEND=noninteractive apt-get install --yes \
    wget \
    ninja-build \
    #unzip is needed for protobuf
    unzip \
    moreutils \
    vim-tiny \
    nano \
    # To build libnvipc
    cmake libpcap-dev libcunit1-dev libnuma-dev pkg-config \
    emacs-nox iputils-ping iproute2

RUN apt install -y zlib1g-dev
RUN apt install -y libnats-dev
RUN apt install -y libsctp-dev
RUN apt install -y gdb
