#!/bin/sh

set -xe ; exec < /dev/null

GATEWAY=@@GATEWAY@@
PREFIX=@@PREFIX@@
BENCHDIR=@@BENCHDIR@@
POOLDIR=@@POOLDIR@@
SOURCEDIR=@@SOURCEDIR@@
TESTINGDIR=@@TESTINGDIR@@

# update /etc/fstab with current /source and /testing

mkdir -p /pool /source /testing
sed -i -e '/source/d' -e '/testing/d' /etc/fstab
cat <<EOF | tee -a /etc/fstab
${GATEWAY}:${SOURCEDIR}   /source   nfs  rw,tcp
${GATEWAY}:${TESTINGDIR}  /testing  nfs  rw,tcp
EOF

cp -v /bench/testing/libvirt/openbsd/rc.conf.local /etc/rc.conf.local
chmod a+r /etc/rc.conf.local

cp -v /bench/testing/libvirt/openbsd/rc.local /etc/rc.local
chmod a+x /etc/rc.local

chsh -s /usr/local/bin/bash root
cp -v /bench/testing/libvirt/bash_profile /root/.bash_profile

exit 0
