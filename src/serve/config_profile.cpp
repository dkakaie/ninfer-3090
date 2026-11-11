#include "serve/config_profile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ninfer::serve {
namespace {

using Json = nlohmann::json;

[[noreturn]] void fail(std::string message) { throw std::invalid_argument(std::move(message)); }

// A variable the surrounding environment provides (what a shell exported before the server was
// started). The profile's own "env" object is looked up separately and takes precedence.
std::string inherited_env(std::string_view name) {
    if (const char* value = std::getenv(name.data())) { return value; }
    return {};
}

// Environment-sourced values must be whole numbers where run.sh used them numerically; anything
// else would reach the engine as a flag value it cannot parse.
long parse_whole(std::string_view value, std::string_view what) {
    try {
        std::size_t consumed = 0;
        const long result = std::stol(std::string(value), &consumed);
        if (consumed != value.size()) { fail(std::string(what) + " is not a whole number: " + std::string(value)); }
        return result;
    } catch (const std::exception&) {
        fail(std::string(what) + " is not a whole number: " + std::string(value));
    }
}

// The tuned-branch default for one spec: draft count, lanes and prefill chunk. The dflash2 set is
// the fast single-stream one (K7, one lane, chunk 4096); mtp and none share the two-lane 2048 set.
struct SpecDefaults {
    std::uint32_t draft_tokens = 3;
    std::uint32_t concurrency  = 2;
    std::uint32_t prefill_chunk = 2048;
};

SpecDefaults spec_defaults(std::string_view spec, bool qwen38) {
    SpecDefaults d;
    if (spec == "dflash2") {
        if (!qwen38) { fail("NINFER_SPEC must be mtp or none, got " + std::string(spec)); }
        d.draft_tokens = 7;
        d.concurrency = 1;
        d.prefill_chunk = 4096;
        return d;
    }
    if (spec == "mtp") {
        d.draft_tokens = 3;
    } else if (spec == "none") {
        d.draft_tokens = 0;
    } else {
        fail("NINFER_SPEC must be dflash2, mtp or none, got " + std::string(spec));
    }
    d.concurrency = qwen38 ? 2u : 3u;
    d.prefill_chunk = qwen38 ? 2048u : 4096u;
    return d;
}

std::string trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) { return {}; }
    const auto end = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(begin, end - begin + 1));
}

// JSON values come through as number or string depending on how the profile was written; both are
// accepted for the numeric keys.
long json_int(const Json& root, std::string_view key, long fallback) {
    auto it = root.find(key);
    if (it == root.end() || it->is_null()) { return fallback; }
    if (it->is_number_integer()) { return it->get<long>(); }
    if (it->is_number_unsigned()) { return static_cast<long>(it->get<unsigned long>()); }
    if (it->is_string()) {
        const std::string text = trim(it->get<std::string>());
        try {
            std::size_t consumed = 0;
            const long value = std::stol(text, &consumed);
            if (consumed != text.size()) {
                fail("profile key \"" + std::string(key) + "\" is not a whole number: " + text);
            }
            return value;
        } catch (const std::invalid_argument&) {
            fail("profile key \"" + std::string(key) + "\" is not a whole number: " + text);
        }
    }
    fail("profile key \"" + std::string(key) + "\" must be a number");
}

bool json_bool(const Json& root, std::string_view key, bool fallback) {
    auto it = root.find(key);
    if (it == root.end() || it->is_null()) { return fallback; }
    if (it->is_boolean()) { return it->get<bool>(); }
    if (it->is_string()) {
        const std::string value = trim(it->get<std::string>());
        if (value == "on" || value == "true" || value == "1") { return true; }
        if (value == "off" || value == "false" || value == "0") { return false; }
    }
    fail("profile key \"" + std::string(key) + "\" must be a boolean");
}

std::string json_string(const Json& root, std::string_view key, const std::string& fallback) {
    auto it = root.find(key);
    if (it == root.end() || it->is_null()) { return fallback; }
    if (it->is_string()) { return it->get<std::string>(); }
    if (it->is_number_integer()) { return std::to_string(it->get<long>()); }
    fail("profile key \"" + std::string(key) + "\" must be a string");
}

const Json& json_object(const Json& root, std::string_view key) {
    static const Json empty = Json::object();
    auto it = root.find(key);
    if (it == root.end() || it->is_null()) { return empty; }
    if (!it->is_object()) { fail("profile key \"" + std::string(key) + "\" must be an object"); }
    return *it;
}

