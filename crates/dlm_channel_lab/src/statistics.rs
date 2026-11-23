use crate::{require, Cancellation, Random, Result};
#[derive(Clone, Copy, Debug)]
pub struct Interval {
    pub available: bool,
    pub low: f64,
    pub high: f64,
}
impl Interval {
    fn unavailable() -> Self {
        Self {
            available: false,
            low: 0.0,
            high: 0.0,
        }
    }
    pub fn json(self) -> String {
        if self.available {
            format!("[{},{}]", self.low, self.high)
        } else {
            "null".into()
        }
    }
}
pub fn wilson(errors: u64, total: u64) -> Interval {
    if total == 0 || errors > total {
        return Interval::unavailable();
    }
    let z = 1.959963984540054;
    let n = total as f64;
    let p = errors as f64 / n;
    let d = 1.0 + z * z / n;
    let mid = (p + z * z / (2.0 * n)) / d;
    let radius = z * ((p * (1.0 - p) + z * z / (4.0 * n)) / n).sqrt() / d;
    Interval {
        available: true,
        low: if errors == 0 {
            0.0
        } else {
            (mid - radius).max(0.0)
        },
        high: if errors == total {
            1.0
        } else {
            (mid + radius).min(1.0)
        },
    }
}
#[derive(Clone, Copy, Debug)]
pub struct Capacity {
    pub available: bool,
    pub lower: f64,
    pub upper: f64,
    pub iterations: u32,
}
pub fn capacity(matrix: &[Vec<u64>]) -> Result<Capacity> {
    require(
        !matrix.is_empty() && matrix.len() <= 8,
        4,
        "DMC input alphabet bound",
    )?;
    let columns = matrix[0].len();
    require(
        columns > 0 && columns <= 9 && matrix.iter().all(|r| r.len() == columns),
        4,
        "DMC matrix dimensions invalid",
    )?;
    if matrix.iter().any(|r| r.iter().all(|&v| v == 0)) {
        return Ok(Capacity {
            available: false,
            lower: 0.0,
            upper: 0.0,
            iterations: 0,
        });
    }
    let rows = matrix.len();
    let w: Vec<Vec<f64>> = matrix
        .iter()
        .map(|r| {
            let total = r.iter().map(|&v| v as f64).sum::<f64>();
            r.iter().map(|&v| v as f64 / total).collect()
        })
        .collect();
    let mut p = vec![1.0 / rows as f64; rows];
    let mut result = Capacity {
        available: true,
        lower: 0.0,
        upper: (rows as f64).log2(),
        iterations: 0,
    };
    for iteration in 1..=1024 {
        let q: Vec<f64> = (0..columns)
            .map(|y| (0..rows).map(|x| p[x] * w[x][y]).sum())
            .collect();
        let d: Vec<f64> = (0..rows)
            .map(|x| {
                (0..columns)
                    .filter(|&y| w[x][y] > 0.0)
                    .map(|y| w[x][y] * (w[x][y] / q[y].max(1e-300)).log2())
                    .sum()
            })
            .collect();
        let low = (0..rows).map(|x| p[x] * d[x]).sum::<f64>().max(0.0);
        let high = d
            .iter()
            .copied()
            .fold(0.0, f64::max)
            .min((rows as f64).log2())
            .max(low);
        result = Capacity {
            available: true,
            lower: low,
            upper: high,
            iterations: iteration,
        };
        if high - low < 1e-10 {
            break;
        }
        for x in 0..rows {
            p[x] = (p[x] * (d[x] - high).exp2()).max(1e-15);
        }
        let total = p.iter().sum::<f64>();
        for n in &mut p {
            *n /= total;
        }
    }
    Ok(result)
}
pub fn mutual_information(matrix: &[Vec<u64>]) -> Result<f64> {
    let _ = capacity(matrix)?;
    let total = matrix.iter().flatten().map(|&v| v as f64).sum::<f64>();
    if total == 0.0 {
        return Ok(0.0);
    }
    let rows: Vec<f64> = matrix
        .iter()
        .map(|r| r.iter().map(|&v| v as f64).sum())
        .collect();
    let columns: Vec<f64> = (0..matrix[0].len())
        .map(|y| matrix.iter().map(|r| r[y] as f64).sum())
        .collect();
    let mut value = 0.0;
    for (x, row) in matrix.iter().enumerate() {
        for (y, &count) in row.iter().enumerate() {
            if count > 0 {
                value +=
                    count as f64 / total * (count as f64 * total / (rows[x] * columns[y])).log2();
            }
        }
    }
    Ok(value.max(0.0))
}
fn bootstrap(
    series: &[[u64; 4]],
    samples: usize,
    seed: u64,
    cancel: &Cancellation,
) -> Result<(Interval, Interval, usize)> {
    let n = series.len();
    if n == 0 {
        return Ok((Interval::unavailable(), Interval::unavailable(), 0));
    }
    let length = ((n as f64).sqrt().ceil() as usize).max(8).min(n);
    let mut prefix = vec![[0u64; 4]; n + 1];
    for (i, s) in series.iter().enumerate() {
        for (k, &value) in s.iter().enumerate() {
            prefix[i + 1][k] = prefix[i][k] + value;
        }
    }
    let mut rng = Random::new(seed);
    let mut ber = Vec::with_capacity(samples);
    let mut erasure = Vec::with_capacity(samples);
    for i in 0..samples {
        if i % 16 == 0 {
            cancel.check()?;
        }
        let mut sum = [0u64; 4];
        let mut remaining = n;
        while remaining > 0 {
            let count = remaining.min(length);
            let start = rng.next() as usize % (n - count + 1);
            for (k, total) in sum.iter_mut().enumerate() {
                *total += prefix[start + count][k] - prefix[start][k];
            }
            remaining -= count;
        }
        if sum[1] > 0 {
            ber.push(sum[0] as f64 / sum[1] as f64);
        }
        erasure.push(sum[2] as f64 / sum[3] as f64);
    }
    let effective = n.div_ceil(length).max(1) as u64;
    let interval = |mut values: Vec<f64>| {
        if values.is_empty() {
            return Interval::unavailable();
        }
        values.sort_by(f64::total_cmp);
        let low = values[((values.len() - 1) as f64 * 0.025).floor() as usize];
        let high = values[((values.len() - 1) as f64 * 0.975).ceil() as usize];
        Interval {
            available: true,
            low: if low == 1.0 {
                wilson(effective, effective).low
            } else {
                low
            },
            high: if high == 0.0 {
                wilson(0, effective).high
            } else {
                high
            },
        }
    };
    Ok((interval(ber), interval(erasure), length))
}
#[derive(Clone, Debug)]
pub struct Statistics {
    pub matrix: Vec<Vec<u64>>,
    pub symbols: u64,
    pub erased: u64,
    pub bit_errors: u64,
    pub observed_bits: u64,
    pub mutual_information: f64,
    pub capacity: Capacity,
    pub ber95: Interval,
    pub erasure95: Interval,
    pub block_length: usize,
    pub method: &'static str,
}
impl Statistics {
    pub fn json(&self, period_us: u64) -> String {
        let matrix = self
            .matrix
            .iter()
            .map(|r| {
                format!(
                    "[{}]",
                    r.iter().map(u64::to_string).collect::<Vec<_>>().join(",")
                )
            })
            .collect::<Vec<_>>()
            .join(",");
        format!("{{\"symbols\":{},\"erased_symbols\":{},\"bit_errors\":{},\"observed_bits\":{},\"ber\":{},\"symbol_erasure_rate\":{},\"ber_95\":{},\"erasure_95\":{},\"confidence_method\":\"{}\",\"bootstrap_block_symbols\":{},\"bootstrap_boundary_adjustment\":\"Wilson effective block count\",\"confusion_counts_including_erasure_column\":[{}],\"empirical_mutual_information_bits_per_symbol\":{},\"dmc_fit_available\":{},\"dmc_fit_capacity_lower_bits_per_symbol\":{},\"dmc_fit_capacity_upper_bits_per_symbol\":{},\"dmc_fit_capacity_lower_bits_per_second\":{},\"dmc_fit_iterations\":{},\"capacity_is_memoryless_decoder_fit\":true}}",self.symbols,self.erased,self.bit_errors,self.observed_bits,if self.observed_bits>0{(self.bit_errors as f64/self.observed_bits as f64).to_string()}else{"null".into()},self.erased as f64/self.symbols.max(1)as f64,self.ber95.json(),self.erasure95.json(),self.method,self.block_length,matrix,self.mutual_information,self.capacity.available,self.capacity.lower,self.capacity.upper,self.capacity.lower*1_000_000.0/period_us as f64,self.capacity.iterations)
    }
}
pub fn evaluate(
    expected: &[u8],
    observed: &[Option<u8>],
    alphabet: usize,
    memory: bool,
    samples: usize,
    seed: u64,
    cancel: &Cancellation,
) -> Result<Statistics> {
    require(
        expected.len() == observed.len() && !expected.is_empty() && [2, 8].contains(&alphabet),
        4,
        "measurement symbol dimensions invalid",
    )?;
    require((32..=512).contains(&samples), 4, "bootstrap resample quota")?;
    let bits = alphabet.ilog2() as u64;
    let mut matrix = vec![vec![0u64; alphabet + 1]; alphabet];
    let mut erased = 0;
    let mut errors = 0;
    let mut valid = 0;
    let mut series = Vec::with_capacity(expected.len());
    for (i, (&a, &b)) in expected.iter().zip(observed).enumerate() {
        if i % 1024 == 0 {
            cancel.check()?;
        }
        require(
            (a as usize) < alphabet && b.is_none_or(|v| (v as usize) < alphabet),
            4,
            "symbol outside measurement alphabet",
        )?;
        matrix[a as usize][b.map_or(alphabet, |v| v as usize)] += 1;
        if let Some(b) = b {
            let n = (a ^ b).count_ones() as u64;
            errors += n;
            valid += bits;
            series.push([n, bits, 0, 1]);
        } else {
            erased += 1;
            series.push([0, 0, 1, 1]);
        }
    }
    let (ber95, erasure95, block_length, method) = if memory || bits > 1 {
        let (a, b, n) = bootstrap(&series, samples, seed ^ 0x6c61622d63692d31, cancel)?;
        (a, b, n, "moving_block_bootstrap_95")
    } else {
        (
            wilson(errors, valid),
            wilson(erased, expected.len() as u64),
            0,
            "wilson_95_iid_assumption",
        )
    };
    Ok(Statistics {
        capacity: capacity(&matrix)?,
        mutual_information: mutual_information(&matrix)?,
        matrix,
        symbols: expected.len() as u64,
        erased,
        bit_errors: errors,
        observed_bits: valid,
        ber95,
        erasure95,
        block_length,
        method,
    })
}
