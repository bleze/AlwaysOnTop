#include "Updater.h"

#include "AlwaysOnTop/Version.h"

#include <winhttp.h>

#include <cwctype>
#include <memory>
#include <vector>

namespace {

constexpr wchar_t kUserAgent[] = L"AlwaysOnTop-Updater/1.0";
constexpr wchar_t kApiHost[] = L"api.github.com";
constexpr wchar_t kApiPath[] = L"/repos/bleze/AlwaysOnTop/releases/latest";
constexpr wchar_t kAssetHost[] = L"github.com";
constexpr wchar_t kRepoPath[] = L"bleze/AlwaysOnTop";

struct HttpResponse
{
    bool ok = false;
    std::string body;
};

[[nodiscard]] HINTERNET OpenSession()
{
    return WinHttpOpen(
        kUserAgent,
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
}

[[nodiscard]] HINTERNET OpenGetRequest(HINTERNET connection, const std::wstring& path)
{
    if (connection == nullptr) {
        return nullptr;
    }

    return WinHttpOpenRequest(
        connection,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
}

[[nodiscard]] bool SendAndReceive(HINTERNET request)
{
    if (request == nullptr) {
        return false;
    }

    if (!WinHttpSendRequest(
            request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        return false;
    }

    if (!WinHttpReceiveResponse(request, nullptr)) {
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX);

    return statusCode == 200;
}

[[nodiscard]] HttpResponse HttpGet(const std::wstring& host, const std::wstring& path)
{
    HttpResponse response;

    HINTERNET session = OpenSession();
    HINTERNET connection = session != nullptr
        ? WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)
        : nullptr;
    HINTERNET request = OpenGetRequest(connection, path);

    if (SendAndReceive(request)) {
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
            std::vector<char> buffer(available);
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer.data(), available, &read)) {
                break;
            }
            response.body.append(buffer.data(), read);
        }
        response.ok = true;
    }

    if (request != nullptr) {
        WinHttpCloseHandle(request);
    }
    if (connection != nullptr) {
        WinHttpCloseHandle(connection);
    }
    if (session != nullptr) {
        WinHttpCloseHandle(session);
    }

    return response;
}

[[nodiscard]] bool DownloadToFile(
    const std::wstring& host,
    const std::wstring& path,
    const std::wstring& destPath)
{
    HINTERNET session = OpenSession();
    HINTERNET connection = session != nullptr
        ? WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)
        : nullptr;
    HINTERNET request = OpenGetRequest(connection, path);

    bool ok = false;
    if (SendAndReceive(request)) {
        HANDLE file = CreateFileW(
            destPath.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            bool writeFailed = false;
            DWORD available = 0;
            while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
                std::vector<char> buffer(available);
                DWORD read = 0;
                if (!WinHttpReadData(request, buffer.data(), available, &read)) {
                    writeFailed = true;
                    break;
                }
                DWORD written = 0;
                if (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read) {
                    writeFailed = true;
                    break;
                }
            }
            CloseHandle(file);
            ok = !writeFailed;
        }
    }

    if (request != nullptr) {
        WinHttpCloseHandle(request);
    }
    if (connection != nullptr) {
        WinHttpCloseHandle(connection);
    }
    if (session != nullptr) {
        WinHttpCloseHandle(session);
    }

    if (!ok) {
        DeleteFileW(destPath.c_str());
    }

    return ok;
}

[[nodiscard]] std::wstring ExtractJsonString(const std::string& json, const char* key)
{
    const std::string needle = std::string("\"") + key + "\":\"";
    const auto start = json.find(needle);
    if (start == std::string::npos) {
        return {};
    }

    const auto valueStart = start + needle.size();
    const auto valueEnd = json.find('"', valueStart);
    if (valueEnd == std::string::npos) {
        return {};
    }

    const std::string value = json.substr(valueStart, valueEnd - valueStart);
    return std::wstring(value.begin(), value.end());
}

struct ParsedVersion
{
    int major = 0;
    int minor = 0;
    int patch = 0;
};

[[nodiscard]] bool ParseVersion(std::wstring text, ParsedVersion& out)
{
    if (!text.empty() && (text[0] == L'v' || text[0] == L'V')) {
        text.erase(0, 1);
    }
    if (text.empty()) {
        return false;
    }

    int values[3] = {0, 0, 0};
    size_t start = 0;
    for (int index = 0; index < 3; ++index) {
        const auto dot = text.find(L'.', start);
        const bool isLast = dot == std::wstring::npos;
        const std::wstring part = text.substr(start, isLast ? std::wstring::npos : dot - start);
        if (part.empty()) {
            return false;
        }
        for (wchar_t c : part) {
            if (std::iswdigit(c) == 0) {
                return false;
            }
        }

        values[index] = std::stoi(part);
        if (isLast) {
            break;
        }
        start = dot + 1;
    }

    out.major = values[0];
    out.minor = values[1];
    out.patch = values[2];
    return true;
}

