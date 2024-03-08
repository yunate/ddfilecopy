#include "ddfilecopy/stdafx.h"

#include "ddbase/ddmini_include.h"
#include "ddbase/ddcmd_line_utils.h"
#include "ddbase/ddio.h"
#include "ddbase/ddlocale.h"

#include <mbctype.h>
#pragma comment(lib, "ddbase.lib")

namespace NSP_DD {
static ddtimer s_timer;
static s32 s_result = 1;

static void push_filter_str(std::vector<std::wstring>& vec, const std::wstring& str)
{
    std::wstring common_str = str;
    std::vector<std::wstring> spliters = {L",", L";" };
    for (const auto& it : spliters) {
        common_str = ddstr::replace(common_str.c_str(), it.c_str(), L" ");
    }
    std::vector<std::wstring> splits;
    ddstr::split(common_str.c_str(), L" ", splits);
    vec.insert(vec.end(), splits.begin(), splits.end());
}

static bool str_match(const std::wstring& src, const std::vector<std::wstring>& cmps)
{
    for (const auto& it : cmps) {
        if (ddstr::strwildcard(src.c_str(), it.c_str())) {
            return true;
        }
    }
    return false;
}

struct ddfile_info
{
    std::wstring src_path;
    std::wstring dst_path;
    s64 file_size = 0;
};

static void log(ddconsole_color color, const std::wstring& log_str)
{
    ddcout(color) << log_str;
}

static void copy_file(
    const std::wstring& src_path,
    const std::wstring& dst_path,
    std::vector<std::wstring> includes,
    std::vector<std::wstring> excludes,
    bool skip_empty_dir,
    bool detail_log)
{
    std::vector<ddfile_info*> file_infos;
    ddexec_guard guard([&file_infos]() {
        for (auto it : file_infos) {
            delete it;
        }
        file_infos.clear();
    });

    s64 all_size = 0;
    s64 file_count = 0;
    if (dddir::is_dir(src_path)) {
        dddir::enum_dir(src_path, [&](const std::wstring& full_path, bool is_dir) {
            if (!includes.empty()) {
                if (!str_match(full_path, includes)) {
                    return false;
                }
            }

            if (!excludes.empty()) {
                if (str_match(full_path, excludes)) {
                    return false;
                }
            }

            if (is_dir && skip_empty_dir) {
                return false;
            }

            ddfile_info* info = new ddfile_info();
            file_infos.push_back(info);
            info->src_path = full_path;
            info->dst_path = ddpath::join(dst_path, full_path.substr(src_path.length() + 1));;
            if (!is_dir) {
                info->file_size = ddfile::file_size(full_path);
                all_size += info->file_size;
                ++file_count;
            }
            return false;
        });
    } else {
        do {
            if (!includes.empty()) {
                if (!str_match(src_path, includes)) {
                    break;
                }
            }

            if (!excludes.empty()) {
                if (str_match(src_path, excludes)) {
                    break;
                }
            }

            ddfile_info* info = new ddfile_info();
            file_infos.push_back(info);
            info->src_path = src_path;
            info->dst_path = dst_path;
            info->file_size = ddfile::file_size(src_path);
            all_size += info->file_size;
            ++file_count;
        } while (0);
    }

    s64 copied_size = 0;
    log(ddconsole_color::gray, ddstr::format(L"file count:%lld, all size:%lldB \r\n", file_count, all_size));
    std::vector<std::wstring> error_strs;
    ddtimer current_time_use;
    for (size_t i = 0; i < file_infos.size(); ++i) {
        current_time_use.reset();
        auto it = file_infos[i];
        if (!dddir::copy_path(it->src_path, it->dst_path)) {
            std::wstring error_str = ddstr::format(
                L"[copy %s -> %s failure]\r\n%s",
                it->src_path.c_str(), it->dst_path.c_str(),
                last_error_msgw(::GetLastError()).c_str());
            error_strs.push_back(error_str);
            if (detail_log) {
                log(ddconsole_color::red, error_str);
            }
        } else {
            copied_size += it->file_size;
            if (detail_log) {
                ddtime time = ddtime::now_fmt();
                int precent = (int)((double)copied_size / (double)all_size * 1000);
                std::wstring log_str = ddstr::format(L"[%d:%d:%d, %dms/%dms], [%d/%d], [%lldB %lldB/%lldB, %d.%d%%], [copy %s complete]\r\n",
                    time.hour, time.min, time.sec, current_time_use.get_time_pass() / 1000000, s_timer.get_time_pass() / 1000000,
                    i + 1, file_infos.size(),
                    it->file_size, copied_size, all_size, precent / 10, precent % 10,
                    it->src_path.c_str());
                log(ddconsole_color::green, log_str);
            }
        }
    }

    log(ddconsole_color::green, ddstr::format(L"total %d files to copy, ", file_count));
    log(ddconsole_color::green, ddstr::format(L"%d file copy successful.\r\n", file_count - error_strs.size()));
    if (!error_strs.empty()) {
        log(ddconsole_color::red, ddstr::format(L"%d file copy failure \r\n", error_strs.size()));
        for (const auto& it : error_strs) {
            log(ddconsole_color::red, it);
        }
    }

    log(ddconsole_color::gray, ddstr::format(L"time used: %ds\r\n", s_timer.get_time_pass() / 1000000000));
    s_result = (s32)error_strs.size();
}


static void help()
{
    log(ddconsole_color::gray, L"ddfilecopy.exe src_path dst_path [-skip_empty_dir] [-include *.h *.cpp] [-exclude *.ink *tmp*] [-detail_log]\r\n");
    log(ddconsole_color::gray, L"* represents zoro or more arbitrary characters; '?' represents one character (in Unicode, Chinese characters count as two characters)\r\n");
}

static void process_cmds(const std::vector<std::wstring>& cmds)
{
    if (cmds.size() < 3) {
        help();
        return;
    }

    std::wstring src_path = cmds[1];
    std::wstring dst_path = cmds[2];
    std::vector<std::wstring> includes;
    std::vector<std::wstring> excludes;
    bool skip_empty_dir = false;
    bool detail_log = false;

    for (size_t i = 3; i < cmds.size(); ++i) {
        if (cmds[i] == L"-help") {
            help();
            return;
        }

        if (cmds[i] == L"-skip_empty_dir") {
            skip_empty_dir = true;
            continue;
        }

        if (cmds[i] == L"-detail_log") {
            detail_log = true;
            continue;
        }

        if (cmds[i] == L"-include") {
            ++i;
            for (; i < cmds.size(); ++i) {
                if (ddstr::beginwith(cmds[i].c_str(), L"-")) {
                    --i;
                    break;
                }
                push_filter_str(includes, cmds[i]);
            }
            continue;
        }

        if (cmds[i] == L"-exclude") {
            ++i;
            for (; i < cmds.size(); ++i) {
                if (ddstr::beginwith(cmds[i].c_str(), L"-")) {
                    --i;
                    break;
                }
                push_filter_str(excludes, cmds[i]);
            }
            continue;
        }
    }

    copy_file(src_path, dst_path, includes, excludes, skip_empty_dir, detail_log);
}

int ddmain()
{
    // ddfilecopy.exe E:\ddworkspace\dd E:\ddworkspace\export -include *.lib *.dll *.exe *.pdb *.h *.hpp -exclude *bin\*\tmp\* *\__DD_DEMO__\* *projects\test*
    ddlocale::set_utf8_locale_and_io_codepage();
    s_timer.reset();
    std::vector<std::wstring> cmds;
    ddcmd_line_utils::get_cmds(cmds);
    process_cmds(cmds);
    return s_result;
}
} // namespace NSP_DD

void main()
{
    // ::_CrtSetBreakAlloc(918);
    int result = NSP_DD::ddmain();

#ifdef _DEBUG
    _cexit();
    DDASSERT_FMT(!::_CrtDumpMemoryLeaks(), L"Memory leak!!! Check the output to find the log.");
    ::system("pause");
    ::_exit(result);
#else
    ::system("pause");
    ::exit(result);
#endif
}

