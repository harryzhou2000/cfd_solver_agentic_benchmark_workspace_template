#!/usr/bin/env bun
/**
 * Manually refresh the native Codex catalog (~/.codex/opencodex-catalog.json)
 * and models cache using the vendored opencodex source.
 *
 * Why not plain 'ocx sync': the installed build skips catalog refresh when
 * config.toml selects an external model_provider (e.g. openai_vanilla) or the
 * Codex integration toggle is OFF. The low-level catalog sync has neither
 * gate, so calling it directly refreshes the catalog for side profiles too.
 * Once the catalog-only sync PR lands, plain 'ocx sync' handles this natively.
 *
 * Never restarts the ocx service or Codex app-servers.
 *
 * Usage:
 *   bun scripts/sync-codex-catalog.ts [--vendor]
 *
 * --vendor also copies the fresh catalog to
 * docker/configs/codex/opencodex-catalog.json so the harness stack records
 * the same model list.
 */
import { existsSync, statSync, readFileSync, copyFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { loadConfig } from "../opencodex/src/config";
import { refreshCodexModelCatalog } from "../opencodex/src/codex/refresh";
import { admitCodexWrite } from "../opencodex/src/codex/admission";
import { getCodexHome } from "../opencodex/src/codex/paths";
import { readCodexCatalogPath } from "../opencodex/src/codex/catalog";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const VENDOR_FLAG = process.argv.includes("--vendor");

function modelCount(path: string): number | null {
  try {
    const parsed = JSON.parse(readFileSync(path, "utf8")) as { models?: unknown[] };
    return Array.isArray(parsed.models) ? parsed.models.length : null;
  } catch {
    return null;
  }
}

const admission = admitCodexWrite();
if (admission.kind === "refused" && admission.authority === "service-home") {
  console.error("refused: " + admission.message);
  process.exit(1);
}

const config = loadConfig();
const codexHome = getCodexHome();
const catalogPath = readCodexCatalogPath();
const beforeCount = modelCount(catalogPath);
const beforeMtime = existsSync(catalogPath) ? statSync(catalogPath).mtimeMs : 0;

console.log("refreshing catalog for CODEX_HOME=" + codexHome);
const result = await refreshCodexModelCatalog(config);

const afterMtime = existsSync(result.path) ? statSync(result.path).mtimeMs : 0;
const afterCount = modelCount(result.path);
console.log("catalog: " + result.path);
console.log("models: " + (beforeCount ?? "?") + " -> " + (afterCount ?? "?") + " (+" + result.added + " gathered)");
console.log("catalogWritten: " + result.catalogWritten + " | cacheSynced: " + result.cacheSynced + " | changed: " + (afterMtime > beforeMtime));
if ((result.comboOmissions ?? []).length > 0) {
  console.log("combo omissions: " + result.comboOmissions!.length);
}

if (VENDOR_FLAG) {
  const vendorPath = join(ROOT, "docker/configs/codex/opencodex-catalog.json");
  copyFileSync(result.path, vendorPath);
  console.log("vendored: " + vendorPath);
}

if (!result.catalogExists) {
  console.error("no catalog was produced; check provider discovery");
  process.exit(1);
}
