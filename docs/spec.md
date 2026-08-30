# Yuga Language Specification

How-to and architecture: [yuga.md](yuga.md) (language), [zeus.md](zeus.md) (UI library),
[boundary.md](boundary.md) (one `yuga_rt`, bodyless boundary `fn`s).

Yuga is a statically-typed systems language. `yugac` is C11 + libc only and
**transpiles to C99**, then invokes `cc`.

## Syntax (ground truth)

```yuga
import "std:fmt"

struct Counter {
    name: string,
    count: int,
}

fn increment(c: &mut Counter) {
    c.count += 1
}

fn greet(c: &Counter) {
    fmt.println("hello,", c.name)
}

fn main() {
    let mut c = Counter { name: "tick", count: 0 }

    increment(&mut c)     // explicit mutable borrow
    increment(c)          // auto-borrow: &mut Counter
    greet(c)              // auto-borrow: &Counter

    fmt.println("count:", c.count)

    let b = Box::new(42)
    fmt.println("boxed:", *b)
}
```

## Design choices (v1)

| Question | Decision |
|---|---|
| Bindings | `let` immutable, `let mut` mutable. Ownership is still the safety mechanism. |
| Functions | `fn name(params) -> T { ... }` |
| Closures | `|x| { x + 1 }` as a value. Params between pipes; body is always `{ ... }`. Zero-arg: `|| { ... }`. Types are inferred from the expected `fn` type, or written `|x: int|`. Optional `-> T` after the pipes. Last expression is the return value. No `fn() { }` as a value (`fn` is only for named items). Type: `fn(T, U) -> R`. Captures Copy locals into a heap env; the `fn` value owns that env (not Copy). Dropped like `Box<T>`. May escape (return, store, pass). Host intern (`plat_intern_fn`) memcpy's the env so a stored handler outlives the `fn` value. |
| Tuples | Not a value type. `.child(a, b, c)` takes extra args (expanded to nested `.child` calls). |
| Generics | Functions and structs: `fn id<T>(x: T) -> T`, `struct Pair<T> { a: T, b: T }`. Monomorphized. Struct literals infer type args (`Pair { a: 1, b: 2 }`). |
| References | `&T` shared, `&mut T` exclusive. `&x` / `&mut x`. Deref: `*x`. |
| Auto-borrow | If a param is `&T` / `&mut T` and the caller passes an owned place, insert `&` / `&mut`. Checked like explicit borrows. |
| Heap | `Box::new(expr)` → owning `Box<T>`. `[]T` → owning growable array (`push` / `pop` / `.len`). Capturing closures → heap env. All freed at scope exit unless moved. |
| Imports | Quoted only: `import "std:fmt"` or `import "path/file.yuga"`. Symbols used as `name.symbol`. No glob imports. |
| Backend | Transpile to C99, then `cc -O2` (no unwind tables, dead-strip). |
| Strings | Fat pointer `{ptr, len}`, UTF-8. Copy. `s[i]` is the unsigned byte at `i` (traps out of range). Heap strings: `string_from_bytes([]int)`. |
| Integers | `int` is `int64_t`. `+ - *` trap on overflow. `/ %` trap on div-by-zero. Builtins: `wrapping_add`, `saturating_add`, `wrapping_shr` / `wrapping_shl` / `wrapping_or` / `wrapping_and`. |
| Floats | `float` is IEEE `double`. `+ - * /` are unchecked. `%` is not defined. `n as float` / `x as int` (trunc toward zero). Integer literals coerce when the expected type is `float` (`let x: float = 1`). |
| Booleans | `bool` is `true` / `false`. `&&` `||` `!`. `b as int` is 0 or 1. |
| Match | `match x { 0 \| 1 => { ... } _ => { ... } }`. Patterns: int/float/bool/string literals, or-patterns, `_`. Exhaustive: bool needs both arms or `_`; other types need `_`. |
| Loops | `for i in lo..hi { }`, `while cond { }`. `break` and `continue` (no labels). |
| Copy types | `int`, `float`, `bool`, `string`, `[N]T` if T is Copy, `&T`, structs whose fields are all Copy. Not Copy: `Box<T>`, `[]T`, `fn(...)`, `&mut T`, structs with a non-Copy field. |
| Borrow model | Statement-level temps; stored borrows (`let a = &mut x`) last until `a` leaves scope. Not NLL. |
| Semicolons | Optional. |
| Doc comments | `///` on the next `fn` / `struct` / `let` / field / import. `//!` is module docs (file top). Shown on LSP hover. `//` and `/* */` are ordinary comments. |
| Concurrency | Out of scope. |

## Imports

```
import_item = "import" STRING ;
```

`import "std:foo"` loads `std/foo.yuga` from the compiler std directory. The module name is `foo`.

`import "rel/path.yuga"` is relative to the importing file. The module name is the file stem (`header`, `math`).

Imported functions are called as `mod.fn(...)`. Imported module-level `let` bindings
are `mod.name` (a place: `counter.n += 1` is valid when `n` is `let mut`).

`fmt.write` / `fmt.write_int` / `fmt.write_bool` / `fmt.write_float` / `fmt.writeln` write to stdout with `write(2)`: length-based, no heap, no `printf`. `fmt.println(...)` is compile-time lowering to those writes (variadic mixed types are not a Yuga fn yet).

## Doc comments

```
//! Module documentation. Several `//!` lines join with newlines.
//! Shown when hovering the module name (`fmt` in `fmt.println`).

