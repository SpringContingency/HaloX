#pragma once

namespace halox::network {

// Replace g_game_manager's vtable with a logging shim that captures every
// engine_context vtable slot called by halo1 (or any other halo module) during
// launch and early game frames. Each slot logs its first few calls (then
// throttles) and chains to the captured original — so behavior stays identical
// to today, but halox.log now contains a complete list of slots exercised.
//
// Use this to scope the engine_context_shim work: after a halo1 launch run,
// grep halox.log for `ec_log: slot[` to see which slots actually need halox
// implementations. Almost certainly a small subset of MCC's ~190-slot vtable.
//
// Idempotent. NOT compatible with the existing engine_context_shim's
// patch_vtable — only install one or the other on a given object instance.
bool engine_context_logger_install();

// Walk the per-slot call counters and emit a summary of every observed slot
// to halox.log. Call after a launch run to see the captured surface in one
// place instead of having to grep across the whole log.
void engine_context_logger_dump_summary();

} // namespace halox::network
