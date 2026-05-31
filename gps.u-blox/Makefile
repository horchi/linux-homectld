
include ../Make.config

install:
	@echo gps service
	install --mode=755 -D gpsmqtt.py $(BINDEST)/
	cat gpsmqtt.service | sed s:"<BINDEST>":"$(_BINDEST)":g | sed s:"<AFTER>":"$(INIT_AFTER)":g | install --mode=644 -C -D /dev/stdin $(SYSTEMDDEST)/gpsmqtt.service
	chmod a+r $(SYSTEMDDEST)/gpsmqtt.service
	if ! test -f $(DESTDIR)/etc/default/gps2mqtt; then \
		cp gps2mqtt $(DESTDIR)/etc/default/gps2mqtt; \
		echo "!! Adjust settings in $(DESTDIR)/etc/default/gps2mqtt !!"; \
	fi

   ifeq ($(DESTDIR),)
		systemctl enable gpsmqtt.service
		systemctl daemon-reload
   endif
