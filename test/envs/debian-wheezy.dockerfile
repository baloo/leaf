FROM debian:wheezy

RUN sed -i '/ wheezy / {p; s/wheezy/wheezy-backports/}' /etc/apt/sources.list
RUN apt-get update
# split because of https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=794569
RUN apt-get install -y libbsd0
RUN apt-get install -y -t wheezy-backports liblldpctl-dev lldpd
RUN apt-get install -y libnl-3-200 libnl-route-3-200 libbsd-dev libnl-3-dev make pkg-config libnl-route-3-dev gcc libnl-genl-3-dev automake autoconf

ADD . /opt/

WORKDIR /opt
RUN ./autogen.sh
RUN ./configure
RUN make clean all

