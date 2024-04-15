
include ../Make.config

install:
	@echo mopeka service
	install --mode=755 -D mopeka.py $(BINDEST)/
	cat mopeka.service | sed s:"<BINDEST>":"$(_BINDEST)":g | sed s:"<AFTER>":"$(INIT_AFTER)":g | install --mode=644 -C -D /dev/stdin $(SYSTEMDDEST)/mopeka.service
	chmod a+r $(SYSTEMDDEST)/mopeka.service
	if ! test -f $(DESTDIR)/etc/default/mopeka2mqtt; then \
		cp mopeka2mqtt $(DESTDIR)/etc/default/mopeka2mqtt; \
		echo "!! Adjust settings in $(DESTDIR)/etc/default/mopeka2mqtt !!"; \
	fi
   ifeq ($(DESTDIR),)
	   systemctl daemon-reload
	   systemctl enable mopeka.service
   endif
