#pragma once

#include <list>
#include <tesla.hpp>

struct SystemModule {
    tsl::elm::ListItem *listItem;
    u64 programId;
    bool needReboot;
};
enum class BootDatType {
    SXOS_BOOT_TYPE,
    SXGEAR_BOOT_TYPE
};
class GuiMain : public tsl::Gui {
  private:
    FsFileSystem m_fs;
    std::list<SystemModule> m_sysmoduleListItems;
    tsl::elm::ListItem *m_listItemSXOSBootType;
    tsl::elm::ListItem *m_listItemSXGEARBootType;
    bool m_scanned;

  public:
    GuiMain();
    ~GuiMain();

    virtual tsl::elm::Element *createUI();
    virtual void update() override;

  private:
    void updateStatus(const SystemModule &module);
    bool hasFlag(const SystemModule &module);
    bool isRunning(const SystemModule &module);
    Result CopyFile(const char *srcPath, const char *destPath);
    BootDatType m_bootRunning;
};
