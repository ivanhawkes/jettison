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

/// Ai.CruiseEnded
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Ai_CruiseEnded {
    pub ship: jettison_entity,
    pub body: jettison_string,
}
const _: () = assert!(size_of::<Ai_CruiseEnded>() == 12);
const _: () = assert!(offset_of!(Ai_CruiseEnded, ship) == 0);
const _: () = assert!(offset_of!(Ai_CruiseEnded, body) == 4);

/// Sim.ReasonValue
pub type Sim_ReasonValue = u8;
pub const Sim_ReasonValue_Integer: Sim_ReasonValue = 0;
pub const Sim_ReasonValue_Entity: Sim_ReasonValue = 1;
pub const Sim_ReasonValue_Identifier: Sim_ReasonValue = 2;

/// Sim.ReasonArgument
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Sim_ReasonArgument {
    pub kind: Sim_ReasonValue,
    pub integer: i64,
    pub entity: jettison_entity,
    pub identifier: jettison_string,
    pub definition: jettison_string,
}
const _: () = assert!(size_of::<Sim_ReasonArgument>() == 40);
const _: () = assert!(offset_of!(Sim_ReasonArgument, kind) == 0);
const _: () = assert!(offset_of!(Sim_ReasonArgument, integer) == 8);
const _: () = assert!(offset_of!(Sim_ReasonArgument, entity) == 16);
const _: () = assert!(offset_of!(Sim_ReasonArgument, identifier) == 20);
const _: () = assert!(offset_of!(Sim_ReasonArgument, definition) == 28);

#[repr(C)]
#[derive(Clone, Copy)]
pub struct jettison_array_Sim_ReasonArgument {
    pub data: *const Sim_ReasonArgument,
    pub size: u32,
}
impl jettison_array_Sim_ReasonArgument {
    pub const fn new(items: &[Sim_ReasonArgument]) -> Self {
        Self { data: items.as_ptr(), size: items.len() as u32 }
    }
    pub fn as_slice(&self) -> &[Sim_ReasonArgument] {
        if self.size == 0 { &[] } else { unsafe { slice::from_raw_parts(self.data, self.size as usize) } }
    }
}

/// Sim.Reason
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Sim_Reason {
    pub key: jettison_string,
    pub arguments: jettison_array_Sim_ReasonArgument,
}
const _: () = assert!(size_of::<Sim_Reason>() == 16);
const _: () = assert!(offset_of!(Sim_Reason, key) == 0);
const _: () = assert!(offset_of!(Sim_Reason, arguments) == 8);

/// Ai.CruiseRefused
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Ai_CruiseRefused {
    pub ship: jettison_entity,
    pub body: jettison_string,
    pub reason: Sim_Reason,
}
const _: () = assert!(size_of::<Ai_CruiseRefused>() == 28);
const _: () = assert!(offset_of!(Ai_CruiseRefused, ship) == 0);
const _: () = assert!(offset_of!(Ai_CruiseRefused, body) == 4);
const _: () = assert!(offset_of!(Ai_CruiseRefused, reason) == 12);

/// Ai.CruiseStarted
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Ai_CruiseStarted {
    pub ship: jettison_entity,
    pub body: jettison_string,
    pub seconds: f64,
    pub metres: f64,
    pub arrivalStep: u64,
}
const _: () = assert!(size_of::<Ai_CruiseStarted>() == 40);
const _: () = assert!(offset_of!(Ai_CruiseStarted, ship) == 0);
const _: () = assert!(offset_of!(Ai_CruiseStarted, body) == 4);
const _: () = assert!(offset_of!(Ai_CruiseStarted, seconds) == 16);
const _: () = assert!(offset_of!(Ai_CruiseStarted, metres) == 24);
const _: () = assert!(offset_of!(Ai_CruiseStarted, arrivalStep) == 32);

/// Ai.GatePassed
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Ai_GatePassed {
    pub ship: jettison_entity,
    pub gate: jettison_entity,
    pub reason: Sim_Reason,
}
const _: () = assert!(size_of::<Ai_GatePassed>() == 24);
const _: () = assert!(offset_of!(Ai_GatePassed, ship) == 0);
const _: () = assert!(offset_of!(Ai_GatePassed, gate) == 4);
const _: () = assert!(offset_of!(Ai_GatePassed, reason) == 8);

/// Ai.OnLane
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Ai_OnLane {
    pub from: u64,
    pub departure: jettison_string,
    pub to: u64,
    pub arrival: jettison_string,
    pub arrivalStep: u64,
}
const _: () = assert!(size_of::<Ai_OnLane>() == 40);
const _: () = assert!(offset_of!(Ai_OnLane, from) == 0);
const _: () = assert!(offset_of!(Ai_OnLane, departure) == 8);
const _: () = assert!(offset_of!(Ai_OnLane, to) == 16);
const _: () = assert!(offset_of!(Ai_OnLane, arrival) == 24);
const _: () = assert!(offset_of!(Ai_OnLane, arrivalStep) == 32);

