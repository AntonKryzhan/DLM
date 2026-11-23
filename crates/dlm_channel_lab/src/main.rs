use dlm_channel_lab::{
    codec::{self, Plan},
    config::Config,
    experiment, model, trace, Cancellation, Error, Result, MAX_PAYLOAD,
};
use std::{
    collections::BTreeMap,
    path::{Path, PathBuf},
    io::{Read, Write},
    sync::atomic::{AtomicU64, Ordering},
};
struct Options(BTreeMap<String, String>);
impl Options {
    fn parse(args: &[String]) -> Result<Self> {
        let mut values = BTreeMap::new();
        let mut i = 0;
        while i < args.len() {
            let k = args[i]
                .strip_prefix("--")
                .ok_or(Error::new(4, "options require --name value"))?;
            let v = args
                .get(i + 1)
                .ok_or(Error::new(4, "missing option value"))?;
            if values.insert(k.to_owned(), v.clone()).is_some() {
                return Err(Error::new(4, "duplicate command option"));
            }
            i += 2;
        }
        Ok(Self(values))
    }
    fn take(&mut self, k: &str) -> Result<String> {
        self.0
            .remove(k)
            .ok_or(Error::new(4, "missing required command option"))
    }
    fn optional(&mut self, k: &str) -> Option<String> {
        self.0.remove(k)
    }
    fn finish(&self) -> Result<()> {
        if self.0.is_empty() {
            Ok(())
        } else {
            Err(Error::new(4, "unknown command option"))
        }
    }
}
fn bytes(path: &Path, max: usize) -> Result<Vec<u8>> {
    let mut f = std::fs::File::open(path)?;
    if f.metadata()?.len() > max as u64 {
        return Err(Error::new(7, "input file quota exceeded"));
    }
    let mut b = Vec::new();
    std::io::Read::by_ref(&mut f)
        .take(max as u64 + 1)
        .read_to_end(&mut b)?;
    if b.len() > max {
        return Err(Error::new(7, "input grew beyond quota"));
    }
    Ok(b)
}
fn write(path: &Path, bytes: &[u8]) -> Result<()> {
    static NEXT: AtomicU64 = AtomicU64::new(1);
    let parent = path
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    let name = path
        .file_name()
        .ok_or(Error::new(4, "output filename required"))?
        .to_string_lossy();
    let temp = parent.join(format!(
        ".{name}.lab-{}-{}",
        std::process::id(),
        NEXT.fetch_add(1, Ordering::Relaxed)
    ));
    let result = (|| -> Result<()> {
        let mut f = std::fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temp)?;
        f.write_all(bytes)?;
        f.sync_all()?;
        drop(f);
        std::fs::rename(&temp, path)?;
        Ok(())
    })();
    if result.is_err() {
        let _ = std::fs::remove_file(temp);
    }
    result
}
fn config(options: &mut Options) -> Result<Config> {
    let path = options.take("config")?;
    let b = bytes(Path::new(&path), 65536)?;
    Config::parse(
        std::str::from_utf8(&b).map_err(|_| Error::new(4, "configuration must be UTF-8"))?,
    )
}
fn output_report(path: Option<String>, report: &str) -> Result<()> {
    if let Some(path) = path {
        write(Path::new(&path), report.as_bytes())?;
    }
    println!("{report}");
    Ok(())
}
fn run(command: &str, args: &[String]) -> Result<()> {
    let mut options = Options::parse(args)?;
    let cancel = Cancellation::default();
    match command {
        "simulate" | "loopback" | "matrix" => {
            let c = config(&mut options)?;
            let input = PathBuf::from(options.take("input")?);
            let report = options.optional("report");
            let output = options.optional("output");
            let capture = options.optional("capture");
            options.finish()?;
            let input = bytes(&input, MAX_PAYLOAD)?;
            if command == "matrix" {
                if output.is_some() || capture.is_some() {
                    return Err(Error::new(4, "matrix produces reports only"));
                }
                return output_report(report, &experiment::matrix(&c, &input, &cancel)?);
            }
            let m = if command == "loopback" {
                experiment::loopback(&c, &input, &cancel)?
            } else {
                experiment::simulate(&c, &input, &cancel)?
            };
            if let Some(path) = capture {
                write(Path::new(&path), &trace::encode(&m.capture)?)?;
            }
            output_report(report, &m.report)?;
            if let Some(path) = output {
                let payload = m.decoded.payload.ok_or(Error::new(
                    19,
                    "no valid payload to write; measurement report retained",
                ))?;
                write(Path::new(&path), &payload)?;
            }
        }
        "encode" => {
            let c = config(&mut options)?;
            let input = PathBuf::from(options.take("input")?);
            let capture = PathBuf::from(options.take("capture")?);
            options.finish()?;
            let payload = bytes(&input, MAX_PAYLOAD)?;
            let t = codec::encode(
                Plan {
                    codec: c.codec,
                    frame_id: c.frame_id,
                    payload_bytes: payload.len(),
                },
                &payload,
                &cancel,
            )?;
            write(&capture, &trace::encode(&t)?)?;
            println!(
                "{{\"status\":\"ENCODED\",\"events\":{},\"symbols\":{}}}",
                t.events.len(),
                t.plan.symbols()
            );
        }
        "distort" => {
            let c = config(&mut options)?;
            let input = PathBuf::from(options.take("capture")?);
            let output = PathBuf::from(options.take("output")?);
            options.finish()?;
            let t = trace::read(&input)?;
            let (t, s) = model::distort(&t, &c.noise, &cancel)?;
            write(&output, &trace::encode(&t)?)?;
            println!(
                "{{\"status\":\"DISTORTED\",\"sent_events\":{},\"received_events\":{}}}",
                s.sent_events, s.received_events
            );
        }
        "decode" => {
            let input = PathBuf::from(options.take("capture")?);
            let output = PathBuf::from(options.take("output")?);
            let report = options.optional("report");
            options.finish()?;
            let t = trace::read(&input)?;
            let d = codec::decode(&t, &cancel)?;
            output_report(report, &experiment::decoded_report(&d))?;
            write(
                &output,
                &d.payload
                    .ok_or(Error::new(19, "capture contains no valid frame"))?,
            )?;
        }
        _ => {
            return Err(Error::new(
                4,
                "unknown information-channel laboratory command",
            ))
        }
    }
    Ok(())
}
fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() == 2 && args[1] == "--version" {
        println!("dlm-channel-lab {}", env!("CARGO_PKG_VERSION"));
        return;
    }
    if args.len() < 2 || matches!(args[1].as_str(), "--help" | "help" | "-h") {
        println!("dlm-channel-lab simulate|loopback|matrix --config FILE --input FILE [--report FILE] [--capture FILE] [--output FILE]\ndlm-channel-lab encode --config FILE --input FILE --capture FILE\ndlm-channel-lab distort --config FILE --capture FILE --output FILE\ndlm-channel-lab decode --capture FILE --output FILE [--report FILE]\nAlphabets: absolute, interval, presence, count, protocol, order, size, hybrid. Codes: none, repeat3, hamming84. Loopback records local TCP/UDP application events. All modes remain laboratory experiments.");
        return;
    }
    if let Err(e) = run(&args[1], &args[2..]) {
        eprintln!("dlm-channel-lab: {e}");
        std::process::exit(e.code.clamp(1, 125));
    }
}
