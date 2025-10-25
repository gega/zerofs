#

all:		zerofs littlefs

zerofs:		zerofs.c zerofs.h lua/src/liblua.a test.h flash.h flash.c
		gcc -g -Ilua/src -Llua/src -Wall -o zerofs zerofs.c flash.c -lcurses -llua -lm

littlefs:	littlefs.c flash.c flash.h lfs/liblfs.a lua/src/liblua.a test.h
		gcc -Ilua/src -Llua/src -Ilfs/ -Llfs/ -Wall -o littlefs littlefs.c flash.c -lcurses -llfs -llua -lm

lua/src/liblua.a:
		make -C lua/

lfs/liblfs.a:
		make CFLAGS="-DLFS_NO_ERROR" -C lfs/


clean:
		rm -f zerofs littlefs *.o
		make -C lfs/ clean
		make -C lua/ clean
