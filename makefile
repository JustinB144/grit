# Compiler 
CXX = g++ 

# Compiler Flags
CXXFLAGS = -std=c++20 -Wall -Wextra -pedantic -g -fsanitize=address,undefined -MMD

# Target
TARGET = grit

# Compile with flags
COMPILE = $(CXX) $(CXXFLAGS)

# Executable
grit: grit.cpp
	$(COMPILE) grit.cpp -o grit

# Include the generated dependancy files if they exist
-include grit.d
# Declare phony
.PHONY: clean

# clean out executable
clean:
	rm -f grit grit.d
