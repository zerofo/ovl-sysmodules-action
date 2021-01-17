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
    tsl::elm::ListItem *m_powerResetListItem;
    tsl::elm::ListItem *m_powerOffListItem;
    std::list<SystemModule> m_sysmoduleListItems;
    tsl::elm::CategoryHeader *m_bootCatHeader;
    tsl::elm::ListItem *m_listItem1;
    tsl::elm::ListItem *m_listItem2;
    tsl::elm::ListItem *m_opAutoboot;
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
    Result CopyFile(const char *src_path, const char *dest_path);
    BootDatType m_bootRunning;
    //s64 m_bootSize;
};