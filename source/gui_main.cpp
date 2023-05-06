#include "gui_main.hpp"

#include "dir_iterator.hpp"

#include <json.hpp>
using json = nlohmann::json;

constexpr const char *const amsContentsPath = "/atmosphere/contents";
constexpr const char *const boot2FlagFormat = "/atmosphere/contents/%016lX/flags/boot2.flag";
constexpr const char *const boot2FlagFolder = "/atmosphere/contents/%016lX/flags";

static char pathBuffer[FS_MAX_PATH];

constexpr const char *const descriptions[2][2] = {
    [0] = {
        [0] = "Off | \uE098",
        [1] = "Off | \uE0F4",
    },
    [1] = {
        [0] = "On | \uE098",
        [1] = "On | \uE0F4",
    },
};

GuiMain::GuiMain() {
    Result rc = fsOpenSdCardFileSystem(&this->m_fs);
    if (R_FAILED(rc))
        return;
    if (R_FAILED(rc = smInitialize())) return;

    if (R_FAILED(rc = nifmInitialize(NifmServiceType_Admin))) return;

    if (R_FAILED(rc = fsOpenSdCardFileSystem(&this->m_fs))) return;
    if (R_SUCCEEDED(rc = splInitialize())) {
        u64 version{0};
        u32 version_micro{0xff};
        u32 version_minor{0xff};
        u32 version_major{0xff};
        if (R_SUCCEEDED(rc = splGetConfig(static_cast<SplConfigItem>(AMSVersionConfigItem), &version))) {
            version_micro = (version >> 40) & 0xff;
            version_minor = (version >> 48) & 0xff;
            version_major = (version >> 56) & 0xff;
        }
        splExit();
        if (version_major == 0 && version_minor == 0 && version_micro == 0) {
                this->m_bootRunning = BootDatType::SXOS_BOOT_TYPE;
            std::strcpy(pathBuffer, sxosTitlesPath);
        } else if ((version_major == 0 && version_minor >= 9 && version_micro >= 0) || (version_major == 1 && version_minor >= 0 && version_micro >= 0)) {
                this->m_bootRunning = BootDatType::SXGEAR_BOOT_TYPE;
            std::strcpy(pathBuffer, amsContentsPath);
        } else {
            return;
        }
    }
    FsDir contentDir;
    std::strcpy(pathBuffer, amsContentsPath);
    rc = fsFsOpenDirectory(&this->m_fs, pathBuffer, FsDirOpenMode_ReadDirs, &contentDir);
    if (R_FAILED(rc))
        return;
    tsl::hlp::ScopeGuard dirGuard([&] { fsDirClose(&contentDir); });

    /* Iterate over contents folder. */
    for (const auto &entry : FsDirIterator(contentDir)) {
        FsFile toolboxFile;
        std::snprintf(pathBuffer, FS_MAX_PATH, "/atmosphere/contents/%.*s/toolbox.json", FS_MAX_PATH - 35, entry.name);
        rc = fsFsOpenFile(&this->m_fs, pathBuffer, FsOpenMode_Read, &toolboxFile);
        if (R_FAILED(rc))
            continue;
        tsl::hlp::ScopeGuard fileGuard([&] { fsFileClose(&toolboxFile); });

        /* Get toolbox file size. */
        s64 size;
        rc = fsFileGetSize(&toolboxFile, &size);
        if (R_FAILED(rc))
            continue;

        /* Read toolbox file. */
        std::string toolBoxData(size, '\0');
        u64 bytesRead;
        rc = fsFileRead(&toolboxFile, 0, toolBoxData.data(), size, FsReadOption_None, &bytesRead);
        if (R_FAILED(rc))
            continue;

        /* Parse toolbox file data. */
        json toolboxFileContent = json::parse(toolBoxData);

        const std::string &sysmoduleProgramIdString = toolboxFileContent["tid"];
        u64 sysmoduleProgramId = std::strtoul(sysmoduleProgramIdString.c_str(), nullptr, 16);

        /* Let's not allow Tesla to be killed with this. */
        if (sysmoduleProgramId == 0x420000000007E51AULL)
            continue;

        SystemModule module = {
            .listItem = new tsl::elm::ListItem(toolboxFileContent["name"]),
            .programId = sysmoduleProgramId,
            .needReboot = toolboxFileContent["requires_reboot"],
        };

        module.listItem->setClickListener([this, module](u64 click) -> bool {
            /* if the folder "flags" does not exist, it will be created */
            std::snprintf(pathBuffer, FS_MAX_PATH, boot2FlagFolder, module.programId);
            fsFsCreateDirectory(&this->m_fs, pathBuffer);
            std::snprintf(pathBuffer, FS_MAX_PATH, boot2FlagFormat, module.programId);

            if (click & HidNpadButton_A && !module.needReboot) {
                if (this->isRunning(module)) {
                    /* Kill process. */
                    pmshellTerminateProgram(module.programId);

                    /* Remove boot2 flag file. */
                    if (this->hasFlag(module))
                        fsFsDeleteFile(&this->m_fs, pathBuffer);
                } else {
                    /* Start process. */
                    const NcmProgramLocation programLocation{
                        .program_id = module.programId,
                        .storageID = NcmStorageId_None,
                    };
                    u64 pid = 0;
                    pmshellLaunchProgram(0, &programLocation, &pid);

                    /* Create boot2 flag file. */
                    if (!this->hasFlag(module))
                        fsFsCreateFile(&this->m_fs, pathBuffer, 0, FsCreateOption(0));
                }
                return true;
            }

            if (click & HidNpadButton_Y) {
                if (this->hasFlag(module)) {
                    /* Remove boot2 flag file. */
                    fsFsDeleteFile(&this->m_fs, pathBuffer);
                } else {
                    /* Create boot2 flag file. */
                    fsFsCreateFile(&this->m_fs, pathBuffer, 0, FsCreateOption(0));
                }
                return true;
            }

            return false;
        });
        this->m_sysmoduleListItems.push_back(std::move(module));
    }
    this->m_scanned = true;
}

