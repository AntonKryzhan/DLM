use dlm_core::math_dispatch as math;
use dlm_core::prelude_lowering::PreludeLoweringTarget;
use std::process;

pub fn run(args: &[String]) {
    if let Err(message) = report(args) {
        eprintln!("math-plan: {message}");
        process::exit(2);
    }
}

fn report(args: &[String]) -> Result<(), String> {
    let args: Vec<_> = args.iter().map(String::as_str).collect();
    let plan = match args.as_slice() {
        [] | ["list"] => {
            for entry in math::ALGORITHMS {
                println!("{}\t{}\t{}\t{}", entry.name, entry.target, entry.numerical_contract, entry.complexity);
            }
            return Ok(());
        }
        ["matmul", rows, inner, columns] => {
            let (rows, inner, columns) = (dimension(rows)?, dimension(inner)?, dimension(columns)?);
            if [(rows, inner), (inner, columns), (rows, columns)]
                .iter()
                .any(|(a, b)| a.checked_mul(*b).is_none_or(|n| n > dlm_core::float::VEC_MAX))
            {
                return Err("matrix exceeds the FloatVec capacity".into());
            }
            Some(math::plan_matmul(rows, inner, columns, math::host_avx2()))
        }
        ["fft", points] => {
            let points = dimension(points)?;
            if points > dlm_core::float::VEC_MAX / 2 {
                return Err("interleaved complex vector exceeds the FloatVec capacity".into());
            }
            math::plan_fourier(points)
        }
        ["convolve", left, right] => math::plan_real_convolution(dimension(left)?, dimension(right)?),
        ["ntt", left, right] => math::plan_modular_convolution(dimension(left)?, dimension(right)?),
        ["vulkan"] => {
            show_vulkan();
            return Ok(());
        }
        _ => return Err("expected list, matmul ROWS INNER COLS, fft POINTS, convolve LEFT RIGHT, ntt LEFT RIGHT, or vulkan".into()),
    };
    let plan = plan.ok_or("no registered algorithm accepts this shape")?;
    println!("{plan}");
    Ok(())
}

fn dimension(text: &str) -> Result<usize, String> {
    text.parse()
        .map_err(|_| format!("invalid dimension '{text}'"))
}

fn show_vulkan() {
    match math::vulkan::probe() {
        Err(error) => println!("Vulkan device discovery: {error}"),
        Ok(devices) => {
            println!("Vulkan physical devices: {}", devices.len());
            for device in devices {
                let family = device
                    .compute_queue_family
                    .map_or_else(|| "none".into(), |n| n.to_string());
                println!(
                    "device {}: compute_queue={} dedicated={} shaderFloat64={} shaderInt64={}",
                    device.index,
                    family,
                    device.dedicated_compute,
                    device.shader_float64,
                    device.shader_int64,
                );
            }
        }
    }
    let registered = math::ALGORITHMS
        .iter()
        .filter(|a| a.target == PreludeLoweringTarget::GpuBatch)
        .count();
    println!("registered GPU algorithms: {registered}");
}
