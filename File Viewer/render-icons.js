const fs = require("fs");
const path = require("path");
const { chromium } = require("playwright");

const iconDir = process.argv[2] || "C:\\Catppuccin-Icons\\Theme\\CatppuccinIcon";
const outDir = process.argv[3] || path.join(__dirname, "icon-cache", "96");
const size = Number(process.argv[4] || 96);
const edgePath = "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";
const iconBackground = "#303446";
const rendererVersion = 2;

fs.mkdirSync(outDir, { recursive: true });

function cleanSvg(svg) {
  return svg
    .replace(/<\?xml[\s\S]*?\?>/g, "")
    .replace(/<!DOCTYPE[\s\S]*?>/g, "");
}

(async () => {
  const launchOptions = {
    headless: true,
    args: [
      "--disable-gpu",
      "--disable-gpu-compositing",
      "--disable-dev-shm-usage",
      "--disable-software-rasterizer=false",
      "--single-process",
      "--no-sandbox",
      "--no-first-run",
      "--no-default-browser-check",
    ],
  };
  if (fs.existsSync(edgePath)) launchOptions.executablePath = edgePath;
  const browser = await chromium.launch(launchOptions);
  const page = await browser.newPage({
    viewport: { width: size, height: size },
    deviceScaleFactor: 1,
  });

  const files = fs
    .readdirSync(iconDir)
    .filter((name) => name.toLowerCase().endsWith(".svg"))
    .sort((a, b) => a.localeCompare(b));

  for (const file of files) {
    const svgPath = path.join(iconDir, file);
    const pngPath = path.join(outDir, file.replace(/\.svg$/i, ".png"));
    const sourceMtime = Math.max(fs.statSync(svgPath).mtimeMs, fs.statSync(__filename).mtimeMs);
    if (fs.existsSync(pngPath) && fs.statSync(pngPath).mtimeMs >= sourceMtime + rendererVersion) continue;

    const svg = cleanSvg(fs.readFileSync(svgPath, "utf8"));
    await page.setContent(
      `<!doctype html>
       <meta charset="utf-8">
       <style>
         html, body {
           margin: 0;
           width: ${size}px;
           height: ${size}px;
           overflow: hidden;
           background: ${iconBackground};
         }
         body {
           display: grid;
           place-items: center;
         }
         svg {
           width: ${Math.round(size * 0.82)}px;
           height: ${Math.round(size * 0.82)}px;
           display: block;
           overflow: visible;
           shape-rendering: geometricPrecision;
         }
       </style>
       ${svg}`,
      { waitUntil: "load" }
    );
    await page.screenshot({
      path: pngPath,
      omitBackground: false,
      clip: { x: 0, y: 0, width: size, height: size },
    });
  }

  await browser.close();
  console.log(`Rendered ${files.length} Catppuccin icons to ${outDir}`);
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
