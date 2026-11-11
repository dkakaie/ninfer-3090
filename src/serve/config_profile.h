#pragma once

// JSON serving profiles (configs/*.json) resolved into the exact engine invocation the measured
// launcher used to build: bootstrap.sh translated the profile into NINFER_* environment variables
// and exec'd scripts/run.sh `tuned`, whose qwen38-27b/qwen36-35b-a3b branch turned them into the
// flag set this mapper now reproduces directly, so a profile starts the same configuration with no
// shell and no Python in between.

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ninfer::serve {

// The engine invocation a profile selects. argv is the full command line without the program name
// (argv[0] is the artifact path, then --host/--port, then the tuned-branch flag set in run.sh's
// order). env holds the profile's "env" entries verbatim: they are applied with setenv before the
// Engine starts so any NINFER_* knob the Engine reads straight from the environment keeps working.
struct DerivedProfile {
    std::vector<std::string> argv;
    std::vector<std::pair<std::string, std::string>> env;
    // webui.enabled: false serves no /webui routes at all (API only).
    bool webui_enabled = true;
    // True when the Host context budget is the profile default (8192 MiB) rather than a value the
    // caller chose through env or CLI: then the launcher's halve-and-retry on a failed pin
    // (cudaMallocHost failed) applies, down to 0.
    bool host_budget_retriable = false;
    // "api_key": null in the profile clears any NINFER_API_KEY the surrounding environment
    // inherited, so the profile is unambiguous about its own auth (bootstrap did the same with
    // `unset`). env entries are applied afterwards, so an env NINFER_API_KEY still wins.
    bool clear_api_key = false;
    // The launch line for --print-args: the shell-quoted argv (any API key value redacted) plus
    // one "# env NAME=VALUE" line per env entry, so it can be diffed against what bootstrap.sh
    // used to exec.
    [[nodiscard]] std::string render() const;
};

// Resolves the model key to an artifact path, probing <root>/models then <root>/scripts/models as
// run.sh does, honouring NINFER_MODEL (explicit path) and NINFER_MODEL_DIR (explicit directory).
// repo_root defaults to the repository root the binary sits in, falling back to the working
// directory; pass it explicitly to make resolution deterministic (tests).
[[nodiscard]] DerivedProfile derive_profile(const std::filesystem::path& profile,
                                            const std::filesystem::path& repo_root = {});

} // namespace ninfer::serve
