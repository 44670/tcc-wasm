const fs = require('fs');

const [templatePath, wasmPath, outputPath, ...extraEmbeds] = process.argv.slice(2);

if (!templatePath || !wasmPath || !outputPath) {
  console.error('usage: node web/embed_wasm.js TEMPLATE WASM OUTPUT [MARKER=WASM ...]');
  process.exit(2);
}

const defaultMarker = '__TCC_BROWSER_BARE_WASM_BASE64__';
let output = fs.readFileSync(templatePath, 'utf8');

function quotedMarker(marker) {
  if (marker.startsWith('"') && marker.endsWith('"'))
    return marker;
  return JSON.stringify(marker);
}

function embed(marker, path) {
  const quoted = quotedMarker(marker);
  const wasmBase64 = fs.readFileSync(path).toString('base64');

  if (!output.includes(quoted)) {
    console.error(`${templatePath}: missing ${quoted}`);
    process.exit(1);
  }
  output = output.replace(quoted, JSON.stringify(wasmBase64));
}

embed(defaultMarker, wasmPath);
for (const spec of extraEmbeds) {
  const eq = spec.indexOf('=');
  if (eq <= 0 || eq === spec.length - 1) {
    console.error(`bad embed spec ${spec}; expected MARKER=WASM`);
    process.exit(2);
  }
  embed(spec.slice(0, eq), spec.slice(eq + 1));
}

fs.writeFileSync(outputPath, output);
