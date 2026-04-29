#include <stdlib.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

LUA_NUMBER lua_wasm_str2number(const char *s, char **endptr)
{
    const char *p = s;
    int sign = 1;
    int value = 0;
    int frac_div = 1;
    int exp_sign = 1;
    int exp = 0;
    int saw_digit = 0;
    int i;

    if (*p == '+' || *p == '-') {
        if (*p == '-')
            sign = -1;
        ++p;
    }
    while (*p >= '0' && *p <= '9') {
        saw_digit = 1;
        value = value * 10 + *p++ - '0';
    }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            saw_digit = 1;
            if (frac_div < 100000000) {
                value = value * 10 + *p - '0';
                frac_div *= 10;
            }
            ++p;
        }
    }
    if (saw_digit && (*p == 'e' || *p == 'E')) {
        const char *e = p + 1;
        int saw_exp = 0;
        if (*e == '+' || *e == '-') {
            if (*e == '-')
                exp_sign = -1;
            ++e;
        }
        while (*e >= '0' && *e <= '9') {
            saw_exp = 1;
            exp = exp * 10 + *e++ - '0';
        }
        if (saw_exp)
            p = e;
    }
    value *= sign;
    for (i = 0; i < exp; ++i) {
        if (exp_sign > 0)
            value *= 10;
        else
            frac_div *= 10;
    }
    if (frac_div > 1)
        value /= frac_div;
    if (endptr)
        *endptr = (char *)(saw_digit ? p : s);
    return value;
}

int lua_wasm_ipow(int a, int b)
{
    int r = 1;
    int neg = 0;

    if (b < 0) {
        neg = 1;
        b = -b;
    }
    while (b) {
        if (b & 1)
            r *= a;
        b >>= 1;
        if (b)
            a *= a;
    }
    return neg ? 0 : r;
}

static int wasm_math_abs(lua_State *L)
{
    int v = luaL_checkint(L, 1);
    lua_pushinteger(L, v < 0 ? -v : v);
    return 1;
}

static int wasm_math_max(lua_State *L)
{
    int n = lua_gettop(L);
    int best = luaL_checkint(L, 1);
    int i;

    for (i = 2; i <= n; ++i) {
        int v = luaL_checkint(L, i);
        if (v > best)
            best = v;
    }
    lua_pushinteger(L, best);
    return 1;
}

static int wasm_math_min(lua_State *L)
{
    int n = lua_gettop(L);
    int best = luaL_checkint(L, 1);
    int i;

    for (i = 2; i <= n; ++i) {
        int v = luaL_checkint(L, i);
        if (v < best)
            best = v;
    }
    lua_pushinteger(L, best);
    return 1;
}

static int wasm_math_pow(lua_State *L)
{
    lua_pushinteger(L, lua_wasm_ipow(luaL_checkint(L, 1), luaL_checkint(L, 2)));
    return 1;
}

static int wasm_math_mod(lua_State *L)
{
    int b = luaL_checkint(L, 2);
    lua_pushinteger(L, b ? luaL_checkint(L, 1) % b : 0);
    return 1;
}

static int wasm_math_floor(lua_State *L)
{
    lua_pushinteger(L, luaL_checkint(L, 1));
    return 1;
}

static int wasm_math_ceil(lua_State *L)
{
    lua_pushinteger(L, luaL_checkint(L, 1));
    return 1;
}

static int wasm_math_random(lua_State *L)
{
    int n = lua_gettop(L);
    int r = rand() & 0x7fffffff;

    if (n == 0) {
        lua_pushinteger(L, r);
    } else if (n == 1) {
        int upper = luaL_checkint(L, 1);
        lua_pushinteger(L, upper > 0 ? (r % upper) + 1 : 1);
    } else {
        int lower = luaL_checkint(L, 1);
        int upper = luaL_checkint(L, 2);
        int span = upper - lower + 1;
        lua_pushinteger(L, span > 0 ? lower + (r % span) : lower);
    }
    return 1;
}

static int wasm_math_randomseed(lua_State *L)
{
    srand((unsigned)luaL_checkint(L, 1));
    return 0;
}

static const luaL_Reg wasm_mathlib[] = {
    { "abs", wasm_math_abs },
    { "ceil", wasm_math_ceil },
    { "floor", wasm_math_floor },
    { "max", wasm_math_max },
    { "min", wasm_math_min },
    { "mod", wasm_math_mod },
    { "pow", wasm_math_pow },
    { "random", wasm_math_random },
    { "randomseed", wasm_math_randomseed },
    { 0, 0 }
};

int luaopen_math(lua_State *L)
{
    luaL_register(L, LUA_MATHLIBNAME, wasm_mathlib);
    return 1;
}
