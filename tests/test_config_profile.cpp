// The --config profile mapper: a JSON serving profile resolves to the exact engine invocation the
// measured launcher used to build (scripts/run.sh `tuned`), plus its env passthrough, the
// api-key tri-state, the webui toggle and the host-budget retriable marker. The tests pin that
// mapping for every key and every refusal, with fixture profiles written to a scratch directory;
// no model and no GPU are needed.

#include "serve/config_profile.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using namespace ninfer::serve;

std::filesystem::path scratch;
int failures = 0;

void check(bool condition, const char* message) {
    if (condition) { return; }
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

// Inherited NINFER_* values would reach the mapper (it honours them where the old bootstrap did),
// so every variable the mapper reads is cleared before the first derivation.
void clear_ninfer_env() {
    const char* names[] = {"NINFER_API_KEY", "NINFER_CONTEXT", "NINFER_KV_CAPACITY", "NINFER_HOST",
                           "NINFER_PORT", "NINFER_MODEL", "NINFER_MODEL_DIR", "NINFER_SPEC",
                           "NINFER_DRAFT_TOKENS", "NINFER_CONCURRENCY", "NINFER_KV_DTYPE",
                           "NINFER_PREFILL_CHUNK", "NINFER_NGRAM", "NINFER_NGRAM_VOLUME",
                           "NINFER_VISION", "NINFER_VISION_RESIDENCY", "NINFER_FALLBACK",
                           "NINFER_AUTO_HOST_CACHE", "NINFER_HOST_CACHE_PERCENT",
                           "NINFER_HOST_CACHE_RESERVE_MIB", "NINFER_HOST_CACHE_MAX_MIB",
                           "NINFER_HOST_CONTEXT_MIB", "NINFER_HOST_RETRY", "NINFER_MLP_A8_DECODE",
                           "NINFER_REASONING_LOOP", "NINFER_CONTEXT_STORE_MAX_GIB", "NINFER_MIN_P",
                           "NINFER_PRESENCE_PENALTY", "NINFER_CHAT_TEMPLATE",
                           "NINFER_MAX_OUTPUT_TOKENS"};
    for (const char* name : names) { ::unsetenv(name); }
}

std::filesystem::path write_profile(const std::string& name, const std::string& json) {
    const std::filesystem::path path = scratch / name;
    std::ofstream out(path);
    out << json;
    return path;
}

DerivedProfile derive(const std::string& json, const std::filesystem::path& root = {}) {
    return derive_profile(write_profile("profile.json", json), root);
}

const std::filesystem::path artifact_of(const DerivedProfile& profile) { return profile.argv.front(); }

bool has_flag(const std::vector<std::string>& argv, std::string_view flag) {
    return std::any_of(argv.begin(), argv.end(),
                       [&](const std::string& entry) { return entry == flag; });
}

bool env_has(const DerivedProfile& profile, std::string_view name, std::string_view value) {
    for (const auto& [key, entry] : profile.env) {
        if (key == name && entry == value) { return true; }
    }
    return false;
}

bool has_arg(const std::vector<std::string>& argv, std::string_view flag, std::string_view value) {
    for (std::size_t i = 0; i + 1 < argv.size(); ++i) {
        if (argv[i] == flag && argv[i + 1] == value) { return true; }
    }
    return false;
}

bool tail_equals(const DerivedProfile& profile, const std::vector<std::string>& expected) {
    if (profile.argv.size() != expected.size() + 1) { return false; }
    for (std::size_t i = 1; i < profile.argv.size(); ++i) {
        if (profile.argv[i] != expected[i - 1]) { return false; }
    }
    return true;
}

// Expects an invalid_argument whose text carries the fragment.
bool rejects(const std::string& json, const char* fragment) {
    try {
        (void)derive(json);
        return false;
    } catch (const std::invalid_argument& error) {
        return std::string(error.what()).find(fragment) != std::string::npos;
    }
}

} // namespace

