#ifndef IC_PROJECT_MENU_H
#define IC_PROJECT_MENU_H

#include "app/project_manager.h"
#include <string>
#include <vector>

namespace ic {

class ProjectMenu {
public:
    ProjectMenu();

    // Show context menu at given console position, block until selection
    ProjectAction open(ProjectManager& pm, int mouseX, int mouseY);
    const std::string& chosenName() const { return chosenName_; }

private:
    int cursorPos_ = 0;
    std::string chosenName_;
    ProjectManager* pm_ = nullptr;
    int mouseX_ = 0, mouseY_ = 0;

    struct MenuItem {
        std::string label;
        bool isSeparator_ = false;
        bool isDisabled_ = false;
        ProjectAction action_ = ProjectAction::None;
    };

    void drawMenu();
    std::vector<MenuItem> buildItems();
    ProjectAction handleKey(char key, bool ctrl);
};

} // namespace ic

#endif // IC_PROJECT_MENU_H
