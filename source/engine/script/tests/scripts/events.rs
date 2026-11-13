//! The script suite's module for the host API in Rust: the same handler, inputs and refused calls as
//! events.c, so the suite holds both languages to one behaviour.
#![no_std]

#[path = "jettison_script.rs"]
mod jettison;

use core::sync::atomic::{AtomicI32, Ordering};
use jettison::*;

static PINGS: AtomicI32 = AtomicI32::new(0);
static ANSWERS: AtomicI32 = AtomicI32::new(0);

#[unsafe(export_name = "on_ping")]
pub extern "C" fn on_ping(ping: &Test_Ping) {
    let pings = PINGS.fetch_add(1, Ordering::Relaxed) + 1;
    let mut total = ping.count + ping.numbers.as_slice().iter().map(|&number| number as i64).sum::<i64>();
    if ping.flag {
        total += (ping.r#where.x + ping.r#where.y + ping.r#where.z) as i64;
    }
    let items = [ping.inner, Test_Inner { mood: Test_Mood_Cross, weight: pings }];
    send_Test_Pong(&Test_Pong {
        ship: ping.ship,
        total,
        text: ping.text,
        items: jettison_array_Test_Inner::new(&items),
    });
    log(JETTISON_INFO, ping.text.as_str());
}

#[unsafe(export_name = "pings")]
pub extern "C" fn pings() -> i32 {
    PINGS.load(Ordering::Relaxed)
}

/// Sends a command whose text points past the end of memory.
#[unsafe(export_name = "bad_send")]
pub extern "C" fn bad_send(ping: &Test_Ping) {
    send_Test_Pong(&Test_Pong {
        ship: ping.ship,
        total: 0,
        text: jettison_string { data: 0x7fff_fff0 as *const u8, size: 100 },
        items: jettison_array_Test_Inner::new(&[]),
    });
}

#[unsafe(export_name = "doubled")]
pub extern "C" fn doubled() -> f64 {
    2.0 * input("base")
}

/// What it has answered before, which a fresh instance each call keeps at one.
#[unsafe(export_name = "answers")]
pub extern "C" fn answers() -> f64 {
    (ANSWERS.fetch_add(1, Ordering::Relaxed) + 1) as f64
}

#[unsafe(export_name = "unknown")]
pub extern "C" fn unknown() -> f64 {
    input("nothing")
}

#[unsafe(export_name = "sends")]
pub extern "C" fn sends() -> f64 {
    send_Test_Pong(&Test_Pong {
        ship: JETTISON_NO_ENTITY,
        total: 0,
        text: jettison_string::new(""),
        items: jettison_array_Test_Inner::new(&[]),
    });
    1.0
}

#[unsafe(export_name = "not_a_number")]
pub extern "C" fn not_a_number() -> f64 {
    f64::NAN
}