int main() {
    clear_ninfer_env();
    const long long tick = std::chrono::steady_clock::now().time_since_epoch().count();
    scratch = std::filesystem::temp_directory_path() / ("ninfer_config_profile_" + std::to_string(tick));
    std::error_code ec;
    std::filesystem::create_directories(scratch, ec);
    if (ec) {
        std::cerr << "cannot create scratch dir " << scratch << ": " << ec.message() << '\n';
        return 1;
    }
    const std::filesystem::path root = scratch; // explicit repo root keeps resolutions deterministic

    // ---- the mtp default set: the exact tuned-branch invocation ----------------------------
    {
        const DerivedProfile p = derive(
            R"({"notes":"free-form text the engine never reads","spec":"mtp"})", root);
        check(artifact_of(p).filename() == "qwen3_8_27b.ninfer" &&
                  artifact_of(p).parent_path().filename() == "models",
              "a known model key does not resolve under models/");
        check(tail_equals(p, {"--host", "127.0.0.1", "--port", "8082", "--max-concurrency", "2",
                              "--max-context", "262144", "--kv-capacity", "262144", "--kv-dtype",
                              "rk4v4", "--spec", "mtp", "--draft-tokens", "3", "--lm-head-draft",
                              "--gdn-state-fp16", "--prefill-cublas", "--prefill-chunk", "2048",
                              "--max-pending-requests", "16", "--pending-timeout-ms", "600000",
                              "--host-context-mib", "8192", "--max-output-tokens", "32768",
                              "--context-store", (root / "configs" / "context-store" / "qwen38-27b").string(),
                              "--context-store-max-gib", "2", "--min-p", "0.03",
                              "--presence-penalty", "0.5", "--fit-context", "32768"}),
              "the mtp default profile did not reproduce the tuned-branch invocation");
        check(p.webui_enabled && p.host_budget_retriable && !p.clear_api_key && p.env.empty(),
              "default webui/retry/api-key/env markers are wrong");
        check(!has_flag(p.argv, "--ngram-draft-tokens") && !has_flag(p.argv, "--vision") &&
                  !has_flag(p.argv, "--mtp-experts-q4") && !has_flag(p.argv, "--api-key"),
              "an unexpected flag crept into the mtp default invocation");
    }

    // ---- a pinned dflash2 coding profile: pinning turns fit off, everything else composes ---
    {
        const DerivedProfile p = derive(R"({
            "spec":"dflash2","lanes":1,"draft_tokens":7,
            "vision":true,"vision_residency":"overlay",
            "context":192000,"api_key":"test-key-123","port":9000,"host":"0.0.0.0",
            "context_store":"","webui":{"enabled":false,"auto_open":true}})");
        check(tail_equals(p, {"--host", "0.0.0.0", "--port", "9000", "--max-concurrency", "1",
                              "--max-context", "192000", "--kv-capacity", "192000", "--kv-dtype",
                              "rk4v4", "--spec", "dflash2", "--draft-tokens", "7", "--lm-head-draft",
                              "--ngram-draft-tokens", "15", "--gdn-state-fp16", "--prefill-cublas",
                              "--prefill-chunk", "4096", "--max-pending-requests", "16",
                              "--pending-timeout-ms", "600000", "--vision", "--vision-residency",
                              "overlay", "--host-context-mib", "8192", "--max-output-tokens",
                              "32768", "--min-p", "0.03", "--presence-penalty", "0.5",
                              "--api-key", "test-key-123"}),
              "the pinned dflash2 profile did not reproduce its tuned-branch invocation");
        check(!has_flag(p.argv, "--fit-context") && !has_flag(p.argv, "--context-store"),
              "pinned context left fit on or an empty context_store left the store on");
        check(!p.webui_enabled && p.host_budget_retriable && !p.clear_api_key,
              "the pinned profile's webui/retry/api-key markers are wrong");
        const std::string rendered = p.render();
        check(rendered.find("test-key-123") == std::string::npos &&
                  rendered.find("--api-key <redacted>") != std::string::npos,
              "--print-args did not redact the api key");
    }

    // ---- the 35B-A3B branch: its own artifact, three lanes, chunk 4096, q4 experts ---------
    {
        const DerivedProfile p = derive(R"({"model":"qwen36-35b-a3b","spec":"mtp"})", root);
        check(artifact_of(p).filename() == "qwen3_6_35b_a3b.ninfer" &&
                  has_arg(p.argv, "--max-concurrency", "3") && has_arg(p.argv, "--prefill-chunk", "4096") &&
                  has_flag(p.argv, "--mtp-experts-q4") &&
                  has_arg(p.argv, "--context-store", (root / "configs" / "context-store" / "qwen36-35b-a3b").string()),
              "the qwen36-35b-a3b branch did not select its own shape");
    }
    check(rejects(R"({"model":"qwen36-35b-a3b","spec":"dflash2","context":4096})",
                  "NINFER_SPEC must be mtp or none"),
          "dflash2 was accepted for qwen36-35b-a3b");
    check(rejects(R"({"model":"bogus-key"})", "Unknown model"),
          "an unknown model key was not refused");

    // ---- n-gram copy drafting: default for dflash2 at one-two lanes, explicit anywhere ------
    {
        check(has_arg(derive(R"({"spec":"dflash2"})").argv, "--ngram-draft-tokens", "15"),
              "n-gram drafting is not on by default for dflash2");
        check(!has_flag(derive(R"({"spec":"dflash2","lanes":3})").argv, "--ngram-draft-tokens"),
              "the n-gram default leaked past two lanes");
        check(has_arg(derive(R"({"spec":"dflash2","lanes":3,"ngram":true})").argv,
                          "--ngram-draft-tokens", "15"),
              "an explicit ngram was refused above two lanes");
        check(!has_flag(derive(R"({"spec":"dflash2","ngram":false})").argv, "--ngram-draft-tokens"),
              "an explicit ngram:false was not honoured");
    }
    check(rejects(R"({"spec":"mtp","ngram":true})", "NINFER_NGRAM=on needs NINFER_SPEC=dflash2"),
          "ngram+mtp was not refused");
    check(rejects(R"({"spec":"mtp","env":{"NINFER_NGRAM":"on"}})",
                  "NINFER_NGRAM=on needs NINFER_SPEC=dflash2"),
          "ngram+mtp via env was not refused");
    check(rejects(R"({"env":{"NINFER_NGRAM":"maybe"}})", "NINFER_NGRAM must be on or off"),
          "an invalid NINFER_NGRAM value was accepted");
    check(rejects(R"({"env":{"NINFER_NGRAM_VOLUME":"/v.ngram"}})",
                  "NINFER_NGRAM_VOLUME is for qwen38-flash-next only"),
          "NINFER_NGRAM_VOLUME was not refused for this architecture");

    // ---- env passthrough: verbatim entries, ordered, and winning over the mirrored keys -----
    {
        const DerivedProfile p = derive(
            R"({"spec":"mtp","env":{"NINFER_MIN_P":"0.05","NINFER_PREFILL_CHUNK":"1024","NINFER_CUDA_SYNC":"blocking"}})");
        check(has_arg(p.argv, "--min-p", "0.05") && has_arg(p.argv, "--prefill-chunk", "1024"),
              "env entries for mirrored keys did not win");
        check(env_has(p, "NINFER_MIN_P", "0.05") && env_has(p, "NINFER_PREFILL_CHUNK", "1024") &&
                  env_has(p, "NINFER_CUDA_SYNC", "blocking") && p.env.size() == 3,
              "the env passthrough lost an entry");
        const DerivedProfile def = derive(R"({"env":{"NINFER_MIN_P":"default","NINFER_PRESENCE_PENALTY":"default"}})");
        check(!has_flag(def.argv, "--min-p") && !has_flag(def.argv, "--presence-penalty"),
              "a \"default\" sampling guard still emitted its flag");
        check(has_arg(derive(R"({"env":{"NINFER_MAX_OUTPUT_TOKENS":"8192"}})").argv,
                          "--max-output-tokens", "8192"),
              "NINFER_MAX_OUTPUT_TOKENS did not win over the json default");
    }

    // ---- context: a number pins both limits and turns fit off; env pins too ----------------
    {
        const DerivedProfile pinned = derive(R"({"context":65536})");
        check(has_arg(pinned.argv, "--max-context", "65536") &&
                  has_arg(pinned.argv, "--kv-capacity", "65536") &&
                  !has_flag(pinned.argv, "--fit-context"),
              "a pinned context did not pin both limits and turn fit off");
        const DerivedProfile null_context = derive(R"({"context":null})");
        check(has_arg(null_context.argv, "--max-context", "262144") &&
                  has_flag(null_context.argv, "--fit-context"),
              "a null context did not leave the fit in force");
        const DerivedProfile capacity = derive(R"({"env":{"NINFER_KV_CAPACITY":"131072"}})");
        check(has_arg(capacity.argv, "--max-context", "262144") &&
                  has_arg(capacity.argv, "--kv-capacity", "131072") &&
                  !has_flag(capacity.argv, "--fit-context"),
              "an explicit KV capacity did not turn fit off beside a fitted max context");
        const DerivedProfile env_context = derive(R"({"env":{"NINFER_CONTEXT":"65536"},"context":32768})");
        check(has_arg(env_context.argv, "--max-context", "65536"),
              "a profile env NINFER_CONTEXT did not win over the json pin");
        check(!has_flag(derive(R"({"env":{"NINFER_FALLBACK":"off"}})").argv, "--fit-context"),
              "NINFER_FALLBACK=off did not turn the fit off");
    }

    // ---- bind address and port: env wins, json second, inherited last, 127.0.0.1 bottom ----
    {
        check(has_arg(derive(R"({"port":9000})").argv, "--port", "9000"), "a json port was lost");
        check(has_arg(derive(R"({"port":9000,"env":{"NINFER_PORT":"8181"}})").argv, "--port", "8181"),
              "an env NINFER_PORT did not shadow the json port");
        check(has_arg(derive(R"({"host":"10.0.0.5"})").argv, "--host", "10.0.0.5"),
              "a json host was lost");
        ::setenv("NINFER_HOST", "10.0.0.7", 1);
        check(has_arg(derive(R"({})").argv, "--host", "10.0.0.7"),
              "an inherited NINFER_HOST did not apply when the profile sets none");
        check(has_arg(derive(R"({"host":"10.0.0.5"})").argv, "--host", "10.0.0.5"),
              "a json host did not win over the inherited one");
        check(has_arg(derive(R"({"env":{"NINFER_HOST":"10.0.0.9"}})").argv, "--host", "10.0.0.9"),
              "an env NINFER_HOST did not win over both");
        ::unsetenv("NINFER_HOST");
    }

    // ---- api_key: a value sets it, null clears the inherited one, absent inherits ----------
    {
        check(has_arg(derive(R"({"api_key":"secret-1"})").argv, "--api-key", "secret-1"),
              "an api_key value did not reach the invocation");
        const DerivedProfile cleared = derive(R"({"api_key":null})");
        check(cleared.clear_api_key && !has_flag(cleared.argv, "--api-key"),
              "api_key:null did not mark the inherited key for clearing");
        ::setenv("NINFER_API_KEY", "inherited-key", 1);
        check(has_arg(derive(R"({})").argv, "--api-key", "inherited-key"),
              "an inherited NINFER_API_KEY did not apply when the key is absent");
        check(has_arg(derive(R"({"api_key":"profile-key"})").argv, "--api-key", "profile-key"),
              "a profile key did not win over the inherited one");
        check(has_arg(derive(R"({"api_key":"profile-key","env":{"NINFER_API_KEY":"env-key"}})").argv,
                          "--api-key", "env-key"),
              "an env NINFER_API_KEY did not win over the profile value");
        const DerivedProfile inherited_cleared =
            derive(R"({"api_key":null,"env":{"NINFER_API_KEY":"env-key"}})");
        check(inherited_cleared.clear_api_key &&
                  has_arg(inherited_cleared.argv, "--api-key", "env-key"),
              "env did not override a null-cleared key");
        ::unsetenv("NINFER_API_KEY");
        check(rejects(R"({"api_key":5})", "must be a string or null"),
              "a non-string api_key was accepted");
    }

    // ---- the context store: default dir, relative and absolute paths, off -------------------
    {
        check(has_arg(derive(R"({})", root).argv, "--context-store",
                          (root / "configs" / "context-store" / "qwen38-27b").string()) &&
                  has_arg(derive(R"({})", root).argv, "--context-store-max-gib", "2"),
              "the default store did not land under configs/context-store with its 2 GiB budget");
        check(has_arg(derive(R"({"context_store":"my-cache"})", root).argv, "--context-store",
                          (root / "my-cache").string()),
              "a relative store path did not resolve against the repository root");
        check(has_arg(derive(R"({"context_store":"/var/store"})", root).argv, "--context-store", "/var/store"),
              "an absolute store path was rewritten");
        check(!has_flag(derive(R"({"context_store":""})").argv, "--context-store"),
              "an empty context_store did not turn the store off");
        check(has_arg(derive(R"({"env":{"NINFER_CONTEXT_STORE_MAX_GIB":"8"}})", root).argv,
                          "--context-store-max-gib", "8"),
              "a chosen store budget was lost");
        check(rejects(R"({"context_store":"","env":{"NINFER_CONTEXT_STORE_MAX_GIB":"8"}})",
                      "NINFER_CONTEXT_STORE_MAX_GIB needs NINFER_CONTEXT_STORE"),
              "a store budget without a store was accepted");
        const DerivedProfile direct = derive(R"({"model":"my/Custom-27B.ninfer"})", root);
        check(artifact_of(direct).filename() == "Custom-27B.ninfer" &&
                  has_arg(direct.argv, "--context-store",
                          (root / "configs" / "context-store" / "Custom-27B").string()),
              "a direct artifact path did not resolve relative to the repository root or name its store");
        const DerivedProfile absolute = derive(R"({"model":"/opt/models/Model.ninfer"})", root);
        check(absolute.argv.front() == std::filesystem::path("/opt/models/Model.ninfer"),
              "an absolute artifact path was not taken verbatim");
        ::setenv("NINFER_MODEL_DIR", "/custom/dir", 1);
        check(derive(R"({})").argv.front() == std::filesystem::path("/custom/dir") / "qwen3_8_27b.ninfer",
              "NINFER_MODEL_DIR did not replace the probed directories");
        ::unsetenv("NINFER_MODEL_DIR");
    }

    // ---- the host budget: default retriable, a choice or the auto cache is not --------------
    {
        check(derive(R"({})").host_budget_retriable, "the default budget is not marked retriable");
        check(!derive(R"({"env":{"NINFER_HOST_CONTEXT_MIB":"4096"}})").host_budget_retriable &&
                  has_arg(derive(R"({"env":{"NINFER_HOST_CONTEXT_MIB":"4096"}})").argv,
                          "--host-context-mib", "4096"),
              "a chosen budget was not honoured as non-retriable");
        check(derive(R"({"env":{"NINFER_HOST_CONTEXT_MIB":"4096","NINFER_HOST_RETRY":"1"}})")
                      .host_budget_retriable,
              "an explicit NINFER_HOST_RETRY did not re-mark the budget retriable");
        const DerivedProfile auto_cache =
            derive(R"({"env":{"NINFER_AUTO_HOST_CACHE":"on","NINFER_HOST_CACHE_PERCENT":"50",
                    "NINFER_HOST_CACHE_RESERVE_MIB":"256","NINFER_HOST_CACHE_MAX_MIB":"1024"}})");
        check(has_flag(auto_cache.argv, "--auto-host-cache") &&
                  !has_flag(auto_cache.argv, "--host-context-mib") &&
                  has_arg(auto_cache.argv, "--host-cache-percent", "50") &&
                  has_arg(auto_cache.argv, "--host-cache-reserve-mib", "256") &&
                  has_arg(auto_cache.argv, "--host-cache-max-mib", "1024") &&
                  !auto_cache.host_budget_retriable,
              "the auto host cache did not replace the pinned budget or lose its tuning");
        check(rejects(R"({"env":{"NINFER_HOST_CACHE_PERCENT":"50"}})",
                      "NINFER_HOST_CACHE_* needs NINFER_AUTO_HOST_CACHE=on"),
              "a host cache tuning without the auto cache was accepted");
    }

    // ---- the optional extras each reach their flag -----------------------------------------
    {
        check(has_flag(derive(R"({"env":{"NINFER_MLP_A8_DECODE":"on"}})").argv, "--mlp-a8-decode"),
              "NINFER_MLP_A8_DECODE=on did not reach --mlp-a8-decode");
        check(has_arg(derive(R"({"env":{"NINFER_REASONING_LOOP":"stop"}})").argv, "--reasoning-loop",
                          "stop"),
              "NINFER_REASONING_LOOP did not reach --reasoning-loop");
        check(has_arg(derive(R"({"env":{"NINFER_CHAT_TEMPLATE":"tpl.jinja"}})").argv,
                          "--chat-template", "tpl.jinja"),
              "NINFER_CHAT_TEMPLATE did not reach --chat-template");
        check(rejects(R"({"env":{"NINFER_AUTO_HOST_CACHE":"maybe"}})", "must be on or off"),
              "an invalid NINFER_AUTO_HOST_CACHE was accepted");
        check(rejects(R"({"env":{"NINFER_MLP_A8_DECODE":"maybe"}})", "must be on or off"),
              "an invalid NINFER_MLP_A8_DECODE was accepted");
        check(rejects(R"({"env":{"NINFER_REASONING_LOOP":"banana"}})", "must be off, stop or conclude"),
              "an invalid NINFER_REASONING_LOOP was accepted");
    }

    // ---- vision: json and env compose, residency follows -------------------------------------
    {
        check(has_flag(derive(R"({"vision":true})").argv, "--vision") &&
                  has_arg(derive(R"({"vision":true,"vision_residency":"resident"})").argv,
                          "--vision-residency", "resident"),
              "vision and its residency did not reach the invocation");
        check(!has_flag(derive(R"({"vision":true,"env":{"NINFER_VISION":"off"}})").argv, "--vision"),
              "an env NINFER_VISION=off did not shadow json vision");
        check(rejects(R"({"env":{"NINFER_VISION":"sometimes"}})", "NINFER_VISION must be on or off"),
              "an invalid NINFER_VISION was accepted");
    }

    // ---- webui: the enabled flag is read; auto_open is ignored; the shape is checked --------
    {
        check(!derive(R"({"webui":{"enabled":false}})").webui_enabled,
              "webui.enabled=false did not switch the routes off");
        check(derive(R"({"webui":{"auto_open":true}})").webui_enabled,
              "auto_open alone was treated as disabling the webui");
        check(rejects(R"({"webui":true})", "must be an object"),
              "a non-object webui section was accepted");
    }

    // ---- malformed inputs are refused, not guessed at ---------------------------------------
    {
        check(rejects("[1,2]", "must be a JSON object"), "a non-object profile was accepted");
        check(rejects("{not json", "bad JSON"), "bad JSON was not refused");
        check(rejects(R"({"lanes":"x"})", "not a whole number"), "a non-numeric lanes was accepted");
        check(rejects(R"({"lanes":7,"env":{"NINFER_CONCURRENCY":"two"}})", "not a whole number"),
              "a non-numeric NINFER_CONCURRENCY was accepted");
        check(rejects(R"({"spec":"dflash"})", "NINFER_SPEC must be dflash2, mtp or none"),
              "an unknown spec was accepted");
        check(rejects(R"({"max_output_tokens":"big"})", "not a whole number"),
              "a non-numeric max_output_tokens was accepted");
        bool missing_refused = false;
        try {
            (void)derive_profile(scratch / "absent.json", root);
        } catch (const std::invalid_argument& error) {
            missing_refused = std::string(error.what()).find("cannot read profile") != std::string::npos;
        }
        check(missing_refused, "a missing profile file was not refused");
    }

    // ---- render: shell quoting of arbitrary values ------------------------------------------
    {
        const std::string rendered = derive(R"({"env":{"NINFER_MIN_P":"0.5 5"}})").render();
        check(rendered.find("# env NINFER_MIN_P='0.5 5'") != std::string::npos,
              "--print-args did not shell-quote an env value");
    }

    std::filesystem::remove_all(scratch);
    if (failures != 0) {
        std::cerr << failures << " config profile check(s) failed\n";
        return 1;
    }
    std::cout << "config profile checks passed\n";
    return 0;
}
