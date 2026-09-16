
include ../Make.config

install:
	@echo gps service
	install --mode=755 -D gpsmqtt.py $(BINDEST)/
	@$(call installUnit,gpsmqtt.service,gpsmqtt.service)
	if ! test -f $(DESTDIR)/etc/default/gps2mqtt; then \
		cp gps2mqtt $(DESTDIR)/etc/default/gps2mqtt; \
		echo "!! Adjust settings in $(DESTDIR)/etc/default/gps2mqtt !!"; \
	fi

   ifeq ($(DESTDIR),)
	   @systemctl is-enabled -q gpsmqtt.service || systemctl enable gpsmqtt.service
   endif
