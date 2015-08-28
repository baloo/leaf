
FROM debian:sid

RUN apt-get update
RUN apt-get install -y lldpd
RUN apt-get install -y libnl-3-200 libnl-route-3-200 libnl-genl-3-200
RUN apt-get install -y strace


VOLUME ["/opt/leaf"]
CMD /opt/leaf/test/test.sh
