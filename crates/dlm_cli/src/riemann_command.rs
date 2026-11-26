use std::fs::{self, File, Metadata};
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::process::{self, Command, Stdio};

use dlm_core::math::riemann;
use dlm_core::spectral::sha256_reader;

pub fn run(checker: &str, challenge: &str, solution: &str) {
    if let Err(error) = replay(checker, challenge, solution) {
        eprintln!("Riemann proof replay failed: {error}");
        process::exit(1);
    }
}

fn replay(checker: &str, challenge: &str, solution: &str) -> Result<(), String> {
    let challenge = PinnedExport::open(
        challenge,
        riemann::CHALLENGE_BYTES,
        riemann::CHALLENGE_SHA256,
    )?;
    let solution = PinnedExport::open(solution, riemann::SOLUTION_BYTES, riemann::SOLUTION_SHA256)?;
    challenge.ensure_unchanged()?;
    solution.ensure_unchanged()?;

    let output = Command::new(checker)
        .arg("--compare")
        .arg("--")
        .arg(&challenge.path)
        .arg(&solution.path)
        .stderr(Stdio::inherit())
        .output()
        .map_err(|error| format!("cannot start con-leche ({checker}): {error}"))?;
    check_result(&output)?;
    challenge.ensure_unchanged()?;
    solution.ensure_unchanged()?;

    println!("external Lean proof replay: checked");
    println!("theorem: {}", riemann::SOURCE);
    println!("proposition: {}", riemann::PROPOSITION);
    println!("axioms: {}", riemann::AXIOMS.join(", "));
    Ok(())
}

fn check_result(output: &process::Output) -> Result<(), String> {
    if !output.status.success() {
        return Err(format!("con-leche comparison returned {}", output.status));
    }
    let stdout = String::from_utf8_lossy(&output.stdout);
    if !stdout
        .lines()
        .any(|line| line == "con-leche: theorem riemannHypothesis justified by the solution")
    {
        return Err("con-leche did not report the expected RiemannHypothesis comparison".into());
    }
    Ok(())
}

struct PinnedExport {
    path: PathBuf,
    file: File,
    metadata: Metadata,
}

impl PinnedExport {
    fn open(path: &str, size: u64, sha256: &str) -> Result<Self, String> {
        let path = Path::new(path);
        let io_error = |error: io::Error| format!("{}: {error}", path.display());
        let absolute = fs::canonicalize(path).map_err(io_error)?;
        let candidate = fs::metadata(&absolute).map_err(io_error)?;
        if !candidate.is_file() || candidate.len() != size {
            return Err(format!(
                "{}: expected a regular, unpacked NDJSON export of {size} bytes",
                path.display()
            ));
        }
        let mut options = File::options();
        options.read(true);
        #[cfg(windows)]
        {
            use std::os::windows::fs::OpenOptionsExt;
            options.share_mode(0x0000_0001); // FILE_SHARE_READ
        }
        let file = options.open(&absolute).map_err(io_error)?;
        let metadata = file.metadata().map_err(io_error)?;
        if !same_snapshot(&candidate, &metadata).map_err(io_error)? {
            return Err(format!("{}: export changed while opening", path.display()));
        }
        let actual = sha256_reader((&file).take(size + 1)).map_err(io_error)?;
        if actual != sha256 {
            return Err(format!(
                "{}: SHA-256 differs from the published Lean export",
                path.display()
            ));
        }
        let export = Self {
            path: absolute,
            file,
            metadata,
        };
        export.ensure_unchanged()?;
        Ok(export)
    }

    fn ensure_unchanged(&self) -> Result<(), String> {
        let io_error = |error: io::Error| format!("{}: {error}", self.path.display());
        let by_name = fs::metadata(&self.path).map_err(io_error)?;
        let by_handle = self.file.metadata().map_err(io_error)?;
        if !same_snapshot(&self.metadata, &by_name).map_err(io_error)?
            || !same_snapshot(&self.metadata, &by_handle).map_err(io_error)?
        {
            return Err(format!(
                "{}: export changed during proof verification",
                self.path.display()
            ));
        }
        Ok(())
    }
}

fn same_snapshot(before: &Metadata, after: &Metadata) -> io::Result<bool> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::MetadataExt;
        if (
            before.dev(),
            before.ino(),
            before.ctime(),
            before.ctime_nsec(),
        ) != (after.dev(), after.ino(), after.ctime(), after.ctime_nsec())
        {
            return Ok(false);
        }
    }
    Ok(after.is_file() && before.len() == after.len() && before.modified()? == after.modified()?)
}
