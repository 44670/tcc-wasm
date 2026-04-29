/*
 * Single-translation-unit Lua 5.1.5 wasm build harness.
 *
 * The upstream Lua sources are included unmodified. We exclude lmathlib.c
 * because this wasm32 backend does not lower double values yet, and provide a
 * small integer math library in lua_wasm_support.c.
 */

#define luaall_c

#include "lopcodes.c"

#include "lapi.c"
#include "lcode.c"
#include "ldebug.c"
#include "ldo.c"
#include "ldump.c"
#include "lfunc.c"
#include "lgc.c"
#include "llex.c"
#include "lmem.c"
#include "lobject.c"
#include "lparser.c"
#include "lstate.c"
#include "lstring.c"
#include "ltable.c"
#include "ltm.c"
#include "lundump.c"
#include "lvm.c"
#include "lzio.c"

#include "lauxlib.c"
#include "lbaselib.c"
#include "ldblib.c"
#include "liolib.c"
#include "loadlib.c"
#include "loslib.c"
#include "lstrlib.c"
#include "ltablib.c"
#include "lua_wasm_support.c"
#include "linit.c"

#include "lua.c"
