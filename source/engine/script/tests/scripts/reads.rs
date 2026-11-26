//! The script suite's module for reading the world in Rust: the same handler and input as reads.c.
#![no_std]

#[path = "jettison_script.rs"]
mod jettison;

use jettison::*;

/// Reads the pinged ship's hold, and its own, which it has none of, and sends what it read.
#[unsafe(export_name = "on_look")]
pub extern "C" fn on_look(ping: &Test_Ping) {
    let read = read_Test_Hold(ping.ship, |hold| (hold.cargo, hold.owner));
    let none = read_Test_Hold(JETTISON_NO_ENTITY, |_| ());
    let (total, text) = match (read, none) {
        (Some(found), None) => found,
        _ => (-1, jettison_string::new("")),
    };
    send_Test_Pong(&Test_Pong {
        ship: ping.ship,
        total,
        text,
        items: jettison_array_Test_Inner::new(&[]),
        sender: Sim_Controller { kind: Sim_ControllerKind_None, slot: 0 },
        order: Test_Order { by: Sim_Controller { kind: Sim_ControllerKind_None, slot: 0 }, amount: 0 },
        orders: jettison_array_Test_Order::new(&[]),
        why: Sim_Reason { key: jettison_string::new(""), arguments: jettison_array_Sim_ReasonArgument::new(&[]) },
    });
}

/// Reads while answering an input, which only an option's inputs may inform.
#[unsafe(export_name = "reads")]
pub extern "C" fn reads() -> f64 {
    if read_Test_Hold(0, |_| ()).is_none() { 0.0 } else { 1.0 }
}

/// A tree's task that asks the game for an input, which may be one this script answers, and then
/// reads, which a task may not, however it was called.
#[unsafe(export_name = "peek")]
pub extern "C" fn peek(context: &mut Script_TaskContext) {
    input("reads");
    context.memory = if read_Test_Hold(0, |_| ()).is_none() { 1 } else { 2 };
    context.status = Script_TaskStatus_Success;
}
