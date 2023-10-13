all: demo1 demo2 demo3 myjit-disassembler

demo1: demo1.c jitlib-core.o mman-win32.o
	$(CC) -c -g -O0 -Winline -Wall -std=c99 -pedantic -D_XOPEN_SOURCE=600 -DTARGET_WIN32 demo1.c
	$(CC) -o demo1 -g -O0 -Wall -std=c99 -pedantic demo1.o jitlib-core.o mman-win32.o

demo2: demo2.c jitlib-core.o mman-win32.o
	$(CC) -c -g -O0 -Winline -Wall -std=c99 -pedantic -D_XOPEN_SOURCE=600 -DTARGET_WIN32 demo2.c
	$(CC) -o demo2 -g -O0 -Wall -std=c99 -pedantic demo2.o jitlib-core.o mman-win32.o

demo3: demo3.c jitlib-core.o mman-win32.o
	$(CC) -O0 -c -g -Winline -Wall -std=c99 -pedantic -D_XOPEN_SOURCE=600 -DTARGET_WIN32 demo3.c
	$(CC) -O0 -o demo3 -g -Wall -std=c99 -pedantic demo3.o jitlib-core.o mman-win32.o

debug: debug.c jitlib-core.o mman-win32.o
	$(CC) -c -g -O0 -Winline -Wall -std=c99 -pedantic -D_XOPEN_SOURCE=600 -DTARGET_WIN32 debug.c
	$(CC) -o debug -g -O0 -Wall -std=c99 -pedantic debug.o jitlib-core.o mman-win32.o

myjit-disassembler:
	rm myjit-disasm || true
	$(MAKE) -C disasm
	ln -s disasm/myjit-disasm .

run-tests:
	cd test && ${MAKE} test



jitlib-core.o: myjit/jitlib.h myjit/jitlib-core.h myjit/jitlib-core.c myjit/jitlib-debug.c myjit/x86-codegen.h myjit/x86-specific.h myjit/reg-allocator.h myjit/flow-analysis.h myjit/set.h myjit/amd64-specific.h myjit/amd64-codegen.h myjit/llrb.c myjit/reg-allocator.h myjit/rmap.h myjit/cpu-detect.h myjit/x86-common-stuff.c myjit/common86-specific.h myjit/common86-codegen.h myjit/sse2-specific.h myjit/code-check.c
	$(CC) -c -g -O0 -Winline -Wall -std=c99 -pedantic -D_XOPEN_SOURCE=600 -DTARGET_WIN32 -I. myjit/jitlib-core.c

mman-win32.o: mman-win32/mman.h mman-win32/mman.c
	$(CC) -c -g -O0 -Winline -Wall -std=c99 -pedantic -D_XOPEN_SOURCE=600 -DTARGET_WIN32 mman-win32/mman.c -o $@

clean:
	rm -f demo1
	rm -f demo2
	rm -f demo3
	rm -f debug
	rm -f *.o
	rm -f myjit-disasm
