module.exports = grammar({
  name: 'yuga',

  // `if` is both a statement and an expression (`let x = if c { a } else { b }`);
  // the two parses produce identical `if_statement` subtrees, so either choice
  // highlights the same way.
  conflicts: $ => [
    [$._statement, $._expr_no_struct],
  ],

  extras: $ => [
    /\s/,
    $.comment,
  ],

  word: $ => $.identifier,

  rules: {
    source_file: $ => repeat($._item),

    _item: $ => choice(
      $.import_item,
      $.function_item,
      $.struct_item,
      $.enum_item,
      $.let_statement,
    ),

    import_item: $ => seq(
      'import',
      choice($.identifier, $.string),
      optional(';'),
    ),

    function_item: $ => seq(
      'fn',
      field('name', $.identifier),
      field('parameters', $.parameters),
      optional(seq('->', field('return_type', $._type))),
      field('body', $.block),
    ),

    parameters: $ => seq(
      '(',
      optional(seq($.parameter, repeat(seq(',', $.parameter)), optional(','))),
      ')',
    ),

    parameter: $ => seq(
      field('name', $.identifier),
      ':',
      field('type', $._type),
    ),

    struct_item: $ => seq(
      'struct',
      field('name', $.identifier),
      '{',
      optional(seq($.field_declaration, repeat(seq(',', $.field_declaration)), optional(','))),
      '}',
    ),

    enum_item: $ => seq(
      'enum',
      field('name', $.identifier),
      '{',
      optional(seq($.enum_variant, repeat(seq(',', $.enum_variant)), optional(','))),
      '}',
    ),

    enum_variant: $ => seq(
      field('name', $.identifier),
      optional(seq('=', field('value', choice($.number, $.unary_expression)))),
    ),

    field_declaration: $ => seq(
      field('name', $.identifier),
      ':',
      field('type', $._type),
    ),

    _type: $ => choice(
      $.reference_type,
      $.box_type,
      $.array_type,
      $.identifier,
    ),

    reference_type: $ => seq('&', optional('mut'), $._type),

    box_type: $ => seq('Box', '<', $._type, '>'),

    array_type: $ => seq('[', $.number, ']', $._type),

    block: $ => seq('{', repeat($._statement), '}'),

    _statement: $ => choice(
      $.let_statement,
      $.if_statement,
      $.for_statement,
      $.return_statement,
      $.assignment_statement,
      $.expression_statement,
    ),

    let_statement: $ => seq(
      'let',
      optional('mut'),
      field('name', $.identifier),
      optional(seq(':', $._type)),
      '=',
      field('value', $._expression),
      optional(';'),
    ),

    if_statement: $ => prec.right(seq(
      'if',
      field('condition', $._condition),
      field('consequence', $.block),
      optional(seq('else', field('alternative', choice($.if_statement, $.block)))),
    )),

    for_statement: $ => seq(
      'for',
      field('variable', $.identifier),
      'in',
      field('iterable', $._condition),
      field('body', $.block),
    ),

    return_statement: $ => prec.right(seq(
      'return',
      optional($._expression),
      optional(';'),
    )),

    assignment_statement: $ => seq(
      $._expression,
      choice('=', '+=', '-=', '*=', '/='),
      $._expression,
      optional(';'),
    ),

    expression_statement: $ => seq($._expression, optional(';')),

    // Conditions cannot start a struct literal (`if x {` is a block).
    _condition: $ => $._expr_no_struct,

    _expression: $ => choice(
      $.struct_literal,
      $._expr_no_struct,
    ),

    _expr_no_struct: $ => choice(
      $.binary_expression,
      $.unary_expression,
      $.call_expression,
      $.index_expression,
      $.field_expression,
      $.path_expression,
      $.array_literal,
      $.parenthesized_expression,
      $.if_statement,
      $.identifier,
      $.number,
      $.string,
      $.boolean,
    ),

    parenthesized_expression: $ => seq('(', $._expression, ')'),

    unary_expression: $ => prec(7, choice(
      seq('&', optional('mut'), $._expr_no_struct),
      seq('*', $._expr_no_struct),
      seq('!', $._expr_no_struct),
      seq('-', $._expr_no_struct),
    )),

    binary_expression: $ => {
      const ops = [
        [1, '..'],
        [2, '||'],
        [3, '&&'],
        [4, choice('==', '!=', '<', '>', '<=', '>=')],
        [5, choice('+', '-')],
        [6, choice('*', '/', '%')],
      ];
      return choice(...ops.map(([p, op]) =>
        prec.left(p, seq(
          field('left', $._expr_no_struct),
          field('operator', op),
          field('right', $._expr_no_struct),
        ))
      ));
    },

    call_expression: $ => prec(8, seq(
      field('function', $._expr_no_struct),
      field('arguments', $.arguments),
    )),

    arguments: $ => seq(
      '(',
      optional(seq($._expression, repeat(seq(',', $._expression)), optional(','))),
      ')',
    ),

    field_expression: $ => prec(8, seq(
      field('value', $._expr_no_struct),
      '.',
      field('field', $.identifier),
    )),

    path_expression: $ => prec(8, seq(
      field('value', $._expr_no_struct),
      '::',
      field('name', $.identifier),
    )),

    index_expression: $ => prec(8, seq(
      field('value', $._expr_no_struct),
      '[',
      field('index', $._expression),
      ']',
    )),

    struct_literal: $ => prec.dynamic(-1, seq(
      field('type', $.identifier),
      '{',
      optional(seq($.field_initializer, repeat(seq(',', $.field_initializer)), optional(','))),
      '}',
    )),

    field_initializer: $ => seq(
      field('name', $.identifier),
      ':',
      field('value', $._expression),
    ),

    array_literal: $ => seq(
      '[', $.number, ']', $._type,
      '{',
      optional(seq($._expression, repeat(seq(',', $._expression)), optional(','))),
      '}',
    ),

    boolean: _ => choice('true', 'false'),

    identifier: _ => /[a-zA-Z_][a-zA-Z0-9_]*/,

    number: _ => /[0-9]+/,

    string: _ => seq(
      '"',
      repeat(choice(
        token.immediate(prec(1, /[^"\\]+/)),
        token.immediate(/\\./),
      )),
      '"',
    ),

    comment: _ => token(choice(
      seq('//', /[^\n]*/),
      seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/'),
    )),
  },
});
