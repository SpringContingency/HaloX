#pragma once

// Installs an instrumentation hook on halo2's FUN_180103290 (the render
// helper that dereferences *(this+0xd58) into a d3d11 vtable call). The
// AV at d3d11.dll+0x1059C5 hits when that field is null/stale; the hook
// logs each call's pointer-chain validity, suppresses unsafe calls, and
// mimics the original's post-call counter writes so callers don't loop.
//
// Cheap to leave on — first 6 calls log, then every 1024th. Safe to
// call multiple times; second invocation is a no-op.
void halo2_d3d11_trace_install();
