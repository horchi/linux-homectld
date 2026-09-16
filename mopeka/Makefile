
include ../Make.config

install:
	@echo mopeka service
	install --mode=755 -D mopeka.py $(BINDEST)/
	@$(call installUnit,mopeka.service,mopeka.service)
	if ! test -f $(DESTDIR)/etc/default/mopeka2mqtt; then \
		cp mopeka2mqtt $(DESTDIR)/etc/default/mopeka2mqtt; \
		echo "!! Adjust settings in $(DESTDIR)/etc/default/mopeka2mqtt !!"; \
	fi
   ifeq ($(DESTDIR),)
	   @systemctl is-enabled -q mopeka.service || systemctl enable mopeka.service
   endif
