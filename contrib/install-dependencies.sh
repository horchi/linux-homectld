#!/bin/bash
#
# install-dependencies.sh - install the build and runtime dependencies of homectld
#
# contrib/install-dependencies.sh, called by 'make install-dependencies', the feature switches of Make.user are passed
# as environment variables (WOMO, MOPEKA, THETFORD, GARMIN, VDR).
#
# Supports Armbian (Debian / Ubuntu based) and Ubuntu (24.04, 26.04).
# The ESP / Arduino sketches (kenwood, alpicool, arduino) are not covered,
# see their Makefiles (make install-deps there).
#
# SRC_DIR (Make.user, default /usr/src) is where libgpiod / libwebsockets are cloned and
# built if the distribution's versions are too old. DRYRUN=1 only prints the commands.
#

set -e

SRC_DIR="${SRC_DIR:-/usr/src}"

run()
{
   if [ -n "${DRYRUN}" ]; then
      echo "  $*"
   else
      echo "+ $*"
      "$@"
   fi
}

# --- detect the distribution ---------------------------------------------

. /etc/os-release

DIST="${ID}"                      # ubuntu / debian
DIST_VERSION="${VERSION_ID}"      # 24.04 / 26.04 / 12 / 13
ARMBIAN=""

if [ -f /etc/armbian-release ]; then
   ARMBIAN="yes"
fi

echo "Distribution: ${PRETTY_NAME} (${DIST} ${DIST_VERSION}${ARMBIAN:+, Armbian})"
echo "Switches:     WOMO=${WOMO:-0} MOPEKA=${MOPEKA:-0} THETFORD=${THETFORD:-0} GARMIN=${GARMIN:-0} VDR=${VDR:-0}"
echo "Source dir:   ${SRC_DIR} (for libraries built from source)"
echo

if [ "${DIST}" != "ubuntu" ] && [ "${DIST}" != "debian" ]; then
   echo "Unsupported distribution '${DIST}', only Debian / Ubuntu based systems (Armbian, Ubuntu)"
   exit 1
fi

# candidate version of an apt package ('' if not available)

aptVersion()
{
   apt-cache policy "$1" 2>/dev/null | sed -n 's/^  Candidate: //p' | sed 's/^\([0-9][0-9.]*\).*/\1/'
}

# version compare: versionAtLeast <version> <minimum>

versionAtLeast()
{
   [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]
}

# install only the packages which are missing, never upgrade installed ones
# (a plain 'apt-get install' would pull the pending updates of every listed package)

APT_UPDATED=""

aptInstall()
{
   local missing=""

   for pkg in "$@"; do
      if ! dpkg-query -W -f='${Status}' "${pkg}" 2>/dev/null | grep -q "install ok installed"; then
         missing="${missing} ${pkg}"
      fi
   done

   if [ -z "${missing}" ]; then
      echo "already installed: $*"
      return 0
   fi

   if [ -z "${APT_UPDATED}" ]; then
      run apt-get update
      APT_UPDATED="yes"
   fi

   aptInstall --no-upgrade ${missing}
}

# --- build tools and libraries (all platforms) ---------------------------

aptInstall build-essential pkg-config cmake git \
   libssl-dev libcurl4-openssl-dev uuid-dev libcap-dev libsystemd-dev zlib1g-dev \
   libjansson-dev libmariadb-dev liblua5.3-dev

# --- runtime tools used by the daemon and its scripts --------------------

aptInstall mosquitto-clients jq jo bc util-linux-extra nodejs dialog

# --- libgpiod: version 2 is mandatory ------------------------------------
#   Ubuntu 24.04 (and Armbian based on it) ships 1.6, Ubuntu 26.04 ships 2.x

GPIOD_INSTALLED="$(pkg-config --modversion libgpiod 2>/dev/null || true)"

if [ -n "${GPIOD_INSTALLED}" ] && versionAtLeast "${GPIOD_INSTALLED}" "2.0"; then
   echo "libgpiod ${GPIOD_INSTALLED} already installed"
elif versionAtLeast "$(aptVersion libgpiod-dev)" "2.0"; then
   aptInstall libgpiod-dev gpiod
else
   echo "libgpiod-dev of the distribution is $(aptVersion libgpiod-dev), version 2 is needed - building it from source"
   aptInstall autoconf automake libtool autoconf-archive
   run rm -rf ${SRC_DIR}/libgpiod
   run git clone --depth 1 --branch v2.1.3 https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git ${SRC_DIR}/libgpiod
   run bash -c "cd ${SRC_DIR}/libgpiod && ./autogen.sh --enable-tools=yes --enable-bindings-cxx=no && make -j2 && make install && ldconfig"
fi

# --- libwebsockets: at least 4.3.0 ----------------------------------------

LWS_INSTALLED="$(pkg-config --modversion libwebsockets 2>/dev/null || true)"

if [ -n "${LWS_INSTALLED}" ] && versionAtLeast "${LWS_INSTALLED}" "4.3.0"; then
   echo "libwebsockets ${LWS_INSTALLED} already installed"
elif versionAtLeast "$(aptVersion libwebsockets-dev)" "4.3.0"; then
   aptInstall libwebsockets-dev
else
   echo "libwebsockets-dev of the distribution is $(aptVersion libwebsockets-dev), 4.3.0 is needed - building it from source"
   run rm -rf ${SRC_DIR}/libwebsockets
   run git clone --depth 1 https://github.com/warmcat/libwebsockets.git ${SRC_DIR}/libwebsockets
   run bash -c "cd ${SRC_DIR}/libwebsockets && mkdir -p build && cd build && cmake .. && make -j2 && make install && ldconfig"
fi

# --- optional components (Make.user) ------------------------------------

if [ "${WOMO}" = "1" ]; then
   echo
   echo "WOMO: the box is the server of the vehicle - database, MQTT broker, network (dnsmasq,"
   echo "      NetworkManager, LTE modem), time sync, firewall / VPN"
   aptInstall mariadb-server mosquitto
   aptInstall dnsmasq dnsmasq-utils network-manager chrony \
      modemmanager usb-modeswitch libqmi-utils iptables openvpn
   echo "      note: database and user (see README) and the mosquitto listener (/etc/mosquitto/"
   echo "      mosquitto.conf: 'listener 1883', 'allow_anonymous true') are not set up by this script"
fi

if [ "${MOPEKA}" = "1" ]; then
   echo
   echo "MOPEKA: python with bleak (BLE) and paho-mqtt"
   aptInstall bluez python3 python3-bleak python3-paho-mqtt
fi

if [ "${THETFORD}" = "1" ]; then
   echo
   echo "THETFORD: python with paho-mqtt and usblini (usblini is not packaged, installed via pip)"
   aptInstall python3 python3-pip python3-paho-mqtt
   run python3 -m pip install --quiet --break-system-packages usblini
fi

if [ "${GARMIN}" = "1" ]; then
   echo
   echo "GARMIN: python venv (garminconnect is installed into the venv by 'make install')"
   aptInstall python3 python3-venv python3-pip
fi

# VDR: the SVDRP connection needs no additional packages

echo
echo "Done. Not installed by this script:"
if [ "${WOMO}" != "1" ]; then
   echo "  - mariadb-server and mosquitto (broker), if they should run on this host (see README)"
fi
echo "  - the dependencies of the ESP32 / Arduino sketches (see kenwood/, alpicool/: make install-deps)"
