#include "gui_main.hpp"

#include "dir_iterator.hpp"

#include <json.hpp>
using json = nlohmann::json;

constexpr const char *const amsContentsPath = "/atmosphere/contents";
//constexpr const char *const boot2FlagFormat = "/atmosphere/contents/%016lX/flags/boot2.flag";
constexpr const char *const sxosTitlesPath = "/sxos/titles";
constexpr const char *const boot2FlagPath = "/%016lX/flags/boot2.flag";
constexpr const char *const toolboxJsonPath = "/%s/toolbox.json";

static std::string boot2FlagFormat{amsContentsPath};
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

constexpr const char *const bootFiledescriptions[2] = {
        [0] = "SXOS boot.dat",
        [1] = "SXGEAR boot.dat"
};

constexpr const char *const bootFileSrcPath[2] = {
        [0] = "/bootloader/boot-sxos.dat",
        [1] = "/bootloader/boot-sxgear.dat"
};

GuiMain::GuiMain() {
    Result rc = fsOpenSdCardFileSystem(&this->m_fs);
    if (R_FAILED(rc))
        return;

    FsDir contentDir;
    std::strcpy(pathBuffer, amsContentsPath);
    rc = fsFsOpenDirectory(&this->m_fs, pathBuffer, FsDirOpenMode_ReadDirs, &contentDir);
    if (R_FAILED(rc)) {
        std::strcpy(pathBuffer, sxosTitlesPath);
        rc = fsFsOpenDirectory(&this->m_fs, pathBuffer, FsDirOpenMode_ReadDirs, &contentDir);
        if (R_FAILED(rc))
            return;
    }
    boot2FlagFormat = std::string(pathBuffer) + boot2FlagPath;
    std::string toolboxJsonFormat = std::string(pathBuffer) + toolboxJsonPath;

    tsl::hlp::ScopeGuard dirGuard([&] { fsDirClose(&contentDir); });

    /* Iterate over contents folder. */
    for (const auto &entry : FsDirIterator(contentDir)) {
        FsFile toolboxFile;
        std::snprintf(pathBuffer, FS_MAX_PATH, toolboxJsonFormat.c_str(), entry.name);
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
            if (click & KEY_A && !module.needReboot) {
                if (this->isRunning(module)) {
                    /* Kill process. */
                    pmshellTerminateProgram(module.programId);
                } else {
                    /* Start process. */
                    const NcmProgramLocation programLocation{
                        .program_id = module.programId,
                        .storageID = NcmStorageId_None,
                    };
                    u64 pid = 0;
                    pmshellLaunchProgram(0, &programLocation, &pid);
                }
                return true;
            }

            if (click & KEY_Y) {
                std::snprintf(pathBuffer, FS_MAX_PATH, boot2FlagFormat.c_str(), module.programId);
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
    tsl::elm::List *sysmoduleList = new tsl::elm::List();
    if (this->m_sysmoduleListItems.size() == 0) {
        const char *description = this->m_scanned ? "没有找到任何系统模块！" : "检索失败！";
        auto *warning = new tsl::elm::CustomDrawer([description](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString("\uE150", false, 180, 250, 90, renderer->a(0xFFFF));
            renderer->drawString(description, false, 110, 340, 25, renderer->a(0xFFFF));
        });
        sysmoduleList->addItem(warning);
    } else {
        sysmoduleList->addItem(new tsl::elm::CategoryHeader("动态  |  \uE0E0 切换  |  \uE0E3 自动启动", true));
        sysmoduleList->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString("\uE016  这些系统模块可以任何时候切换。", false, x + 5, y + 20, 15, renderer->a(tsl::style::color::ColorDescription));
        }), 30);
        for (const auto &module : this->m_sysmoduleListItems) {
            if (!module.needReboot)
                sysmoduleList->addItem(module.listItem);
        }

        sysmoduleList->addItem(new tsl::elm::CategoryHeader("静态  |  \uE0E3 自动启动", true));
        sysmoduleList->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString("\uE016  这些系统模块需要重启切换。", false, x + 5, y + 20, 15, renderer->a(tsl::style::color::ColorDescription));
        }), 30);
        for (const auto &module : this->m_sysmoduleListItems) {
            if (module.needReboot)
                sysmoduleList->addItem(module.listItem);
        }
    }

    sysmoduleList->addItem(new tsl::elm::CategoryHeader("支持系统引导的Boot文件  |  \uE0E0 切换", true));
    sysmoduleList->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
        renderer->drawString("\uE016  切换后重启生效。", false, x + 5, y + 20, 15, renderer->a(tsl::style::color::ColorDescription));
    }), 30);

    s64 size = 0; 
	FsFile bootHandle;
	if (R_FAILED(fsFsOpenFile(&m_fs, "/boot.dat", FsOpenMode_Read, &bootHandle))) return rootFrame;
	if (R_FAILED(fsFileGetSize(&bootHandle, &size))) {
		fsFileClose(&bootHandle);
		return rootFrame;
	}
	fsFileClose(&bootHandle);

	m_bootRunning = (size < 20 * 1024 * 1024) ? BootDatType::SXGEAR_BOOT_TYPE : BootDatType::SXOS_BOOT_TYPE;

    m_listItem1 = new tsl::elm::ListItem(bootFiledescriptions[0]);
    m_listItem1->setValue((m_bootRunning == BootDatType::SXOS_BOOT_TYPE) ? "正在使用 | \uE0F4" : "未启用 | \uE098");
    m_listItem1->setClickListener([this](u64 click) -> bool {
        if (click & KEY_A) {
            m_bootRunning = (m_bootRunning == BootDatType::SXOS_BOOT_TYPE) ? BootDatType::SXGEAR_BOOT_TYPE : BootDatType::SXOS_BOOT_TYPE;
            const char *path = (m_bootRunning == BootDatType::SXOS_BOOT_TYPE) ? bootFileSrcPath[0]: bootFileSrcPath[1];
            CopyFile(path, "/boot.dat");
            return true;
        }
        return false;
    });
    sysmoduleList->addItem(m_listItem1);

	m_listItem2 = new tsl::elm::ListItem(bootFiledescriptions[1]);
    m_listItem2->setValue((m_bootRunning == BootDatType::SXGEAR_BOOT_TYPE) ? "正在使用 | \uE0F4" : "未启用 | \uE098");
    m_listItem2->setClickListener([this](u64 click) -> bool {
        if (click & KEY_A) {
            m_bootRunning = (m_bootRunning == BootDatType::SXGEAR_BOOT_TYPE) ? BootDatType::SXOS_BOOT_TYPE : BootDatType::SXGEAR_BOOT_TYPE;
            const char *path = (m_bootRunning == BootDatType::SXGEAR_BOOT_TYPE) ? bootFileSrcPath[1]: bootFileSrcPath[0];
            CopyFile(path, "/boot.dat");
#if 0
            std::ifstream src(path, std::ios::binary);
            std::ofstream dest("sdmc:/boot.dat", std::ios::trunc|std::ios::binary);
            dest << src.rdbuf();
            src.close();
            dest.flush();
            dest.close();
#endif
            return true;
        }
        return false;
    });
    sysmoduleList->addItem(m_listItem2);

    rootFrame->setContent(sysmoduleList);

    return rootFrame;
}

