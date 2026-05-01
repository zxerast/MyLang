CXX = g++
CXXFLAGS = -Wall -Wextra -Iinc -I/usr/lib/llvm-18/include -MMD -MP -std=c++23
LDFLAGS = -L/usr/lib/llvm-18/lib -lclang -lreadline -lncurses
SRC = src/main.cpp src/Lexer.cpp src/Parser.cpp src/Semantic.cpp src/CodeGen.cpp
OBJ = $(SRC:src/%.cpp=obj/%.o)
DEP = $(OBJ:%.o=%.d)

NAME = lang

.PHONY: all clean fclean re

all: $(NAME) 

$(NAME): $(OBJ)
	$(CXX) $(OBJ) -o $(NAME) $(LDFLAGS)

obj/%.o: src/%.cpp
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf obj

fclean: clean
	rm -f $(NAME) 

re: fclean all

-include $(DEP)
