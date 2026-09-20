/******************************************************************************
**
** Copyright (C) 2026 Ivan Pinezhaninov <ivan.pinezhaninov@gmail.com>
**
** This file is part of the extora which can be found at
** https://github.com/IvanPinezhaninov/extora/.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
** DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
** OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
** THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**
******************************************************************************/

#include "RootPage.h"

namespace extoraHttpExample {
namespace {

constexpr char rootPage[] = R"html(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="/favicon.ico" type="image/svg+xml">
  <title>Extora HTTP example</title>
  <style>
    :root {
      color-scheme: light dark;
      --background: #f5f7fb;
      --surface: #ffffff;
      --text: #172033;
      --muted: #5c667a;
      --border: #dce2ec;
      --code: #eef2f8;
    }
    @media (prefers-color-scheme: dark) {
      :root {
        --background: #111520;
        --surface: #1a2030;
        --text: #edf1fa;
        --muted: #a9b2c5;
        --border: #343d52;
        --code: #242c3d;
      }
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      background: var(--background);
      color: var(--text);
      font: 16px/1.55 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    }
    main { width: min(1080px, calc(100% - 32px)); margin: 48px auto 72px; }
    h1 { margin: 0 0 8px; font-size: clamp(2rem, 5vw, 3.5rem); line-height: 1.1; }
    h2 { margin-top: 40px; }
    p { color: var(--muted); }
    .table-wrap { overflow-x: auto; border: 1px solid var(--border); border-radius: 12px; }
    table { width: 100%; border-collapse: collapse; background: var(--surface); }
    th, td { padding: 11px 14px; border-bottom: 1px solid var(--border); text-align: left; vertical-align: top; }
    tr:last-child td { border-bottom: 0; }
    th { color: var(--muted); font-size: 0.82rem; text-transform: uppercase; letter-spacing: 0.04em; }
    code, pre { font-family: ui-monospace, "SFMono-Regular", Consolas, monospace; }
    code { padding: 2px 5px; border-radius: 5px; background: var(--code); }
    pre {
      overflow-x: auto;
      padding: 16px;
      border: 1px solid var(--border);
      border-radius: 10px;
      background: var(--code);
      line-height: 1.45;
    }
    pre code { padding: 0; background: transparent; }
    .method { font-weight: 700; white-space: nowrap; }
    footer { margin-top: 44px; padding-top: 20px; border-top: 1px solid var(--border); color: var(--muted); }
  </style>
</head>
<body>
  <main>
    <h1>Extora HTTP example</h1>
    <p>
      A small asynchronous demo server for Extora. It supports streaming object bodies and basic S3-style operations,
      including versioning and multipart uploads. It is meant for local testing and does not implement a complete
      S3 server.
    </p>

