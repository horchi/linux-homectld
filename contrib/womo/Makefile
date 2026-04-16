
include ../../Make.config

install:
	@echo install womo stuff
	install --mode=755 -D bin/* $(BINDEST)/
	install --mode=755 -D scripts.d/* $(CONFDEST)/scripts.d/
	install --mode=755 -D scripts/* $(BINDEST)/
	if test -d $(DESTDIR)/etc/dnsmasq.d/; then \
		install --mode=755 -D dnsmasq/dnsmasq.d/* $(DESTDIR)/etc/dnsmasq.d/; \
		cp -r dnsmasq/dnsmasq.profiles/ $(DESTDIR)/etc/; \
		install --mode=755 -D dnsmasq/systemd/* $(DESTDIR)/etc/systemd/system/; \
		systemctl daemon-reload; \
		systemctl enable --now lan-mode.timer; \
	fi
	install --mode=755 -D etc/logrotate.d/* $(DESTDIR)/etc/logrotate.d/

# etc/vdr.logrotate
