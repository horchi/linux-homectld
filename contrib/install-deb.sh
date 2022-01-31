#!/bin/bash

WHITE='\033[0;37m'
BWHITE='\033[1;37m'
BLUE='\033[0;34m'
BBLUE='\033[1;34m'
CYAN='\033[0;36m'
BCYAN='\033[1;36m'
RED='\033[0;31m'
BRED='\033[1;31m'
GREEN='\033[0;32m'
BGREEN='\033[1;32m'
NC='\033[0m'

update=0

echo -e "${BLUE}-------------------------------------------------------------------------------------------${NC}"
echo -e "${BLUE}- Starting the installation (or update) of homectld 'home control deamon'${NC}"
echo -e "${BLUE}-------------------------------------------------------------------------------------------${NC}"

echo -e -n "${BLUE}Continue? [y/N] ${NC}"
echo ""
read -n 1 c

if [ "${c}" != "y" ]; then
    exit 0
fi

if [ $# == 1 ] && [ "$1" == "-u" ]; then
   update=1
fi

SET_LANG=de_DE.UTF-8
IP=$(ip route get 8.8.8.8 | sed -n '/src/{s/.*src *\([^ ]*\).*/\1/p;q}')

if [ ${update} != 1 ]; then
   apt update || exit 1
   apt -y dist-upgrade || exit 1
   apt-get -y install locales || exit 1
   timedatectl set-timezone 'Europe/Berlin'
   dpkg-reconfigure --frontend noninteractive tzdata

   sed -i -e "s/# $SET_LANG.*/$SET_LANG UTF-8/" /etc/locale.gen
   dpkg-reconfigure --frontend=noninteractive locales && \
      update-locale LANG=$SET_LANG
fi

wget www.jwendel.de/homectld/homectld-latest.deb -O /tmp/homectld-latest.deb || exit 1

apt -y remove homectld
apt -y install /tmp/homectld-latest.deb || exit 1

if [ ${update} != 1 ]; then
   grep -q '^alias homectldb=' ~/.bashrc   || echo "alias homectldb='mysql -u homectl -D homectl -phomectl'" >> ~/.bashrc
   grep -q '^alias vp=' ~/.bashrc          || echo "alias vp='tail -f /var/log/homectld.log'" >> ~/.bashrc

   homectld-patch-raspi-config.sh

   echo -e "${BLUE}-------------------------------------------------------------------------------------------${NC}"
   echo -e "${BLUE}- The installation is completed and will be available after reboot${NC}"
   echo -e "${BLUE}- ${NC}"
   echo -e "${BLUE}- You can reach the web interface at http://<raspi-ip>:61109${NC}"
   echo -e "${BLUE}- Your IP seems to be ${IP} therefore you can try:${NC} ${BWHITE}http://${IP}:61109${NC}"
   echo -e "${BLUE}- Default user/password is homectl/homectl${NC}"
   echo -e "${BLUE}- ${NC}"
   echo -e "${BLUE}- Added aliases for convenience:${NC}"
   echo -e "${BLUE}-  homectlddb  - go to the SQL prompt${NC}"
   echo -e "${BLUE}-  vp    - view daemon log (abort with CTRL-C)${NC}"
   echo -e "${BLUE}-------------------------------------------------------------------------------------------${NC}"
   echo -e "${WHITE}- to permit the daemon sending mails: ${NC}"
   echo -e "${WHITE}-   setup your mail account in /etc/msmtprc properly${NC}"
   echo -e "${WHITE}-   and check your setting with:${NC}"
   echo -e "${WHITE}-    #> ${CYAN}homectld-mail.sh 'Test Mail' 'just a test' text/plain your@mail.de${NC}"
   echo -e "${BLUE}-------------------------------------------------------------------------------------------${NC}"
   echo -e -n "${CYAN}Reboot now? [y/N] ${NC}"
   echo ""
   read -n 1 c

   if [ "${c}" == "y" ]; then
      reboot
   fi
fi
