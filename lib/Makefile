EXT_LIBS_FLAGS=-DHAVE_LIBMNL
CFLAGS+=-I./include/uapi
CFLAGS+=-I./include/uapi/mlxdevm
CFLAGS+=-I./include/uapi/linux 
EXT_LIBS+=-lmnl
LIB_SRCS=mlxdevm.c libnetlink.c mnl_utils.c

mlxdevmlib.a: mlxdevm.c libnetlink.c mnl_utils.c
	gcc -Wall -O -c $(CFLAGS) $(EXT_LIBS_FLAGS) $(EXT_LIBS) $(LIB_SRCS)
	ar rcs mlxdevmlib.a mlxdevm.o libnetlink.o mnl_utils.o
	ranlib mlxdevmlib.a

clean:
	rm -rf mlxdevmlib.a *.o
