
include ../../Make.user
include ../../Make.config

install:
	@echo install womo stuff
	install --mode=755 -D bin/* $(BINDEST)/
	install --mode=755 -D scripts.d/* $(CONFDEST)/scripts.d/
	install --mode=755 -D scripts/* $(BINDEST)/
	if test -d $(DESTDIR)/etc/dnsmasq.d/; then \
		install --mode=644 -D dnsmasq/dnsmasq.d/00-base.conf $(DESTDIR)/etc/dnsmasq.d/; \
		install --mode=644 -D dnsmasq/dnsmasq.d/20-details.conf $(DESTDIR)/etc/dnsmasq.d/; \
		if ! test -f $(DESTDIR)/etc/dnsmasq.d/30-fixed.conf; then \
			cat dnsmasq/dnsmasq.d/30-fixed.conf | sed s:"<SUBNET>":"$(SUBNET)":g | install --mode=644 -C -D /dev/stdin $(DESTDIR)/etc/dnsmasq.d/30-fixed.conf; \
		fi; \
		cp -r dnsmasq/dnsmasq.profiles/ $(DESTDIR)/etc/; \
		cat dnsmasq/dnsmasq.profiles/womo-offline.conf | sed s:"<SUBNET>":"$(SUBNET)":g | install --mode=644 -C -D /dev/stdin $(DESTDIR)/etc/dnsmasq.profiles/womo-offline.conf; \
		cat dnsmasq/dnsmasq.profiles/womo-online.conf | sed s:"<SUBNET>":"$(SUBNET)":g | install --mode=644 -C -D /dev/stdin $(DESTDIR)/etc/dnsmasq.profiles/womo-online.conf; \
		install --mode=755 -D dnsmasq/systemd/* $(DESTDIR)/etc/systemd/system/; \
		systemctl daemon-reload; \
		systemctl enable --now lan-mode.timer; \
	fi
	install --mode=755 -D etc/logrotate.d/* $(DESTDIR)/etc/logrotate.d/
