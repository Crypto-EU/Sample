//! C ABI: build Pearl plain_proof bincode blob for stratum mining.submit.
//!
//! Wire format matches ARC-miner / pearlhash pools (pearl/v1 positional submit).

use pearl_blake3::MerkleTree;
use std::collections::BTreeSet;
use std::os::raw::{c_char, c_int, c_uint};

const OK: c_int = 0;
const ERR_BAD_ARG: c_int = 1;
const ERR_INTERNAL: c_int = 2;

fn set_err(err: *mut *mut c_char, msg: impl Into<String>) {
    if err.is_null() {
        return;
    }
    let mut v = msg.into().into_bytes();
    v.push(0);
    let raw = v.into_boxed_slice();
    let ptr = Box::into_raw(raw) as *mut c_char;
    unsafe { *err = ptr };
}

fn map_xof_to_int7(out: &mut [u8]) {
    for b in out.iter_mut() {
        *b = (((*b % 127) as i8) - 63) as u8;
    }
}

fn write_u64_le(buf: &mut Vec<u8>, v: u64) {
    buf.extend_from_slice(&v.to_le_bytes());
}

fn write_merkle_proof(buf: &mut Vec<u8>, proof: &pearl_blake3::MerkleProof, root: &[u8; 32]) {
    write_u64_le(buf, proof.leaf_data.len() as u64);
    for leaf in &proof.leaf_data {
        write_u64_le(buf, leaf.len() as u64);
        buf.extend_from_slice(leaf);
    }
    write_u64_le(buf, proof.leaf_indices.len() as u64);
    for &idx in &proof.leaf_indices {
        write_u64_le(buf, idx as u64);
    }
    write_u64_le(buf, proof.total_leaves as u64);
    buf.extend_from_slice(root);
    write_u64_le(buf, proof.siblings.len() as u64);
    for sib in &proof.siblings {
        buf.extend_from_slice(sib);
    }
}

fn write_matrix_proof(
    buf: &mut Vec<u8>,
    matrix: &[u8],
    key: &[u8],
    row_indices: &[u32],
    k: usize,
) -> Result<(), String> {
    let mut key_arr = [0u8; 32];
    let copy_len = key.len().min(32);
    key_arr[..copy_len].copy_from_slice(&key[..copy_len]);
    let padded = pearl_blake3::pad_to_chunk_boundary(matrix);
    let mut indices: BTreeSet<usize> = BTreeSet::new();
    for &row in row_indices {
        let base = (row as usize) * k;
        let chunk = base / 1024;
        indices.insert(chunk);
        if k > 1024 {
            indices.insert((base + k - 1) / 1024);
        }
    }
    let leaf_indices: Vec<usize> = indices.into_iter().collect();
    let tree = MerkleTree::new(&padded, key_arr);
    let proof = tree.get_multileaf_proof(&leaf_indices);
    let root = tree.root();
    write_merkle_proof(buf, &proof, &root);
    write_u64_le(buf, row_indices.len() as u64);
    for &row in row_indices {
        write_u64_le(buf, row as u64);
    }
    Ok(())
}

