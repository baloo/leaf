FROM debian:stretch

RUN apt-get update
RUN apt-get install -y libnl-3-200 libnl-route-3-200 libbsd-dev libnl-3-dev make pkg-config libnl-route-3-dev gcc libnl-genl-3-dev liblldpctl-dev lldpd automake autoconf

ADD . /opt/

WORKDIR /opt
RUN ./autogen.sh
RUN ./configure
RUN make clean all