GuiMain::~GuiMain() {
    fsFsClose(&this->m_fs);
}

tsl::elm::Element *GuiMain::createUI() {
    tsl::elm::OverlayFrame *rootFrame = new tsl::elm::OverlayFrame("Sysmodules", VERSION);
    tsl::elm::List *sysmoduleList_base = new tsl::elm::List();
        sysmoduleList_base->addItem(new tsl::elm::CategoryHeader("SWITCH Power Control  |  \uE0E0  Restart and Power off", true));
        sysmoduleList_base->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString("\uE016  Quick reset or power off your console.", false, x + 5, y + 20, 15, renderer->a(tsl::style::color::ColorDescription));
        }), 30);
        tsl::elm::ListItem *powerResetListItem = new tsl::elm::ListItem("Reboot");
        powerResetListItem->setValue("|  \uE0F4");
        powerResetListItem->setClickListener([this, powerResetListItem](u64 click) -> bool {
            if (click & HidNpadButton_A) {
                Result rc,rc1;
                // if (R_FAILED(rc = bpcInitialize()) || R_FAILED(rc = bpcRebootSystem()))
                if (R_FAILED(rc = spsmInitialize()) || R_FAILED(rc1 = spsmShutdown(true)))
                    powerResetListItem->setText("failed! code:" + std::to_string(rc) + " , " + std::to_string(rc1));
                spsmExit();
                //bpcExit();
                return true;
            }
            return false;
        });
        sysmoduleList_base->addItem(powerResetListItem);
        tsl::elm::ListItem *powerOffListItem = new tsl::elm::ListItem("Power off");
        powerOffListItem->setValue("|  \uE098");
        powerOffListItem->setClickListener([this, powerOffListItem](u64 click) -> bool {
            if (click & HidNpadButton_A) {
                Result rc,rc1;
                // if (R_FAILED(rc = bpcInitialize()) || R_FAILED(rc = bpcShutdownSystem()))
                if (R_FAILED(rc = spsmInitialize()) || R_FAILED(rc1 = spsmShutdown(false)))
                    powerOffListItem->setText("failed! code:" + std::to_string(rc) + " , " + std::to_string(rc1));
                spsmExit();
                //bpcExit();
                return true;
            }
            return false;
        });
        sysmoduleList_base->addItem(powerOffListItem);
    tsl::elm::CategoryHeader *bootCatHeader = new tsl::elm::CategoryHeader("Support CFW boot file switch  |  \uE0E0 Toggle", true);
    sysmoduleList_base->addItem(bootCatHeader);
    sysmoduleList_base->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
        renderer->drawString("\uE016  Takes effect after console restart.", false, x + 5, y + 20, 15, renderer->a(tsl::style::color::ColorDescription));
    }), 30);
    this->m_listItemSXOSBootType = new tsl::elm::ListItem(bootFiledescriptions[0]);
    this->m_listItemSXOSBootType->setClickListener([this, bootCatHeader](u64 click) -> bool {
        if (click & HidNpadButton_A) {
            if (this->m_bootRunning == BootDatType::SXOS_BOOT_TYPE) return true;
            Result rc;
            rc = this->CopyFile("/bootloader/boot-sxos.dat", "/boot.dat");
            if (R_FAILED(rc)) {
                if (rc == 514) {
                    bootCatHeader->setText("Select SXOS boot.dat failed! Boot file not exist!");
                } else {
                    bootCatHeader->setText("Select SXOS boot.dat failed! Error code: " + std::to_string(rc));
                }
                return false;
            }
            this->m_bootRunning = BootDatType::SXOS_BOOT_TYPE;
            return true;
        }
        return false;
    });
    
    if (this->m_sysmoduleListItems.size() == 0) {
        const char *description = this->m_scanned ? "No sysmodules found!" : "Scan failed!";

        auto *warning = new tsl::elm::CustomDrawer([description](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString("\uE150", false, 180, 250, 90, renderer->a(0xFFFF));
            renderer->drawString(description, false, 110, 340, 25, renderer->a(0xFFFF));
        });

        rootFrame->setContent(warning);
    } else {
        tsl::elm::List *sysmoduleList = new tsl::elm::List();
        sysmoduleList->addItem(new tsl::elm::CategoryHeader("Dynamic  |  \uE0E0  Toggle  |  \uE0E3  Toggle auto start", true));
        sysmoduleList->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString("\uE016  These sysmodules can be toggled at any time.", false, x + 5, y + 20, 15, renderer->a(tsl::style::color::ColorDescription));
        }), 30);
        for (const auto &module : this->m_sysmoduleListItems) {
            if (!module.needReboot)
                sysmoduleList->addItem(module.listItem);
        }

        sysmoduleList->addItem(new tsl::elm::CategoryHeader("Static  |  \uE0E3  Toggle auto start", true));
        sysmoduleList->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString("\uE016  These sysmodules need a reboot to work.", false, x + 5, y + 20, 15, renderer->a(tsl::style::color::ColorDescription));
        }), 30);
        for (const auto &module : this->m_sysmoduleListItems) {
            if (module.needReboot)
                sysmoduleList->addItem(module.listItem);
        }
        rootFrame->setContent(sysmoduleList);
    }

    return rootFrame;
}

