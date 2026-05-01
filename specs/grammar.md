# Грамматика языка MyLang (EBNF)

## Обозначения

```
=       определение
|       альтернатива
( )     группировка
[ ]     ноль или один раз (опционально)
{ }     ноль или более раз
"x"     конкретный символ
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
float_lit = digit { digit } "." { digit }
```

### Строковые литералы

```
string_lit = { любой символ кроме '"' и '\n' } 
char_lit = "'" {любой символ кроме "'" и '\n' длины 1} "'" 
```

### Булевы литералы

```
bool_lit = "true" | "false"
```

### Ключевые слова

```
const | struct | type | namespace
if | else | while | break | continue | return
auto | import | export | class | cast 
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
+   -   *   /   %   ++  --
==  !=  <   >   <=  >=
&&  ||  ! ^
=
.   ::
(   )   {   }   [   ]
;   :   ,
```

---

## Синтаксис

### Программа

```
program = { import_decl } { top_decl }
```

### Импорт модулей

```
import_decl = ("import"  '"' string_lit '"' | "import" '<' string_lit '>') 
```

Импорт должен находиться в начале файла, до любых объявлений.
Случай с '"' импорт файла на таком же языке, случай с '<' импорт C библиотеки

Пример:
```
import "math.lang"
import "utils.lang"
import <stdio.h>
```

### Объявления верхнего уровня

```
top_decl = [ "export" ] ( var_decl
                        | func_decl
                        | struct_decl
                        | type_alias
                        | class_decl
                        | namespace_decl )
```

`export` делает объявление видимым для других модулей. Без `export` объявление доступно только внутри текущего файла.

### Объявление переменной

```
var_decl = [ "const" ] type iden [ "=" expr ] { "," iden [ "=" expr ] } ";"
         | [ "const" ] "auto" iden "=" expr { "," iden "=" expr } ";"
```

При использовании `auto` тип выводится из выражения инициализации. `auto` требует обязательного инициализатора.

Примеры:
```
int x = 5;
int y = 3, z = 4;
const int MAX = 100;
float ratio = 0.5;
auto name = "hello";       // выведен тип string
auto flag = x > 0;         // выведен тип bool
const auto PI = 3.14;      // выведен тип float, переменная константна
```

### Объявление функции

```
func_decl = type iden "(" [ param_list ] ")" "{" block "}"  
param_list = param { "," param }
param      = [ "const" ] ( type | "auto" ) iden ["=" expr]   //  Возможно значение по умолчанию
```

Примеры:
```
int add(int a, int b) { 
    return a + b;
}
void print_sum(int a, int b) { ... }
float point(int a = 3, int b) { ... }
```

### Объявление структуры

```
struct_decl = "struct" iden "{" { struct_field } "}"
struct_field = [ "const" ] ( type | "auto" ) iden ["=" expr] { "," iden [ "=" expr ] } ";"
```

Пример:
```
struct Point {
    int x = 3;  //  Значение по умолчанию
    int y;
}
```
### Объявление класса

```
class_decl = "class" class_name "{" { 
    | {struct_field} 
    | [class_name "(" [param_list] ")" "{" block "}"]
    | {struct_decl}
    | {func_decl}
    | ["~" class_name "()" "{" block "}"]
} "}"

class_name = iden
```
Пример:
```
class MyClass {
    int x;
    int y = 4;

    MyClass(int val){
        x = val;
    }

    struct Elem {
        float der;
        string fer = 8;
    }

    int method(int a, int b = 7){
        return (a + b) / x;
    }

    ~MyClass() {}
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
     | dyn_array_type
     | iden             (имя структуры или синонима типа)

builtin_type = "int"   | "uint"   | "float" | "bool"
             | "string" | "char"  | "void"
             | "int8"  | "int16"  | "int32" | "int64"
             | "uint8" | "uint16" | "uint32"| "uint64"
             | "float32" | "float64"

array_type     = type "[" expr "]"  (фиксированный массив)
dyn_array_type = type "[]" {"[]"}   (динамический массив)
```

Примеры типов:
```
int
float64
string
int[10]         // фиксированный массив из 10 int
float[size]     // фиксированный массив из size элементов float
int[]           // динамический массив int
string[]        // динамический массив строк
int[][]         // двойной динамический массив(динамическая матрица)
int[3][4]       // массив размера 3 у которого элементы это массивы размера 4
int[][2]        // динамический массив хранящий пары
int[][][]       // тройной массив
Point           // структурный тип
Point[]         // массив хранящий структуры
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
     | "{" block "}"
```

#### Присваивание

```
assign_stmt = lvalue ( "=" | "+=" | "-=" | "*=" | "/=" | "%=" ) expr ";"

lvalue = iden
       | lvalue "." iden
       | lvalue "[" expr "]"
       | lvalue
```

#### Ветвление

```
if_stmt = "if" "(" expr ")" "{" block "}" [ "else" ( if_stmt | block ) ]
```

Примеры:
```
if (x > 0) { ... }
if (x > 0) { ... } else { ... }
if (x > 0) { ... } else if (x < 0) { ... } else { ... }
```

#### Цикл

```
while_stmt = "while" "(" expr ")" "{" block "}"
```

#### Возврат

```
return_stmt = "return" [ expr ] ";"
```

#### Выражение как инструкция

```
expr_stmt = expr ";"
```

Допускается вызов функции:
```
print(x);
```

#### Блок

```
block = { stmt }
```

---

### Выражения

Приоритет операторов (от низшего к высшему):

| Уровень |                Операторы            | Ассоциативность |
|---------|-------------------------------------|-----------------|
|    1    |                  `\|\|`             |      левая      |
|    2    |                   `&&`              |      левая      |
|    3    |                `==` `!=`            |      левая      |
|    4    |            `<` `>` `<=` `>=`        |      левая      |
|    5    |                `+` `-`              |      левая      |
|    6    |              `*` `/` `%`            |      левая      |
|    7    |           унарные `!` `-`           |      правая     |
|    8    | постфиксные `.` `[]` `()` `++` `--` |      левая      |

```
expr     = or 
or       = and { "||" and }
and      = equality { "&&" equality }
equality = compare { ( "==" | "!=" ) compare }
compare  = add { ( "<" | ">" | "<=" | ">=" ) add }
add      = mul { ( "+" | "-" ) mul }
mul      = power { ( "*" | "/" | "%" ) power }
power    = unary [ "^" power ]
unary    = ( "+" | "-" | "!" ) unary | postfix
postfix  = primary { "." iden | "[" expr "]" | "(" [ arg_list ] ")" | "++" | "--" }

primary  = int_lit
         | float_lit
         | '"' string_lit '"'
         | bool_lit
         | iden                                    (простой идентификатор — если после iden нет "::" и "{")
         | "null"
         | iden {"::" iden}                        (доступ к namespace — разрешается по "::" после iden)
         | iden {"::" iden} "{" [ field_init_list ] "}" (литерал структуры — разрешается по "{" после iden)
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
(x - y) + z^2
"Hello" + "world!"
arr[i]
p.x
add(1, 2)
Point { x: 1, y: 2 }
ClassName(3, 4)
[1, 2, 3]
cast<float>(x)
Math::PI_INT
Math::Add::PI_FLOAT
arr[i].method(2)
method()[0]
func(x).method(y)
```
