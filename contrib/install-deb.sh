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

echo -e "${BLUE}-------------------------------------------------------------------------------------------${NC}"
echo -e "${BLUE}- Starting the installation (or update) of poold deamon${NC}"
echo -e "${BLUE}-    Deamon to fetch sensor data of the 'Lambdatronic s3200' and store to a MySQL database${NC}"
echo -e "${BLUE}-------------------------------------------------------------------------------------------${NC}"

echo -e -n "${BLUE}Continue? [y/N] ${NC}"
echo ""
read -n 1 c

if [ "${c}" != "y" ]; then
    exit 0
fi

SET_LANG=de_DE.UTF-8
IP=$(ip route get 8.8.8.8 | sed -n '/src/{s/.*src *\([^ ]*\).*/\1/p;q}')

apt update || exit 1
apt -y dist-upgrade || exit 1
apt -y install ssmtp || exit 1
apt-get -y install locales || exit 1

timedatectl set-timezone 'Europe/Berlin'
dpkg-reconfigure --frontend noninteractive tzdata

sed -i -e "s/# $SET_LANG.*/$SET_LANG UTF-8/" /etc/locale.gen
dpkg-reconfigure --frontend=noninteractive locales && \
    update-locale LANG=$SET_LANG

wget www.jwendel.de/poold/poold-latest.deb -O /tmp/poold-latest.deb || exit 1
apt -y install /tmp/poold-latest.deb || exit 1

echo "alias pooldb='mysql -u pool -D pool -ppool'" >> ~/.bashrc
echo "alias vs='tail -f /var/log/syslog'" >> ~/.bashrc
echo "alias va='tail -f /var/log/apache2/error.log'" >> ~/.bashrc

echo "alias pooldb='mysql -u pool -D pool -ppool'" >> ~pi/.bashrc
echo "alias vs='tail -f /var/log/syslog'" >> ~pi/.bashrc
echo "alias va='tail -f /var/log/apache2/error.log'" >> ~pi/.bashrc

echo -e "${BLUE}-------------------------------------------------------------------------------------------${NC}"
echo -e "${BLUE}- The installation is completed and will be available after reboot${NC}"
echo -e "${BLUE}- ${NC}"
echo -e "${BLUE}- You can reach the web interface at http://<raspi-ip>/pool${NC}"
echo -e "${BLUE}- Guess your IP is ${IP} use:${NC} ${BWHITE}http://${IP}/pool${NC}"
echo -e "${BLUE}- Default user/password is pool/pool{NC}"
echo -e "${BLUE}- ${NC}"
echo -e "${BLUE}- Added aliases for convenience:${NC}"
echo -e "${BLUE}-  pooldb  - go to the SQL prompt${NC}"
echo -e "${BLUE}-  vs    - view syslog (abort with CTRL-C)${NC}"
echo -e "${BLUE}-  va    - view apace error log (abort with CTRL-C)${NC}"
echo -e "${BLUE}-------------------------------------------------------------------------------------------${NC}"
echo -e "${WHITE}- to permit poold sending mails: ${NC}"
echo -e "${WHITE}-   setup your SMTP account in /etc/ssmtp/ssmtp.conf properly${NC}"
echo -e "${WHITE}-   and check your setting with:${NC}"
echo -e "${WHITE}-    #> ${CYAN}poold-mail.sh 'Test Mail' 'just a test' text/plain your@mail.de${NC}"
echo -e "${BLUE}-------------------------------------------------------------------------------------------${NC}"
echo -e -n "${CYAN}Reboot now? [y/N] ${NC}"
echo ""
read -n 1 c

if [ "${c}" == "y" ]; then
    reboot
fi
