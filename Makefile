CXX = g++
CXXFLAGS = -Wall -Wextra -Iinc -I/usr/lib/llvm-18/include -MMD -MP -std=c++23
LDFLAGS = -L/usr/lib/llvm-18/lib -lclang -lreadline -lncurses
SRC = $(wildcard src/*.cpp)
OBJ = $(SRC:src/%.cpp=obj/%.o)
DEP = $(OBJ:%.o=%.d)
RUNTIME_ASM = $(wildcard runtime/*.asm)
RUNTIME_OBJ = $(RUNTIME_ASM:%.asm=%.o)
NAME = lang

.PHONY: all clean fclean re

all: $(NAME) $(RUNTIME_OBJ)

$(NAME): $(OBJ)
	$(CXX) $(OBJ) -o $(NAME) $(LDFLAGS)

runtime/%.o: runtime/%.asm
	nasm -f elf64 $< -o $@

obj/%.o: src/%.cpp
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf obj

fclean: clean
	rm -f $(NAME) $(RUNTIME_OBJ)

re: fclean all

-include $(DEP)