    <h2>Endpoints</h2>
    <div class="table-wrap">
      <table>
        <thead><tr><th>Method</th><th>Path</th><th>Operation</th></tr></thead>
        <tbody>
          <tr><td class="method">GET</td><td><code>/</code></td><td>Show this documentation.</td></tr>
          <tr><td class="method">GET</td><td><code>/_extora/health</code></td><td>Check server health.</td></tr>
          <tr><td class="method">GET</td><td><code>/_extora/buckets</code></td><td>List buckets.</td></tr>
          <tr><td class="method">GET</td><td><code>/_extora/buckets/{bucket}/usage</code></td><td>Show logical bucket usage.</td></tr>
          <tr><td class="method">GET</td><td><code>/_extora/reclamation</code></td><td>Show reclamation estimate.</td></tr>
          <tr><td class="method">POST</td><td><code>/_extora/reclamation</code></td><td>Reclaim unused storage.</td></tr>
          <tr><td class="method">POST</td><td><code>/_extora/compaction</code></td><td>Compact object data.</td></tr>
          <tr><td class="method">PUT</td><td><code>/{bucket}</code></td><td>Create a bucket.</td></tr>
          <tr><td class="method">GET</td><td><code>/{bucket}</code></td><td>List objects.</td></tr>
          <tr><td class="method">HEAD</td><td><code>/{bucket}</code></td><td>Read bucket metadata headers.</td></tr>
          <tr><td class="method">DELETE</td><td><code>/{bucket}</code></td><td>Delete an empty bucket.</td></tr>
          <tr><td class="method">PUT</td><td><code>/{bucket}/{key}</code></td><td>Upload an object.</td></tr>
          <tr><td class="method">PUT</td><td><code>/{bucket}/{key}</code></td><td>Copy the object named by <code>X-Extora-Copy-Source</code>.</td></tr>
          <tr><td class="method">GET</td><td><code>/{bucket}/{key}</code></td><td>Download an object.</td></tr>
          <tr><td class="method">HEAD</td><td><code>/{bucket}/{key}</code></td><td>Read object headers.</td></tr>
          <tr><td class="method">DELETE</td><td><code>/{bucket}/{key}</code></td><td>Delete an object.</td></tr>
          <tr><td class="method">GET</td><td><code>/{bucket}?versioning</code></td><td>Read versioning status.</td></tr>
          <tr><td class="method">PUT</td><td><code>/{bucket}?versioning=enabled</code></td><td>Enable versioning.</td></tr>
          <tr><td class="method">PUT</td><td><code>/{bucket}?versioning=suspended</code></td><td>Suspend versioning.</td></tr>
          <tr><td class="method">GET</td><td><code>/{bucket}?versions</code></td><td>List object versions.</td></tr>
          <tr><td class="method">GET</td><td><code>/{bucket}/{key}?version-id={id}</code></td><td>Download a specific object version.</td></tr>
          <tr><td class="method">HEAD</td><td><code>/{bucket}/{key}?version-id={id}</code></td><td>Read headers of a specific version.</td></tr>
          <tr><td class="method">DELETE</td><td><code>/{bucket}/{key}?version-id={id}</code></td><td>Delete a specific version.</td></tr>
          <tr><td class="method">POST</td><td><code>/{bucket}/{key}?uploads</code></td><td>Start a multipart upload.</td></tr>
          <tr><td class="method">GET</td><td><code>/{bucket}?uploads</code></td><td>List multipart uploads.</td></tr>
          <tr><td class="method">PUT</td><td><code>/{bucket}/{key}?part-number={n}&amp;upload-id={id}</code></td><td>Upload a part.</td></tr>
          <tr><td class="method">PUT</td><td><code>/{bucket}/{key}?part-number={n}&amp;upload-id={id}</code></td><td>Copy a part from <code>X-Extora-Copy-Source</code>.</td></tr>
          <tr><td class="method">GET</td><td><code>/{bucket}/{key}?upload-id={id}</code></td><td>List uploaded parts.</td></tr>
          <tr><td class="method">POST</td><td><code>/{bucket}/{key}?upload-id={id}</code></td><td>Complete a multipart upload.</td></tr>
          <tr><td class="method">DELETE</td><td><code>/{bucket}/{key}?upload-id={id}</code></td><td>Abort a multipart upload.</td></tr>
          <tr><td class="method">GET</td><td><code>/{bucket}/{key}?object-parts</code></td><td>List a completed object's parts.</td></tr>
        </tbody>
      </table>
    </div>

    <h2>Quick start</h2>
    <pre><code>curl -X PUT http://127.0.0.1:8080/photos

curl -X PUT \
  -H "Content-Type: image/jpeg" \
  --data-binary @photo.jpg \
  http://127.0.0.1:8080/photos/2026/photo.jpg

curl http://127.0.0.1:8080/photos?prefix=2026%2F

curl -OJ http://127.0.0.1:8080/photos/2026/photo.jpg</code></pre>

    <h2>Multipart objects</h2>
    <p>
      Set the completed object's <code>Content-Type</code> when starting the upload; part and completion request
      headers do not replace that metadata.
    </p>
    <pre><code>curl -X POST \
  -H "Content-Type: image/jpeg" \
  http://127.0.0.1:8080/photos/photo.jpg?uploads
# response: {"upload_id":"{id}",...}

curl -i -X PUT --data-binary @part1 \
  "http://127.0.0.1:8080/photos/photo.jpg?part-number=1&amp;upload-id={id}"
curl -i -X PUT --data-binary @part2 \
  "http://127.0.0.1:8080/photos/photo.jpg?part-number=2&amp;upload-id={id}"
# copy each response ETag into the completion body

curl -X POST -H "Content-Type: application/json" \
  --data '{"parts":[{"part_number":1,"etag":"{etag-1}"},{"part_number":2,"etag":"{etag-2}"}]}' \
  "http://127.0.0.1:8080/photos/photo.jpg?upload-id={id}"

curl -o photo.jpg http://127.0.0.1:8080/photos/photo.jpg
curl http://127.0.0.1:8080/photos/photo.jpg?object-parts</code></pre>
    <p>
      Every non-final part must meet the configured minimum size. The server's optional fourth argument overrides
      the default 5242880-byte minimum for local tests.
    </p>

