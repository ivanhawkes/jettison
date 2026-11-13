//! The script suite's module for the limits: fuel, time, memory and the stack, and traps. It calls
//! nothing of the host, so it imports nothing; the bindings bring its panic handler.
#![no_std]

#[path = "jettison_script.rs"]
mod jettison;

use core::hint::black_box;
use core::ptr::{read_volatile, write_volatile};
use core::sync::atomic::{AtomicI32, Ordering};

#[unsafe(export_name = "add")]
pub extern "C" fn add(left: i32, right: i32) -> i32 {
    left.wrapping_add(right)
}

/// Runs until something stops it.
#[unsafe(export_name = "spin")]
pub extern "C" fn spin() -> i32 {
    let mut turns = 0i32;
    loop {
        unsafe { write_volatile(&mut turns, read_volatile(&turns).wrapping_add(1)) }
    }
}

/// A loop of so many turns, for the fuel a call needs.
#[unsafe(export_name = "burn")]
pub extern "C" fn burn(turns: i32) -> i32 {
    let mut done = 0i32;
    for _ in 0..turns {
        unsafe { write_volatile(&mut done, read_volatile(&done) + 1) }
    }
    done
}

/// Kept from call to call while the instance lasts.
static COUNT: AtomicI32 = AtomicI32::new(0);
#[unsafe(export_name = "count")]
pub extern "C" fn count() -> i32 {
    COUNT.fetch_add(1, Ordering::Relaxed) + 1
}

/// Recursion deep enough to run out of the stack it has.
#[unsafe(export_name = "deep")]
pub extern "C" fn deep(depth: i32) -> i32 {
    let pad = black_box([depth as u8; 256]);
    if depth > 0 { deep(depth - 1).wrapping_add(i32::from(black_box(pad)[0])) } else { 0 }
}

/// The page count before, or -1 where the memory may not grow so far.
#[unsafe(export_name = "grow")]
pub extern "C" fn grow(pages: i32) -> i32 {
    core::arch::wasm32::memory_grow(0, pages as usize) as i32
}

/// A read past the end of memory, which the interpreter's bounds check refuses.
#[unsafe(export_name = "wild")]
pub extern "C" fn wild() -> i32 {
    unsafe { read_volatile(black_box(0xffff_fff0usize) as *const i32) }
}

#[unsafe(export_name = "boom")]
pub extern "C" fn boom() {
    core::arch::wasm32::unreachable()
}