void GuiMain::update() {
    static u32 counter = 0;

    if (counter++ % 20 != 0)
        return;

    for (const auto &module : this->m_sysmoduleListItems) {
        this->updateStatus(module);
    }
}

void GuiMain::updateStatus(const SystemModule &module) {
    bool running = this->isRunning(module);
    bool hasFlag = this->hasFlag(module);

    const char *desc = descriptions[running][hasFlag];
    module.listItem->setValue(desc);
}

bool GuiMain::hasFlag(const SystemModule &module) {
    FsFile flagFile;
    std::snprintf(pathBuffer, FS_MAX_PATH, boot2FlagFormat, module.programId);
    Result rc = fsFsOpenFile(&this->m_fs, pathBuffer, FsOpenMode_Read, &flagFile);
    if (R_SUCCEEDED(rc)) {
        fsFileClose(&flagFile);
        return true;
    } else {
        return false;
    }
}

bool GuiMain::isRunning(const SystemModule &module) {
    u64 pid = 0;
    if (R_FAILED(pmdmntGetProcessId(&pid, module.programId)))
        return false;

    return pid > 0;
}
Result GuiMain::CopyFile(const char *srcPath, const char *destPath) {
    Result ret{0};
    FsFile src_handle, dest_handle;
    if (R_FAILED(ret = fsFsOpenFile(&this->m_fs, srcPath, FsOpenMode_Read, &src_handle))) return ret;
    tsl::hlp::ScopeGuard fileGuard1([&] { fsFileClose(&src_handle); });

    s64 size = 0;
    if (R_FAILED(ret = fsFileGetSize(&src_handle, &size))) return ret;

    if (R_SUCCEEDED(fsFsOpenFile(&this->m_fs, destPath, FsOpenMode_Read, &dest_handle))) {
        fsFileClose(&dest_handle);
        if (R_FAILED(ret = fsFsDeleteFile(&this->m_fs, destPath))) return ret;
	    if (R_FAILED(ret = fsFsCreateFile(&this->m_fs, destPath, size, 0))) return ret;
    }

    if (R_FAILED(ret = fsFsOpenFile(&this->m_fs, destPath, FsOpenMode_Write, &dest_handle))) return ret;
    tsl::hlp::ScopeGuard fileGuard2([&] { fsFileClose(&dest_handle); });

    u64 bytes_read = 0;
    const u64 buf_size = 0x10000;
    s64 offset = 0;
    unsigned char *buf = new unsigned char[buf_size];
    tsl::hlp::ScopeGuard fileGuard3([&] { delete[] buf; });
    std::string str(srcPath);
    std::size_t pos = str.rfind("/");     
    std::string filename = str.substr(pos); 

    do {
        std::memset(buf, 0, buf_size);
        if (R_FAILED(ret = fsFileRead(&src_handle, offset, buf, buf_size, FsReadOption_None, &bytes_read))) return ret;
        if (R_FAILED(ret = fsFileWrite(&dest_handle, offset, buf, bytes_read, FsWriteOption_Flush))) return ret;
        offset += bytes_read;
    } while(offset < size);

    return ret;
}
