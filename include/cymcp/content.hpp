#pragma once
#include "core.hpp"
namespace cymcp {
// Portable blocks follow MCP/ACP: text, image/audio (mimeType + base64 data),
// and resource (uri + mimeType + text or base64 blob). No implicit file reads.
Json text_block(std::string text);
Json attachment_from_file(const std::filesystem::path&, std::string mime_type = {},
                          std::size_t max_bytes = 32 * 1024 * 1024);
void validate_content(const Json&);
namespace detail {
enum class ContentFormat { ChatCompletions, Responses, Anthropic, Ollama, Acp, Codex };
Json content_for(const Json&, ContentFormat, const std::string& role = "user");
std::string content_text(const Json&);
} // namespace detail
} // namespace cymcp
