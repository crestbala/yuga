; Yuga syntax highlighting for Zed — node types must match the compiled grammar.

(comment) @comment

[
  "fn"
  "let"
  "mut"
  "struct"
  "import"
  "if"
  "else"
  "for"
  "in"
  "return"
] @keyword

[
  "true"
  "false"
] @boolean

(number) @number
(string) @string
(identifier) @variable

(function_item
  name: (identifier) @function)

(parameter
  name: (identifier) @variable.parameter)

(let_statement
  name: (identifier) @variable)

(field_declaration
  name: (identifier) @property)

(field_initializer
  name: (identifier) @property)

(call_expression
  function: (identifier) @function)

(call_expression
  function: (field_expression
    field: (identifier) @function))

(call_expression
  function: (path_expression
    name: (identifier) @function))

(struct_item
  name: (identifier) @type)

(struct_literal
  type: (identifier) @type)

(reference_type) @type
(box_type) @type
(array_type) @type
"Box" @type.builtin

[
  "&"
  "*"
  "!"
  "-"
  "+"
  "/"
  "%"
  "="
  "+="
  "-="
  "*="
  "/="
  "=="
  "!="
  "<"
  ">"
  "<="
  ">="
  "&&"
  "||"
  ".."
  "->"
  "::"
] @operator

[
  "("
  ")"
  "{"
  "}"
  "["
  "]"
] @punctuation.bracket

[
  ","
  ":"
  ";"
  "."
] @punctuation.delimiter
