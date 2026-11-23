pub fn configure(command: &mut std::process::Command) {
    #[cfg(windows)]
    {
        let sdk = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../DLM-OUT/bin");
        for (variable, name) in [
            ("DLM_LIBGNUTLS", "libgnutls-30.dll"),
            ("DLM_LIBHOGWEED", "libhogweed-6.dll"),
        ] {
            if std::env::var_os(variable).is_none() {
                let library = sdk.join(name);
                if let Ok(path) = library.canonicalize() {
                    command.env(variable, path);
                }
            }
        }
    }
    #[cfg(not(windows))]
    let _ = command;
}
