# this is makefile of torus

VPATH=.
RTREE_LIB=./deps/spatialindex/lib
RTREE_INCLUDE=./deps/spatialindex/include
GSL_LIB=./deps/gsl/lib
GSL_INCLUDE=./deps/gsl/include
SOCKETDIR=./socket
TORUSDIR=./torus_node
CONTROLDIR=./control_node
SKIPLISTDIR=./skip_list
LOGDIR=./logs
TESTDIR=./test
BIN=./bin
CC=gcc
CXX=g++
CFLAGS= -lrt -Wall

all: control start-node test1

control: control.o torus-node.o socket.o skip-list.o log.o
	$(CC) $(CFLAGS) -o $(BIN)/$@ $(BIN)/control.o $(BIN)/torus-node.o $(BIN)/socket.o $(BIN)/skip-list.o $(BIN)/log.o 

start-node: torus-node.o server.o socket.o skip-list.o log.o torus_rtree.o
	$(CXX) $(CFLAGS) $(BIN)/torus-node.o $(BIN)/server.o $(BIN)/socket.o $(BIN)/skip-list.o $(BIN)/log.o $(BIN)/torus_rtree.o -o $(BIN)/$@ -L$(GSL_LIB) -L$(RTREE_LIB) -lspatialindex -lspatialindex_c -lgsl -lgslcblas -pthread

control.o:
	$(CC) $(CFLAGS) -o $(BIN)/$@ -c $(CONTROLDIR)/control.c -I$(VPATH)

torus-node.o:
	$(CC) $(CFLAGS) -o $(BIN)/$@ -c $(TORUSDIR)/torus_node.c -I$(VPATH)
	
socket.o:
	$(CC) $(CFLAGS) -o $(BIN)/$@ -c $(SOCKETDIR)/socket.c -I$(VPATH)

skip-list.o:
	$(CC) $(CFLAGS) -o $(BIN)/$@ -c $(SKIPLISTDIR)/skip_list.c -I$(VPATH)
	
torus_rtree.o:
	$(CXX) $(CFLAGS) -o $(BIN)/$@ -c $(TORUSDIR)/torus_rtree.c -I$(VPATH) -I$(RTREE_INCLUDE)

server.o:
	$(CXX) $(CFLAGS) -o $(BIN)/$@ -c $(TORUSDIR)/server.c -I$(VPATH) -I$(RTREE_INCLUDE) -I$(GSL_INCLUDE)
	
log.o:
	$(CC) $(CFLAGS) -o $(BIN)/$@ -c $(LOGDIR)/log.c -I$(VPATH)

test1: socket.o test1.o
	$(CC) $(CFLAGS) -o $(BIN)/$@ $(BIN)/socket.o $(BIN)/test1.o

test1.o: 
	$(CC) $(CFLAGS) -o $(BIN)/$@ -c test.c -I$(VPATH) 
	
.PHONY: clean
clean:
	-rm $(BIN)/*.o
	-rm $(BIN)/control $(BIN)/start-node $(BIN)/test1




