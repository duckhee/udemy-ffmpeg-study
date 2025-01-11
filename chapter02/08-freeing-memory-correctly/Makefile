CC = gcc

OBJDUMP = objdump

SRCS = main.c

TARGET = chapter0208FreeingMemoryCorrectly

BUILD_DIR = buildDir

COBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

C_EXTRA :=
C_EXTRA +=

C_EXTRA_LIB :=
C_EXTRA_LIB +=

CLIBS:=
CLIBS += `pkg-config --cflags --libs libavformat libavcodec libavutil libswscale`


CFLAGS:=
CFLAGS += -W -Wall -pedantic-errors $(C_EXTRA)

all: clean createdirs pre assemble dump build

clean:
	rm -rf ${BUILD_DIR}

createdirs:
	$(shell mkdir ${BUILD_DIR})

pre: $(SRCS)
	$(CC) $(CFLAGS) $(CLIBS) -E $< -o $(BUILD_DIR)/$(patsubst %.c,%.i,$<)

assemble: $(SRCS)
	$(CC) $(CFLAGS) $(CLIBS) -S $< -o $(BUILD_DIR)/$(patsubst %.c,%.s,$<)

$(COBJS): $(BUILD_DIR)/%.o : %.c
	$(CC) $(CFLAGS) $(CLIBS) -o $@ -c $<

dump: $(COBJS)
	$(OBJDUMP) -S $< > $(patsubst %.o,%.dump,$<)

build: $(COBJS)
	$(CC) $(CFLAGS) $(CLIBS) -o $(BUILD_DIR)/$(TARGET) $^

.SUFFIXES: .c .i .s .o .dump
.PHONNY: clean createdirs pre assemble dump build