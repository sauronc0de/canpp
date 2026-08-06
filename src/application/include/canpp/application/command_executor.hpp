#pragma once

#include "canpp/application/command.hpp"
#include "canpp/core/session.hpp"

namespace canpp::application {

struct ExecuteOptions {
    // Interactive adapters retain the historical status line after mutations;
    // batch adapters disable it to keep output deterministic and scriptable.
    bool include_status{true};
};

class CommandExecutor {
public:
    explicit CommandExecutor(core::Session& session) noexcept : session_(session) {}

    [[nodiscard]] CommandResult execute(const CommandRequest& request,
                                        ExecuteOptions options = {});

private:
    core::Session& session_;
};

} // namespace canpp::application
