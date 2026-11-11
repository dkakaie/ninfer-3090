#include "product/logging/logging.h"
#include "product/logging/startup_log.h"
#include "product/version/version.h"
#include "serve/config_profile.h"
#include "serve/generation_service.h"
#include "serve/http_server.h"
#include "serve/serve_options.h"

#include <spdlog/logger.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::atomic<ninfer::serve::HttpServer*> g_server{nullptr};

void handle_signal(int) {
    ninfer::serve::HttpServer* server = g_server.load();
    if (server != nullptr) { server->stop(); }
}

// Time for in-flight error responses to flush once the Engine has latched, before the exit.
constexpr std::chrono::seconds kEngineFailureExitGrace{5};
constexpr int kEngineFailureExitStatus = 3;

// Called on the Engine worker thread after every host-side worker failure. A recovered failure is
// only logged. A latch leaves the process alive holding VRAM and answering 503 forever, so unless
// disabled it is logged FATAL and the process exits non-zero for a supervisor to restart; /health
// is already 503 during the grace period.
void handle_engine_fault(const ninfer::serve::OperationalLog& log,
                         const std::shared_ptr<spdlog::logger>& logger, bool exit_on_failure,
                         const ninfer::EngineFaultEvent& event) {
    // The exit is scheduled before anything that can throw: rendering or writing the event must
    // not be able to keep a latched process alive.
    static std::atomic<bool> exit_scheduled{false};
    const bool first_latch = event.latched && !exit_scheduled.exchange(true);
    if (first_latch && exit_on_failure) {
        try {
            std::thread([logger] {
                std::this_thread::sleep_for(kEngineFailureExitGrace);
                try {
                    logger->flush();
                } catch (...) {}
                std::_Exit(kEngineFailureExitStatus);
            }).detach();
        } catch (...) {
            // The thread could not be started. The exit is mandatory, so take it now without the
            // grace period rather than staying up latched.
            std::_Exit(kEngineFailureExitStatus);
        }
    }
    try {
        log.engine_fault(event);
        if (first_latch) {
            log.engine_fatal_exit(std::chrono::duration<double>(kEngineFailureExitGrace).count(),
                                  exit_on_failure);
        }
        logger->flush();
    } catch (...) {}
}

// The --config selection in the process argv: the profile path, the --print-args dry run, and an
// explicit artifact path given alongside it, which overrides the profile's own artifact.
struct ConfigSelection {
    bool present = false;
    std::string path;
    bool print_args = false;
    std::optional<std::string> artifact_override;
};

