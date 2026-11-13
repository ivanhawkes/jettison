//! The host API for Jettison's scripts, generated from the reflection tables of the game that wrote
//! it (Script::RustModule): the imports a script may be granted, and the game's types a script
//! sees, laid out as the host copies them in and out of a script's memory, under the names the C
//! header gives them. Do not edit it; write it again from the game, which the sample shell does with
//! its console's "script_bindings <directory>".
//!
//! A script is a no_std crate of one file, built as a cdylib for wasm32-unknown-unknown with
//! panic=abort and linked by wasm-ld, which takes this module in with
//!     #[path = "jettison_script.rs"] mod jettison;
//! Its handlers and inputs are exported by name, #[unsafe(export_name = "...")] pub extern "C" fn,
//! and its definition (defs/scripts/<name>.yaml) names them, with what it is granted. A module that
//! imports what it is not granted is refused as it loads; a function here is imported only by a
//! script that calls it. The module brings the crate's panic handler, a trap.
#![allow(non_camel_case_types, non_upper_case_globals, non_snake_case, dead_code)]

use core::mem::{offset_of, size_of};
use core::slice;

pub type jettison_entity = u32;
pub const JETTISON_NO_ENTITY: jettison_entity = 0xffff_ffff;

/// A string's bytes, not terminated, and their count. An event's strings are the host's copy,
/// valid for the handler's call.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct jettison_string {
    pub data: *const u8,
    pub size: u32,
}
impl jettison_string {
    pub const fn new(text: &str) -> Self {
        Self { data: text.as_ptr(), size: text.len() as u32 }
    }
    pub fn as_bytes(&self) -> &[u8] {
        if self.size == 0 { &[] } else { unsafe { slice::from_raw_parts(self.data, self.size as usize) } }
    }
    /// The text, or nothing where it is not UTF-8.
    pub fn as_str(&self) -> &str {
        core::str::from_utf8(self.as_bytes()).unwrap_or("")
    }
}

#[repr(C)] #[derive(Clone, Copy)] pub struct jettison_vec2 { pub x: f32, pub y: f32 }
#[repr(C)] #[derive(Clone, Copy)] pub struct jettison_vec3 { pub x: f32, pub y: f32, pub z: f32 }
#[repr(C)] #[derive(Clone, Copy)] pub struct jettison_vec4 { pub x: f32, pub y: f32, pub z: f32, pub w: f32 }
#[repr(C)] #[derive(Clone, Copy)] pub struct jettison_dvec2 { pub x: f64, pub y: f64 }
#[repr(C)] #[derive(Clone, Copy)] pub struct jettison_dvec3 { pub x: f64, pub y: f64, pub z: f64 }
#[repr(C)] #[derive(Clone, Copy)] pub struct jettison_dvec4 { pub x: f64, pub y: f64, pub z: f64, pub w: f64 }
#[repr(C)] #[derive(Clone, Copy)] pub struct jettison_quat { pub x: f32, pub y: f32, pub z: f32, pub w: f32 }
#[repr(C)] #[derive(Clone, Copy)] pub struct jettison_dquat { pub x: f64, pub y: f64, pub z: f64, pub w: f64 }

pub const JETTISON_DEBUG: i32 = 0;
pub const JETTISON_INFO: i32 = 1;
pub const JETTISON_WARNING: i32 = 2;
pub const JETTISON_ERROR: i32 = 3;

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct jettison_array_int32_t {
    pub data: *const i32,
    pub size: u32,
}
impl jettison_array_int32_t {
    pub const fn new(items: &[i32]) -> Self {
        Self { data: items.as_ptr(), size: items.len() as u32 }
    }
    pub fn as_slice(&self) -> &[i32] {
        if self.size == 0 { &[] } else { unsafe { slice::from_raw_parts(self.data, self.size as usize) } }
    }
}

/// Test.Mood
pub type Test_Mood = u8;
pub const Test_Mood_Calm: Test_Mood = 0;
pub const Test_Mood_Cross: Test_Mood = 1;

/// Test.Inner
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Test_Inner {
    pub mood: Test_Mood,
    pub weight: i32,
}
const _: () = assert!(size_of::<Test_Inner>() == 8);
const _: () = assert!(offset_of!(Test_Inner, mood) == 0);
const _: () = assert!(offset_of!(Test_Inner, weight) == 4);

