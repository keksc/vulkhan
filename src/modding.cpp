#include "modding.hpp"

#include <wasmtime.hh>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <print>
#include <variant>
#include <vector>

const char*
get_wasm_str(wasmtime::Caller& caller, int64_t offset)
{
    auto exp  = caller.get_export("memory");
    auto* mem = exp ? std::get_if<wasmtime::Memory>(&*exp) : nullptr;
    return mem ? reinterpret_cast<const char*>(mem->data(caller).data() + offset) : "";
}

void
hostLog(wasmtime::Caller caller, int64_t offset)
{
    std::println("{}", get_wasm_str(caller, offset));
}

void
hostAlloc(wasmtime::Caller, int64_t size)
{
}

Mod::Mod(ModManager& mgr, std::filesystem::path path) : name {path.stem().string()}, mgr {mgr}
{

    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> bytes {std::istreambuf_iterator<char>(file), {}};

    module = std::make_unique<wasmtime::Module>(
        wasmtime::Module::compile(mgr.getEngine(), bytes).unwrap());
}

void
Mod::instantiate()
{
    instance = std::make_unique<wasmtime::Instance>(
        mgr.getLinker().instantiate(mgr.getStore(), *module).unwrap());
}

void
Mod::init()
{
    auto initExport = instance->get(mgr.getStore(), "init");
    auto* initFunc  = initExport ? std::get_if<wasmtime::Func>(&*initExport) : nullptr;

    if (!initFunc) {
        std::println(stderr, "Error: Mod {} missing 'init' function.", name);
        return;
    }

    auto initResult = initFunc->call(mgr.getStore(), {});
    if (!initResult) {
        std::println(
            stderr, "[Mod Alert]: crash/trap from {} init:\n{}", name, initResult.err().message());
        crash();
    }
}

void
Mod::crash()
{
    dead               = true;
    auto cleanupExport = instance->get(mgr.getStore(), "cleanup");
    auto* cleanupFunc  = cleanupExport ? std::get_if<wasmtime::Func>(&*cleanupExport) : nullptr;

    if (!cleanupFunc) {
        std::println(stderr, "[Mod Alert]: No 'cleanup' function provided by {}.", name);
        return;
    }

    auto cleanupResult = cleanupFunc->call(mgr.getStore(), {});
    if (!cleanupResult) {
        std::println(stderr,
                     "[Mod Alert]: 'cleanup' function from {} crashed:\n{}",
                     name,
                     cleanupResult.err().message());
    }
}

ModManager::ModManager()
{
    engine = std::make_unique<wasmtime::Engine>();
    store  = std::make_unique<wasmtime::Store>(*engine);
    linker = std::make_unique<wasmtime::Linker>(*engine);

    // Allow shadowing so real native links overwrite temporary lazy proxies
    linker->allow_shadowing(true);

    linker->define(*store, "env", "log", wasmtime::Func::wrap(*store, hostLog)).unwrap();

    std::filesystem::path modsDir {"mods"};

    for (const auto& entry : std::filesystem::recursive_directory_iterator {modsDir}) {
        if (!entry.is_regular_file() || entry.path().extension() != ".oneirosMod")
            continue;

        std::string modName = entry.path().stem().string();

        mods.emplace(std::piecewise_construct,
                     std::forward_as_tuple(modName),
                     std::forward_as_tuple(*this, entry.path()));
    }

    for (auto& [modName, mod] : mods) {
        for (auto importType : mod.module->imports()) {
            std::string targetMod {importType.module()};
            std::string targetName {importType.name()};

            if (!linker->get(*store, targetMod, targetName).has_value()) {

                // Extracting private members from wasmtime's restrictive classes which
                // keep everything fk private
                // Extract the raw C API pointer safely via layout compatibility
                struct ImportTypeRefLayout {
                    const wasm_importtype_t* ptr;
                };
                const auto* rawImport =
                    reinterpret_cast<const ImportTypeRefLayout*>(&importType)->ptr;

                // Query the underlying C API for type classification
                const auto* rawExtType = wasm_importtype_type(rawImport);

                if (wasm_externtype_kind(rawExtType) == WASM_EXTERN_FUNC) {
                    // Downcast and wrap into a public C++ non-owning Ref wrapper
                    const auto* rawFuncType =
                        wasm_externtype_as_functype(const_cast<wasm_externtype_t*>(rawExtType));
                    wasmtime::FuncType::Ref funcType(rawFuncType);

                    // std::println("Creating lazy proxy for missing dependency: {}::{}",
                    //              targetMod, targetName);

                    auto proxy = wasmtime::Func(
                        *store,
                        funcType,
                        [this, targetMod, targetName](wasmtime::Caller caller,
                                                      std::span<const wasmtime::Val> args,
                                                      std::span<wasmtime::Val> results)
                            -> wasmtime::Result<std::monostate, wasmtime::Trap> {
                            auto it = mods.find(targetMod);
                            if (it == mods.end() || !it->second.instance) {
                                std::println(stderr,
                                             "Fatal: Called proxy for uninstantiated mod {}::{}",
                                             targetMod,
                                             targetName);
                                std::terminate();
                            }

                            auto targetExport = it->second.instance->get(caller, targetName);
                            auto* func        = targetExport ?
                                                    std::get_if<wasmtime::Func>(&*targetExport) :
                                                    nullptr;

                            if (!func) {
                                std::println(stderr,
                                             "Fatal: Export missing or not a function {}::{}",
                                             targetMod,
                                             targetName);
                                std::terminate();
                            }

                            auto result =
                                func->call(caller,
                                           const_cast<wasmtime::Val*>(args.data()),
                                           const_cast<wasmtime::Val*>(args.data() + args.size()));

                            if (!result) {
                                std::println(stderr,
                                             "Fatal: Proxy call failed {}::{}: {}",
                                             targetMod,
                                             targetName,
                                             result.err().message());
                                std::terminate();
                            }

                            const auto& innerResults = result.unwrap();
                            for (size_t i = 0; i < results.size() && i < innerResults.size(); ++i) {
                                results[i] = innerResults[i];
                            }

                            return std::monostate {};
                        });

                    linker->define(*store, targetMod, targetName, proxy).unwrap();
                } else {
                    std::println(stderr,
                                 "[Mod Alert]: Non-function circular dependency detected "
                                 "({}::{}). This cannot be proxied.",
                                 targetMod,
                                 targetName);
                }
            }
        }

        mod.instantiate();

        linker->define_instance(*store, modName, *mod.instance).unwrap();
    }

    for (auto& [modName, mod] : mods) {
        mod.init();
    }
}

bool
Mod::isDead() const
{
    return dead;
}

wasmtime::Engine&
ModManager::getEngine()
{
    return *engine;
}

wasmtime::Store&
ModManager::getStore()
{
    return *store;
}

wasmtime::Linker&
ModManager::getLinker()
{
    return *linker;
}

Mod::~Mod()               = default;
ModManager::~ModManager() = default;
