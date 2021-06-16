# mlxdevmlib
Mellanox device management C library

## How to use it?
### compile library

$ cd lib
$ ./autogen.sh && ./configure --prefix=/usr && make -j 8 && make install

### compile test program

$ cd test
$ make all

### how to run test progrm?

$ test/mlxdevm_test mlxdevm pci 0000:03:00.0
