/**
 * @file
 * @brief Historical CPU teaching implementation, retained for reference.
 *
 * The current application uses rt::SceneData and the shared TraceCore.inl kernel.
 */
#pragma once
#include <string>
#include <fstream>
#include <initializer_list>

#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#include <ftw.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>

#elif defined(_WIN32)
#define NOMINMAX
#include <direct.h>
#include <Shlobj.h>
#include <windows.h>
#include <Shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#endif

// System calls.

#if defined(__linux__) || defined(__APPLE__)
/** @brief Remove one entry during a depth-first nftw traversal; report platform errors. */
int s_unlink_cb(const char* fpath, const struct stat* sb, int typeflag, struct FTW* ftwbuf)
{
    int rv = remove(fpath);

    if (rv)
        perror(fpath);
    return rv;
}

/**
 * @brief Recursively remove a path without following symbolic links.
 * @param path Path to remove.
 */
int s_rmrf(std::string path)
{
    return nftw(path.c_str(), s_unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
}

/** @brief Create POSIX path components with the requested permissions; requires a nonempty path. */
int s_mkpath(std::string s, mode_t mode)
{
    size_t pre = 0, pos;
    std::string dir;
    int mdret = 1;

    if (s[s.size() - 1] != '/')
        s += '/';

    while ((pos = s.find_first_of('/', pre)) != std::string::npos)
    {
        dir = s.substr(0, pos++);
        pre = pos;

        if (dir.size() == 0)
            continue;

        if ((mdret = mkdir(dir.c_str(), mode)) && errno != EEXIST)
            return mdret;
    }

    return mdret;
}
#endif

/**
 * @brief Return a string with all nonoverlapping occurrences replaced.
 *
 * The search string must not be empty.
 */
std::string replace_all(const std::string &in, const std::string &search, const std::string &replace)
{
    std::string s = in;

    for (size_t pos = 0;; pos += replace.length())
    {
        pos = s.find(search, pos);

        if (pos == std::string::npos)
            break;
        s.erase(pos, search.length());
        s.insert(pos, replace);
    }

    return s;
}

/** @brief Apply the historical platform path handling, optionally adding a Windows long-path prefix. */
std::string s_format_path(const std::string in, bool support_long_path = false)
{
#if defined(__linux__) || defined(__APPLE__)
    return std::string(in);
#elif defined(_WIN32)
    std::string out(in);
    replace_all(out, "/", "\\");

    if (support_long_path)
        out = std::string("\\\\?\\") + out;
    return out;
#endif
}

/**
 * @brief Request directory creation through the platform API.
 * @param arg Directory path to create.
 */
void s_mkdir(const std::string arg)
{
#if defined(__linux__) || defined(__APPLE__)
    s_mkpath(arg, 0755);
#elif defined(_WIN32)
    std::string in = s_format_path(arg);
    std::wstring w_arg = std::wstring(in.begin(), in.end());
    CreateDirectoryW(w_arg.c_str(), NULL);
#endif
}

/**
 * @brief Request file removal through the platform API.
 * @param arg File path to remove.
 */
void s_rm(const std::string arg)
{
#if defined(__linux__) || defined(__APPLE__)
    ::remove(arg.c_str());
#elif defined(_WIN32)
    std::ifstream infile(arg);

    if (infile.is_open())
    {
        infile.close();
        std::string in = s_format_path(arg);
        std::wstring w_arg = std::wstring(in.begin(), in.end());
        DeleteFileW(w_arg.c_str());
    }
#endif
}

// Directory and file management.


#if defined(__linux__) || defined(__APPLE__)
const std::string filesystemSeparator = "/";
#elif defined(_WIN32)
const std::string filesystemSeparator = "\\";
#endif

/**
 * @brief Return whether a directory can be identified at the supplied path.
 */
bool directory_exist(std::string path)
{
    const char* pzPath = path.c_str();
#if defined(__linux__) || defined(__APPLE__)
    if (pzPath == NULL)
        return false;

    DIR* pDir;
    bool bExists = false;

    pDir = opendir(pzPath);

    if (pDir != NULL)
    {
        bExists = true;
        (void)closedir(pDir);
    }

    return bExists;
#elif defined(_WIN32)
    DWORD ftyp = GetFileAttributesA(pzPath);

    if (ftyp == INVALID_FILE_ATTRIBUTES)
        return false;

    if (ftyp & FILE_ATTRIBUTE_DIRECTORY)
        return true;

    return false;
#endif
}

/**
 * @brief Return whether an input file stream can open the supplied path.
 */
bool file_exist(std::string fileName)
{
    std::ifstream infile(fileName.c_str());
    bool res = infile.is_open();

    if (res)
        infile.close();
    return res;
}

/**
 * @brief Return the executable directory on Linux or Windows, or an empty string on failure.
 */
std::string get_app_folder()
{
#if defined(__linux__)
    std::string path = "";
    pid_t pid = getpid();
    char buf[20] = {0};
    sprintf(buf, "%d", pid);
    std::string _link = "/proc/";
    _link.append(buf);
    _link.append("/exe");
    char proc[512];
    int ch = readlink(_link.c_str(), proc, 512);

    if (ch != -1)
    {
        proc[ch] = 0;
        path = proc;
        std::string::size_type t = path.find_last_of("/");
        path = path.substr(0, t);
    }

    return path;
#elif defined(_WIN32)
    wchar_t ownPth[MAX_PATH];
    char mbOwnPth[MAX_PATH];

    HMODULE hModule = GetModuleHandle(NULL);

    if (hModule != NULL)
    {
        GetModuleFileNameW(hModule, ownPth, MAX_PATH);
        PathRemoveFileSpecW(ownPth);
        wcstombs(mbOwnPth, ownPth, MAX_PATH);

        return std::string(mbOwnPth);
    }
    else
        return std::string("");
#endif
}

/**
 * @brief Create a directory relative to the executable.
 * @warning On POSIX, an existing directory is recursively removed first.
 * This historical helper is not a non-destructive ensure-directory operation.
 */
void create_directory(std::string directory_name)
{
    std::string path = get_app_folder() + filesystemSeparator + directory_name;
#if defined(__linux__) || defined(__APPLE__)
    if (directory_exist(path))
        s_rmrf(path);
#endif
    s_mkdir(path);
}

/**
 * @brief Join path components relative to the executable directory using the platform separator.
 */
std::string filename_join(std::initializer_list<std::string> path)
{
    std::string fullPath = get_app_folder();

    for (auto elem : path)
    {
        fullPath += filesystemSeparator;
        fullPath += elem;
    }

    return fullPath;
}
