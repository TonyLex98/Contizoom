#ifndef IC_PROJECT_MANAGER_H
#define IC_PROJECT_MANAGER_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <ctime>

namespace ic {

class Canvas;
class IStorage;

struct ProjectInfo {
    std::string uuid;
    std::string name;
    std::string path;
    std::time_t createdAt = 0;
    std::time_t modifiedAt = 0;

    std::string relativePath() const;
};

enum class ProjectAction {
    None,
    CreateNew,
    OpenExisting,
    SaveCurrent,
    SaveAs,
    CloseCurrent,
    SwitchProject,
    RevertChanges,
    CloseAll,
    Quit
};

class ProjectManager {
public:
    ProjectManager();
    ~ProjectManager();

    void setStorage(std::unique_ptr<IStorage> storage);
    IStorage* storage() { return storage_.get(); }
    const IStorage* storage() const { return storage_.get(); }

    static std::string defaultProjectsDir();

    std::string createProject(const std::string& name = "Untitled");
    std::string openProject(const std::string& path);
    bool closeProject(const std::string& uuid);
    void closeAll();
    void switchTo(const std::string& uuid);

    // Full save: writes config.json + all node files
    bool saveCurrent();

    // Save to a new path
    bool saveAs(const std::string& newPath);

    // Revert: reload from disk
    bool revert();

    void processAutoSave();

    Canvas* activeCanvas();
    const Canvas* activeCanvas() const;
    ProjectInfo* activeProject();
    const ProjectInfo* activeProject() const;

    const std::vector<ProjectInfo>& projects() const { return projects_; }
    ProjectInfo* findProject(const std::string& uuid) const;
    std::vector<std::string> listSavedProjects() const;
    std::vector<std::string> recentProjects(int maxCount = 10) const;

    using BeforeCloseCallback = std::function<bool(const ProjectInfo&)>;
    void onBeforeClose(BeforeCloseCallback cb) { onBeforeClose_ = cb; }

    using ActiveChangedCallback = std::function<void(const ProjectInfo*)>;
    void onActiveChanged(ActiveChangedCallback cb) { onActiveChanged_ = cb; }

private:
    std::unique_ptr<IStorage> storage_;
    std::vector<ProjectInfo>  projects_;
    std::string               activeUuid_;
    std::vector<std::string>  recentPaths_;

    BeforeCloseCallback  onBeforeClose_;
    ActiveChangedCallback onActiveChanged_;

    std::map<std::string, std::unique_ptr<Canvas>> canvases_;

    // Set up auto-save for a canvas: each node change saves that node file immediately
    void setupAutoSave(const std::string& uuid);

    static std::string generateProjectUuid();
    void addRecent(const std::string& path);
    void notifyActiveChanged();
};

} // namespace ic

#endif // IC_PROJECT_MANAGER_H
