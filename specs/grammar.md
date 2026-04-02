# Грамматика языка MyLang (EBNF)

## Обозначения

```
=       определение
|       альтернатива
( )     группировка
[ ]     ноль или один раз (опционально)
{ }     ноль или более раз
"x"     терминал (буквально)
```

---

## Лексика

### Алфавит и пробельные символы

```
whitespace = " " | "\t" | "\r" | "\n"
```

Пробельные символы игнорируются везде между токенами.

### Комментарии

```
comment = "//" { любой символ кроме "\n" } "\n"
```

Комментарии в поток токенов не передаются.

### Идентификаторы

```
letter    = "a".."z" | "A".."Z" | "_"
digit     = "0".."9"
iden      = letter { letter | digit }
```

Идентификатор не может совпадать с ключевым словом или именем типа.

### Числовые литералы

```
int_lit   = digit { digit }
float_lit = digit { digit } "." digit { digit }
```

### Строковые литералы

```
string_lit = '"' { любой символ кроме '"' и '\n' } '"'
```

### Булевы литералы

```
bool_lit = "true" | "false"
```

### Ключевые слова

```
const | struct | type | namespace
if | else | while | break | continue | return
```

### Имена типов (зарезервированы)

```
int | uint | float | bool | string | void | char
int8 | int16 | int32 | int64
uint8 | uint16 | uint32 | uint64
float32 | float64
```

### Операторы и разделители

```
+   -   *   /   %   ++  --  ^
==  !=  <   >   <=  >=
&&  ||  !
=
.   ->
(   )   {   }   [   ]
;   :   ,
```

---

## Синтаксис

### Программа

```
program = { top_decl }
```

### Объявления верхнего уровня

```
top_decl = var_decl
         | func_decl
         | struct_decl
         | type_alias
         | namespace_decl
```

### Объявление переменной

```
var_decl = [ "const" ] type iden "=" expr ";"
```

Примеры:
```
int x = 5;
const int MAX = 100;
float ratio = 0.5;
```

### Объявление функции

```
func_decl = type iden "(" [ param_list ] ")" block
          | "void" iden "(" [ param_list ] ")" block

param_list = param { "," param }
param      = type iden
```

Примеры:
```
int add(int a, int b) { return a + b; }
void print_sum(int a, int b) { ... }
```

### Объявление структуры

```
struct_decl = "struct" iden "{" { struct_field } "}"
struct_field = type iden ";"
```

Пример:
```
struct Point {
    int x;
    int y;
}
```

### Синоним типа

```
type_alias = "type" iden "=" type ";"
```

Пример:
```
type Meters = int;
```

### Пространство имён

```
namespace_decl = "namespace" iden "{" { top_decl } "}"
```

Пример:
```
namespace Math {
    int PI_INT = 3;
    int square(int x) { return x * x; }
}
```

Доступ к элементу: `Math::square(5)`

---

### Типы

```
type = builtin_type
     | array_type
     | iden             (имя структуры или синонима типа)

builtin_type = "int"   | "uint"   | "float" | "bool"
             | "string" | "char"  | "void"
             | "int8"  | "int16"  | "int32" | "int64"
             | "uint8" | "uint16" | "uint32"| "uint64"
             | "float32" | "float64"

array_type = "[" type ";" int_lit "]"
```

Примеры типов:
```
int
float64
[int; 10]
Point
```

---

### Инструкции

```
stmt = var_decl
     | assign_stmt
     | if_stmt
     | while_stmt
     | "break" ";"
     | "continue" ";"
     | return_stmt
     | expr_stmt
     | ";"
     | block
```

#### Присваивание

```
assign_stmt = lvalue "=" expr ";"

lvalue = iden
       | lvalue "." iden
       | lvalue "[" expr "]"
```

#### Ветвление

```
if_stmt = "if" expr block [ "else" ( if_stmt | block ) ]
```

Примеры:
```
if x > 0 { ... }
if x > 0 { ... } else { ... }
if x > 0 { ... } else if x < 0 { ... } else { ... }
```

#### Цикл

```
while_stmt = "while" expr block
```

#### Возврат

```
return_stmt = "return" [ expr ] ";"
```

#### Выражение как инструкция

```
expr_stmt = expr ";"
```

Допускается только вызов функции:
```
print(x);
```

#### Блок

```
block = "{" { stmt } "}"
```

---

### Выражения

Приоритет операторов (от низшего к высшему):

| Уровень | Операторы | Ассоциативность |
|---------|-----------|-----------------|
| 1 | `\|\|` | левая |
| 2 | `&&` | левая |
| 3 | `==` `!=` | левая |
| 4 | `<` `>` `<=` `>=` | левая |
| 5 | `+` `-` | левая |
| 6 | `*` `/` `%` | левая |
| 7 | унарные `!` `-` | правая |
| 8 | постфиксные `.` `[]` `()` | левая |

```
expr     = or
or       = and { "||" and }
and      = equality { "&&" equality }
equality = compare { ( "==" | "!=" ) compare }
compare  = add { ( "<" | ">" | "<=" | ">=" ) add }
add      = mul { ( "+" | "-" ) mul }
mul      = unary { ( "*" | "/" | "%" ) unary }
unary    = ( "!" | "-" ) unary | postfix
postfix  = primary { "." iden | "[" expr "]" | "(" [ arg_list ] ")" }

primary  = int_lit
         | float_lit
         | string_lit
         | bool_lit
         | iden
         | iden "::" iden                          (доступ к namespace)
         | iden "{" [ field_init_list ] "}"        (литерал структуры)
         | "[" [ expr { "," expr } ] "]"           (литерал массива)
         | "(" expr ")"
         | cast_expr

cast_expr = "cast" "<" type ">" "(" expr ")"

arg_list        = expr { "," expr }
field_init_list = iden ":" expr { "," iden ":" expr }
```

Примеры выражений:
```
x + y * 2
arr[i]
p.x
add(1, 2)
Point { x: 1, y: 2 }
[1, 2, 3]
cast<float>(x)
Math::PI_INT
```