    <h2>Object metadata</h2>
    <p>
      Upload and multipart-initiation requests preserve <code>Content-Type</code>, <code>Cache-Control</code>,
      <code>Content-Disposition</code>, <code>Content-Encoding</code>, <code>Content-Language</code>,
      <code>Expires</code>, and custom <code>X-Extora-Meta-*</code> headers. Object <code>GET</code> and
      <code>HEAD</code> return them.
    </p>
    <pre><code>curl -X PUT --data-binary @photo.jpg \
  -H "Content-Type: image/jpeg" \
  -H "X-Extora-Meta-Owner: Alice" \
  -H "X-Extora-Meta-Project: Extora" \
  http://127.0.0.1:8080/photos/photo.jpg

curl -I http://127.0.0.1:8080/photos/photo.jpg</code></pre>
    <p>
      The text after <code>X-Extora-Meta-</code> is the metadata name. For multipart uploads, send these headers on
      the initiation request.
    </p>

    <h2>Copy objects</h2>
    <p>
      Send <code>PUT</code> to the target key and identify the source with
      <code>X-Extora-Copy-Source: /{bucket}/{key}</code>. Append <code>?version-id={id}</code> to copy a specific
      version. Add <code>X-Extora-Metadata-Directive: Replace</code> to replace its metadata.
    </p>
    <pre><code>curl -i -X PUT \
  -H "X-Extora-Copy-Source: /photos/photo.jpg?version-id={id}" \
  http://127.0.0.1:8080/photos/photo-copy.jpg</code></pre>
    <p>
      To copy bytes into a multipart part, use the upload-part URL with <code>X-Extora-Copy-Source</code> and an
      optional <code>X-Extora-Copy-Source-Range: bytes=0-5242879</code>. Source conditions use
      <code>X-Extora-Copy-Source-If-*</code>; target conditions use the standard <code>If-*</code> headers.
    </p>

    <h2>Range and conditions</h2>
    <p>
      Object downloads support a single <code>Range</code> header. Object <code>GET</code> and
      <code>HEAD</code> support <code>ETag</code> and modification-time preconditions.
    </p>
    <pre><code>curl -i -H "Range: bytes=0-1023" \
  http://127.0.0.1:8080/photos/photo.jpg

curl -i -H 'If-None-Match: "{etag}"' \
  http://127.0.0.1:8080/photos/photo.jpg</code></pre>

    <h2>Pagination</h2>
    <p>
      Listings use the S3 page-size names <code>max-buckets</code>, <code>max-keys</code>,
      <code>max-uploads</code>, and <code>max-parts</code>. Copy the next token or markers from
      a truncated JSON response into the following request.
    </p>
    <pre><code>curl "http://127.0.0.1:8080/photos?prefix=2026%2F&amp;max-keys=2"
# copy next_continuation_token from the response
curl "http://127.0.0.1:8080/photos?prefix=2026%2F&amp;max-keys=2&amp;continuation-token={token}"</code></pre>

    <h2>Errors</h2>
    <p>
      Errors are returned as JSON with an <code>error.code</code> and <code>error.message</code>.
      Object downloads keep their original content type and body.
    </p>

    <footer>
      Extora keeps object keys as logical identifiers and never interprets them as filesystem paths.
    </footer>
  </main>
</body>
</html>
)html";

constexpr char favicon[] = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
  <rect width="64" height="64" rx="14" fill="#000"/>
  <path d="M17 11h31v9H27v7h18v9H27v8h21v9H17z" fill="#fff"/>
</svg>
)svg";

} // namespace

std::string_view rootPageHtml()
{
  return rootPage;
}

std::string_view faviconSvg()
{
  return favicon;
}

} // namespace extoraHttpExample
