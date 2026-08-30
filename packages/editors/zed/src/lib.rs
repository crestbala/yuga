use zed_extension_api::{self as zed, Result};

struct YugaExtension;

impl zed::Extension for YugaExtension {
    fn new() -> Self {
        Self
    }

    fn language_server_command(
        &mut self,
        _language_server_id: &zed::LanguageServerId,
        worktree: &zed::Worktree,
    ) -> Result<zed::Command> {
        let path = worktree
            .which("yuga-lsp")
            .unwrap_or_else(|| format!("{}/bin/yuga-lsp", worktree.root_path()));
        Ok(zed::Command {
            command: path,
            args: vec![],
            env: Default::default(),
        })
    }
}

zed::register_extension!(YugaExtension);
