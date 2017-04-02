INC = -I../include/
CC = g++ -g -Wall
LIB = -lpthread -lrt
BIN = libevent
INCLUDE = ../include/

OBJ = mainsvrd.o \
$(INCLUDE)log.o \
$(INCLUDE)minheap.o \
$(INCLUDE)signal.o \
$(INCLUDE)buffer.o \
$(INCLUDE)evbuffer.o \
$(INCLUDE)epoll.o \
$(INCLUDE)event.o \
$(INCLUDE)workqueue.o


all : $(BIN)

$(BIN) : ${OBJ}
	rm -f $@
	$(CC) -o $@ $(INC) $^ $(LIB)
	cp $(BIN) ../bin/
	chmod +x ../bin/*

%.o : %.cpp
	$(CC) $(INC) -c -o $@ $<

clean :
	rm -f ${OBJ} ${BIN}

	
