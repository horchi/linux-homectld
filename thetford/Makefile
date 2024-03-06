
include ../Make.config

install:
	@echo thetford service
	install --mode=755 -D thetford.py $(BINDEST)/
	cat thetford.service | sed s:"<BINDEST>":"$(_BINDEST)":g | sed s:"<AFTER>":"$(INIT_AFTER)":g | install --mode=644 -C -D /dev/stdin $(SYSTEMDDEST)/thetford.service
	chmod a+r $(SYSTEMDDEST)/thetford.service
	if ! test -f $(DESTDIR)/etc/default/thetford2mqtt; then \
		cp thetford2mqtt $(DESTDIR)/etc/default/thetford2mqtt; \
		echo "!! Adjust settings in $(DESTDIR)/etc/default/thetford2mqtt !!"; \
	fi
   ifeq ($(DESTDIR),)
	   systemctl daemon-reload
	   systemctl enable thetford.service
   endif
