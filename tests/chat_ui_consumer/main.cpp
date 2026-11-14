#include <cymcp/chat_ui.hpp>
#include <cyui/Core.h>
int main() {
    cymcp::ChatController controller;
    ImGui::CreateContext();
    cyui::apply();
    {
        cymcp::ChatPanel panel(controller);
        panel.set_draft("Installed ChatUI component");
    }
    ImGui::DestroyContext();
    return controller.state().phase == "disconnected" ? 0 : 1;
}
