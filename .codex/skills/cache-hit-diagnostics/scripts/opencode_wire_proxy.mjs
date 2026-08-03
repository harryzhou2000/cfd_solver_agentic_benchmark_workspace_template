#!/usr/bin/env node
/**
 * Logging reverse proxy for opencode wire capture.
 *
 * Points an opencode provider at a local HTTP port and logs every request
 * body and response body to files, so consecutive /chat/completions requests
 * can be byte-diffed to find exactly what breaks the provider's prefix cache
 * (system-prompt mutations, tool-definition drift, per-request metadata, ...).
 *
 * Usage:
 *   node scripts/opencode_wire_proxy.mjs <port> <logdir> [upstream-host] [upstream-prefix]
 *
 * Example (capture api.deepseek.com traffic):
 *   node scripts/opencode_wire_proxy.mjs 8791 /tmp/oc-wire api.deepseek.com /v1
 *
 * Then run opencode with a minimal config that routes the provider through it:
 *
 *   {
 *     "provider": {
 *       "deepseek": {
 *         "npm": "@ai-sdk/openai-compatible",
 *         "options": { "baseURL": "http://127.0.0.1:8791" },   // NOTE: no /v1 here
 *         "models": { "deepseek-v4-pro": { "limit": { "context": 560000, "output": 384000 } } }
 *       }
 *     }
 *   }
 *
 *   env -u HTTP_PROXY -u HTTPS_PROXY -u http_proxy -u https_proxy -u ALL_PROXY \
 *     XDG_CONFIG_HOME=/tmp/oc-wire/cfg \
 *     opencode run -m deepseek/deepseek-v4-pro --agent build "hi"
 *
 * Unset the environment proxies first: a LAN HTTP proxy intercepts even
 * 127.0.0.1 and silently 503s it (see notes/cache-hit-diagnostics.md).
 *
 * Files written per request: <NNN>_meta.txt (headers), <NNN>_req.txt (body),
 * <NNN>_resp.bin (decompressed upstream response, SSE text).
 */

import http from "node:http";
import https from "node:https";
import zlib from "node:zlib";
import fs from "node:fs";
import path from "node:path";

const PORT = Number(process.argv[2] || 8791);
const LOGDIR = path.resolve(process.argv[3] || "/tmp/oc-wire/logs");
const UPSTREAM_HOST = process.argv[4] || "api.deepseek.com";
const UPSTREAM_PREFIX = process.argv[5] || "/v1";

fs.mkdirSync(LOGDIR, { recursive: true });
let counter = 0;

const server = http.createServer((req, res) => {
  const chunks = [];
  req.on("data", (c) => chunks.push(c));
  req.on("end", () => {
    const body = Buffer.concat(chunks);
    counter++;
    const id = String(counter).padStart(3, "0");
    const url = new URL(req.url, "http://x");
    fs.writeFileSync(path.join(LOGDIR, `${id}_meta.txt`), JSON.stringify(
      { method: req.method, url: req.url, headers: req.headers }, null, 1));
    fs.writeFileSync(path.join(LOGDIR, `${id}_req.txt`), body.toString("utf8"));

    const upstreamReq = https.request(
      {
        host: UPSTREAM_HOST,
        port: 443,
        path: UPSTREAM_PREFIX + url.pathname + url.search,
        method: req.method,
        headers: { ...req.headers, host: UPSTREAM_HOST },
      },
      (upRes) => {
        const respChunks = [];
        upRes.on("data", (c) => respChunks.push(c));
        upRes.on("end", () => {
          let respBody = Buffer.concat(respChunks);
          const enc = upRes.headers["content-encoding"];
          if (enc === "gzip") respBody = zlib.gunzipSync(respBody);
          fs.writeFileSync(path.join(LOGDIR, `${id}_resp.bin`), respBody);
          res.writeHead(upRes.statusCode, { ...upRes.headers, "content-encoding": "identity" });
          res.end(respBody);
        });
      },
    );
    upstreamReq.on("error", (e) => {
      res.writeHead(502, { "content-type": "text/plain" });
      res.end(String(e));
    });
    upstreamReq.end(body);
  });
});

server.listen(PORT, "127.0.0.1", () => {
  console.log(`proxy on 127.0.0.1:${PORT} -> https://${UPSTREAM_HOST}${UPSTREAM_PREFIX}, logging to ${LOGDIR}`);
});
