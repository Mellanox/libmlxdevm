ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libmlxdevm.la

libmlxdevm_ladir = $(includedir)/

libmlxdevm_la_CFLAGS = -Wall -I./include/uapi \
		       -I./include/uapi/mlxdevm \
		       -I./include/uapi/linux

libmlxdevm_la_LDFLAGS = -lmnl -DHAVE_LIBMNL

libmlxdevm_la_HEADERS = mlxdevm.h libnetlink.h mnl_utils.h
libmlxdevm_la_SOURCES = mlxdevm.c libnetlink.c mnl_utils.c
