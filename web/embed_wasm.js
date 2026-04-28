const fs = require('fs');

const [templatePath, wasmPath, outputPath] = process.argv.slice(2);

if (!templatePath || !wasmPath || !outputPath) {
  console.error('usage: node web/embed_wasm.js TEMPLATE WASM OUTPUT');
  process.exit(2);
}

const marker = '"__TCC_BROWSER_BARE_WASM_BASE64__"';
const template = fs.readFileSync(templatePath, 'utf8');
const wasmBase64 = fs.readFileSync(wasmPath).toString('base64');

if (!template.includes(marker)) {
  console.error(`${templatePath}: missing ${marker}`);
  process.exit(1);
}

fs.writeFileSync(outputPath, template.replace(marker, JSON.stringify(wasmBase64)));
