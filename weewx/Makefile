include ../Make.config

install:
	@echo weewx service
	install --mode=755 -D weewxmqtt.py $(BINDEST)/
	@$(call installUnit,weewxmqtt.service,weewxmqtt.service)
	if ! test -f $(DESTDIR)/etc/default/weewxmqtt; then \
		cp weewxmqtt $(DESTDIR)/etc/default/weewxmqtt; \
		echo "!! Adjust settings in $(DESTDIR)/etc/default/weewxmqtt !!"; \
	fi

ifeq ($(DESTDIR),)
	@systemctl is-enabled -q weewxmqtt.service || systemctl enable weewxmqtt.service
endif
