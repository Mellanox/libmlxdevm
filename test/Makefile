EXT_LIBS_FLAGS=-DHAVE_LIBMNL
CFLAGS+=-I../lib/include/uapi/mlxdevm
CFLAGS+=-I../lib/include/uapi/linux 
CFLAGS+=-I../lib
EXT_LIBS+=-lmnl

all: ../lib/mlxdevmlib.a
	gcc -o mlxdevm_test $(CFLAGS) $(EXT_LIBS_FLAGS) $(EXT_LIBS) \
		main.c ../lib/mlxdevmlib.a

clean:
	rm -rf mlxdevm_test *.o
