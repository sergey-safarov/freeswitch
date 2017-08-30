#!/bin/bash -e
#
# FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
# Copyright (C) 2005-2016, Anthony Minessale II <anthm@freeswitch.org>
#
# Version: MPL 1.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/F
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
#
# The Initial Developer of the Original Code is
# Michael Jerris <mike@jerris.com>
# Portions created by the Initial Developer are Copyright (C)
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#
#  Sergey Safarov <s.safarov@gmail.com>
#

get_release_name() {
cat /etc/os-release  | grep 'VERSION=' | sed -e 's/^VERSION="[0-9]\+ (//' -e 's/)"$//'
}

get_util_version_opt() {
    set +e
    if [ -z "$SOURCE_BRANCH" ]; then
        set -e
        return
    fi

    if [ "$SOURCE_BRANCH" == "master" ]; then
        set -e
        return
    fi

    if [ "$SOURCE_BRANCH" == "v1.6" ]; then
        set -e
        return
    fi

    set -e
    echo "$SOURCE_BRANCH" | sed -e 's/^v/-v /'
}

prepare_build() {
    #Preparing build environment
    echo "deb http://files.freeswitch.org/repo/deb/debian-unstable/ $RELEASE_NAME main" > /etc/apt/sources.list.d/freeswitch.list
    apt-get update && apt-get install -y git wget apt-utils xz-utils devscripts cowbuilder
    wget -O - https://files.freeswitch.org/repo/deb/debian/freeswitch_archive_g0.pub | apt-key add -
    if [ ! -d /usr/src/freeswitch-debs/freeswitch ]; then
        mkdir /usr/src/freeswitch-debs
        git clone https://freeswitch.org/stash/scm/fs/freeswitch.git /usr/src/freeswitch-debs/freeswitch
    fi
}

check_git_config() {
    cd /usr/src/freeswitch-debs/freeswitch
    set +e
    git config --get user.email
    if [ $? -ne 0 ]; then
        echo "WARNING: git 'user.email' is not configured. Setting to 'you@example.com' value"
        git config --global user.email "you@example.com"
    fi
    git config --get user.name
    if [ $? -ne 0 ]; then
        echo "WARNING: git 'user.name' is not configured. Setting to 'Your Name' value"
        git config --global user.name "Your Name"
    fi
    set -e
}

build_fs_packages() {
    # Build FreeSwitch packages
    cd /usr/src/freeswitch-debs/freeswitch
    git reset --hard
    git clean -fdx
    ./debian/util.sh create-orig $VERSION_OPT
    ./debian/util.sh create-dsc $RELEASE_NAME ../freeswitch_*.orig.tar.xz
    mk-build-deps -i --tool="apt-get --no-install-recommends -y --allow-unauthenticated" ../freeswitch_*.dsc
    cd ..
    dpkg-source -x freeswitch_*.dsc
    cd freeswitch-*
    dpkg-buildpackage
}

build_base_img_fs() {
    cd /usr/src/freeswitch-debs
    mkdir dbg
    mv *-dbg_*.deb dbg
    mv freeswitch-all_*.deb dbg
    mv freeswitch-meta-all*.deb dbg
    mkdir deb
    mv *.deb deb
    cd deb
    rm -f /etc/apt/sources.list.d/freeswitch.list
    apt-get update
    apt-get -y install erlang vlc-nox
    dpkg -i *.deb
    cd /usr/src/freeswitch-debs/freeswitch/docker/base_image
    ./make_min_archive.sh
}

# Checking dist is Debian
cat /etc/os-release  | grep -q "ID=debian"
if [ $? -ne 0 ]; then
    echo "ERROR: Supported only Debian dist"
fi

RELEASE_NAME=$(get_release_name)
VERSION_OPT=$(get_util_version_opt)

prepare_build
check_git_config
build_fs_packages
build_base_img_fs

# Build FreeSwitch base image filesystem
echo "FreeSwitch base image located at 'freeswitch_img.tar.gz'"