[[nodiscard]] bool IsNewerVersion(const ParsedVersion& latest, const ParsedVersion& current)
{
    if (latest.major != current.major) {
        return latest.major > current.major;
    }
    if (latest.minor != current.minor) {
        return latest.minor > current.minor;
    }
    return latest.patch > current.patch;
}

[[nodiscard]] bool LooksLikeExecutable(const std::wstring& path)
{
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    char header[2] = {};
    DWORD read = 0;
    const bool ok = ReadFile(file, header, sizeof(header), &read, nullptr) &&
        read == sizeof(header) && header[0] == 'M' && header[1] == 'Z';
    CloseHandle(file);
    return ok;
}

[[nodiscard]] std::wstring StripLeadingV(std::wstring tag)
{
    if (!tag.empty() && (tag[0] == L'v' || tag[0] == L'V')) {
        tag.erase(0, 1);
    }
    return tag;
}

struct UpdateThreadArgs
{
    HWND notifyWindow = nullptr;
    bool manual = false;
};

void PostResult(HWND window, std::unique_ptr<UpdateResult> result)
{
    PostMessageW(window, kUpdateResultMessage, 0, reinterpret_cast<LPARAM>(result.release()));
}

DWORD WINAPI UpdateThreadProc(LPVOID param)
{
    const std::unique_ptr<UpdateThreadArgs> args(static_cast<UpdateThreadArgs*>(param));
    auto result = std::make_unique<UpdateResult>();
    result->manual = args->manual;

    const HttpResponse response = HttpGet(kApiHost, kApiPath);
    if (!response.ok) {
        result->hasError = true;
        result->message = L"Couldn't reach GitHub to check for updates.";
        PostResult(args->notifyWindow, std::move(result));
        return 0;
    }

    const std::wstring tag = ExtractJsonString(response.body, "tag_name");

    ParsedVersion latest;
    ParsedVersion current;
    if (tag.empty() || !ParseVersion(tag, latest) || !ParseVersion(AOT_VERSION_STRING_W, current)) {
        result->hasError = true;
        result->message = L"Couldn't parse the latest release information.";
        PostResult(args->notifyWindow, std::move(result));
        return 0;
    }

    if (!IsNewerVersion(latest, current)) {
        result->upToDate = true;
        result->version = AOT_VERSION_STRING_W;
        PostResult(args->notifyWindow, std::move(result));
        return 0;
    }

    const std::wstring versionNumber = StripLeadingV(tag);
    const std::wstring assetName = L"AlwaysOnTop-Setup-" + versionNumber + L".exe";
    const std::wstring downloadPath =
        L"/" + std::wstring(kRepoPath) + L"/releases/download/" + tag + L"/" + assetName;

    wchar_t tempDir[MAX_PATH] = {};
    GetTempPathW(static_cast<DWORD>(std::size(tempDir)), tempDir);
    const std::wstring installerPath = std::wstring(tempDir) + assetName;

    if (!DownloadToFile(kAssetHost, downloadPath, installerPath) ||
        !LooksLikeExecutable(installerPath)) {
        DeleteFileW(installerPath.c_str());
        result->hasError = true;
        result->message = L"Failed to download the update.";
        PostResult(args->notifyWindow, std::move(result));
        return 0;
    }

    std::wstring commandLine = L"\"" + installerPath + L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART";

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    const bool launched = CreateProcessW(
        nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    if (!launched) {
        result->hasError = true;
        result->message = L"Failed to launch the update installer.";
        PostResult(args->notifyWindow, std::move(result));
        return 0;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    result->version = tag;
    PostResult(args->notifyWindow, std::move(result));
    return 0;
}

} // namespace

void CheckForUpdatesAsync(HWND notifyWindow, bool manual)
{
    auto args = std::make_unique<UpdateThreadArgs>();
    args->notifyWindow = notifyWindow;
    args->manual = manual;

    HANDLE thread = CreateThread(nullptr, 0, UpdateThreadProc, args.get(), 0, nullptr);
    if (thread != nullptr) {
        args.release();
        CloseHandle(thread);
    }
}
