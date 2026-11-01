PREFIX ?= /usr/local

all: g5moused

g5moused: g5moused.c
	gcc -O2 -Wall -o $@ $< -lusb-1.0

install: g5moused
	install -Dm755 g5moused $(DESTDIR)$(PREFIX)/bin/g5moused
	install -Dm644 g5moused.service $(DESTDIR)/etc/systemd/system/g5moused.service

uninstall:
	systemctl disable --now g5moused 2>/dev/null || true
	rm -f $(DESTDIR)$(PREFIX)/bin/g5moused
	rm -f $(DESTDIR)/etc/systemd/system/g5moused.service

clean:
	rm -f g5moused

.PHONY: all install uninstall clean
