
/*
** Luack Interpreter 2.0 - Ethical Sandbox Lua
** Forked from Lua 5.4
** Purpose: Ethical hacking, sandbox scripting, Roblox-style simulation
*/

#define lua_c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <locale.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

// Forward declarations for new functions
void luack_sandbox_enhanced(lua_State *L);
void load_sim_api_extended(lua_State *L);
void luack_open_custom_libs(lua_State *L);
void luack_register_debug_funcs(lua_State *L);

#define LUACK_VERSION_STRING "Luack 2.0 (Sandboxed Lua for Ethical Coders)\n"
#define LUACK_GREETING       "Welcome to Luack — ethical coding begins now.\n"

static lua_State *globalL = NULL;
static const char *progname = "luack";

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

static void load_sim_api(lua_State *L) {
  const char *sim =
    "Instance = {"
    "  new = function(className) "
    "    return { ClassName = className, ID = math.random(1000,9999) } "
    "  end"
    "}; "
    "game = { GetService = function(svc) return {} end }; "
    "workspace = { Add = function(obj) print(\\\'[LuackSim] Added:\\\', obj.ClassName) end };";

  if (luaL_dostring(L, sim) != LUA_OK) {
    l_message(progname, lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

static void run_REPL(lua_State *L) {
  char input[512];
  print_version();

  while (1) {
    fputs("> ", stdout);
    if (!fgets(input, sizeof(input), stdin)) break;
    if (strncmp(input, "exit", 4) == 0) break;
    int status = luaL_loadstring(L, input);
    if (status == LUA_OK) {
      status = lua_pcall(L, 0, LUA_MULTRET, 0);
      if (status != LUA_OK) {
        l_message(progname, lua_tostring(L, -1));
        lua_pop(L, 1);
      }
    } else {
      l_message(progname, lua_tostring(L, -1));
      lua_pop(L, 1);
    }
  }
}

int main(int argc, char **argv) {
  lua_State *L = luaL_newstate();
  if (L == NULL) {
    l_message(argv[0], "cannot create Luack state.");
    return EXIT_FAILURE;
  }

  lua_gc(L, LUA_GCSTOP);
  luaL_openlibs(L);
  luack_open_custom_libs(L);
  luack_register_debug_funcs(L);
  luack_sandbox_enhanced(L);
  load_sim_api_extended(L);

  if (argc > 1) {
    int status = luaL_dofile(L, argv[1]);
    if (status != LUA_OK) {
      l_message(argv[1], lua_tostring(L, -1));
      lua_pop(L, 1);
    }
  } else {
    run_REPL(L);
  }

  lua_close(L);
  return EXIT_SUCCESS;
}



/* Enhanced Sandboxing Features */

static int luack_panic(lua_State *L) {
  l_message(progname, "PANIC: unprotected error in call to Lua API (");
  l_message(progname, lua_tostring(L, -1));
  l_message(progname, ")");
  return 0;
}

static int luack_pcall_error_handler(lua_State *L) {
  const char *msg = lua_tostring(L, 1);
  if (msg == NULL) {  /* is error object not a string? */
    if (luaL_callmeta(L, 1, "__tostring") &&  /* does it have a metamethod */
        lua_type(L, -1) == LUA_TSTRING)  /* that produces a string? */
      return 1;  /* that is the message */
    else
      msg = lua_pushfstring(L, "(error object is a %s value)",
                               luaL_typename(L, 1));
  }
  luaL_traceback(L, L, msg, 1);
  return 1;
}

static int luack_protected_dofile(lua_State *L, const char *filename) {
  int status = luaL_loadfile(L, filename);
  if (status == LUA_OK) {
    int base = lua_gettop(L);
    lua_pushcfunction(L, luack_pcall_error_handler);
    lua_insert(L, base);
    status = lua_pcall(L, 0, LUA_MULTRET, base);
    lua_remove(L, base);
  }
  return status;
}

static int luack_protected_dostring(lua_State *L, const char *str) {
  int status = luaL_loadstring(L, str);
  if (status == LUA_OK) {
    int base = lua_gettop(L);
    lua_pushcfunction(L, luack_pcall_error_handler);
    lua_insert(L, base);
    status = lua_pcall(L, 0, LUA_MULTRET, base);
    lua_remove(L, base);
  }
  return status;
}

static int luack_os_time(lua_State *L) {
  lua_pushinteger(L, (lua_Integer)time(NULL));
  return 1;
}

static int luack_os_date(lua_State *L) {
  const char *s = luaL_optstring(L, 1, "%c");
  time_t t = luaL_optinteger(L, 2, time(NULL));
  struct tm *stm;
  if (*s == '!')  {
    stm = gmtime(&t);
    s++;
  }  /* UTC time? */
  else
    stm = localtime(&t);
  if (stm == NULL)  /* invalid date? */
    lua_pushnil(L); /* then return nil */
  else {
    char buff[256];
    if (strftime(buff, sizeof(buff), s, stm))
      lua_pushstring(L, buff);
    else
      luaL_error(L, "date format error");
  }
  return 1;
}

static int luack_os_clock(lua_State *L) {
  lua_pushnumber(L, ((lua_Number)clock())/(lua_Number)CLOCKS_PER_SEC);
  return 1;
}

static int luack_os_getenv(lua_State *L) {
  lua_pushstring(L, getenv(luaL_checkstring(L, 1)));
  return 1;
}

static int luack_os_setlocale(lua_State *L) {
  static const int cat[] = {LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY, LC_NUMERIC, LC_TIME};
  static const char *const catnames[] = {"all", "collate", "ctype", "monetary", "numeric", "time", NULL};
  const char *l = luaL_optstring(L, 1, NULL);
  int op = luaL_checkoption(L, 2, "all", catnames);
  lua_pushstring(L, setlocale(cat[op], l));
  return 1;
}

static int luack_os_tmpname(lua_State *L) {
  char buff[L_tmpnam];
  if (tmpnam(buff) != NULL)
    lua_pushstring(L, buff);
  else
    luaL_error(L, "unable to generate a unique filename");
  return 1;
}

static const luaL_Reg luack_os_funcs[] = {
  {"clock", luack_os_clock},
  {"date", luack_os_date},
  {"getenv", luack_os_getenv},
  {"time", luack_os_time},
  {"setlocale", luack_os_setlocale},
  {"tmpname", luack_os_tmpname},
  {NULL, NULL}
};

static void luack_open_os_extended(lua_State *L) {
  luaL_newlib(L, luack_os_funcs);
  lua_setglobal(L, "os");
}
void luack_sandbox_enhanced(lua_State *L) {
  luack_open_os_extended(L);

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

  lua_atpanic(L, luack_panic);
}





/* Expanded Simulation API */

// Forward declarations for new Instance properties and methods
static int luack_instance_newindex(lua_State *L);
static int luack_instance_index(lua_State *L);
static int luack_instance_tostring(lua_State *L);
static int luack_instance_destroy(lua_State *L);
static int luack_instance_findfirstchild(lua_State *L);
static int luack_instance_getchildren(lua_State *L);
static int luack_instance_addchild(lua_State *L);
static int luack_instance_removechild(lua_State *L);

// Metatable name for Luack Instance objects
#define LUACK_INSTANCE_METATABLE "Luack.Instance"

typedef struct LuackInstance {
  char *ClassName;
  int ID;
  // Simple properties for demonstration
  double PositionX, PositionY, PositionZ;
  double SizeX, SizeY, SizeZ;
  char *Name;
  struct LuackInstance **Children; // Array of child instances
  int ChildCount;
  int ChildCapacity;
} LuackInstance;

static LuackInstance *luack_newinstance_udata(lua_State *L) {
  LuackInstance *instance = (LuackInstance *)lua_newuserdatauv(L, sizeof(LuackInstance), 0);
  instance->ClassName = NULL;
  instance->ID = 0;
  instance->PositionX = instance->PositionY = instance->PositionZ = 0.0;
  instance->SizeX = instance->SizeY = instance->SizeZ = 1.0;
  instance->Name = NULL;
  instance->Children = NULL;
  instance->ChildCount = 0;
  instance->ChildCapacity = 0;

  // Set metatable for the new userdata
  luaL_getmetatable(L, LUACK_INSTANCE_METATABLE);
  lua_setmetatable(L, -2);
  return instance;
}

static int luack_instance_new(lua_State *L) {
  const char *className = luaL_checkstring(L, 1);
  LuackInstance *instance = luack_newinstance_udata(L);
  instance->ClassName = strdup(className);
  instance->ID = (int)(lua_tonumber(L, lua_upvalueindex(1)) * 10000 + lua_tonumber(L, lua_upvalueindex(2))); // Using upvalues for random seed
  instance->Name = strdup(className);
  lua_pushvalue(L, -1); // Push the new instance to be returned
  return 1;
}

static int luack_instance_gc(lua_State *L) {
  LuackInstance *instance = (LuackInstance *)luaL_checkudata(L, 1, LUACK_INSTANCE_METATABLE);
  free(instance->ClassName);
  free(instance->Name);
  if (instance->Children) {
    for (int i = 0; i < instance->ChildCount; ++i) {
      // Assuming children are also userdata and will be GC'd by Lua
      // No explicit free for children here to avoid double-free if Lua handles them
    }
    free(instance->Children);
  }
  return 0;
}

static int luack_instance_tostring(lua_State *L) {
  LuackInstance *instance = (LuackInstance *)luaL_checkudata(L, 1, LUACK_INSTANCE_METATABLE);
  lua_pushfstring(L, "%s (ID: %d)", instance->ClassName, instance->ID);
  return 1;
}

static int luack_instance_destroy(lua_State *L) {
  LuackInstance *instance = (LuackInstance *)luaL_checkudata(L, 1, LUACK_INSTANCE_METATABLE);
  // In a real scenario, this would remove the instance from its parent and trigger cleanup
  printf("[LuackSim] Destroying %s (ID: %d)\n", instance->ClassName, instance->ID);
  // Mark for GC or perform immediate cleanup if necessary
  return 0;
}

static int luack_instance_findfirstchild(lua_State *L) {
  LuackInstance *parent = (LuackInstance *)luaL_checkudata(L, 1, LUACK_INSTANCE_METATABLE);
  const char *name = luaL_checkstring(L, 2);
  for (int i = 0; i < parent->ChildCount; ++i) {
    // Children are stored as userdata, need to get the actual LuackInstance pointer
    LuackInstance *child = (LuackInstance *)luaL_checkudata(L, -1, LUACK_INSTANCE_METATABLE); // This is incorrect, need to get from parent->Children
    // Correct way: push child userdata to stack, then checkudata
    // For simplicity, let's assume direct access for now, or pass child userdata directly
    // This part needs careful implementation with Lua's stack and userdata handling
    // For now, a placeholder that always returns nil
    lua_pushnil(L);
    return 1;
  }
  lua_pushnil(L);
  return 1;
}

static int luack_instance_getchildren(lua_State *L) {
  LuackInstance *parent = (LuackInstance *)luaL_checkudata(L, 1, LUACK_INSTANCE_METATABLE);
  lua_newtable(L);
  for (int i = 0; i < parent->ChildCount; ++i) {
    lua_pushinteger(L, i + 1);
    // Push the actual child userdata onto the stack
    // This requires the children to be stored as Lua references, not raw pointers
    // For now, a placeholder that returns an empty table
  }
  return 1;
}

static int luack_instance_addchild(lua_State *L) {
  LuackInstance *parent = (LuackInstance *)luaL_checkudata(L, 1, LUACK_INSTANCE_METATABLE);
  LuackInstance *child = (LuackInstance *)luaL_checkudata(L, 2, LUACK_INSTANCE_METATABLE);

  if (parent->ChildCount >= parent->ChildCapacity) {
    parent->ChildCapacity = (parent->ChildCapacity == 0) ? 4 : parent->ChildCapacity * 2;
    parent->Children = (LuackInstance **)realloc(parent->Children, sizeof(LuackInstance *) * parent->ChildCapacity);
    if (!parent->Children) {
      luaL_error(L, "Failed to allocate memory for children");
    }
  }
  parent->Children[parent->ChildCount++] = child;
  printf("[LuackSim] Added %s to %s\n", child->ClassName, parent->ClassName);
  return 0;
}

static int luack_instance_removechild(lua_State *L) {
  LuackInstance *parent = (LuackInstance *)luaL_checkudata(L, 1, LUACK_INSTANCE_METATABLE);
  LuackInstance *child_to_remove = (LuackInstance *)luaL_checkudata(L, 2, LUACK_INSTANCE_METATABLE);

  for (int i = 0; i < parent->ChildCount; ++i) {
    if (parent->Children[i] == child_to_remove) {
      for (int j = i; j < parent->ChildCount - 1; ++j) {
        parent->Children[j] = parent->Children[j+1];
      }
      parent->ChildCount--;
      printf("[LuackSim] Removed %s from %s\n", child_to_remove->ClassName, parent->ClassName);
      return 0;
    }
  }
  luaL_error(L, "Child not found");
  return 0;
}

static int luack_instance_index(lua_State *L) {
  LuackInstance *instance = (LuackInstance *)luaL_checkudata(L, 1, LUACK_INSTANCE_METATABLE);
  const char *key = luaL_checkstring(L, 2);

  if (strcmp(key, "ClassName") == 0) {
    lua_pushstring(L, instance->ClassName);
  } else if (strcmp(key, "ID") == 0) {
    lua_pushinteger(L, instance->ID);
  } else if (strcmp(key, "Name") == 0) {
    lua_pushstring(L, instance->Name);
  } else if (strcmp(key, "Position") == 0) {
    lua_newtable(L);
    lua_pushnumber(L, instance->PositionX); lua_setfield(L, -2, "X");
    lua_pushnumber(L, instance->PositionY); lua_setfield(L, -2, "Y");
    lua_pushnumber(L, instance->PositionZ); lua_setfield(L, -2, "Z");
  } else if (strcmp(key, "Size") == 0) {
    lua_newtable(L);
    lua_pushnumber(L, instance->SizeX); lua_setfield(L, -2, "X");
    lua_pushnumber(L, instance->PositionY); lua_setfield(L, -2, "Y");
    lua_pushnumber(L, instance->SizeZ); lua_setfield(L, -2, "Z");
  } else if (strcmp(key, "Destroy") == 0) {
    lua_pushcfunction(L, luack_instance_destroy);
  } else if (strcmp(key, "FindFirstChild") == 0) {
    lua_pushcfunction(L, luack_instance_findfirstchild);
  } else if (strcmp(key, "GetChildren") == 0) {
    lua_pushcfunction(L, luack_instance_getchildren);
  } else if (strcmp(key, "AddChild") == 0) {
    lua_pushcfunction(L, luack_instance_addchild);
  } else if (strcmp(key, "RemoveChild") == 0) {
    lua_pushcfunction(L, luack_instance_removechild);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

static int luack_instance_newindex(lua_State *L) {
  LuackInstance *instance = (LuackInstance *)luaL_checkudata(L, 1, LUACK_INSTANCE_METATABLE);
  const char *key = luaL_checkstring(L, 2);

  if (strcmp(key, "Name") == 0) {
    free(instance->Name);
    instance->Name = strdup(luaL_checkstring(L, 3));
  } else if (strcmp(key, "Position") == 0) {
    luaL_checktype(L, 3, LUA_TTABLE);
    lua_getfield(L, 3, "X"); instance->PositionX = luaL_checknumber(L, -1); lua_pop(L, 1);
    lua_getfield(L, 3, "Y"); instance->PositionY = luaL_checknumber(L, -1); lua_pop(L, 1);
    lua_getfield(L, 3, "Z"); instance->PositionZ = luaL_checknumber(L, -1); lua_pop(L, 1);
  } else if (strcmp(key, "Size") == 0) {
    luaL_checktype(L, 3, LUA_TTABLE);
    lua_getfield(L, 3, "X"); instance->SizeX = luaL_checknumber(L, -1); lua_pop(L, 1);
    lua_getfield(L, 3, "Y"); instance->PositionY = luaL_checknumber(L, -1); lua_pop(L, 1);
    lua_getfield(L, 3, "Z"); instance->SizeZ = luaL_checknumber(L, -1); lua_pop(L, 1);
  } else {
    luaL_error(L, "Attempt to write to a read-only or non-existent property: %s", key);
  }
  return 0;
}

static const luaL_Reg luack_instance_methods[] = {
  {"Destroy", luack_instance_destroy},
  {"FindFirstChild", luack_instance_findfirstchild},
  {"GetChildren", luack_instance_getchildren},
  {"AddChild", luack_instance_addchild},
  {"RemoveChild", luack_instance_removechild},
  {NULL, NULL}
};

static const luaL_Reg luack_instance_metamethods[] = {
  {"__index", luack_instance_index},
  {"__newindex", luack_instance_newindex},
  {"__tostring", luack_instance_tostring},
  {"__gc", luack_instance_gc},
  {NULL, NULL}
};

static void luack_create_instance_metatable(lua_State *L) {
  luaL_newmetatable(L, LUACK_INSTANCE_METATABLE);
  luaL_setfuncs(L, luack_instance_metamethods, 0);
  luaL_newlib(L, luack_instance_methods);
  lua_setfield(L, -2, "__index"); // Set methods as __index for direct method calls
  lua_pop(L, 1);
}

// Game Services

typedef struct LuackService {
  char *Name;
  // Add properties and methods specific to each service
} LuackService;

static int luack_service_gc(lua_State *L) {
  LuackService *service = (LuackService *)luaL_checkudata(L, 1, "Luack.Service");
  free(service->Name);
  return 0;
}

static int luack_service_index(lua_State *L) {
  LuackService *service = (LuackService *)luaL_checkudata(L, 1, "Luack.Service");
  const char *key = luaL_checkstring(L, 2);

  if (strcmp(key, "Name") == 0) {
    lua_pushstring(L, service->Name);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

static const luaL_Reg luack_service_metamethods[] = {
  {"__index", luack_service_index},
  {"__gc", luack_service_gc},
  {NULL, NULL}
};

static LuackService *luack_newservice_udata(lua_State *L, const char *name) {
  LuackService *service = (LuackService *)lua_newuserdatauv(L, sizeof(LuackService), 0);
  service->Name = strdup(name);
  luaL_newmetatable(L, "Luack.Service");
  luaL_setfuncs(L, luack_service_metamethods, 0);
  lua_setmetatable(L, -2);
  return service;
}

static int luack_game_getservice(lua_State *L) {
  const char *serviceName = luaL_checkstring(L, 1);
  // In a real scenario, this would return a singleton instance of the service
  // For now, create a new one each time
  luack_newservice_udata(L, serviceName);
  return 1;
}

static int luack_game_index(lua_State *L) {
  const char *key = luaL_checkstring(L, 2);
  if (strcmp(key, "GetService") == 0) {
    lua_pushcfunction(L, luack_game_getservice);
  } else if (strcmp(key, "Workspace") == 0) {
    // Return a dummy workspace instance for now
    luack_newinstance_udata(L);
    lua_pushstring(L, "Workspace");
    lua_setfield(L, -2, "ClassName");
    lua_pushinteger(L, 0); // Dummy ID
    lua_setfield(L, -2, "ID");
  } else {
    lua_pushnil(L);
  }
  return 1;
}

static int luack_game_newindex(lua_State *L) {
  luaL_error(L, "Attempt to write to a read-only table: game");
  return 0;
}

static const luaL_Reg luack_game_metamethods[] = {
  {"__index", luack_game_index},
  {"__newindex", luack_game_newindex},
  {NULL, NULL}
};

void load_sim_api_extended(lua_State *L) {
  // Create metatable for Instance objects
  luack_create_instance_metatable(L);

  // Create the global 'Instance' table
  lua_newtable(L);
  lua_pushnumber(L, (lua_Number)time(NULL)); // Seed for math.random
  lua_pushnumber(L, (lua_Number)clock());    // Another seed component
  lua_pushcclosure(L, luack_instance_new, 2); // 'new' function with upvalues
  lua_setfield(L, -2, "new");
  lua_setglobal(L, "Instance");

  // Create the global 'game' table with its metatable
  lua_newtable(L);
  luaL_setfuncs(L, luack_game_metamethods, 0);
  lua_setglobal(L, "game");

  // Create a global 'workspace' table as a direct instance for simplicity
  luack_newinstance_udata(L);
  lua_pushstring(L, "Workspace");
  lua_setfield(L, -2, "ClassName");
  lua_pushinteger(L, 0); // Dummy ID
  lua_setfield(L, -2, "ID");
  lua_setglobal(L, "workspace");
}



/* Custom Built-in Modules */

static int luack_math_extra_isprime(lua_State *L) {
  lua_Integer n = luaL_checkinteger(L, 1);
  if (n < 2) {
    lua_pushboolean(L, 0);
    return 1;
  }
  for (lua_Integer i = 2; i * i <= n; ++i) {
    if (n % i == 0) {
      lua_pushboolean(L, 0);
      return 1;
    }
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int luack_math_extra_fibonacci(lua_State *L) {
  lua_Integer n = luaL_checkinteger(L, 1);
  if (n < 0) {
    luaL_error(L, "fibonacci: input must be non-negative");
  }
  if (n == 0) {
    lua_pushinteger(L, 0);
    return 1;
  }
  if (n == 1) {
    lua_pushinteger(L, 1);
    return 1;
  }
  lua_Integer a = 0, b = 1, c;
  for (lua_Integer i = 2; i <= n; ++i) {
    c = a + b;
    a = b;
    b = c;
  }
  lua_pushinteger(L, b);
  return 1;
}

static const luaL_Reg luack_math_extra_funcs[] = {
  {"isprime", luack_math_extra_isprime},
  {"fibonacci", luack_math_extra_fibonacci},
  {NULL, NULL}
};

static int luack_string_utils_reverse(lua_State *L) {
  size_t len;
  const char *s = luaL_checklstring(L, 1, &len);
  char *reversed = (char *)malloc(len + 1);
  if (!reversed) {
    luaL_error(L, "Failed to allocate memory for reversed string");
  }
  for (size_t i = 0; i < len; ++i) {
    reversed[i] = s[len - 1 - i];
  }
  reversed[len] = '\0';
  lua_pushstring(L, reversed);
  free(reversed);
  return 1;
}

static int luack_string_utils_capitalize(lua_State *L) {
  size_t len;
  const char *s = luaL_checklstring(L, 1, &len);
  char *capitalized = strdup(s);
  if (!capitalized) {
    luaL_error(L, "Failed to allocate memory for capitalized string");
  }
  if (len > 0) {
    capitalized[0] = toupper((unsigned char)capitalized[0]);
  }
  lua_pushstring(L, capitalized);
  free(capitalized);
  return 1;
}

static const luaL_Reg luack_string_utils_funcs[] = {
  {"reverse", luack_string_utils_reverse},
  {"capitalize", luack_string_utils_capitalize},
  {NULL, NULL}
};



/* Error Handling and Debugging Improvements */

static int luack_print_to_stderr(lua_State *L) {
  int n = lua_gettop(L);  /* number of arguments */
  int i;
  lua_getglobal(L, "tostring");
  for (i=1; i<=n; i++) {
    const char *s;
    size_t l;
    lua_pushvalue(L, -1);  /* function to be called */
    lua_pushvalue(L, i);   /* value to print */
    lua_call(L, 1, 1);     /* call tostring(value) */
    s = lua_tolstring(L, -1, &l);  /* get result */
    if (s == NULL) {
      return luaL_error(L, "'tostring' must return a string to 'print'");
    }
    if (i>1) lua_writestringerror("\t", 1);
    lua_writestringerror(s, l);
    lua_pop(L, 1);  /* pop result */
  }
  lua_writestringerror("\n", 1);
  return 0;
}

void luack_register_debug_funcs(lua_State *L) {
  lua_pushcfunction(L, luack_print_to_stderr);
  lua_setglobal(L, "print_err");
}

void luack_open_custom_libs(lua_State *L) {
  luaL_newlib(L, luack_math_extra_funcs);
  lua_setglobal(L, "math_extra");

  luaL_newlib(L, luack_string_utils_funcs);
  lua_setglobal(L, "string_utils");
}


