//! The script suite's module that imports what the host does not offer.
#![no_std]

#[link(wasm_import_module = "env")]
unsafe extern "C" {
    fn abort();
}

#[unsafe(export_name = "run")]
pub extern "C" fn run() {
    unsafe { abort() }
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}