/// Ai.LaneEntered
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Ai_LaneEntered {
    pub entity: jettison_entity,
    pub lane: Ai_OnLane,
    pub transitSeconds: f64,
}
const _: () = assert!(size_of::<Ai_LaneEntered>() == 56);
const _: () = assert!(offset_of!(Ai_LaneEntered, entity) == 0);
const _: () = assert!(offset_of!(Ai_LaneEntered, lane) == 8);
const _: () = assert!(offset_of!(Ai_LaneEntered, transitSeconds) == 48);

/// Ai.LaneLeft
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Ai_LaneLeft {
    pub entity: jettison_entity,
    pub system: u64,
    pub arrival: jettison_string,
}
const _: () = assert!(size_of::<Ai_LaneLeft>() == 24);
const _: () = assert!(offset_of!(Ai_LaneLeft, entity) == 0);
const _: () = assert!(offset_of!(Ai_LaneLeft, system) == 8);
const _: () = assert!(offset_of!(Ai_LaneLeft, arrival) == 16);

/// Ai.LaneRefused
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Ai_LaneRefused {
    pub entity: jettison_entity,
    pub jumpPoint: jettison_string,
    pub reason: Sim_Reason,
}
const _: () = assert!(size_of::<Ai_LaneRefused>() == 28);
const _: () = assert!(offset_of!(Ai_LaneRefused, entity) == 0);
const _: () = assert!(offset_of!(Ai_LaneRefused, jumpPoint) == 4);
const _: () = assert!(offset_of!(Ai_LaneRefused, reason) == 12);

/// Ai.TransitCleared
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Ai_TransitCleared {
    pub ship: jettison_entity,
    pub gate: jettison_entity,
    pub toll: i64,
    pub lapses: u64,
}
const _: () = assert!(size_of::<Ai_TransitCleared>() == 24);
const _: () = assert!(offset_of!(Ai_TransitCleared, ship) == 0);
const _: () = assert!(offset_of!(Ai_TransitCleared, gate) == 4);
const _: () = assert!(offset_of!(Ai_TransitCleared, toll) == 8);
const _: () = assert!(offset_of!(Ai_TransitCleared, lapses) == 16);

/// Ai.TransitLapsed
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Ai_TransitLapsed {
    pub ship: jettison_entity,
    pub gate: jettison_entity,
}
const _: () = assert!(size_of::<Ai_TransitLapsed>() == 8);
const _: () = assert!(offset_of!(Ai_TransitLapsed, ship) == 0);
const _: () = assert!(offset_of!(Ai_TransitLapsed, gate) == 4);

/// Ai.TransitQueued
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Ai_TransitQueued {
    pub ship: jettison_entity,
    pub gate: jettison_entity,
    pub place: u32,
}
const _: () = assert!(size_of::<Ai_TransitQueued>() == 12);
const _: () = assert!(offset_of!(Ai_TransitQueued, ship) == 0);
const _: () = assert!(offset_of!(Ai_TransitQueued, gate) == 4);
const _: () = assert!(offset_of!(Ai_TransitQueued, place) == 8);

/// Ai.TransitRefused
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Ai_TransitRefused {
    pub ship: jettison_entity,
    pub gate: jettison_entity,
    pub reason: Sim_Reason,
}
const _: () = assert!(size_of::<Ai_TransitRefused>() == 24);
const _: () = assert!(offset_of!(Ai_TransitRefused, ship) == 0);
const _: () = assert!(offset_of!(Ai_TransitRefused, gate) == 4);
const _: () = assert!(offset_of!(Ai_TransitRefused, reason) == 8);

/// Ai.TransitSent
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Ai_TransitSent {
    pub ship: jettison_entity,
    pub gate: jettison_entity,
    pub toll: i64,
}
const _: () = assert!(size_of::<Ai_TransitSent>() == 16);
const _: () = assert!(offset_of!(Ai_TransitSent, ship) == 0);
const _: () = assert!(offset_of!(Ai_TransitSent, gate) == 4);
const _: () = assert!(offset_of!(Ai_TransitSent, toll) == 8);

/// Dock.Landed
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Dock_Landed {
    pub ship: jettison_entity,
    pub station: jettison_entity,
    pub pad: jettison_entity,
    pub number: u32,
}
const _: () = assert!(size_of::<Dock_Landed>() == 16);
const _: () = assert!(offset_of!(Dock_Landed, ship) == 0);
const _: () = assert!(offset_of!(Dock_Landed, station) == 4);
const _: () = assert!(offset_of!(Dock_Landed, pad) == 8);
const _: () = assert!(offset_of!(Dock_Landed, number) == 12);

/// Dock.Left
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Dock_Left {
    pub ship: jettison_entity,
    pub station: jettison_entity,
    pub pad: jettison_entity,
    pub number: u32,
}
const _: () = assert!(size_of::<Dock_Left>() == 16);
const _: () = assert!(offset_of!(Dock_Left, ship) == 0);
const _: () = assert!(offset_of!(Dock_Left, station) == 4);
const _: () = assert!(offset_of!(Dock_Left, pad) == 8);
const _: () = assert!(offset_of!(Dock_Left, number) == 12);