std::string shell_quote(std::string_view value) {
    if (!value.empty()) {
        const bool plain = std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   std::string_view("._/-+=%:@,~").find(c) != std::string_view::npos;
        });
        if (plain) { return std::string(value); }
    }
    std::string out = "'";
    for (char c : value) { out += (c == '\'' ? std::string("'\\''") : std::string(1, c)); }
    out += '\'';
    return out;
}

} // namespace

namespace {

// The two layouts run.sh reached from its own location: a checkout (models/ beside scripts/, the
// binary under build-linux/apps) and an unpacked archive (binary and models/ in one directory).
std::vector<std::filesystem::path> repository_roots() {
    std::vector<std::filesystem::path> roots;
    const auto add = [&roots](std::filesystem::path candidate) {
        if (candidate.empty()) { return; }
        try {
            candidate = std::filesystem::canonical(candidate);
        } catch (const std::filesystem::filesystem_error&) { return; }
        if (std::find(roots.begin(), roots.end(), candidate) == roots.end()) { roots.push_back(candidate); }
    };
    add(std::filesystem::current_path());
#if defined(_WIN32)
    char buffer[4096];
    if (GetModuleFileNameA(nullptr, buffer, static_cast<DWORD>(sizeof(buffer))) > 0) {
        add(std::filesystem::path(buffer).parent_path().parent_path()); // build-linux/apps -> root
        add(std::filesystem::path(buffer).parent_path());               // archive root
    }
#else
    char buffer[4096];
    const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length > 0) {
        buffer[length] = '\0';
        add(std::filesystem::path(buffer).parent_path().parent_path()); // build-linux/apps -> root
        add(std::filesystem::path(buffer).parent_path());               // archive root
    }
#endif
    return roots;
}

std::filesystem::path resolve_repo_root(const std::filesystem::path& override_) {
    if (!override_.empty()) { return override_; }
    for (const auto& root : repository_roots()) {
        if (std::filesystem::exists(root / "scripts") || std::filesystem::exists(root / "configs")) {
            return root;
        }
    }
    return std::filesystem::current_path();
}

struct ModelRef {
    std::string key = "qwen38-27b"; // launcher key, also names the default context-store dir
    std::string artifact;           // file name for a known key
    bool known_key = false;
};

// The profile's model is one of the two launcher keys (selecting the measured branch) or a direct
// artifact path, relative to the repository root or absolute: the new direct path runs the
// qwen38-27b branch, the only shape this fork measures. NINFER_MODEL/NINFER_MODEL_DIR only
// override where the artifact lives, never the branch, exactly as run.sh takes its argument.
ModelRef model_ref(const Json& root) {
    ModelRef ref;
    ref.key = json_string(root, "model", "qwen38-27b");
    if (ref.key == "qwen38-27b" || ref.key == "qwen36-35b-a3b") {
        ref.known_key = true;
        ref.artifact  = ref.key == "qwen38-27b" ? "qwen3_8_27b.ninfer" : "qwen3_6_35b_a3b.ninfer";
    } else {
        const bool has_separator = ref.key.find_first_of("/\\") != std::string::npos;
        const bool ends_ninfer   = ref.key.size() > 6 &&
                                    ref.key.compare(ref.key.size() - 6, 6, ".ninfer") == 0;
        if (!has_separator && !ends_ninfer) {
            fail("Unknown model: " + ref.key + " (expected qwen38-27b, qwen36-35b-a3b or an artifact path)");
        }
    }
    return ref;
}

// The name of the default context-store directory: the model key, or the file name of a direct
// artifact path without its .ninfer extension.
std::string store_name(const ModelRef& ref) {
    if (ref.known_key) { return ref.key; }
    std::filesystem::path name = std::filesystem::path(ref.key).filename();
    if (name.extension() == ".ninfer") { name.replace_extension(); }
    return name.string();
}

