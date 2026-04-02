CXX = g++
CXXFLAGS = -Wall -Wextra -Iinc -MMD -MP -std=c++23
LDFLAGS = -lreadline -lncurses
SRC = $(wildcard src/*.cpp)
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
