EXT_LIBS_FLAGS=-DHAVE_LIBMNL
CFLAGS+=-I../lib/include/uapi/mlxdevm
CFLAGS+=-I../lib/include/uapi/linux 
CFLAGS+=-I../lib
EXT_LIBS+=-lmnl

all: ../lib/mlxdevmlib.a
	gcc -o mlxdevm_add_test $(CFLAGS) $(EXT_LIBS_FLAGS) $(EXT_LIBS) \
		add.c ../lib/mlxdevmlib.a
	gcc -o mlxdevm_param_test $(CFLAGS) $(EXT_LIBS_FLAGS) $(EXT_LIBS) \
		param.c ../lib/mlxdevmlib.a
	gcc -o mlxdevm_state_test $(CFLAGS) $(EXT_LIBS_FLAGS) $(EXT_LIBS) \
		state.c ../lib/mlxdevmlib.a

clean:
	rm -rf mlxdevm_test *.o