/// Dock.PadGiven
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Dock_PadGiven {
    pub ship: jettison_entity,
    pub station: jettison_entity,
    pub pad: jettison_entity,
    pub number: u32,
}
const _: () = assert!(size_of::<Dock_PadGiven>() == 16);
const _: () = assert!(offset_of!(Dock_PadGiven, ship) == 0);
const _: () = assert!(offset_of!(Dock_PadGiven, station) == 4);
const _: () = assert!(offset_of!(Dock_PadGiven, pad) == 8);
const _: () = assert!(offset_of!(Dock_PadGiven, number) == 12);

/// Dock.PadRefused
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Dock_PadRefused {
    pub ship: jettison_entity,
    pub station: jettison_entity,
    pub reason: Sim_Reason,
}
const _: () = assert!(size_of::<Dock_PadRefused>() == 24);
const _: () = assert!(offset_of!(Dock_PadRefused, ship) == 0);
const _: () = assert!(offset_of!(Dock_PadRefused, station) == 4);
const _: () = assert!(offset_of!(Dock_PadRefused, reason) == 8);

/// Sim.ControllerKind
pub type Sim_ControllerKind = u8;
pub const Sim_ControllerKind_None: Sim_ControllerKind = 0;
pub const Sim_ControllerKind_Player: Sim_ControllerKind = 1;
pub const Sim_ControllerKind_Ai: Sim_ControllerKind = 2;

/// Sim.Controller
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Sim_Controller {
    pub kind: Sim_ControllerKind,
    pub slot: u32,
}
const _: () = assert!(size_of::<Sim_Controller>() == 8);
const _: () = assert!(offset_of!(Sim_Controller, kind) == 0);
const _: () = assert!(offset_of!(Sim_Controller, slot) == 4);

/// Shell.ShipBought
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Shell_ShipBought {
    pub buyer: Sim_Controller,
    pub ship: jettison_entity,
    pub station: jettison_entity,
    pub hull: jettison_string,
    pub price: i64,
    pub credits: i64,
}
const _: () = assert!(size_of::<Shell_ShipBought>() == 40);
const _: () = assert!(offset_of!(Shell_ShipBought, buyer) == 0);
const _: () = assert!(offset_of!(Shell_ShipBought, ship) == 8);
const _: () = assert!(offset_of!(Shell_ShipBought, station) == 12);
const _: () = assert!(offset_of!(Shell_ShipBought, hull) == 16);
const _: () = assert!(offset_of!(Shell_ShipBought, price) == 24);
const _: () = assert!(offset_of!(Shell_ShipBought, credits) == 32);

/// Shell.Trade
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Shell_Trade {
    pub sender: Sim_Controller,
    pub ship: jettison_entity,
    pub market: jettison_entity,
    pub good: jettison_string,
    pub crates: i64,
}
const _: () = assert!(size_of::<Shell_Trade>() == 32);
const _: () = assert!(offset_of!(Shell_Trade, sender) == 0);
const _: () = assert!(offset_of!(Shell_Trade, ship) == 8);
const _: () = assert!(offset_of!(Shell_Trade, market) == 12);
const _: () = assert!(offset_of!(Shell_Trade, good) == 16);
const _: () = assert!(offset_of!(Shell_Trade, crates) == 24);

/// Shell.TradeRefused
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Shell_TradeRefused {
    pub ship: jettison_entity,
    pub good: jettison_string,
    pub reason: Sim_Reason,
}
const _: () = assert!(size_of::<Shell_TradeRefused>() == 28);
const _: () = assert!(offset_of!(Shell_TradeRefused, ship) == 0);
const _: () = assert!(offset_of!(Shell_TradeRefused, good) == 4);
const _: () = assert!(offset_of!(Shell_TradeRefused, reason) == 12);

/// Shell.Traded
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Shell_Traded {
    pub ship: jettison_entity,
    pub market: jettison_entity,
    pub good: jettison_string,
    pub crates: i64,
    pub price: i64,
    pub credits: i64,
}
const _: () = assert!(size_of::<Shell_Traded>() == 40);
const _: () = assert!(offset_of!(Shell_Traded, ship) == 0);
const _: () = assert!(offset_of!(Shell_Traded, market) == 4);
const _: () = assert!(offset_of!(Shell_Traded, good) == 8);
const _: () = assert!(offset_of!(Shell_Traded, crates) == 16);
const _: () = assert!(offset_of!(Shell_Traded, price) == 24);
const _: () = assert!(offset_of!(Shell_Traded, credits) == 32);

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
    /// Granted by "send:Shell.Trade": the command, into the game's queue, applied at the start of the
    /// next step and recorded with the player's.
    #[link_name = "send:Shell.Trade"]
    pub fn jettison_send_Shell_Trade(command: *const Shell_Trade);
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

pub fn send_Shell_Trade(command: &Shell_Trade) {
    unsafe { jettison_send_Shell_Trade(command) }
}
