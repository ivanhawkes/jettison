//! The script suite's module for what a script keeps by key in Rust: the same calls as keys.c.
#![no_std]

#[path = "jettison_script.rs"]
mod jettison;

use core::sync::atomic::{AtomicI32, Ordering};
use jettison::*;

/// What it has remembered in this instance, which a new instance starts again from nothing.
static HELD: AtomicI32 = AtomicI32::new(0);
static BYTES: [u8; 4096] = [0; 4096];

#[unsafe(export_name = "remember")]
pub extern "C" fn remember(value: i32) {
    HELD.fetch_add(1, Ordering::Relaxed);
    store("count", &value.to_le_bytes());
}

/// The count kept, -1 for none and -2 for bytes that are not a count.
#[unsafe(export_name = "recall")]
pub extern "C" fn recall() -> i32 {
    let mut bytes = [0u8; 4];
    match load("count", &mut bytes) {
        None => -1,
        Some(4) => i32::from_le_bytes(bytes),
        Some(_) => -2,
    }
}

#[unsafe(export_name = "forget")]
pub extern "C" fn forget() {
    store("count", &[]);
}

#[unsafe(export_name = "held")]
pub extern "C" fn held() -> i32 {
    HELD.load(Ordering::Relaxed)
}

/// Keys "k" and the index's two bytes, each of size bytes.
#[unsafe(export_name = "hoard")]
pub extern "C" fn hoard(keys: i32, size: i32) {
    for index in 0..keys {
        let key = [b'k', (index & 0xff) as u8, ((index >> 8) & 0xff) as u8];
        unsafe { jettison_store(key.as_ptr(), 3, BYTES.as_ptr(), size as u32) }
    }
}

#[unsafe(export_name = "long_key")]
pub extern "C" fn long_key() {
    unsafe { jettison_store(BYTES.as_ptr(), 65, BYTES.as_ptr(), 1) }
}

#[unsafe(export_name = "stores")]
pub extern "C" fn stores() -> f64 {
    store("count", &1i32.to_le_bytes());
    1.0
}
