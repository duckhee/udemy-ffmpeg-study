CC = gcc

OBJDUMP = objdump

SRCS = main.c

BUILD_DIR = buildDir

TARGET = chapter0103DumpFormatFFMPEG

COBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

C_EXTRA_FLAGS :=

C_FLAGS = -W -Wall -pedantic-errors $(C_EXTRA_FLAGS)

# link library
C_EXTRA_LIBS :=
C_EXTRA_LIBS += avcodec
C_EXTRA_LIBS += avutil
C_EXTRA_LIBS += avformat
C_EXTRA_LIBS += swscale


#CLIBS = $(patsubst %,-l%,$(C_EXTRA_LIBS))
CLIBS = `pkg-config --cflags --libs libavformat libavcodec libavutil libswscale`

all: clean createdirs pre assemble dump build

createdirs:
	$(shell mkdir ${BUILD_DIR})

pre: $(SRCS)
	$(CC) $(C_FLAGS) $(CLIBS) -E $< -o $(BUILD_DIR)/$(patsubst %.c,%.i,$<)

assemble: $(SRCS)
	$(CC) $(C_FLAGS) $(CLIBS) -S $< -o $(BUILD_DIR)/$(patsubst %.c,%.s,$<)

$(COBJS): $(BUILD_DIR)/%.o : %.c
	$(CC) $(C_FLAGS) $(CLIBS) -o $@ -c $<

dump: $(COBJS)
	$(OBJDUMP) -S $< > $(patsubst %.o,%.dump,$<)

build: $(COBJS)
	$(CC) $(C_FLAGS) $(CLIBS) -o $(BUILD_DIR)/$(TARGET) $^

clean:
	rm -rf ${BUILD_DIR}

.SUFFIXES: .c .i .s .o .dump
.PHONNY: clean createdirs pre assemble dump build