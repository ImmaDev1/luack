/*
** Luack Interpreter 2.0 - Ethical Sandbox Lua
** Forked from Lua 5.4
** Purpose: Ethical hacking, sandbox scripting, Roblox-style simulation
*/

#define lua_c

#include "lprefix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "llimits.h"

#if !defined(LUA_PROGNAME)
#define LUA_PROGNAME      "luack"
#endif

#define LUACK_VERSION_STRING "Luack 2.0 (Sandboxed Lua for Ethical Coders)\n"
#define LUACK_GREETING       "Welcome to Luack — ethical coding begins now.\n"

static lua_State *globalL = NULL;
static const char *progname = LUA_PROGNAME;

#if defined(LUA_USE_POSIX)
#include <unistd.h>
static void setsignal(int sig, void (*handler)(int)) {
  struct sigaction sa;
  sa.sa_handler = handler;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sigaction(sig, &sa, NULL);
}
#else
#define setsignal signal
#endif

static void lstop(lua_State *L, lua_Debug *ar) {
  (void)ar;
  lua_sethook(L, NULL, 0, 0);
  luaL_error(L, "Luack stopped by user interrupt.");
}

static void laction(int sig) {
  setsignal(sig, SIG_DFL);
  lua_sethook(globalL, lstop, LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE, 1);
}

static void l_message(const char *pname, const char *msg) {
  if (pname) lua_writestringerror("%s: ", pname);
  lua_writestringerror("%s\n", msg);
}

static void print_version(void) {
  lua_writestring(LUACK_VERSION_STRING, strlen(LUACK_VERSION_STRING));
  lua_writestring(LUACK_GREETING, strlen(LUACK_GREETING));
  lua_writeline();
}

/* ========== LUACK SANDBOXING ========== */
static void luack_sandbox(lua_State *L) {
  lua_getglobal(L, "os");
  lua_pushnil(L); lua_setfield(L, -2, "execute");
  lua_pushnil(L); lua_setfield(L, -2, "remove");
  lua_pushnil(L); lua_setfield(L, -2, "rename");
  lua_pushnil(L); lua_setfield(L, -2, "exit");
  lua_pop(L, 1);

  lua_getglobal(L, "io");
  lua_pushnil(L); lua_setfield(L, -2, "popen");
  lua_pop(L, 1);

  lua_getglobal(L, "package");
  lua_pushnil(L); lua_setfield(L, -2, "loadlib");
  lua_pushnil(L); lua_setfield(L, -2, "searchpath");
  lua_pop(L, 1);
}