// run.sh probes <root>/models first, then <root>/scripts/models, honouring an explicit
// NINFER_MODEL_DIR verbatim. The same order over every candidate root; when no probe hits, the
// first layout's path stands (run.sh would then fail with "Missing model" on that path).
std::filesystem::path resolve_artifact(const ModelRef& ref, const std::vector<std::filesystem::path>& roots,
                                       const std::string& env_model, const std::string& env_model_dir) {
    // NINFER_MODEL is an explicit artifact path taken verbatim; NINFER_MODEL_DIR an explicit
    // directory, never probed past (a typo must fail on the named path, not a fallback).
    if (!env_model.empty()) { return env_model; }
    if (!ref.known_key) {
        // A direct path in the profile: absolute as given, relative against the repository root.
        const std::filesystem::path path(ref.key);
        if (path.is_absolute()) { return path; }
        return (roots.empty() ? std::filesystem::path(".") : roots.front()) / path;
    }
    const std::string& artifact = ref.artifact;
    if (!env_model_dir.empty()) { return std::filesystem::path(env_model_dir) / artifact; }
    for (const auto& layout : {std::filesystem::path("models"), std::filesystem::path("scripts/models")}) {
        for (const auto& root : roots) {
            const std::filesystem::path candidate = root / layout / artifact;
            if (std::filesystem::is_regular_file(candidate)) { return candidate; }
        }
    }
    return (roots.empty() ? std::filesystem::path(".") : roots.front()) / "models" / artifact;
}

// The value run.sh itself would see for one NINFER_* variable: the profile's "env" entry wins,
// then the inherited environment, else unset.
std::optional<std::string> effective_env(const std::vector<std::pair<std::string, std::string>>& env_pairs,
                                         std::string_view name) {
    for (const auto& [key, value] : env_pairs) {
        if (key == name) { return value; }
    }
    const std::string inherited = inherited_env(name);
    if (inherited.empty()) { return std::nullopt; }
    return inherited;
}

bool env_pair_set(const std::vector<std::pair<std::string, std::string>>& env_pairs,
                  std::string_view name) {
    for (const auto& [key, value] : env_pairs) {
        if (key == name) { return true; }
    }
    return false;
}

} // namespace

