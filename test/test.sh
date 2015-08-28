#!/bin/sh -x


ip netns add upstream
ip netns add local
ip netns add downstream1
ip netns add downstream2
ip netns add downstream3

ip link add name swp1 type veth peer name swp32
ip link set swp1 netns upstream
ip link set swp32 netns local

ip link add name swp32 type veth peer name swp1
ip link set swp1 netns local
ip link set swp32 netns downstream1

ip link add name swp32 type veth peer name swp2
ip link set swp2 netns local
ip link set swp32 netns downstream2

ip link add name swp32 type veth peer name swp3
ip link set swp3 netns local
ip link set swp32 netns downstream3

ip netns exec upstream    ip link set swp1  up
ip netns exec local       ip link set swp32 up
ip netns exec local       ip link set swp1  up
ip netns exec local       ip link set swp2  up
ip netns exec local       ip link set swp3  up
ip netns exec downstream1 ip link set swp32 up
ip netns exec downstream2 ip link set swp32 up
ip netns exec downstream3 ip link set swp32 up

ip netns exec upstream    lldpd -u /tmp/upstream
ip netns exec local       lldpd -u /tmp/local
ip netns exec downstream1 lldpd -u /tmp/downstream1
ip netns exec downstream2 lldpd -u /tmp/downstream2
ip netns exec downstream3 lldpd -u /tmp/downstream3

# Wait for lldpd to come up before basic config
sleep 1
ip netns exec upstream    lldpcli -u /tmp/upstream    configure system hostname upstream
ip netns exec local       lldpcli -u /tmp/local       configure system hostname local
ip netns exec downstream1 lldpcli -u /tmp/downstream1 configure system hostname downstream1
ip netns exec downstream2 lldpcli -u /tmp/downstream2 configure system hostname downstream2
ip netns exec downstream3 lldpcli -u /tmp/downstream3 configure system hostname downstream3

# Force out lldpu to make lldpd in ready state immediatly
ip netns exec upstream    lldpcli -u /tmp/upstream    update
ip netns exec local       lldpcli -u /tmp/local       update
ip netns exec downstream1 lldpcli -u /tmp/downstream1 update
ip netns exec downstream2 lldpcli -u /tmp/downstream2 update
ip netns exec downstream3 lldpcli -u /tmp/downstream3 update

DIR=$(realpath $(dirname $0))/tests

run-parts        --verbose --exit-on-error --arg=/opt/leaf/src/leaf --arg=local $DIR
