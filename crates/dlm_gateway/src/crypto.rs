use crate::{ensure, ffi, Result};
use std::ptr::NonNull;
pub fn random<const N: usize>() -> Result<[u8; N]> {
    let mut out = [0; N];
    unsafe { ffi::dgn_random(out.as_mut_ptr(), N) }.checked()?;
    Ok(out)
}
pub fn hash(input: &[u8]) -> Result<[u8; 32]> {
    let mut out = [0; 32];
    unsafe { ffi::dgn_hash(input.as_ptr(), input.len(), out.as_mut_ptr()) }.checked()?;
    Ok(out)
}
pub struct Secret(NonNull<std::ffi::c_void>);
// The native owner has no callbacks or shared mutable references. Ownership moves with it.
unsafe impl Send for Secret {}
impl Drop for Secret {
    fn drop(&mut self) {
        unsafe { ffi::dgn_secret_close(self.0.as_ptr()) }
    }
}
impl Secret {
    pub fn ephemeral() -> Result<(Self, [u8; 32])> {
        let mut public = [0; 32];
        let p = unsafe { ffi::dgn_ephemeral(public.as_mut_ptr()) }.checked()?;
        Ok((Self(NonNull::new(p).unwrap()), public))
    }
    pub fn channel(
        self,
        peer: &[u8; 32],
        binding: &[u8; 32],
        session: &[u8; 32],
        role: u32,
    ) -> Result<Self> {
        let p = self.0.as_ptr();
        std::mem::forget(self);
        let p =
            unsafe { ffi::dgn_channel(p, peer.as_ptr(), binding.as_ptr(), session.as_ptr(), role) }
                .checked()?;
        Ok(Self(NonNull::new(p).unwrap()))
    }
    pub fn seal(&mut self, plain: &[u8], aad: &[u8]) -> Result<Vec<u8>> {
        ensure(plain.len() <= 65500, 7, "record too large")?;
        let mut out = vec![0; plain.len() + 32];
        let n = unsafe {
            ffi::dgn_seal(
                self.0.as_ptr(),
                plain.as_ptr(),
                plain.len(),
                aad.as_ptr(),
                aad.len(),
                out.as_mut_ptr(),
                out.len(),
            )
        }
        .checked()?;
        out.truncate(n);
        Ok(out)
    }
    pub fn open(&mut self, wire: &[u8], aad: &[u8]) -> Result<Vec<u8>> {
        ensure(
            (32..=65532).contains(&wire.len()),
            11,
            "invalid record length",
        )?;
        let mut out = vec![0; wire.len() - 32];
        let n = unsafe {
            ffi::dgn_open(
                self.0.as_ptr(),
                wire.as_ptr(),
                wire.len(),
                aad.as_ptr(),
                aad.len(),
                out.as_mut_ptr(),
                out.len(),
            )
        }
        .checked()?;
        out.truncate(n);
        Ok(out)
    }
}
pub fn verify(key: &[u8; 32], data: &[u8], signature: &[u8]) -> Result<()> {
    ensure(signature.len() == 64, 11, "invalid signature length")?;
    ensure(
        unsafe { ffi::dgn_verify(key.as_ptr(), data.as_ptr(), data.len(), signature.as_ptr()) }
            == 0,
        12,
        "signature verification failed",
    )
}
pub fn journal(encrypt: bool, key: &[u8; 32], aad: &[u8], input: &[u8]) -> Result<Vec<u8>> {
    ensure(input.len() <= 1048576, 7, "journal record too large")?;
    let mut out = vec![
        0;
        if encrypt {
            input.len() + 60
        } else {
            input.len().saturating_sub(60)
        }
    ];
    let n = unsafe {
        ffi::dgn_journal_crypt(
            encrypt,
            key.as_ptr(),
            aad.as_ptr(),
            aad.len(),
            input.as_ptr(),
            input.len(),
            out.as_mut_ptr(),
            out.len(),
        )
    }
    .checked()?;
    out.truncate(n);
    Ok(out)
}
pub fn wipe(bytes: &mut [u8]) {
    unsafe { ffi::dgn_wipe(bytes.as_mut_ptr(), bytes.len()) }
}

pub(crate) struct Bytes(pub Vec<u8>);
impl Drop for Bytes {
    fn drop(&mut self) {
        wipe(&mut self.0);
    }
}
