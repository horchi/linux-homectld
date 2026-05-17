
include ../Make.config

install:
	@echo alpicool service
	install --mode=755 -D alpicool.py $(BINDEST)/
	cat alpicool.service | sed s:"<BINDEST>":"$(_BINDEST)":g | sed s:"<AFTER>":"$(INIT_AFTER)":g | install --mode=644 -C -D /dev/stdin $(SYSTEMDDEST)/alpicool.service
	chmod a+r $(SYSTEMDDEST)/alpicool.service
	if ! test -f $(DESTDIR)/etc/default/alpicool2mqtt; then \
		cp alpicool2mqtt $(DESTDIR)/etc/default/alpicool2mqtt; \
		echo "!! Adjust settings in $(DESTDIR)/etc/default/alpicool2mqtt !!"; \
	fi
   ifeq ($(DESTDIR),)
	   systemctl enable alpicool.service
	   systemctl daemon-reload
   endif
