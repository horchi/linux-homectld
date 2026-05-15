
include ../../Make.user
include ../../Make.config

ETH_IFACE := $(shell ip -o link show | awk '$$2 != "lo:" && /ether/ {gsub(/:/, "", $$2); print $$2; exit}')
BOARD     := $(shell cat /proc/device-tree/model 2>/dev/null | tr -d '\0')

install:
	@echo install womo stuff
	install --mode=755 -D bin/* $(BINDEST)/
	install --mode=755 -D scripts.d/* $(CONFDEST)/scripts.d/
	install --mode=755 -D scripts/* $(BINDEST)/
	if test -d $(DESTDIR)/etc/dnsmasq.d/; then \
		cat dnsmasq/dnsmasq.d/00-base.conf | sed s:"<ETH_IFACE>":"$(ETH_IFACE)":g | install --mode=644 -C -D /dev/stdin $(DESTDIR)/etc/dnsmasq.d/00-base.conf; \
		cat dnsmasq/dnsmasq.d/20-details.conf | sed s:"<DOMAIN>":"$(DOMAIN)":g | install --mode=644 -C -D /dev/stdin $(DESTDIR)/etc/dnsmasq.d/20-details.conf; \
		if ! test -f $(DESTDIR)/etc/dnsmasq.d/30-fixed.conf; then \
			cat dnsmasq/dnsmasq.d/30-fixed.conf | sed s:"<SUBNET>":"$(SUBNET)":g | install --mode=644 -C -D /dev/stdin $(DESTDIR)/etc/dnsmasq.d/30-fixed.conf; \
		fi; \
		cp -r dnsmasq/dnsmasq.profiles/ $(DESTDIR)/etc/; \
		cat dnsmasq/dnsmasq.profiles/womo-offline.conf | sed s:"<SUBNET>":"$(SUBNET)":g | install --mode=644 -C -D /dev/stdin $(DESTDIR)/etc/dnsmasq.profiles/womo-offline.conf; \
		cat dnsmasq/dnsmasq.profiles/womo-online.conf | sed s:"<SUBNET>":"$(SUBNET)":g | install --mode=644 -C -D /dev/stdin $(DESTDIR)/etc/dnsmasq.profiles/womo-online.conf; \
		install --mode=644 -D etc/default/* $(DESTDIR)/etc/default/; \
		install --mode=755 -D dnsmasq/systemd/* $(DESTDIR)/etc/systemd/system/; \
		systemctl daemon-reload; \
		systemctl enable --now lan-mode.timer; \
		systemctl disable --now systemd-resolved; \
		systemctl reload NetworkManager; \
	fi
	for f in $(DESTDIR)/etc/netplan/*.yaml $(DESTDIR)/etc/netplan/*.yml; do \
		[ -f "$$f" ] || continue; \
		[ "$$(basename $$f)" = "00-static.yaml" ] && continue; \
		if grep -q "ethernets:" "$$f" && ! grep -q "wifis:" "$$f"; then \
			echo "Disabling conflicting netplan config: $$f"; \
			mv "$$f" "$$f.disabled"; \
		fi; \
	done
	if ! test -f $(DESTDIR)/etc/netplan/00-static.yaml; then \
		cat netplan/00-static.yaml | sed s:"<STATIC_IP>":"$(STATIC_IP)":g | sed s:"<ETH_IFACE>":"$(ETH_IFACE)":g | install --mode=600 -C -D /dev/stdin $(DESTDIR)/etc/netplan/00-static.yaml; \
		netplan apply; \
	fi
	if ! test -f $(DESTDIR)/etc/NetworkManager/conf.d/disabl-dns.conf; then \
		install --mode=644 -D etc/disabl-dns.conf $(DESTDIR)/etc/NetworkManager/conf.d/disabl-dns.conf; \
		systemctl reload NetworkManager; \
	fi
	if echo "$(BOARD)" | grep -qiE "N2|M1"; then \
		if ! test -f $(DESTDIR)/etc/cron.d/rtc-sync; then \
			echo "@reboot root hwclock --hctosys" > $(DESTDIR)/etc/cron.d/rtc-sync; \
			echo "RTC sync cron installed for $(BOARD)"; \
		fi; \
	fi
	install --mode=755 -D etc/logrotate.d/* $(DESTDIR)/etc/logrotate.d/
	install --mode=664 -D etc/systemd/* $(SYSTEMDDEST)/
	install --mode=644 -D etc/udev.rules.d/* $(DESTDIR)/etc/udev/rules.d/
   ifdef VDR
	   $(MAKE) install-vdr
   endif

install-vdr:
	@echo install vdr specials
	install --mode=755 -D contrib/vdrctl $(BINDEST)/
		install --mode=644 -D contrib/vdrctl.completion $(DESTDIR)/etc/bash_completion.d/vdrctl
	install --mode=644 -D contrib/dvb-demod-m88ds3103.fw $(DESTDIR)/lib/firmware/
