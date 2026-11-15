FROM ubuntu:22.04

ENV container=docker

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        binutils \
        systemd \
        systemd-sysv \
        dbus

WORKDIR /work

STOPSIGNAL SIGRTMIN+3

CMD ["/sbin/init"]