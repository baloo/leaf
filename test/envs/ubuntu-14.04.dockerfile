FROM ubuntu:14.04

RUN apt-get update
RUN apt-get install -y libnl-3-200 libnl-route-3-200 liblldpctl-dev libpthread-stubs0-dev libnl-3-dev make pkg-config libnl-route-3-dev gcc libnl-genl-3-dev

ADD Makefile /opt/
ADD src /opt/src

WORKDIR /opt
RUN make clean all

