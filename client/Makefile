TARGET = pspload
OBJS = pspload.o

CC = gcc
CFLAGS = -O2
LIBS =


.SUFFIXES:
all: $(OBJS)
	$(CC) $< -o $(TARGET)

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -r -f $(OBJS) $(TARGET)
	