/// Item documentation. Several `///` lines join with newlines.
/// Put this immediately above `fn`, `struct`, `let`, `import`, or a struct field.
fn add(a: int, b: int) -> int {
    a + b
}
```

`////` is a normal comment, not a doc. Docs are for humans and `yuga-lsp`; they do not change types or codegen.

`import "std:maya"` is a small engine: 3D analytic spheres (integer world units `1024 = 1.0`) with orbit-camera mouse/scroll control, and optional top-down 2D discs (`maya.flat` / `name` / `orbit`). Scene, tracer, and map live in Yuga (`std/maya.yuga`, `std/mayacore/`); C is the host event loop and present. Scene data (planets, AU, periods) lives in the app. `sin`/`cos` use 1024-turns (result `-1024..1024`). On macOS, present is Cocoa 2D (no Metal). `MAYA_HEADLESS=1` updates once and exits.

## Grammar (EBNF)

```
program      = { inner_doc } { import_item } { item } ;
inner_doc    = "//!" { char } ;
outer_doc    = "///" { char } ;
item         = { outer_doc } ( fn_item | struct_item | let_stmt ) ;
import_item  = { outer_doc } "import" STRING ;
fn_item      = "fn" IDENT [ type_params ] "(" [ param { "," param } ] ")" [ "->" type ] block ;
type_params  = "<" IDENT { "," IDENT } ">" ;
param        = IDENT ":" type ;
struct_item  = "struct" IDENT [ type_params ] "{" [ field { "," field } [ "," ] ] "}" ;
field        = { outer_doc } IDENT ":" type ;
type         = "&" [ "mut" ] type | "Box" "<" type ">" | "[" NUMBER "]" type | "[" "]" type
             | "fn" "(" [ type { "," type } ] ")" [ "->" type ]
             | IDENT [ "<" type { "," type } ">" ] ;
block        = "{" { statement } "}" ;
statement    = if_stmt | for_stmt | while_stmt | match_stmt | return_stmt
             | break_stmt | continue_stmt | let_stmt | assign_stmt | expr_stmt ;
let_stmt     = "let" [ "mut" ] IDENT [ ":" type ] "=" expr ;
assign_stmt  = expr ( "=" | "+=" | "-=" | "*=" | "/=" ) expr ;
if_stmt      = "if" expr block [ "else" ( if_stmt | block ) ] ;
for_stmt     = "for" IDENT "in" expr block ;
while_stmt   = "while" expr block ;
match_stmt   = "match" expr "{" { match_arm } "}" ;
match_arm    = match_pat { "|" match_pat } "=>" block | "_" "=>" block ;
match_pat    = NUMBER | FLOAT | STRING | "true" | "false" ;
return_stmt  = "return" [ expr ] ;
break_stmt   = "break" ;
continue_stmt = "continue" ;
expr         = binary ;
unary        = ( "&" [ "mut" ] | "*" | "!" | "-" ) unary | postfix ;
postfix      = primary { "(" args ")" | "." IDENT | "::" IDENT | "[" expr "]" | "as" type } ;
primary      = IDENT [ "{" field_inits "}" ]
             | NUMBER | FLOAT | STRING | "true" | "false"
             | "[" NUMBER "]" type "{" [ args ] "}"
             | "[" "]" type "{" [ args ] "}"
             | "|" [ IDENT [ ":" type ] { "," IDENT [ ":" type ] } ] "|" [ "->" type ] block
             | "(" expr ")"
             | "(" expr "," expr { "," expr } [ "," ] ")" ;
field_inits  = IDENT ":" expr { "," IDENT ":" expr } [ "," ] ;
```

## Ownership state machine

States: `Owned`, `Borrowed` (shared count), `MutBorrowed`, `Moved`, `Dropped`.

```
Owned --copy_use--> Owned
Owned --move--> Moved
Owned --borrow_shared--> Borrowed
Owned --borrow_exclusive--> MutBorrowed
Borrowed --end_borrow--> Owned
MutBorrowed --end_borrow--> Owned
Owned --scope_exit--> Dropped     (free boxes)
Moved/Dropped --use--> Error
MutBorrowed + any other borrow --> Error
Borrowed + mut_borrow --> Error
use owner while MutBorrowed --> Error
return &local --> Error
```

## Safety

- Boxes: `malloc` + `free` at scope exit (value is always written before use); move NULLs the source.
- `[]T`: heap buffer `{ptr, len, cap}`. `push(v, x)` / `v.push(x)` grow it; `pop(v)` / `v.pop()` remove the last element (trap if empty). `v[i]` traps unless `0 <= i < v.len`. `.len` / `.cap` are readable fields (`[N]T` also has `.len`). Move transfers the buffer; drop frees it (and drops elements that own heap). A borrow `&[]T` is the slice.
- Bounds: `a[i]` traps unless the index is a proven in-range constant.
- Overflow: checked ops, never silent wrap (unless `wrapping_add` / wrapping bit ops).
- Generated C is compiled `-O2` with unused-section stripping. `fmt.println` is one `writev`.
- Capturing closures: env is `malloc`'d, owned by the `fn` value, `free`'d on drop. No lifetime syntax. Non-Copy captures are still rejected. The `yuga_fn` ABI is `{fn, env, env_size}`. `plat_intern_fn` copies `env_size` bytes so Zeus `.on` / `.styled` still run after the interned value is dropped.
