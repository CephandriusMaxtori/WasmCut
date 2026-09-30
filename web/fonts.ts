type CwrapFunction = (...args: unknown[]) => unknown;

type FontHost = {
  cwrap: (name: string, returnType: string | null, argumentTypes: string[]) => CwrapFunction;
  FS?: {
    mkdir: (path: string) => void;
    writeFile: (path: string, data: Uint8Array) => void;
  };
};

const family = "Space Grotesk";
const fontDirectory = "/fonts";
const faces: { file: string; weight: string; guest: string }[] = [
  { file: "fonts/SpaceGrotesk-Regular.ttf", weight: "400", guest: `${fontDirectory}/SpaceGrotesk-Regular.ttf` },
  { file: "fonts/SpaceGrotesk-Medium.ttf", weight: "500", guest: `${fontDirectory}/SpaceGrotesk-Medium.ttf` },
  { file: "fonts/SpaceGrotesk-Bold.ttf", weight: "700", guest: `${fontDirectory}/SpaceGrotesk-Bold.ttf` }
];

function assetUrl(path: string) {
  return new URL(path, new URL(import.meta.env.BASE_URL, window.location.href)).href;
}

async function fetchFace(file: string) {
  const response = await fetch(assetUrl(file));
  if (!response.ok) {
    throw new Error(`${file} responded with ${response.status}`);
  }
  return new Uint8Array(await response.arrayBuffer());
}

// Registers the faces for the DOM chrome (status bar, overlays). The URL has to
// come from assetUrl so the app keeps working from a GitHub Pages sub-path.
async function registerCssFaces() {
  const registered = await Promise.all(
    faces.map(async (face) => {
      const fontFace = new FontFace(family, await fetchFace(face.file), { weight: face.weight, display: "swap" });
      await fontFace.load();
      document.fonts.add(fontFace);
      return true;
    })
  );
  return registered.length === faces.length;
}

// Publishes the same faces into the WASM filesystem so Dear ImGui can rasterise
// them. C++ also polls for these files on its own, so a failure here only means
// the editor keeps the built-in typeface.
async function publishToWasm(host: FontHost) {
  if (host.FS === undefined) {
    return false;
  }
  host.FS.mkdir(fontDirectory);
  await Promise.all(
    faces.map(async (face) => {
      host.FS?.writeFile(face.guest, await fetchFace(face.file));
    })
  );
  const apply = host.cwrap("wasmcut_load_fonts", "number", ["string", "string", "string"]);
  return apply(faces[0].guest, faces[1].guest, faces[2].guest) === 1;
}

export async function installFonts(host: FontHost): Promise<boolean> {
  try {
    await registerCssFaces();
  } catch (error: unknown) {
    console.warn("Space Grotesk could not be registered for the page chrome.", error);
  }
  try {
    return await publishToWasm(host);
  } catch (error: unknown) {
    console.warn("Space Grotesk could not be loaded into the editor; using the built-in font.", error);
    return false;
  }
}
