//! The script suite's crate that must not compile: each function keeps a component read past the
//! read, which the host frees as the handler returns.
#![no_std]

#[path = "jettison_script.rs"]
mod jettison;

use jettison::*;

pub fn copied(entity: jettison_entity) -> Option<Test_Hold<'static>> {
    read_Test_Hold(entity, |hold| *hold)
}

pub fn field(entity: jettison_entity) -> Option<jettison_string<'static>> {
    read_Test_Hold(entity, |hold| hold.owner)
}

pub fn text(entity: jettison_entity) -> Option<&'static str> {
    read_Test_Hold(entity, |hold| hold.owner.as_str())
}
