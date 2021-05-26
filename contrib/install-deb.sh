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
echo -e "${BLUE}- Starting the installation (or update) of poold deamon${NC}"
echo -e "${BLUE}-    Deamon to control almost everything around your pool {NC}"
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
   apt -y install ssmtp || exit 1
   apt-get -y install locales || exit 1
   timedatectl set-timezone 'Europe/Berlin'
   dpkg-reconfigure --frontend noninteractive tzdata

   sed -i -e "s/# $SET_LANG.*/$SET_LANG UTF-8/" /etc/locale.gen
   dpkg-reconfigure --frontend=noninteractive locales && \
      update-locale LANG=$SET_LANG
fi

wget www.jwendel.de/poold/poold-latest.deb -O /tmp/poold-latest.deb || exit 1

apt -y remove poold
apt -y install /tmp/poold-latest.deb || exit 1

if [ ${update} != 1 ]; then
   grep -q '^alias pooldb=' ~/.bashrc   || echo "alias pooldb='mysql -u pool -D pool -ppool'" >> ~/.bashrc
   grep -q '^alias vl=' ~/.bashrc       || echo "alias vl='tail -f /var/log/poold.log'" >> ~/.bashrc

   # w1 config

   poold-patch-raspi-config.sh

   echo -e "${BLUE}-------------------------------------------------------------------------------------------${NC}"
   echo -e "${BLUE}- The installation is completed and will be available after reboot${NC}"
   echo -e "${BLUE}- ${NC}"
   echo -e "${BLUE}- You can reach the web interface at http://<raspi-ip>:61109${NC}"
   echo -e "${BLUE}- Your IP seems to be ${IP} therefore you acn try:${NC} ${BWHITE}http://${IP}:61109${NC}"
   echo -e "${BLUE}- Default user/password is pool/pool${NC}"
   echo -e "${BLUE}- ${NC}"
   echo -e "${BLUE}- Added aliases for convenience:${NC}"
   echo -e "${BLUE}-  pooldb  - go to the SQL prompt${NC}"
   echo -e "${BLUE}-  vl    - view syslog (abort with CTRL-C)${NC}"
   echo -e "${BLUE}-------------------------------------------------------------------------------------------${NC}"
   echo -e "${WHITE}- to permit poold sending mails: ${NC}"
   echo -e "${WHITE}-   setup your SMTP account in /etc/msmtprc properly${NC}"
   echo -e "${WHITE}-   and check your setting with:${NC}"
   echo -e "${WHITE}-    #> ${CYAN}poold-mail.sh 'Test Mail' 'just a test' text/plain your@mail.de${NC}"
   echo -e "${BLUE}-------------------------------------------------------------------------------------------${NC}"
   echo -e -n "${CYAN}Reboot now? [y/N] ${NC}"
   echo ""
   read -n 1 c

   if [ "${c}" == "y" ]; then
      reboot
   fi
fi
