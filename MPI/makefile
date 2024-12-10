CFLAGS= -Wall -lm
SRC_DIR= src
BUILD_DIR = build
OUT_DIR= output

SRC_FILES= $(wildcard $(SRC_DIR)/*.c)
TARGET= $(BUILD_DIR)/main.out

all:
	mkdir -p $(BUILD_DIR)
	mkdir -p $(OUT_DIR)
	mpicc $(CFLAGS) -o $(TARGET) $(SRC_FILES)

clean:
	rm -rf $(BUILD_DIR)

clean-all:
	rm -rf $(BUILD_DIR)
	rm -rf $(OUT_DIR)