/// Build plain_proof bincode bytes. Caller frees with superminer_share_free_buffer.
#[no_mangle]
pub unsafe extern "C" fn superminer_share_build_plain_proof(
    m: c_uint,
    n: c_uint,
    k: c_uint,
    noise_rank: c_uint,
    job_key: *const u8,
    job_key_len: usize,
    a_matrix: *const u8,
    a_len: usize,
    b_matrix: *const u8,
    b_len: usize,
    a_row_indices: *const c_uint,
    a_row_count: usize,
    b_col_indices: *const c_uint,
    b_col_count: usize,
    out_buf: *mut *mut u8,
    out_len: *mut usize,
    err_msg: *mut *mut c_char,
) -> c_int {
    if job_key.is_null()
        || a_matrix.is_null()
        || b_matrix.is_null()
        || a_row_indices.is_null()
        || b_col_indices.is_null()
        || out_buf.is_null()
        || out_len.is_null()
    {
        set_err(err_msg, "null argument");
        return ERR_BAD_ARG;
    }

    let key = std::slice::from_raw_parts(job_key, job_key_len);
    let a_bytes = std::slice::from_raw_parts(a_matrix, a_len);
    let b_bytes = std::slice::from_raw_parts(b_matrix, b_len);
    let a_rows = std::slice::from_raw_parts(a_row_indices, a_row_count);
    let b_cols = std::slice::from_raw_parts(b_col_indices, b_col_count);
    let kk = k as usize;

    let mut buf = Vec::with_capacity(65536);
    write_u64_le(&mut buf, m as u64);
    write_u64_le(&mut buf, n as u64);
    write_u64_le(&mut buf, k as u64);
    write_u64_le(&mut buf, noise_rank as u64);

    if let Err(e) = write_matrix_proof(&mut buf, a_bytes, key, a_rows, kk) {
        set_err(err_msg, e);
        return ERR_INTERNAL;
    }
    if let Err(e) = write_matrix_proof(&mut buf, b_bytes, key, b_cols, kk) {
        set_err(err_msg, e);
        return ERR_INTERNAL;
    }

    let boxed = buf.into_boxed_slice();
    let len = boxed.len();
    let ptr = Box::into_raw(boxed) as *mut u8;
    *out_buf = ptr;
    *out_len = len;
    OK
}

#[no_mangle]
pub unsafe extern "C" fn superminer_share_free_buffer(ptr: *mut u8, len: usize) {
    if ptr.is_null() || len == 0 {
        return;
    }
    let _ = Box::from_raw(std::slice::from_raw_parts_mut(ptr, len));
}

#[no_mangle]
pub unsafe extern "C" fn superminer_share_free_string(ptr: *mut c_char) {
    if ptr.is_null() {
        return;
    }
    let mut len = 0usize;
    while *ptr.add(len) != 0 {
        len += 1;
    }
    len += 1;
    let _ = Box::from_raw(std::slice::from_raw_parts_mut(ptr as *mut u8, len));
}

/// Expand BSeed to int7 matrix on host (fallback / verify).
#[no_mangle]
pub unsafe extern "C" fn superminer_bseed_expand(
    bseed: *const u8,
    out: *mut u8,
    n: usize,
) -> c_int {
    if bseed.is_null() || out.is_null() {
        return ERR_BAD_ARG;
    }
    let seed = std::slice::from_raw_parts(bseed, 32);
    let dst = std::slice::from_raw_parts_mut(out, n);
    let mut hasher = blake3::Hasher::new();
    hasher.update(seed);
    hasher.finalize_xof().fill(dst);
    map_xof_to_int7(dst);
    OK
}

#[no_mangle]
pub unsafe extern "C" fn superminer_challenge_solve(
    seed: *const u8,
    difficulty: c_int,
    out_nonce: *mut u64,
) -> c_int {
    if seed.is_null() || out_nonce.is_null() || difficulty < 0 {
        return ERR_BAD_ARG;
    }
    let seed_bytes = std::slice::from_raw_parts(seed, 32);
    let diff = difficulty as u32;
    for nonce in 0u64.. {
        let mut input = [0u8; 40];
        input[..32].copy_from_slice(seed_bytes);
        input[32..40].copy_from_slice(&nonce.to_le_bytes());
        let hash = blake3::hash(&input);
        let mut bits = 0u32;
        for &byte in hash.as_bytes() {
            if byte == 0 {
                bits += 8;
            } else {
                bits += byte.leading_zeros();
                break;
            }
        }
        if bits >= diff {
            *out_nonce = nonce;
            return OK;
        }
        if nonce > 50_000_000 {
            return ERR_INTERNAL;
        }
    }
    ERR_INTERNAL
}
