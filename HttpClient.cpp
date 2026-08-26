#include "pch.h"
#include "HttpClient.h"

#include <windows.h>
#include <winhttp.h>

#include <string>
#include <vector>

namespace
{
    // Every WinHTTP handle in a request has to be closed on every path out, including
    // the failure paths, and there are five of them. This is the same discipline the
    // GDI code follows with SaveDC/RestoreDC, for the same reason: the previous curl
    // code leaked handles on error paths until those were fixed one at a time.
    class WinHttpHandle
    {
    public:
        WinHttpHandle() = default;
        explicit WinHttpHandle(HINTERNET handle) : m_handle(handle) {}
        ~WinHttpHandle() { if (m_handle != NULL) { WinHttpCloseHandle(m_handle); } }

        WinHttpHandle(const WinHttpHandle&) = delete;
        WinHttpHandle& operator=(const WinHttpHandle&) = delete;

        WinHttpHandle& operator=(HINTERNET handle)
        {
            if (m_handle != NULL) { WinHttpCloseHandle(m_handle); }
            m_handle = handle;
            return *this;
        }

        operator HINTERNET() const { return m_handle; }
        bool Valid() const { return m_handle != NULL; }

    private:
        HINTERNET m_handle = NULL;
    };

    std::wstring Widen(const std::string& narrow)
    {
        if (narrow.empty()) { return std::wstring(); }

        const int needed = MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(),
                                               static_cast<int>(narrow.size()), NULL, 0);
        if (needed <= 0) { return std::wstring(); }

        std::wstring wide(static_cast<size_t>(needed), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), static_cast<int>(narrow.size()),
                            &wide[0], needed);
        return wide;
    }

    std::string DescribeLastError(const char* stage)
    {
        return std::string(stage) + " failed (" + std::to_string(GetLastError()) + ")";
    }

    SituHttp::Response Fail(const std::string& error)
    {
        SituHttp::Response response;
        response.ok = false;
        response.error = error;
        return response;
    }

    SituHttp::Response Perform(const std::string& url,
                               const std::string* postBody,
                               int timeoutMs)
    {
        const std::wstring wideUrl = Widen(url);
        if (wideUrl.empty()) { return Fail("empty or unconvertible URL"); }

        // Split the URL. WinHttpCrackUrl writes pointers into wideUrl rather than
        // copying, so the components are only valid while it is alive.
        URL_COMPONENTS parts = {};
        parts.dwStructSize = sizeof(parts);
        parts.dwSchemeLength = static_cast<DWORD>(-1);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);

        if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &parts))
        {
            return Fail(DescribeLastError("URL parse"));
        }

        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring target(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength > 0)
        {
            target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        }
        if (target.empty()) { target = L"/"; }

        const bool secure = (parts.nScheme == INTERNET_SCHEME_HTTPS);

        WinHttpHandle session(WinHttpOpen(L"VATCANSitu",
                                          WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                          WINHTTP_NO_PROXY_NAME,
                                          WINHTTP_NO_PROXY_BYPASS,
                                          0));
        if (!session.Valid()) { return Fail(DescribeLastError("WinHttpOpen")); }

        // WinHTTP has no single total-operation timeout, so the one value is applied to
        // each stage. See the note on SituHttp::Get.
        WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

        WinHttpHandle connection(WinHttpConnect(session, host.c_str(), parts.nPort, 0));
        if (!connection.Valid()) { return Fail(DescribeLastError("connect")); }

        WinHttpHandle request(WinHttpOpenRequest(connection,
                                                 postBody != nullptr ? L"POST" : L"GET",
                                                 target.c_str(),
                                                 NULL,
                                                 WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                 secure ? WINHTTP_FLAG_SECURE : 0));
        if (!request.Valid()) { return Fail(DescribeLastError("open request")); }

        LPCWSTR headers = WINHTTP_NO_ADDITIONAL_HEADERS;
        DWORD headersLength = 0;
        if (postBody != nullptr)
        {
            // What libcurl sent for CURLOPT_POSTFIELDS, and what the Hoppie gateway
            // expects. Left off, WinHTTP sends no Content-Type at all.
            headers = L"Content-Type: application/x-www-form-urlencoded";
            headersLength = static_cast<DWORD>(-1);
        }

        const DWORD bodyLength = (postBody != nullptr)
            ? static_cast<DWORD>(postBody->size())
            : 0;
        LPVOID bodyData = WINHTTP_NO_REQUEST_DATA;
        if (postBody != nullptr && !postBody->empty())
        {
            bodyData = const_cast<char*>(postBody->data());
        }

        if (!WinHttpSendRequest(request, headers, headersLength,
                                bodyData, bodyLength, bodyLength, 0))
        {
            return Fail(DescribeLastError("send"));
        }

        if (!WinHttpReceiveResponse(request, NULL))
        {
            return Fail(DescribeLastError("receive"));
        }

        SituHttp::Response response;

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (WinHttpQueryHeaders(request,
                                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX,
                                &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX))
        {
            response.status = statusCode;
        }

        // Read to the end whatever the status, so an error body is available to whoever
        // wants to log it, then decide on ok below.
        for (;;)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available))
            {
                return Fail(DescribeLastError("read"));
            }
            if (available == 0) { break; }

            const size_t offset = response.body.size();
            response.body.resize(offset + available);

            DWORD read = 0;
            if (!WinHttpReadData(request, &response.body[offset], available, &read))
            {
                return Fail(DescribeLastError("read"));
            }

            // WinHttpQueryDataAvailable reports what is buffered, and a read can return
            // less. Shrink to what actually arrived rather than leaving uninitialised
            // bytes in the middle of a PNG.
            response.body.resize(offset + read);

            if (read == 0) { break; }
        }

        response.ok = (response.status >= 200 && response.status < 300);
        if (!response.ok)
        {
            response.error = "HTTP " + std::to_string(response.status);
        }
        return response;
    }
}

namespace SituHttp
{
    Response Get(const std::string& url, int timeoutMs)
    {
        return Perform(url, nullptr, timeoutMs);
    }

    Response Post(const std::string& url, const std::string& body, int timeoutMs)
    {
        return Perform(url, &body, timeoutMs);
    }
}
