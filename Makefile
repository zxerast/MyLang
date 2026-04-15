CXX = g++
CXXFLAGS = -Wall -Wextra -Iinc -I/usr/lib/llvm-18/include -MMD -MP -std=c++23
LDFLAGS = -L/usr/lib/llvm-18/lib -lclang -lreadline -lncurses
SRC = $(wildcard src/*.cpp)
OBJ = $(SRC:src/%.cpp=obj/%.o)
DEP = $(OBJ:%.o=%.d)
NAME = lang

.PHONY: all clean fclean re

all: $(NAME) runtime/runtime.o

$(NAME): $(OBJ)
	$(CXX) $(OBJ) -o $(NAME) $(LDFLAGS)

runtime/runtime.o: runtime/runtime.asm
	nasm -f elf64 $< -o $@

obj/%.o: src/%.cpp
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf obj

fclean: clean
	rm -f $(NAME) runtime/runtime.o

re: fclean all

-include $(DEP)
