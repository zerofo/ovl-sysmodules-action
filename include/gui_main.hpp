#pragma once

#include <list>
#include <string>
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
    tsl::elm::ListItem *m_listItem1;
    tsl::elm::ListItem *m_listItem2;
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
    Result setGetIniConfig(std::string iniPath, std::string iniSection, std::string iniOption, std::string &iniValue, bool getOption = true);
    BootDatType m_bootRunning;
    bool m_isTencentVersion;
    //s64 m_bootSize;
};