ConfigSelection scan_config(int argc, char** argv) {
    ConfigSelection selection;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] == nullptr ? "" : argv[i];
        if (arg == "--config") {
            if (i + 1 >= argc) { throw std::invalid_argument("--config needs a path"); }
            ++i;
            selection.present = true;
            selection.path = argv[i] == nullptr ? "" : argv[i];
        } else if (arg == "--print-args") {
            selection.print_args = true;
        } else if (!arg.empty() && arg.front() != '-') {
            if (!selection.artifact_override) { selection.artifact_override = std::string(arg); }
        }
    }
    return selection;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "ninfer-serve " << ninfer::product::build_version() << '\n';
        return 0;
    }

    ConfigSelection config;
    try {
        config = scan_config(argc, argv);
    } catch (const std::exception& exception) {
        std::cerr << "ninfer-serve: " << exception.what() << '\n';
        return 1;
    }
    if (config.print_args && !config.present) {
        std::cerr << "ninfer-serve: --print-args needs --config\n";
        return 1;
    }

    // A profile resolves before the options parse: its env entries are what run.sh would have
    // seen, so they are applied to this process before the Engine reads any of them.
    std::optional<ninfer::serve::DerivedProfile> profile;
    if (config.present) {
        try {
            profile = ninfer::serve::derive_profile(config.path);
        } catch (const std::exception& exception) {
            std::cerr << "ninfer-serve: " << exception.what() << '\n';
            return 1;
        }
        if (config.print_args) {
            // The dry run prints the invocation the profile selects, key redacted, and loads
            // nothing.
            std::cout << profile->render();
            return 0;
        }
        if (profile->clear_api_key) {
            ::unsetenv("NINFER_API_KEY"); // a profile null means no inherited key
        }
        for (const auto& [name, value] : profile->env) {
            ::setenv(name.c_str(), value.c_str(), 1);
        }
    }

    ninfer::serve::ServeOptions options;
    try {
        if (!profile) {
            options = ninfer::serve::parse_serve_options(argc, argv);
        } else {
            // The synthesized command line: the profile's invocation first (its values are the
            // base), then everything the caller wrote after it (which overrides flag by flag).
            std::vector<std::string> synthetic;
            synthetic.reserve(profile->argv.size() + static_cast<std::size_t>(argc));
            synthetic.push_back(argv[0] == nullptr ? "ninfer-serve" : argv[0]);
            synthetic.push_back(config.artifact_override ? *config.artifact_override
                                                         : profile->argv.front());
            for (std::size_t i = 1; i < profile->argv.size(); ++i) {
                synthetic.push_back(profile->argv[i]);
            }
            for (int i = 1; i < argc; ++i) {
                const std::string_view arg = argv[i] == nullptr ? "" : argv[i];
                if (arg == "--config") { ++i; continue; } // the path belongs to the profile
                if (arg == "--print-args") { continue; }
                if (!arg.empty() && arg.front() != '-') { continue; } // slot 1 holds the artifact
                synthetic.emplace_back(arg);
            }
            std::vector<char*> storage;
            storage.reserve(synthetic.size());
            for (auto& argument : synthetic) { storage.push_back(argument.data()); }
            options = ninfer::serve::parse_serve_options(static_cast<int>(synthetic.size()),
                                                         storage.data());
            // The startup record keeps the process's own command line (the profile path shows up
            // there), with the API key redacted exactly as the parser does.
            options.startup_argv.clear();
            bool redact_next = false;
            for (int i = 0; i < argc; ++i) {
                if (redact_next) {
                    options.startup_argv.emplace_back("<redacted>");
                    redact_next = false;
                    continue;
                }
                options.startup_argv.emplace_back(argv[i] == nullptr ? "" : argv[i]);
                redact_next = options.startup_argv.back() == "--api-key";
            }
        }
    } catch (const std::invalid_argument& exception) {
        std::cerr << "ninfer-serve: " << exception.what() << '\n';
        std::cerr << ninfer::serve::serve_usage_text(argv[0]);
        return 1;
    } catch (const std::exception& exception) {
        std::cerr << "ninfer-serve: " << exception.what() << '\n';
        return 1;
    }
    if (options.help_requested) {
        std::cout << ninfer::serve::serve_usage_text(argv[0]);
        return 0;
    }
    if (profile && profile->webui_enabled) {
        options.webui_enabled = true;
    }

    ninfer::product::LoggingRuntime logging(
        {.logger_name  = "ninfer-serve",
         .level        = options.log_level,
         .presentation = ninfer::product::LogPresentation::Service});
    const std::shared_ptr<spdlog::logger> logger = logging.logger();
    ninfer::product::StartupLogRenderer startup_log(logging);
    ninfer::serve::OperationalLog operational_log(logger);

    // The host-pin retry mirrors what scripts/run.sh did around the server: only a failed pin of
    // a budget the caller never chose (DerivedProfile::host_budget_retriable) retries, halving
    // down to 0, one log line per retry.
    const bool host_retriable = profile.has_value() && profile->host_budget_retriable;

    while (true) {
        bool serving = false;
        try {
            ninfer::serve::HttpServer server(options, logger);
            if (!server.bind()) {
                operational_log.bind_failure(options.host, options.port);
                return 1;
            }

            // Answer 503 from here on rather than leaving the accepted connection silent. The
            // socket has been listenable since bind() either way; the difference is whether a
            // caller arriving during the ten seconds of weight loading gets a documented
            // "still loading" or a hang.
            server.start_serving_during_startup();

            ninfer::serve::GenerationService service(
                options, startup_log.observer(),
                [&operational_log](const ninfer::ContextStoreWriteEvent& e) {
                    operational_log.context_store_write(e);
                },
                [operational_log, logger, exit_on_failure = options.exit_on_engine_failure](
                    const ninfer::EngineFaultEvent& event) {
                    handle_engine_fault(operational_log, logger, exit_on_failure, event);
                });
            startup_log.engine_ready(service.load_summary());
            operational_log.engine_capacity(service);

            using Clock                            = std::chrono::steady_clock;
            const Clock::time_point warmup_started = Clock::now();
            operational_log.warmup_started();
            try {
                service.warmup();
            } catch (const std::exception& exception) {
                const double seconds =
                    std::chrono::duration<double>(Clock::now() - warmup_started).count();
                operational_log.warmup_failure(seconds, exception.what());
                return 1;
            }
            operational_log.warmup_complete(
                std::chrono::duration<double>(Clock::now() - warmup_started).count());
            server.attach(service);

            g_server.store(&server);
            std::signal(SIGINT, handle_signal);
            std::signal(SIGTERM, handle_signal);
#ifdef SIGBREAK
            // Windows has no way to deliver SIGTERM to another process: TerminateProcess kills it
            // outright and the shutdown path -- which flushes the final partial throughput
            // interval -- never runs. GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT) is the one
            // graceful stop a parent can request, and the CRT raises it as SIGBREAK.
            std::signal(SIGBREAK, handle_signal);
#endif

            serving = true;
            operational_log.server_ready(options.host, options.port, server.public_model_id(),
                                         !options.api_key.empty(), options.webui_enabled);

            const bool ok = server.listen();
            g_server.store(nullptr);
            if (!ok) {
                operational_log.listen_failure(options.host, options.port);
                return 1;
            }
            operational_log.server_stopped();
            return 0;
        } catch (const std::exception& exception) {
            g_server.store(nullptr);
            const std::string what = exception.what();
            if (host_retriable && !serving &&
                what.find("cudaMallocHost failed") != std::string::npos) {
                const std::size_t budget = options.context_cache.host_capacity_bytes.value_or(0);
                if (budget > 0) {
                    const std::size_t next =
                        budget > (static_cast<std::size_t>(2) << 30) ? budget / 2 : 0;
                    logger->warn(
                        "Could not pin {} MiB of host RAM for the context cache. Retrying with "
                        "{} MiB.\nSet --host-context-mib to choose your own, or "
                        "NINFER_FALLBACK=off to fail instead.",
                        budget >> 20, next >> 20);
                    options.context_cache.host_capacity_bytes = next;
                    continue;
                }
            }
            operational_log.server_failure(serving, what);
            return 1;
        }
    }
}
