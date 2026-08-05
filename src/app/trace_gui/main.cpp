#include "canpp/core/session.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool contains(const canpp::protocol::can::SignalKey& key,
              const std::vector<canpp::protocol::can::SignalKey>& selected) {
    return std::find(selected.begin(), selected.end(), key) != selected.end();
}

std::string label_for(const canpp::protocol::can::SignalKey& key) {
    std::ostringstream id;
    id << std::hex << std::uppercase << key.can_id;
    return key.message_name + "." + key.signal_name + " [0x" + id.str() +
           (key.extended ? " extended]" : "]");
}

void draw_variable_selector(canpp::core::Session& session,
                            std::vector<canpp::protocol::can::SignalKey>& selected,
                            canpp::core::PlotSource& source,
                            std::vector<canpp::core::PlotSeries>& graph,
                            std::string& error) {
    static char search[128]{};
    ImGui::Begin("Graph variables");
    if (!session.has_trace()) {
        ImGui::TextUnformatted("Open a communication trace to select variables.");
        ImGui::End();
        return;
    }
    const auto variables = session.plot_variables();
    if (variables.empty()) {
        ImGui::TextUnformatted("Load a DBC file to select variables.");
        ImGui::End();
        return;
    }

    ImGui::InputTextWithHint("##signal-search", "Search messages or signals", search, sizeof(search));
    if (ImGui::RadioButton("Current selection", source == canpp::core::PlotSource::current_selection)) {
        source = canpp::core::PlotSource::current_selection;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Full trace", source == canpp::core::PlotSource::full_trace)) {
        source = canpp::core::PlotSource::full_trace;
    }
    ImGui::Separator();
    if (ImGui::Button("Select all")) {
        selected.clear();
        for (const auto& descriptor : variables) {
            selected.push_back(descriptor.key);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        selected.clear();
    }

    const std::string_view query(search);
    std::string current_message;
    for (const auto& descriptor : variables) {
        const auto& key = descriptor.key;
        if (!query.empty() && key.message_name.find(query) == std::string::npos &&
            key.signal_name.find(query) == std::string::npos) {
            continue;
        }
        if (key.message_name != current_message) {
            current_message = key.message_name;
            ImGui::SeparatorText(current_message.c_str());
        }
        bool checked = contains(key, selected);
        const auto checkbox_id = label_for(key);
        if (ImGui::Checkbox(checkbox_id.c_str(), &checked)) {
            if (checked) {
                selected.push_back(key);
            } else {
                selected.erase(std::remove(selected.begin(), selected.end(), key), selected.end());
            }
        }
        ImGui::SameLine(330.0F);
        ImGui::Text("%s", descriptor.unit.empty() ? "-" : descriptor.unit.c_str());
    }

    ImGui::Separator();
    ImGui::Text("Selected: %zu", selected.size());
    ImGui::BeginDisabled(selected.empty());
    if (ImGui::Button("Create graph")) {
        canpp::core::PlotRequest request;
        request.variables = selected;
        request.source = source;
        std::vector<canpp::core::PlotSeries> extracted;
        if (session.extract_plot_data(request, extracted, error)) {
            graph = std::move(extracted);
            error.clear();
        }
    }
    ImGui::EndDisabled();
    if (!error.empty()) {
        ImGui::TextColored(ImVec4(1.0F, 0.3F, 0.3F, 1.0F), "%s", error.c_str());
    }
    ImGui::End();
}

void draw_graph(const std::vector<canpp::core::PlotSeries>& graph) {
    ImGui::Begin("Graph");
    if (graph.empty()) {
        ImGui::TextUnformatted("Select variables and create a graph.");
        ImGui::End();
        return;
    }
    if (ImPlot::BeginPlot("##trace-graph", ImVec2(-1.0F, -1.0F))) {
        std::uint64_t origin = graph.front().samples.empty() ? 0U : graph.front().samples.front().timestamp_ns;
        for (const auto& series : graph) {
            for (const auto& sample : series.samples) {
                origin = std::min(origin, sample.timestamp_ns);
            }
        }
        std::vector<double> x;
        std::vector<double> y;
        x.reserve(graph.front().samples.size());
        y.reserve(graph.front().samples.size());
        for (const auto& series : graph) {
            x.clear();
            y.clear();
            for (const auto& sample : series.samples) {
                x.push_back(static_cast<double>(sample.timestamp_ns - origin) / 1'000'000'000.0);
                y.push_back(sample.value.value_or(std::numeric_limits<double>::quiet_NaN()));
            }
            const auto name = label_for(series.variable);
            if (!x.empty()) {
                ImPlot::PlotLine(name.c_str(), x.data(), y.data(), static_cast<int>(x.size()));
            }
        }
        ImPlot::EndPlot();
    }
    ImGui::End();
}

} // namespace

int main(int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_Window* window = SDL_CreateWindow("Canpp graph", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == nullptr) {
        std::cerr << "Window creation failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        std::cerr << "OpenGL context creation failed: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init("#version 150");

    canpp::core::Session session;
    std::string error;
    if (argc > 1 && !session.open(argv[1], error)) {
        std::cerr << error << '\n';
    }
    if (argc > 2 && !session.load_dbc(argv[2], error)) {
        std::cerr << error << '\n';
    }
    std::vector<canpp::protocol::can::SignalKey> selected;
    std::vector<canpp::core::PlotSeries> graph;
    auto source = canpp::core::PlotSource::current_selection;
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        draw_variable_selector(session, selected, source, graph, error);
        draw_graph(graph);
        ImGui::Render();
        int width = 0;
        int height = 0;
        SDL_GL_GetDrawableSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.08F, 0.08F, 0.08F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
