bin_PROGRAMS  = sys
sys_SOURCES   = sys.c sys.h conf.c conf.h xmalloc.c xmalloc.h list.c    \
		list.h bst.c bst.h utils.c utils.h cache.c cache.h      \
		strarray.c strarray.h crc16.c crc16.h ini.c ini.h log.c \
		log.h egetopt.c comp.c comp.h ht.c ht.h check.c check.h \
		passwd.c passwd.h parser.h cachestat.h user.h types.h   \
		parser.l


AM_CFLAGS     = -D_DEFAULT_SOURCE -D_XsysN_SOURCE -DCFG_DIR=@cfg_dir@

EXTRA_DIST    = build.sh

parser.c: parser.l
	lex -o parser.c parser.l

install-exec-hook:
	chown root:root $(DESTDIR)$(bindir)/sys
	chmod 4755 $(DESTDIR)$(bindir)/sys
	mkdir -p @cfg_dir@

clean-local:
	rm -f parser.c

dist_man_MANS = sys.8 sys.cfg.5
