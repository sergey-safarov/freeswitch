About
-----

This is minimized official FreeSwitch docker container.
Container designed to run on host, bridge and swarm network.
Size of container decreased to 149MB (62MB compressed)
Significantly increased security:
1. removed all libs except libc, busybox, tcpdump, dumpcap, freeswitch and dependent libs;
2. removed 'system' API command from vanila config;
3. updated FreeSwitch default SIP password to random value.

Used environment variables
--------------------------

1. ```SOUND_RATES``` - rates of sound files that must be downloaded and installed. Available values ```8000```, ```16000```, ```32000```, ```48000```. May defined multiply values using semicolon as delimiter. Example ```SOUND_RATES=8000:16000```;
2. ```SOUND_TYPES``` - types of sound files that must be downloaded and installed. Available values music, ```en-us-callie```, ```en-us-allison```, ```ru-RU-elena```, ```en-ca-june```, ```fr-ca-june```, ```pt-BR-karina```, ```sv-se-jakob```, ```zh-cn-sinmei```, ```zh-hk-sinmei```. Example ```SOUND_TYPES=music:en-us-callie```;
3. ```EPMD``` - start epmd daemon, useful when you use mod_erlang and mod_kazoo FreeSwitch modules. Available values ```true```, ```false```.

Usage container
---------------

```sh
docker run --net=host --name freeswitch \
           -e SOUND_RATES=8000:16000 \
           -e SOUND_TYPES=music:en-us-callie \
           -v freeswitch-sounds:/usr/share/freeswitch/sounds \
           -v /etc/freeswitch/:/etc/freeswitch \
           safarov/freeswitch
```

systemd unit file
-----------------

You can use this systemd unit files on your docker host.
Unit file can be placed to ```/etc/systemd/system/freeswitch-docker.service``` and enabled by commands
```sh
systemd start freeswitch-docker.service
systemd enable freeswitch-docker.service
```

host network
============

```sh
$ cat /etc/systemd/system/freeswitch-docker.service
[Unit]
Description=freeswitch Container
After=docker.service network-online.target
Requires=docker.service


[Service]
Restart=always
TimeoutStartSec=0
#One ExecStart/ExecStop line to prevent hitting bugs in certain systemd versions
ExecStart=/bin/sh -c 'docker rm -f freeswitch; \
          docker run -t --net=host --name freeswitch \
                 -e SOUND_RATES=8000:16000 \
                 -e SOUND_TYPES=music:en-us-callie \
                 -v freeswitch-sounds:/usr/share/freeswitch/sounds \
                 -v /etc/kazoo/freeswitch/:/etc/freeswitch \
                 freeswitch'
ExecStop=-/bin/sh -c '/usr/bin/docker stop freeswitch; \
          /usr/bin/docker rm -f freeswitch;'

[Install]
WantedBy=multi-user.target
```

default bridge network
======================
```sh
[Unit]
Description=freeswitch Container
After=docker.service network-online.target
Requires=docker.service


[Service]
Restart=always
TimeoutStartSec=0
#One ExecStart/ExecStop line to prevent hitting bugs in certain systemd versions
ExecStart=/bin/sh -c 'docker rm -f freeswitch; \
          docker run -t --network bridge --name freeswitch \
                 -p 5060:5060/udp -p 5060:5060 \
                 -e SOUND_RATES=8000 \
                 -e SOUND_TYPES=music:en-us-callie \
                 -v freeswitch-sounds:/usr/share/freeswitch/sounds \
                 -v /etc/freeswitch/:/etc/freeswitch \
                 safarov/freeswitch'

ExecStartPost=/bin/sh -c 'sleep 5; \
          IP_ADDR=$(docker inspect -f "{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}" freeswitch); \
          /sbin/iptables -A DOCKER -t nat -p udp ! -i docker0  --dport 17000:17999 -j DNAT --to $IP_ADDR:17000-17999; \
          /sbin/iptables -A DOCKER -p udp ! -i docker0 -o docker0 -d $IP_ADDR --dport 17000:17999 -j ACCEPT'

ExecStop=-/bin/sh -c 'IP_ADDR=$(docker inspect -f "{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}" freeswitch); \
          /sbin/iptables -D DOCKER -t nat -p udp ! -i docker0  --dport 17000:17999 -j DNAT --to $IP_ADDR:17000-17999; \
          /sbin/iptables -D DOCKER -p udp ! -i docker0 -o docker0 -d $IP_ADDR --dport 17000:17999 -j ACCEPT; \
          /usr/bin/docker stop freeswitch; \
          /usr/bin/docker rm -f freeswitch;'

[Install]
WantedBy=multi-user.target
```

.bashrc file
------------
To simplify freeswitch managment you can add alias for ```fs_cli``` to ```.bashrc``` file as example bellow.
```sh
alias fs_cli='docker exec -i -t freeswitch /usr/bin/fs_cli'
```

How to create custom container
------------------------------
This container created from scratch image by addiding required freeswitch files packaged to tar.gz archive.
To create custom container:
1. clone freeswitch repo
```sh
git clone https://freeswitch.org/stash/scm/fs/freeswitch.git
```
2. build custom image
```sh
cd freeswitch/docker/base_image
hooks/pre_build
docker build -t custom-fs .
```

Read more
---------

[Dockerfile of official FreeSwitch container](https://freeswitch.org/stash/projects/FS/repos/freeswitch/browse/docker/release)
