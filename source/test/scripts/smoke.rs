//! The smoke mod's script (source/test/mods/smoke/defs/scripts/smoke.yaml): it says what it hears of
//! each trade filled, counting them under a key of its own, and works out how crowded a run's shelf is for the traders' decision. Built
//! against the shell's bindings beside it, which the app suite checks are this build's.
#![no_std]

#[path = "jettison_script.rs"]
mod jettison;

use core::fmt::Write;
use jettison::*;

/// A line of the log, as long as it fits.
struct Line {
    bytes: [u8; 160],
    length: usize,
}

impl Line {
    const fn new() -> Self {
        Self { bytes: [0; 160], length: 0 }
    }
    fn as_str(&self) -> &str {
        core::str::from_utf8(&self.bytes[..self.length]).unwrap_or("")
    }
}

impl Write for Line {
    fn write_str(&mut self, text: &str) -> core::fmt::Result {
        let room = (self.bytes.len() - self.length).min(text.len());
        self.bytes[self.length..self.length + room].copy_from_slice(&text.as_bytes()[..room]);
        self.length += room;
        Ok(())
    }
}

/// The trades heard, kept under a key so the count is the game's, and a save carries it: a version
/// byte first, should the count ever be kept another way, then the count.
const HEARD: &str = "heard";

fn heard_before() -> u32 {
    let mut bytes = [0u8; 5];
    match load(HEARD, &mut bytes) {
        Some(5) if bytes[0] == 1 => u32::from_le_bytes([bytes[1], bytes[2], bytes[3], bytes[4]]),
        _ => 0,
    }
}

#[unsafe(export_name = "on_traded")]
pub extern "C" fn on_traded(traded: &Shell_Traded) {
    let heard = heard_before() + 1;
    let count = heard.to_le_bytes();
    store(HEARD, &[1, count[0], count[1], count[2], count[3]]);
    let mut line = Line::new();
    let _ = write!(
        line,
        "heard trade {}: {} {} crate(s) of {} at {} credits",
        heard,
        if traded.crates > 0 { "bought" } else { "sold" },
        traded.crates.unsigned_abs(),
        traded.good.as_str(),
        traded.price
    );
    log(JETTISON_INFO, line.as_str());
}

/// One for a run no rival is after, a half for one with a rival, a third for two.
#[unsafe(export_name = "crowding")]
pub extern "C" fn crowding() -> f64 {
    1.0 / (1.0 + input("rivals"))
}