Result GuiMain::CopyFile(const char *src_path, const char *dest_path) {
	Result ret = 0;
	FsFile src_handle, dest_handle;

	if (R_FAILED(ret = fsFsOpenFile(&m_fs, src_path, FsOpenMode_Read, &src_handle))) return ret;

	s64 size = 0;
	if (R_FAILED(ret = fsFileGetSize(&src_handle, &size))) {
		fsFileClose(&src_handle);
		return ret;
	}

    fsFsDeleteFile(&m_fs, dest_path);

	// This may fail or not, but we don't care -> create the file if it doesn't exist, otherwise continue.
	fsFsCreateFile(&m_fs, dest_path, size, 0);

	if (R_FAILED(ret = fsFsOpenFile(&m_fs, dest_path, FsOpenMode_Write, &dest_handle))) {
		fsFileClose(&src_handle);
		return ret;
	}

	u64 bytes_read = 0;
	const u64 buf_size = 0x10000;
	s64 offset = 0;
	unsigned char *buf = new unsigned char[buf_size];
	std::string filename = std::filesystem::path(src_path).filename();

	do {
		std::memset(buf, 0, buf_size);

		if (R_FAILED(ret = fsFileRead(&src_handle, offset, buf, buf_size, FsReadOption_None, &bytes_read))) {
			delete[] buf;
			fsFileClose(&src_handle);
			fsFileClose(&dest_handle);
			return ret;
		}

		if (R_FAILED(ret = fsFileWrite(&dest_handle, offset, buf, bytes_read, FsWriteOption_Flush))) {
			delete[] buf;
			fsFileClose(&src_handle);
			fsFileClose(&dest_handle);
			return ret;
		}
		offset += bytes_read;
	} while(offset < size);

	delete[] buf;
	fsFileClose(&src_handle);
	fsFileClose(&dest_handle);
	return 0;
}

void GuiMain::update() {
    static u32 counter = 0;

    if (counter++ % 20 != 0)
        return;

    m_listItem1->setValue((m_bootRunning == BootDatType::SXOS_BOOT_TYPE) ? "正在使用 | \uE0F4" : "未启用 | \uE098");
    m_listItem2->setValue((m_bootRunning == BootDatType::SXGEAR_BOOT_TYPE) ? "正在使用 | \uE0F4" : "未启用 | \uE098");
    //m_listItem1->setText(bootFiledescriptions[index]);
    //m_listItem2->setText(bootFiledescriptions[index?0:1]);

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
    std::snprintf(pathBuffer, FS_MAX_PATH, boot2FlagFormat.c_str(), module.programId);
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