DerivedProfile derive_profile(const std::filesystem::path& profile,
                              const std::filesystem::path& repo_root) {
    std::ifstream in(profile);
    if (!in) { fail("cannot read profile " + profile.string()); }
    Json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        fail("bad JSON in profile " + profile.string() + ": " + e.what());
    }
    if (!root.is_object()) { fail("profile " + profile.string() + " must be a JSON object"); }

    DerivedProfile out;
    const std::filesystem::path root_dir = resolve_repo_root(repo_root);
    const std::vector<std::filesystem::path> roots = repository_roots();

    // ---- the profile's "env" object -------------------------------------------
    // Applied verbatim, in order, after every key-derived value (bootstrap exported it last), so
    // an entry overrides both the JSON keys it mirrors and anything inherited.
    std::vector<std::pair<std::string, std::string>> env_pairs;
    {
        const Json& env = json_object(root, "env");
        for (auto it = env.begin(); it != env.end(); ++it) {
            std::string value;
            if (it->is_string()) { value = trim(it->get<std::string>()); }
            else if (it->is_number()) { value = it->dump(); }
            else { fail("profile env value for \"" + it.key() + "\" must be a string or number"); }
            env_pairs.emplace_back(it.key(), std::move(value));
        }
    }
    const auto eff  = [&env_pairs](std::string_view name) { return effective_env(env_pairs, name); };
    // bootstrap exports the spec/shaping keys unconditionally, so an inherited value for one of
    // them never reaches run.sh: only the profile's own env entry counts.
    const auto env_only = [&env_pairs](std::string_view name) -> std::optional<std::string> {
        for (const auto& [key, value] : env_pairs) {
            if (key == name) { return value; }
        }
        return std::nullopt;
    };
    const auto effl = [&env_pairs](std::string_view name, long fallback) {
        auto value = effective_env(env_pairs, name);
        return value ? parse_whole(*value, name) : fallback;
    };

    // ---- model ------------------------------------------------------------------
    const ModelRef ref = model_ref(root);
    const bool qwen38  = ref.known_key ? ref.key == "qwen38-27b" : true;
    if (eff("NINFER_NGRAM_VOLUME").has_value()) {
        // Flash-Next n-gram volume override is Windows-only; refuse like run.sh, don't ignore it.
        fail("NINFER_NGRAM_VOLUME is for qwen38-flash-next only (run.bat, Windows); clear it to serve " + ref.key);
    }
    const std::string env_model     = eff("NINFER_MODEL").value_or("");
    const std::string env_model_dir = eff("NINFER_MODEL_DIR").value_or("");
    const std::filesystem::path artifact = resolve_artifact(ref, roots, env_model, env_model_dir);

    // ---- spec and what moves with it ----------------------------------------------
    // bootstrap always exports the spec keys, so an inherited NINFER_SPEC never reaches run.sh:
    // precedence is profile env > JSON key > the spec default.
    const std::string spec = env_only("NINFER_SPEC").value_or(json_string(root, "spec", "mtp"));
    const SpecDefaults defaults = spec_defaults(spec, qwen38);
    const long draft_tokens = [&] {
        if (auto v = env_only("NINFER_DRAFT_TOKENS")) { return parse_whole(*v, "NINFER_DRAFT_TOKENS"); }
        return json_int(root, "draft_tokens", static_cast<long>(defaults.draft_tokens));
    }();
    const long concurrency = [&] {
        if (auto v = env_only("NINFER_CONCURRENCY")) { return parse_whole(*v, "NINFER_CONCURRENCY"); }
        return json_int(root, "lanes", static_cast<long>(defaults.concurrency));
    }();
    const std::string kv_dtype =
        env_only("NINFER_KV_DTYPE").value_or(json_string(root, "kv_dtype", "rk4v4"));
    const long prefill_chunk = eff("NINFER_PREFILL_CHUNK")
        ? parse_whole(*eff("NINFER_PREFILL_CHUNK"), "NINFER_PREFILL_CHUNK")
        : static_cast<long>(defaults.prefill_chunk);

    // ---- context ------------------------------------------------------------------
    // A number pins --max-context/--kv-capacity (turning off --fit-context); absent/null leaves
    // the fit to the card. bootstrap does not clear inherited NINFER_CONTEXT/NINFER_KV_CAPACITY,
    // so those reach run.sh too and pin as well.
    long context = 0; // 0: unpinned
    if (auto v = env_only("NINFER_CONTEXT")) { context = parse_whole(*v, "NINFER_CONTEXT"); }
    else {
        const auto it = root.find("context");
        if (it != root.end() && !it->is_null()) {
            context = json_int(root, "context", 0); // bootstrap's export shadows an inherited value
        } else if (const std::string inherited = inherited_env("NINFER_CONTEXT");
                   !inherited.empty()) {
            context = parse_whole(inherited, "NINFER_CONTEXT");
        }
    }
    const long max_context = context != 0 ? context : 262144;
    long kv_capacity;
    if (auto v = env_only("NINFER_KV_CAPACITY")) { kv_capacity = parse_whole(*v, "NINFER_KV_CAPACITY"); }
    else if (context != 0) { kv_capacity = context; } // bootstrap's export when the profile pins
    else if (const std::string inherited = inherited_env("NINFER_KV_CAPACITY"); !inherited.empty()) { kv_capacity = parse_whole(inherited, "NINFER_KV_CAPACITY"); }
    else { kv_capacity = max_context; }

    // ---- n-gram copy drafting ------------------------------------------------------
    const bool ngram_default = spec == "dflash2" && (concurrency == 1 || concurrency == 2);
    const std::string ngram = [&]() -> std::string {
        if (auto v = env_only("NINFER_NGRAM")) { return *v; }
        return json_bool(root, "ngram", ngram_default) ? "on" : "off";
    }();
    bool ngram_on = false;
    if (ngram == "on") {
        if (spec != "dflash2") {
            fail(qwen38 ? std::string("NINFER_NGRAM=on needs NINFER_SPEC=dflash2")
                        : "NINFER_NGRAM=on needs NINFER_SPEC=dflash2, which only qwen38-27b has");
        }
        ngram_on = true;
    } else if (ngram != "off") {
        fail("NINFER_NGRAM must be on or off, got " + ngram);
    }

    // ---- vision --------------------------------------------------------------------
    const std::string vision_raw = [&]() -> std::string {
        if (auto v = env_only("NINFER_VISION")) { return *v; }
        return json_bool(root, "vision", false) ? "on" : "off";
    }();
    const bool vision_on = vision_raw == "on";
    if (vision_raw != "on" && vision_raw != "off") {
        fail("NINFER_VISION must be on or off, got " + vision_raw);
    }
    // bootstrap exports the residency only with vision on, so an inherited one is shadowed then;
    // with vision off no residency flag is emitted anyway.
    const std::string residency =
        env_only("NINFER_VISION_RESIDENCY").value_or(json_string(root, "vision_residency", "overlay"));

    // ---- pinned Host context budget --------------------------------------------------
    const std::string fallback   = eff("NINFER_FALLBACK").value_or("on");
    const std::string auto_cache = eff("NINFER_AUTO_HOST_CACHE").value_or("off");
    if (auto_cache != "on" && auto_cache != "off") {
        fail("NINFER_AUTO_HOST_CACHE must be on or off, got " + auto_cache);
    }
    const long host_mib = effl("NINFER_HOST_CONTEXT_MIB", 8192);
    out.host_budget_retriable =
        fallback != "off" && auto_cache != "on" &&
        (eff("NINFER_HOST_RETRY").has_value() || eff("NINFER_HOST_CONTEXT_MIB").has_value() == false);
    const bool mlp_on = [&] {
        const std::string mlp = eff("NINFER_MLP_A8_DECODE").value_or("off");
        if (mlp != "on" && mlp != "off") { fail("NINFER_MLP_A8_DECODE must be on or off, got " + mlp); }
        return mlp == "on";
    }();
    const std::string loop = eff("NINFER_REASONING_LOOP").value_or("");
    if (loop != "" && loop != "off" && loop != "stop" && loop != "conclude") {
        fail("NINFER_REASONING_LOOP must be off, stop or conclude, got " + loop);
    }

    // ---- context store -----------------------------------------------------------------
    // default: configs/context-store/<model> beside the repository; a path may be absolute or
    // relative to the repository root; "" disables the store.
    const std::string store = json_string(root, "context_store", "default");
    std::string store_dir;
    if (!store.empty()) {
        if (store == "default") {
            store_dir = (root_dir / "configs" / "context-store" / store_name(ref)).string();
        } else if (store.front() == '/') {
            store_dir = store;
        } else {
            store_dir = (root_dir / store).string();
        }
    }
    std::string store_gib = eff("NINFER_CONTEXT_STORE_MAX_GIB").value_or("");
    if (store_dir.empty()) {
        if (!store_gib.empty()) { fail("NINFER_CONTEXT_STORE_MAX_GIB needs NINFER_CONTEXT_STORE"); }
    } else if (store_gib.empty()) {
        store_gib = "2"; // bootstrap's disk budget when the store is on and no budget was chosen
    }

    // ---- sampling guard ---------------------------------------------------------------
    // "default" omits the flag and leaves the registered preset in force.
    const std::string min_p    = eff("NINFER_MIN_P").value_or("0.03");
    const std::string presence = eff("NINFER_PRESENCE_PENALTY").value_or("0.5");

    // ---- bind address and port ---------------------------------------------------------
    // bootstrap always exports NINFER_PORT, so the inherited one never reaches run.sh; NINFER_HOST
    // is exported only when the profile sets it, so an inherited one applies otherwise.
    std::string host;
    const auto host_env = eff("NINFER_HOST"); // profile env entry or the inherited value
    if (env_pair_set(env_pairs, "NINFER_HOST")) {
        host = *host_env; // profile env entry wins over everything
    } else {
        const auto it = root.find("host");
        if (it != root.end() && !it->is_null()) { host = json_string(root, "host", ""); }
        if (host.empty() && host_env) { host = *host_env; } // inherited only when the profile sets none
    }
    if (host.empty()) { host = "127.0.0.1"; }
    // bootstrap always exports NINFER_PORT (json or 8082), shadowing an inherited one.
    const long port = [&] {
        if (auto v = env_only("NINFER_PORT")) { return parse_whole(*v, "NINFER_PORT"); }
        return json_int(root, "port", 8082);
    }();

    // bootstrap always exports NINFER_MAX_OUTPUT_TOKENS, so the flag is always present.
    const long max_output_tokens = env_only("NINFER_MAX_OUTPUT_TOKENS")
        ? parse_whole(*env_only("NINFER_MAX_OUTPUT_TOKENS"), "NINFER_MAX_OUTPUT_TOKENS")
        : json_int(root, "max_output_tokens", 32768);
    const std::string chat_template = eff("NINFER_CHAT_TEMPLATE").value_or("");

    // ---- API key ------------------------------------------------------------------------
    // A profile value sets it; null explicitly clears an inherited NINFER_API_KEY; absent
    // inherits it. An env entry is applied last (bootstrap order), so it wins over both.
    std::string api_key;
    {
        const auto it = root.find("api_key");
        if (it != root.end()) {
            if (it->is_null()) {
                out.clear_api_key = true;
            } else if (it->is_string()) {
                api_key = it->get<std::string>();
            } else {
                fail("profile key \"api_key\" must be a string or null");
            }
        } else {
            api_key = inherited_env("NINFER_API_KEY");
        }
        // An env entry is applied last (bootstrap order) and wins over a profile value.
        if (auto v = env_only("NINFER_API_KEY")) { api_key = *v; }
    }

    // ---- the invocation, in run.sh's order ----------------------------------------------
    std::vector<std::string> argv;
    auto append = [&argv](std::initializer_list<std::string_view> parts) {
        for (const std::string_view part : parts) { argv.emplace_back(part); }
    };
    argv.push_back(artifact.string());
    append({"--host", host, "--port", std::to_string(port)});
    append({"--max-concurrency", std::to_string(concurrency),
             "--max-context", std::to_string(max_context),
             "--kv-capacity", std::to_string(kv_capacity),
             "--kv-dtype", kv_dtype});
    if (spec != "none") {
        append({"--spec", spec, "--draft-tokens", std::to_string(draft_tokens), "--lm-head-draft"});
        if (!qwen38) { argv.push_back("--mtp-experts-q4"); } // 35B-A3B tuned branch
    }
    if (ngram_on) { append({"--ngram-draft-tokens", "15"}); }
    argv.push_back("--gdn-state-fp16");
    append({"--prefill-cublas", "--prefill-chunk", std::to_string(prefill_chunk)});
    append({"--max-pending-requests", "16", "--pending-timeout-ms", "600000"});
    if (vision_on) { append({"--vision", "--vision-residency", residency}); }
    if (auto_cache == "on") {
        argv.push_back("--auto-host-cache");
        if (auto v = eff("NINFER_HOST_CACHE_PERCENT")) { append({"--host-cache-percent", *v}); }
        if (auto v = eff("NINFER_HOST_CACHE_RESERVE_MIB")) { append({"--host-cache-reserve-mib", *v}); }
        if (auto v = eff("NINFER_HOST_CACHE_MAX_MIB")) { append({"--host-cache-max-mib", *v}); }
    } else {
        if (eff("NINFER_HOST_CACHE_PERCENT").has_value() ||
            eff("NINFER_HOST_CACHE_RESERVE_MIB").has_value() ||
            eff("NINFER_HOST_CACHE_MAX_MIB").has_value()) {
            fail("NINFER_HOST_CACHE_* needs NINFER_AUTO_HOST_CACHE=on");
        }
        append({"--host-context-mib", std::to_string(host_mib)});
    }
    append({"--max-output-tokens", std::to_string(max_output_tokens)});
    if (mlp_on) { argv.push_back("--mlp-a8-decode"); }
    if (loop == "stop" || loop == "conclude") { append({"--reasoning-loop", loop}); }
    if (!store_dir.empty()) {
        append({"--context-store", store_dir});
        if (!store_gib.empty()) { append({"--context-store-max-gib", store_gib}); }
    }
    if (min_p != "default") { append({"--min-p", min_p}); }
    if (presence != "default") { append({"--presence-penalty", presence}); }
    // --fit-context only when neither context nor capacity is explicit in run.sh's environment:
    // a profile env entry for either, or an inherited NINFER_KV_CAPACITY, turns it off (a pinned
    // json context already did, since bootstrap exports it).
    const bool capacity_explicit =
        env_pair_set(env_pairs, "NINFER_KV_CAPACITY") || !inherited_env("NINFER_KV_CAPACITY").empty();
    if (fallback != "off" && context == 0 && !capacity_explicit) {
        append({"--fit-context", "32768"});
    }
    if (!chat_template.empty()) { append({"--chat-template", chat_template}); }
    if (!api_key.empty()) { append({"--api-key", api_key}); }

    out.argv = std::move(argv);
    out.env  = std::move(env_pairs);
    out.webui_enabled = json_bool(json_object(root, "webui"), "enabled", true);
    return out;
}

std::string DerivedProfile::render() const {
    std::string out;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i > 0) { out += ' '; }
        if (i > 0 && argv[i - 1] == "--api-key") { out += "<redacted>"; continue; }
        out += shell_quote(argv[i]);
    }
    out += '\n';
    for (const auto& [name, value] : env) {
        out += "# env " + name + "=" +
               (name == "NINFER_API_KEY" ? "<redacted>" : shell_quote(value)) + '\n';
    }
    return out;
}

} // namespace ninfer::serve
