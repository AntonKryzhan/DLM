use crate::{crypto, ensure, Error, Result};
#[derive(Clone, Debug)]
pub struct Shard {
    pub index: u8,
    pub bytes: Vec<u8>,
}
#[derive(Clone, Debug)]
pub struct Group {
    pub data: u8,
    pub total: u8,
    pub length: usize,
    pub digest: [u8; 32],
    pub shards: Vec<Shard>,
}
fn mul(mut a: u8, mut b: u8) -> u8 {
    if a == 0 || b == 0 {
        return 0;
    }
    if a == 1 {
        return b;
    }
    if b == 1 {
        return a;
    }
    let mut out = 0;
    for _ in 0..8 {
        if b & 1 != 0 {
            out ^= a
        }
        let top = a & 128;
        a <<= 1;
        if top != 0 {
            a ^= 0x1d
        }
        b >>= 1;
    }
    out
}
fn inverse(a: u8) -> Result<u8> {
    ensure(a != 0, 11, "singular FEC matrix")?;
    let mut x = a;
    let mut power = 254;
    let mut out = 1;
    while power > 0 {
        if power & 1 != 0 {
            out = mul(out, x)
        }
        x = mul(x, x);
        power >>= 1;
    }
    Ok(out)
}
fn row(index: u8, k: u8) -> Result<Vec<u8>> {
    if index < k {
        let mut out = vec![0; k as usize];
        out[index as usize] = 1;
        return Ok(out);
    }
    let x = index + 1;
    let mut out = Vec::with_capacity(k as usize);
    for i in 0..k {
        let xi = i + 1;
        let (mut numerator, mut denominator) = (1, 1);
        for j in 0..k {
            if j != i {
                numerator = mul(numerator, x ^ (j + 1));
                denominator = mul(denominator, xi ^ (j + 1));
            }
        }
        out.push(mul(numerator, inverse(denominator)?));
    }
    Ok(out)
}
fn bounds(k: u8, n: u8, length: usize) -> Result<()> {
    ensure(
        k > 0 && k <= 16 && n >= k && n <= 32 && length <= 65536,
        4,
        "FEC bounds require 1<=data<=16, data<=total<=32, length<=65536",
    )
}
pub fn encode(bytes: &[u8], k: u8, n: u8) -> Result<Group> {
    bounds(k, n, bytes.len())?;
    let width = bytes.len().div_ceil(k as usize).max(1);
    let mut shards = Vec::with_capacity(n as usize);
    for index in 0..n {
        if index < k {
            let start = index as usize * width;
            let mut output = vec![0; width];
            if start < bytes.len() {
                let count = (bytes.len() - start).min(width);
                output[..count].copy_from_slice(&bytes[start..start + count]);
            }
            shards.push(Shard {
                index,
                bytes: output,
            });
            continue;
        }
        let coefficients = row(index, k)?;
        let mut output = vec![0; width];
        for source in 0..k as usize {
            for (position, item) in output.iter_mut().enumerate() {
                let offset = source * width + position;
                let value = bytes.get(offset).copied().unwrap_or(0);
                *item ^= mul(coefficients[source], value);
            }
        }
        shards.push(Shard {
            index,
            bytes: output,
        });
    }
    Ok(Group {
        data: k,
        total: n,
        length: bytes.len(),
        digest: crypto::hash(bytes)?,
        shards,
    })
}
pub fn decode(shards: &[Shard], k: u8, n: u8, length: usize, digest: &[u8; 32]) -> Result<Vec<u8>> {
    bounds(k, n, length)?;
    ensure(
        shards.len() >= k as usize && shards.len() <= n as usize,
        7,
        "insufficient or excessive FEC shards",
    )?;
    let width = length.div_ceil(k as usize).max(1);
    let mut seen = 0u32;
    for shard in shards {
        ensure(
            shard.index < n && shard.bytes.len() == width && seen & (1u32 << shard.index) == 0,
            11,
            "noncanonical FEC shard",
        )?;
        seen |= 1u32 << shard.index;
    }
    let count = k as usize;
    if seen & ((1u32 << k) - 1) == (1u32 << k) - 1 {
        let mut bytes = Vec::with_capacity(width * k as usize);
        for index in 0..k {
            bytes.extend_from_slice(&shards.iter().find(|s| s.index == index).unwrap().bytes);
        }
        bytes.truncate(length);
        ensure(
            crypto::hash(&bytes)? == *digest,
            12,
            "FEC reconstruction digest mismatch",
        )?;
        return Ok(bytes);
    }
    let mut matrix = vec![vec![0u8; 2 * count]; count];
    for i in 0..count {
        matrix[i][..count].copy_from_slice(&row(shards[i].index, k)?);
        matrix[i][count + i] = 1;
    }
    for column in 0..count {
        let pivot = (column..count)
            .find(|r| matrix[*r][column] != 0)
            .ok_or(Error::new(11, "singular FEC selection"))?;
        matrix.swap(column, pivot);
        let factor = inverse(matrix[column][column])?;
        for value in &mut matrix[column] {
            *value = mul(*value, factor)
        }
        for row in 0..count {
            if row == column {
                continue;
            }
            let factor = matrix[row][column];
            for j in 0..2 * count {
                matrix[row][j] ^= mul(factor, matrix[column][j]);
            }
        }
    }
    let mut output = vec![0; count * width];
    for source in 0..count {
        for shard in 0..count {
            let coefficient = matrix[source][count + shard];
            for at in 0..width {
                output[source * width + at] ^= mul(coefficient, shards[shard].bytes[at]);
            }
        }
    }
    output.truncate(length);
    ensure(
        crypto::hash(&output)? == *digest,
        12,
        "reconstructed FEC content failed integrity check",
    )?;
    Ok(output)
}
