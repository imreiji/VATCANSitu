#pragma once

// The plugin's HTTP client, over WinHTTP.
//
// This replaces libcurl, which was linked as a 7.4MB static library committed to the
// repository with no build provenance, five years out of date, and available only as a
// debug build - which is why the Release configuration had to define _DEBUG and link the
// debug CRT to get it to link at all.
//
// Nothing is lost by the swap. The vendored curl was built with USE_SCHANNEL, so its TLS
// was already Windows' own; WinHTTP uses Schannel too. The certificate store, the cipher
// suites and the security updates behind them are identical. What changes is that there
// is no longer a copy of an HTTP implementation in the tree to keep patched.
//
// The plugin used six libcurl functions and five options - a URL, a write callback, a
// write destination, a timeout, and a POST body. That is the whole surface reproduced
// here.
//
// Every call blocks until it finishes or the timeout expires, exactly as
// curl_easy_perform did. Callers on worker threads stay on worker threads; this does not
// touch the EuroScope SDK or any shared state.

#include <string>

namespace SituHttp
{
    struct Response
    {
        // True only when the request completed AND the server answered 2xx. A 404 or a
        // 500 is a failed Response with a status, not a successful one with an error
        // page in the body - curl_easy_perform returned CURLE_OK for those and every
        // caller then parsed the error page as if it were data.
        bool ok = false;

        // 0 when the request never got far enough to receive a status line.
        unsigned int status = 0;

        // The response body, exactly as received. May contain NUL bytes: the radar tile
        // is a PNG.
        std::string body;

        // Empty when ok. Otherwise a short description suitable for a user message.
        std::string error;
    };

    // timeoutMs applies to each stage of the request - resolve, connect, send, receive -
    // rather than to the operation as a whole, which is the closest WinHTTP offers to
    // CURLOPT_TIMEOUT_MS. A request that stalls in several stages can therefore take
    // longer than curl would have allowed. None of these calls hold a lock or block a
    // draw, so the difference costs a worker thread some patience and nothing else.
    Response Get(const std::string& url, int timeoutMs);

    // Sends body with Content-Type: application/x-www-form-urlencoded, which is what
    // libcurl sent for CURLOPT_POSTFIELDS and what the Hoppie gateway expects. Encode
    // the values with SituUrl::Encode before assembling them.
    Response Post(const std::string& url, const std::string& body, int timeoutMs);
}
