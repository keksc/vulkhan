#include "std.hpp"

WASM_IMPORT("env", "log") void log(const char *msg);
WASM_IMPORT("absurdCheat", "stupidity") void stupidity();

WASM_EXPORT("init")
void init() {
  log("Initializing Better Nothing");
  stupidity();
}

WASM_EXPORT("nerd")
void nerd() {
  log("nerddd");
}

WASM_EXPORT("cleanup")
void cleanup() {
  log("Cleaning up Better Nothing");
}
