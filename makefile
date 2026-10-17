# Compiler 
CXX = g++ 

# Compiler Flags
CXXFLAGS = -std=c++20 -Wall -Wextra -pedantic -g -fsanitize=address,undefined 

# Target
TARGET = grit

# Compile with flags
COMPILE = $(CXX) $(CXXFLAGS)

# Executable
grit: grit.cpp
	$(COMPILE) grit.cpp -o grit

# Declare phony
.PHONY: clean

# clean out executable
clean:
	rm -f grit
