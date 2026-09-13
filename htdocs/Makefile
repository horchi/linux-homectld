
include ../Make.config

# index.html is generated from index.html.in by genindex.sh: the local .css/.js references
# get '?v=<content hash>' appended, a changed file gets a new URL with the next make (also
# during development), unchanged files stay cached. index.html itself is delivered 'no-store'.
# 'make install' creates a gzip twin (<file>.gz) of the text files in WEBDEST ('make gzip'), the
# daemon serves the twin (Content-Encoding: gzip) as long as it is not older than the original.

all: index.html

.PHONY: index.html

index.html: index.html.in
	@bash genindex.sh $< $@

check:
	for f in *.js; do \
		echo Check "$$f" ..; \
		err=$$(node --check "$$f" 2>&1); \
		if [ $$? -ne 0 ]; then \
			echo "$$err" | awk '/^[^[:space:]].*:[0-9]+$$/{loc=$$0; next} /SyntaxError/{print loc ": " $$0}'; \
			exit 1; \
		fi; \
	done

install: check index.html
	make linstall

linstall:
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
	make gzip

# gzip twins of the text files, next to the originals in WEBDEST (also if WEBDEST is a symlink
# to this directory, the daemon compares the time stamps and skips outdated twins)

gzip:
	find "$(WEBDEST)/" -type f \( -name '*.js' -o -name '*.css' -o -name '*.html' -o -name '*.svg' \) -exec gzip -9 -k -f {} \;
