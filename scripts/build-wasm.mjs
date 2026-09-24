import { existsSync } from "node:fs";
import { homedir } from "node:os";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

const projectRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const buildDirectory = resolve(projectRoot, "build");

function fromPath(command) {
  const finder = process.platform === "win32" ? "where.exe" : "which";
  const result = spawnSync(finder, [command], { encoding: "utf8" });
  if (result.status !== 0) {
    return null;
  }
  return result.stdout.split(/\r?\n/).find((value) => value.length > 0)?.trim() ?? null;
}

function findEmscripten() {
  const emsdk = process.env.EMSDK ?? resolve(homedir(), "emsdk");
  const candidates = [
    process.env.EMCMAKE,
    process.env.EMSCRIPTEN_ROOT ? resolve(process.env.EMSCRIPTEN_ROOT, "emcmake.exe") : null,
    resolve(emsdk, "upstream", "emscripten", "emcmake.exe"),
    resolve(emsdk, "upstream", "emscripten", "emcmake")
  ].filter((value) => value !== null && value !== undefined);
  return candidates.find((value) => existsSync(value)) ?? fromPath("emcmake");
}

function findCMake() {
  return process.env.CMAKE ?? fromPath("cmake") ?? fromPath("cmake.exe");
}

const emcmake = findEmscripten();
const cmake = findCMake();

if (emcmake === null || cmake === null) {
  console.error("Unable to find Emscripten or CMake. Set EMSDK or CMAKE and retry.");
  process.exit(1);
}

const configure = spawnSync(
  emcmake,
  [cmake, "-S", projectRoot, "-B", buildDirectory, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"],
  { stdio: "inherit" }
);

if (configure.status !== 0) {
  process.exit(configure.status ?? 1);
}

const build = spawnSync(cmake, ["--build", buildDirectory, "--parallel", "2"], { stdio: "inherit" });
process.exit(build.status ?? 1);