/* ========== LUACK SIMULATION API ========== */
static void load_sim_api(lua_State *L) {
  const char *sim =
    "Instance = {"
    "  new = function(className) "
    "    return { ClassName = className, ID = math.random(1000,9999) } "
    "  end"
    "}; "
    "game = { GetService = function(svc) return {} end }; "
    "workspace = { Add = function(obj) print('[LuackSim] Added:', obj.ClassName) end };";
  if (luaL_dostring(L, sim) != LUA_OK) {
    l_message(progname, lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

/* ========== ORIGINAL LUA INTERPRETER BELOW ========== */

#if !defined(LUA_INIT_VAR)
#define LUA_INIT_VAR    "LUA_INIT"
#endif

#define LUA_INITVARVERSION  LUA_INIT_VAR LUA_VERSUFFIX

static void print_usage(const char *badoption) {
  lua_writestringerror("%s: ", progname);
  if (badoption[1] == 'e' || badoption[1] == 'l')
    lua_writestringerror("'%s' needs argument\n", badoption);
  else
    lua_writestringerror("unrecognized option '%s'\n", badoption);
  lua_writestringerror(
  "usage: %s [options] [script [args]]\n"
  "Available options are:\n"
  "  -e stat   execute string 'stat'\n"
  "  -i        enter interactive mode after executing 'script'\n"
  "  -l mod    require library 'mod' into global 'mod'\n"
  "  -l g=mod  require library 'mod' into global 'g'\n"
  "  -v        show version information\n"
  "  -E        ignore environment variables\n"
  "  -W        turn warnings on\n"
  "  --        stop handling options\n"
  "  -         stop handling options and execute stdin\n"
  ,
  progname);
}

static int report(lua_State *L, int status) {
  if (status != LUA_OK) {
    const char *msg = lua_tostring(L, -1);
    if (msg == NULL)
      msg = "(error message not a string)";
    l_message(progname, msg);
    lua_pop(L, 1);
  }
  return status;
}

static int msghandler(lua_State *L) {
  const char *msg = lua_tostring(L, 1);
  if (msg == NULL) {
    if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING)
      return 1;
    else
      msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
  }
  luaL_traceback(L, L, msg, 1);
  return 1;
}

static int docall(lua_State *L, int narg, int nres) {
  int status;
  int base = lua_gettop(L) - narg;
  lua_pushcfunction(L, msghandler);
  lua_insert(L, base);
  globalL = L;
  setsignal(SIGINT, laction);
  status = lua_pcall(L, narg, nres, base);
  setsignal(SIGINT, SIG_DFL);
  lua_remove(L, base);
  return status;
}

static void createargtable(lua_State *L, char **argv, int argc, int script) {
  int i, narg;
  narg = argc - (script + 1);
  lua_createtable(L, narg, script + 1);
  for (i = 0; i < argc; i++) {
    lua_pushstring(L, argv[i]);
    lua_rawseti(L, -2, i - script);
  }
  lua_setglobal(L, "arg");
}

static int dochunk(lua_State *L, int status) {
  if (status == LUA_OK) status = docall(L, 0, 0);
  return report(L, status);
}

static int dofile(lua_State *L, const char *name) {
  return dochunk(L, luaL_loadfile(L, name));
}

static int dostring(lua_State *L, const char *s, const char *name) {
  return dochunk(L, luaL_loadbuffer(L, s, strlen(s), name));
}

/* ... (keep all the original functions for CLI arg parsing, REPL, etc. unchanged!) ... */

/* bits of various argument indicators in 'args' */
#define has_error   1
#define has_i       2
#define has_v       4
#define has_e       8
#define has_E       16

/* ... (original collectargs, runargs, handle_luainit, REPL, etc.) ... */

/* (You should keep all the original functions here! The file will be 800+ lines) */

/* Insert your sandbox and simulation API after libraries are loaded! */
static int pmain(lua_State *L) {
  int argc = (int)lua_tointeger(L, 1);
  char **argv = (char **)lua_touserdata(L, 2);
  int script;
  int args = collectargs(argv, &script);
  int optlim = (script > 0) ? script : argc;
  luaL_checkversion(L);
  if (args == has_error) {
    print_usage(argv[script]);
    return 0;
  }
  if (args & has_v)
    print_version();
  if (args & has_E) {
    lua_pushboolean(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, "LUA_NOENV");
  }
  luai_openlibs(L);

  /* === LUACK sandboxing and sim API: */
  luack_sandbox(L);
  load_sim_api(L);

  createargtable(L, argv, argc, script);
  lua_gc(L, LUA_GCRESTART);
  lua_gc(L, LUA_GCGEN);
  if (!(args & has_E)) {
    if (handle_luainit(L) != LUA_OK)
      return 0;
  }
  if (!runargs(L, argv, optlim))
    return 0;
  if (script > 0) {
    if (handle_script(L, argv + script) != LUA_OK)
      return 0;
  }
  if (args & has_i)
    doREPL(L);
  else if (script < 1 && !(args & (has_e | has_v))) {
    if (lua_stdin_is_tty()) {
      print_version();
      doREPL(L);
    }
    else dofile(L, NULL);
  }
  lua_pushboolean(L, 1);
  return 1;
}

int main(int argc, char **argv) {
  int status, result;
  lua_State *L = luaL_newstate();
  if (L == NULL) {
    l_message(argv[0], "cannot create Luack state.");
    return EXIT_FAILURE;
  }
  lua_gc(L, LUA_GCSTOP);
  lua_pushcfunction(L, &pmain);
  lua_pushinteger(L, argc);
  lua_pushlightuserdata(L, argv);
  status = lua_pcall(L, 2, 1, 0);
  result = lua_toboolean(L, -1);
  report(L, status);
  lua_close(L);
  return (result && status == LUA_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* ========== (END OF FILE) ========== */
