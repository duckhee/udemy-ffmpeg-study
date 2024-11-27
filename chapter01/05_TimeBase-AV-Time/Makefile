CC = gcc

OBJDUMP = objdump

SRCS = main.c

TARGET = chapter0105TimeBaseAVTime

BUILD_DIR = buildDir

COBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

C_EXTRA_FLAGS :=

CFLAGS = -W -Wall -pedantic-errors $(C_EXTRA_FLAGS)

C_EXTRA_LIBS :=
C_EXTRA_LIBS += avcodec
C_EXTRA_LIBS += avutil
C_EXTRA_LIBS += avformat
C_EXTRA_LIBS += swscale

CLIBS = `pkg-config --cflags --libs libavcodec libavutil libavformat libswscale`

all: clean createdirs pre assemble dump build

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

clean:
	rm -rf ${BUILD_DIR}

.SUFFIXES: .c .i .s .o .dump
.PHONNY: clean createdirs pre assemble dump build