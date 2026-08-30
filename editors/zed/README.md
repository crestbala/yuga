# Yuga for Zed

Syntax highlighting (Tree-sitter) and error checking (`yuga-lsp`).

## Install

1. From the Yuga repo root: `make`
2. One-time: `rustup target add wasm32-wasip2` (Zed compiles the extension to WebAssembly)
3. In Zed: command palette → **zed: install dev extension**
4. Choose `editors/zed`

If the status bar shows **Language Server: unknown**, or install fails with **failed to compile grammar 'yuga'**, uninstall Yuga and install the dev extension again. The grammar is `tree-sitter-yuga` in this repo; `editors/zed/extension.toml` must list that directory and its current `git rev-parse HEAD`.

Open a `.yuga` file. You should see highlighting, and the status bar should show **Yuga** / **Yuga LSP**.

For the `.yuga` file icon: command palette → **theme selector: toggle icon theme** → **Yuga**. Only `.yuga` files get the YG badge; `.expected` and other types keep the normal icons. The badge uses Zed’s `fill="black"` convention, so it follows the UI theme (grey on dark, dark on light), same as TypeScript.

The language server is `bin/yuga-lsp`. With this repo as the workspace, the extension finds it at `<workspace>/bin/yuga-lsp`. Otherwise put `bin/` on `PATH`, or launch Zed from a terminal (`zed .`).

## After changing the grammar

```bash
make grammar
cd tree-sitter-yuga
git add -A && git commit -m "update grammar"
git rev-parse HEAD   # paste this SHA into editors/zed/extension.toml [grammars.yuga].rev
```

Then reinstall the dev extension.
