# Init path variables
DEVROOT := $(realpath ../)

PROTOSDIR ?= $(DEVROOT)/proto
GRPC_HOME ?=/usr/local/grpc

PROTOC ?= $(GRPC_HOME)/bin/protoc
GRPC_CPP_PLUGIN ?= $(GRPC_HOME)/bin/grpc_cpp_plugin
GRPC_PYTHON_PLUGIN ?= $(GRPC_HOME)/bin/grpc_python_plugin

PBUFH := ./src/switchml.pb.h ./src/switchml.grpc.pb.h
PBUFC := ./src/switchml.pb.cc ./src/switchml.grpc.pb.cc
PBUF := $(PBUFH) $(PBUFC)

CXX_BUILDDIR ?= $(DEVROOT)/csrc
PY_BUILDDIR ?= $(DEVROOT)/controller
# Targets

.PHONY: all
all: cpp python

.PHONY: cpp
cpp: $(PROTOSDIR)/flashreduce.proto $(CXX_BUILDDIR)
	LD_LIBRARY_PATH=$(GRPC_HOME)/lib $(PROTOC) -I $(PROTOSDIR) --cpp_out=$(CXX_BUILDDIR) --grpc_out=$(CXX_BUILDDIR) --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN) $<

.PHONY: python
python: $(PROTOSDIR)/flashreduce.proto $(PY_BUILDDIR)
	LD_LIBRARY_PATH=$(GRPC_HOME)/lib $(PROTOC) -I $(PROTOSDIR) --python_out=$(PY_BUILDDIR) --grpc_python_out=$(PY_BUILDDIR) --plugin=protoc-gen-grpc_python=$(GRPC_PYTHON_PLUGIN) $<

.PHONY: clean
clean:
	$(RM) $(CXX_BUILDDIR)/flashreduce*.pb.*
	$(RM) $(CXX_BUILDDIR)/flashreduce*_pb2*
	$(RM) $(PY_BUILDDIR)/flashreduce*.py
	$(RM) $(PY_BUILDDIR)/flashreduce*_pb2*
