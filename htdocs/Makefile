
include ../Make.config

check:
	for f in *.js; do \
		err=$$(node --check "$$f" 2>&1); \
		if [ $$? -ne 0 ]; then \
			echo "$$err" | awk '/^[^[:space:]].*:[0-9]+$$/{loc=$$0; next} /SyntaxError/{print loc ": " $$0}'; \
			exit 1; \
		fi; \
	done

linstall: check
	make install

install: check
	if ! test -L $(WEBDEST); then \
		if ! test -d $(WEBDEST); then \
			mkdir -p "$(WEBDEST)"; \
			chmod a+rx "$(WEBDEST)"; \
		fi ; \
		if test -f "$(WEBDEST)/stylesheet.css"; then \
			cp -Pp "$(WEBDEST)/stylesheet.css" "$(WEBDEST)/stylesheet.css.save"; \
		fi ; \
		cp -r ./* $(WEBDEST)/; \
		if test -f "$(WEBDEST)/stylesheet.css.save"; then \
			cp -Pp "$(WEBDEST)/stylesheet.css.save" "$(WEBDEST)/stylesheet.css"; \
		fi ; \
		chmod -R a+r "$(WEBDEST)"; \
		chown -R $(WEBOWNER):$(WEBOWNER) "$(WEBDEST)"; \
	fi
