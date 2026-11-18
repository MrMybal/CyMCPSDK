// Headless CPU timings for the same CyUI public API across releases. No GPU or provider.
#include <cyui/Core.h>
#include <cyui/modules/DocumentViews.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

using Clock = std::chrono::steady_clock;
using Samples = std::vector<double>;

double elapsed_us(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

void report(const char* phase, Samples samples) {
    std::sort(samples.begin(), samples.end());
    const auto n = samples.size();
    const double median = (samples[(n - 1) / 2] + samples[n / 2]) / 2;
    std::cout << phase << ',' << n << ',' << median << ',' << samples[(95 * n + 99) / 100 - 1]
              << '\n';
}

int main() {
    const std::string section =
        "### Result\n\nA **bold** claim with [a reference](https://example.invalid/local).\n\n"
        "> Consecutive samples share a source.\n\n"
        "| Name | Count |\n| --- | ---: |\n| Sample | 4 |\n\n"
        "- [x] Test parsing\n- [ ] Check output\n\n"
        "```cpp\nauto result = sample();\n```\n\n";
    std::string body;
    for (int i = 0; i < 8; ++i)
        body += section;

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1100, 900};
    io.DeltaTime = 1.f / 60;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    cyui::apply();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(ImTextureID(1));

    Samples parses, streaming, stable;
    std::vector<cyui::TranscriptMessage> messages(32);
    for (std::size_t i = 0; i < messages.size(); ++i) {
        messages[i].id = i + 1;
        messages[i].role = i % 2 ? "assistant" : "user";
        messages[i].content = i % 2 ? section : "Show the results.";
    }
    cyui::TranscriptSource source;
    source.id = 1;
    source.count = messages.size();
    source.message = [&](std::size_t i) { return &messages.at(i); };
    int final_vertices = 0;
    for (int run = 0; run < 10; ++run) {
        cyui::TranscriptState state;
        cyui::Document parsed;
        auto& latest = messages.back();
        latest.content.clear();
        auto draw = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Transcript", nullptr, ImGuiWindowFlags_NoSavedSettings);
            cyui::transcript("chat", state, source, {1040, 800});
            ImGui::End();
            ImGui::Render();
        };
        for (int i = 0; i < 6; ++i)
            draw();
        for (std::size_t offset = 0; offset < body.size(); offset += 64) {
            latest.content.append(body, offset, 64);
            auto start = Clock::now();
            parsed.setMarkdown(latest.content);
            const auto parse = elapsed_us(start);
            start = Clock::now();
            draw();
            const auto frame = elapsed_us(start);
            if (state.documents.at(latest.id).markdown() != latest.content)
                throw std::runtime_error("Latest Markdown was not rendered");
            if (run > 0) {
                parses.push_back(parse);
                streaming.push_back(frame);
            }
        }
        for (int i = 0; i < 48; ++i) {
            auto start = Clock::now();
            draw();
            const auto frame = elapsed_us(start);
            if (run > 0)
                stable.push_back(frame);
        }
        final_vertices = ImGui::GetDrawData()->TotalVtxCount;
        if (parsed.blocks().empty() || final_vertices <= 0)
            throw std::runtime_error("Empty Document or ImGui draw data");
    }
    ImGui::DestroyContext();
    std::cout << "bytes=" << body.size() << " messages=" << source.count
              << " final_vertices=" << final_vertices << "\nphase,n,p50_us,p95_us\n";
    report("parse", parses);
    report("streaming_frame", streaming);
    report("stable_frame", stable);
}
