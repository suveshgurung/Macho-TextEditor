# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -pedantic -std=c99

# Target executable
TARGET = macho

# Source files
SRC = macho.c

# Object files
OBJS = $(SRC:.c=.o)

# Default Target
all: $(TARGET)

# Rule to link the object files to create the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Rule to compile the source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean rule to remove the generated files
clean:
	rm -f $(TARGET) $(OBJS)
