#ifndef LUA_WASM_USER_H
#define LUA_WASM_USER_H

#include <stdlib.h>
#include <stdio.h>

#undef LUA_NUMBER_DOUBLE
#undef LUA_NUMBER
#define LUA_NUMBER int

#undef LUAI_UACNUMBER
#define LUAI_UACNUMBER int

#undef LUA_NUMBER_SCAN
#define LUA_NUMBER_SCAN "%d"

#undef LUA_NUMBER_FMT
#define LUA_NUMBER_FMT "%d"

#undef lua_number2str
#define lua_number2str(s, n) sprintf((s), LUA_NUMBER_FMT, (int)(n))

#undef LUAI_MAXNUMBER2STR
#define LUAI_MAXNUMBER2STR 32

#undef lua_str2number
#define lua_str2number(s, p) lua_wasm_str2number((s), (p))

#undef luai_numadd
#undef luai_numsub
#undef luai_nummul
#undef luai_numdiv
#undef luai_nummod
#undef luai_numpow
#undef luai_numunm
#undef luai_numeq
#undef luai_numlt
#undef luai_numle
#undef luai_numisnan

#define luai_numadd(a, b) ((a) + (b))
#define luai_numsub(a, b) ((a) - (b))
#define luai_nummul(a, b) ((a) * (b))
#define luai_numdiv(a, b) ((a) / (b))
#define luai_nummod(a, b) ((a) % (b))
#define luai_numpow(a, b) lua_wasm_ipow((int)(a), (int)(b))
#define luai_numunm(a) (-(a))
#define luai_numeq(a, b) ((a) == (b))
#define luai_numlt(a, b) ((a) < (b))
#define luai_numle(a, b) ((a) <= (b))
#define luai_numisnan(a) 0

#undef lua_number2int
#undef lua_number2integer
#define lua_number2int(i, d) ((i) = (int)(d))
#define lua_number2integer(i, d) ((i) = (lua_Integer)(d))

#undef LUAI_USER_ALIGNMENT_T
#define LUAI_USER_ALIGNMENT_T union { void *s; long l; int i; }

int lua_wasm_ipow(int a, int b);
LUA_NUMBER lua_wasm_str2number(const char *s, char **endptr);

#endif
