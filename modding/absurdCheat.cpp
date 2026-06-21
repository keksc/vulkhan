#include "std.hpp"

WASM_IMPORT("env", "log") void log(const char *msg);
WASM_IMPORT("betterNothing", "nerd") void nerd();

WASM_EXPORT("init")
void init() {
  log("Initializing an Absurd Cheat");
  nerd();
}

WASM_EXPORT("stupidity")
void stupidity() {
  log("Stupidity called from foreign");
}

WASM_EXPORT("cleanup")
void cleanup() {
  log("Cleaning up Absurd Cheat");
}