/// Test.Ping
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Test_Ping {
    pub ship: jettison_entity,
    pub count: i64,
    pub value: f64,
    pub text: jettison_string,
    pub numbers: jettison_array_int32_t,
    pub inner: Test_Inner,
    pub r#where: jettison_dvec3,
    pub flag: bool,
    // hidden (std::optional<int>): not seen by scripts
}
const _: () = assert!(size_of::<Test_Ping>() == 80);
const _: () = assert!(offset_of!(Test_Ping, ship) == 0);
const _: () = assert!(offset_of!(Test_Ping, count) == 8);
const _: () = assert!(offset_of!(Test_Ping, value) == 16);
const _: () = assert!(offset_of!(Test_Ping, text) == 24);
const _: () = assert!(offset_of!(Test_Ping, numbers) == 32);
const _: () = assert!(offset_of!(Test_Ping, inner) == 40);
const _: () = assert!(offset_of!(Test_Ping, r#where) == 48);
const _: () = assert!(offset_of!(Test_Ping, flag) == 72);

#[repr(C)]
#[derive(Clone, Copy)]
pub struct jettison_array_Test_Inner {
    pub data: *const Test_Inner,
    pub size: u32,
}
impl jettison_array_Test_Inner {
    pub const fn new(items: &[Test_Inner]) -> Self {
        Self { data: items.as_ptr(), size: items.len() as u32 }
    }
    pub fn as_slice(&self) -> &[Test_Inner] {
        if self.size == 0 { &[] } else { unsafe { slice::from_raw_parts(self.data, self.size as usize) } }
    }
}

/// Test.Pong
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Test_Pong {
    pub ship: jettison_entity,
    pub total: i64,
    pub text: jettison_string,
    pub items: jettison_array_Test_Inner,
}
const _: () = assert!(size_of::<Test_Pong>() == 32);
const _: () = assert!(offset_of!(Test_Pong, ship) == 0);
const _: () = assert!(offset_of!(Test_Pong, total) == 8);
const _: () = assert!(offset_of!(Test_Pong, text) == 16);
const _: () = assert!(offset_of!(Test_Pong, items) == 24);

#[link(wasm_import_module = "jettison")]
unsafe extern "C" {
    /// Granted by "log": a line in the game's log, under the script's name.
    #[link_name = "log"]
    pub fn jettison_log(severity: i32, text: *const u8, size: u32);
    /// For a script that answers inputs, and only while it answers one: the option's input of
    /// that name, as the game works it out. A name the game does not know traps.
    #[link_name = "input"]
    pub fn jettison_input(name: *const u8, size: u32) -> f64;
    /// Granted by "store": keeps the bytes under the key, for this script alone and in the
    /// game's saves; no bytes take the key away. A store past the script's keys or bytes
    /// traps, as does one while it answers an input.
    #[link_name = "store"]
    pub fn jettison_store(key: *const u8, key_size: u32, data: *const u8, size: u32);
    /// Granted by "store": copies what is kept under the key into the buffer, as much as
    /// fits, and returns how many bytes are kept, or -1 for none.
    #[link_name = "load"]
    pub fn jettison_load(key: *const u8, key_size: u32, buffer: *mut u8, capacity: u32) -> i32;
    /// Granted by "send:Test.Pong": the command, into the game's queue, applied at the start of the
    /// next step and recorded with the player's.
    #[link_name = "send:Test.Pong"]
    pub fn jettison_send_Test_Pong(command: *const Test_Pong);
}

pub fn log(severity: i32, text: &str) {
    unsafe { jettison_log(severity, text.as_ptr(), text.len() as u32) }
}

pub fn input(name: &str) -> f64 {
    unsafe { jettison_input(name.as_ptr(), name.len() as u32) }
}

pub fn store(key: &str, data: &[u8]) {
    unsafe { jettison_store(key.as_ptr(), key.len() as u32, data.as_ptr(), data.len() as u32) }
}

/// How many bytes are kept under the key, as much of them as fits copied into the buffer.
pub fn load(key: &str, buffer: &mut [u8]) -> Option<usize> {
    let kept = unsafe {
        jettison_load(key.as_ptr(), key.len() as u32, buffer.as_mut_ptr(), buffer.len() as u32)
    };
    if kept < 0 { None } else { Some(kept as usize) }
}

pub fn send_Test_Pong(command: &Test_Pong) {
    unsafe { jettison_send_Test_Pong(command) }
}
