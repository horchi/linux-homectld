include ../Make.config

install:
	@echo weewx service
	install --mode=755 -D weewxmqtt.py $(BINDEST)/
	cat weewxmqtt.service | sed s:"<BINDEST>":"$(_BINDEST)":g | sed s:"<AFTER>":"network-online.target":g | install --mode=644 -C -D /dev/stdin $(SYSTEMDDEST)/weewxmqtt.service
	chmod a+r $(SYSTEMDDEST)/weewxmqtt.service
	if ! test -f $(DESTDIR)/etc/default/weewxmqtt; then \
		cp weewxmqtt $(DESTDIR)/etc/default/weewxmqtt; \
		echo "!! Adjust settings in $(DESTDIR)/etc/default/weewxmqtt !!"; \
	fi

ifeq ($(DESTDIR),)
	systemctl enable weewxmqtt.service
	systemctl daemon-reload
endif
