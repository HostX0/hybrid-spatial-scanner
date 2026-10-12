// Deterministic product framing around unchanged, actual Release UI captures.
// No generated scan, UI replacement, private capture, or source-image upload.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { createRequire } = require('node:module');
const runtimeModules = process.env.HSS_ARTIFACT_NODE_MODULES;
if (!runtimeModules) throw new Error('Set HSS_ARTIFACT_NODE_MODULES to the existing trusted runtime.');
const runtimeRequire = createRequire(path.join(runtimeModules, 'hss-artifacts.cjs'));
const { createCanvas, loadImage, GlobalFonts } = runtimeRequire('@napi-rs/canvas');
const sharp = runtimeRequire('sharp');
const [captureRoot, outputRoot, receiptPath, family] = process.argv.slice(2);
if (!captureRoot || !outputRoot || !receiptPath || !['iphone', 'ipad'].includes(family)) {
  throw new Error('Usage: generate_store_screenshots.cjs capture-directory public-output private-receipt iphone|ipad');
}
GlobalFonts.registerFromPath('/System/Library/Fonts/SFNS.ttf', 'HSS Sans');
GlobalFonts.registerFromPath('/System/Library/Fonts/SFArabic.ttf', 'HSS Arabic');
const isPad = family === 'ipad';
const width = isPad ? 2064 : 1206;
const height = isPad ? 2752 : 2622;
const manifest = JSON.parse(fs.readFileSync(path.join(captureRoot, 'manifest.json'), 'utf8'));
const copies = {
  en: [
    { id: '01-library', name: '01-your-spaces', title: ['Your spaces.', 'Always yours.'], detail: 'A local library. No account required.' },
    { id: '02-review', name: '02-review-your-plan', title: ['See the plan.', 'Know its limits.'], detail: 'Review boundaries, evidence and what stays unknown.' },
    { id: '03-boundaries', name: '03-boundary-corrections', title: ['Refine.', 'Keep control.'], detail: 'Select boundaries. Make reversible corrections.' },
    { id: '04-privacy', name: '04-on-device-privacy', title: ['Private', 'by design.'], detail: 'Processing and projects stay on your device.' },
    { id: '05-help', name: '05-built-in-help', title: ['Clarity at', 'every step.'], detail: 'Built-in help, in English and Arabic.' },
  ],
  ar: [
    { id: '01-library', name: '01-your-spaces', title: ['مساحاتك.', 'ملكك دائماً.'], detail: 'مكتبة محلية. بدون حساب.' },
    { id: '02-review', name: '02-review-your-plan', title: ['راجع المخطط.', 'واعرف حدوده.'], detail: 'راجع الحدود والدليل وما يبقى غير معروف.' },
    { id: '03-boundaries', name: '03-boundary-corrections', title: ['عدّل الحدود.', 'بكل تحكّم.'], detail: 'اختَر الحدود وعدّلها مع إمكانية التراجع.' },
    { id: '04-privacy', name: '04-on-device-privacy', title: ['خصوصية', 'من الأساس.'], detail: 'المعالجة والمشاريع تبقى على جهازك.' },
    { id: '05-help', name: '05-built-in-help', title: ['وضوح في', 'كل خطوة.'], detail: 'مساعدة مدمجة بالعربية والإنكليزية.' },
  ],
};
const receipt = { family, dimensions: [width, height], captures: [], physicalCaptureClaimed: false };
const digest = buffer => crypto.createHash('sha256').update(buffer).digest('hex');

function rounded(ctx, x, y, w, h, radius, fill, stroke) {
  ctx.beginPath(); ctx.roundRect(x, y, w, h, radius);
  if (fill) { ctx.fillStyle = fill; ctx.fill(); }
  if (stroke) { ctx.strokeStyle = stroke; ctx.lineWidth = 3; ctx.stroke(); }
}

function textLine(ctx, value, x, y, size, weight, color, rtl = false) {
  ctx.font = `${weight} ${size}px "${rtl ? 'HSS Arabic' : 'HSS Sans'}", "HSS Sans", sans-serif`;
  ctx.direction = rtl ? 'rtl' : 'ltr';
  ctx.textAlign = rtl ? 'right' : 'left'; ctx.textBaseline = 'top';
  ctx.fillStyle = color; ctx.fillText(value, x, y);
}

function paragraph(ctx, value, x, y, maxWidth, size, rtl = false) {
  ctx.font = `400 ${size}px "${rtl ? 'HSS Arabic' : 'HSS Sans'}"`;
  const lines = []; let line = '';
  for (const word of value.split(' ')) {
    const next = line ? line + ' ' + word : word;
    if (ctx.measureText(next).width > maxWidth && line) { lines.push(line); line = word; }
    else line = next;
  }
  lines.push(line);
  lines.forEach((part, i) => textLine(ctx, part, x, y + i * size * 1.35, size, 400, '#b1c8c3', rtl));
  if (lines.length > 2) throw new Error('Product subtitle requires a layout review.');
}

function publicPNGGuard(buffer) {
  if (!buffer.subarray(0, 8).equals(Buffer.from([137,80,78,71,13,10,26,10]))) throw new Error('Not PNG');
  let pos = 8;
  while (pos < buffer.length) {
    const len = buffer.readUInt32BE(pos);
    const type = buffer.toString('ascii', pos + 4, pos + 8);
    if (['tEXt','iTXt','zTXt','eXIf'].includes(type)) throw new Error('Public image includes hidden text metadata.');
    pos += len + 12;
  }
}

(async () => {
  for (const language of ['en', 'ar']) {
    const rtl = language === 'ar';
    for (const copy of copies[language]) {
      const expected = `${language}-${copy.id}_`;
      const matches = manifest.flatMap(test => test.attachments || []).filter(a =>
        !a.isAssociatedWithFailure && a.suggestedHumanReadableName.startsWith(expected) && a.exportedFileName.endsWith('.png'));
      if (matches.length !== 1) throw new Error(`Need one successful actual capture: ${expected}`);
      const attachment = matches[0];
      const sourcePath = path.join(captureRoot, attachment.exportedFileName);
      const sourceBytes = fs.readFileSync(sourcePath);
      const metadata = await sharp(sourceBytes).metadata();
      if (metadata.width !== width || metadata.height !== height) throw new Error('Capture dimensions/device family mismatch.');
      const source = await loadImage(sourceBytes);
      const canvas = createCanvas(width, height); const ctx = canvas.getContext('2d');
      ctx.fillStyle = '#081112'; ctx.fillRect(0, 0, width, height);
      const glow = ctx.createRadialGradient(width * 0.90, height * 0.5, 0, width * 0.90, height * 0.5, width);
      glow.addColorStop(0, '#143d35'); glow.addColorStop(1, '#081112');
      ctx.fillStyle = glow; ctx.fillRect(0, 0, width, height);
      // Quiet geometry is decorative only; it is never presented as a scan.
      ctx.strokeStyle = '#6bdfc21a'; ctx.lineWidth = 2;
      for (let i = 0; i < 5; ++i) {
        ctx.beginPath(); ctx.arc(width * 1.07, height * 0.60, width * (0.38 + i * 0.12), 0, 2 * Math.PI); ctx.stroke();
      }
      const margin = isPad ? 132 : 80;
      const textX = rtl ? width - margin : margin;
      textLine(ctx, rtl ? 'ماسح المساحات الهجين' : 'Hybrid Spatial Scanner', textX, 76, isPad ? 48 : 34, 600, '#6bdfc2', rtl);
      const titleSize = isPad ? 144 : (rtl ? 96 : 98);
      copy.title.forEach((line, i) => textLine(ctx, line, textX, 150 + i * titleSize * 1.13, titleSize, 700, '#f1faf7', rtl));
      paragraph(ctx, copy.detail, textX, isPad ? 490 : 390, width - margin * 2, isPad ? 51 : 39, rtl);
      // Uniform fit preserves the complete app viewport. No screen content is
      // retouched, erased, rearranged, or replaced with generated interface art.
      const screenHeight = isPad ? 1880 : 1880;
      const screenWidth = screenHeight * source.width / source.height;
      const screenX = (width - screenWidth) / 2;
      const screenY = isPad ? 660 : 560;
      rounded(ctx, screenX - 18, screenY - 18, screenWidth + 36, screenHeight + 36, 62, '#020708', '#4b7168');
      ctx.save(); ctx.beginPath(); ctx.roundRect(screenX, screenY, screenWidth, screenHeight, 46); ctx.clip();
      ctx.drawImage(source, screenX, screenY, screenWidth, screenHeight); ctx.restore();
      const reference = ['01-library','02-review','03-boundaries'].includes(copy.id);
      const footer = reference
        ? (rtl ? 'مثال مرجعي وليس مسح غرفة حقيقية' : 'Reference example — not a captured room')
        : (rtl ? 'أنت تختار ما تشاركه' : 'THE THIRD ACT · You choose what to share');
      ctx.font = `400 ${isPad ? 38 : 29}px "${rtl ? 'HSS Arabic' : 'HSS Sans'}", "HSS Sans", sans-serif`;
      ctx.direction = rtl ? 'rtl' : 'ltr'; ctx.textAlign = 'center'; ctx.textBaseline = 'top';
      ctx.fillStyle = reference ? '#edbd7f' : '#b1c8c3'; ctx.fillText(footer, width / 2, isPad ? 2630 : 2534);
      const png = await sharp(canvas.toBuffer('image/png')).removeAlpha().toColourspace('srgb').png({compressionLevel: 9}).toBuffer();
      publicPNGGuard(png);
      const finalMetadata = await sharp(png).metadata();
      if (finalMetadata.hasAlpha || finalMetadata.channels !== 3) throw new Error('Apple image must be opaque RGB.');
      const outputDirectory = path.join(outputRoot, family, language);
      fs.mkdirSync(outputDirectory, { recursive: true });
      const outputPath = path.join(outputDirectory, copy.name + '.png');
      fs.writeFileSync(outputPath, png);
      receipt.captures.push({ language, id: copy.id, title: copy.title, detail: copy.detail, footer,
        output: outputPath, sourceSHA256: digest(sourceBytes), outputSHA256: digest(png),
        capturedDevice: attachment.deviceName, referenceExample: reference, completeUIViewportPreserved: true,
        alpha: false, textMetadata: false });
    }
  }
  fs.writeFileSync(receiptPath, JSON.stringify(receipt, null, 2) + '\n');
  console.log(JSON.stringify({ family, images: receipt.captures.length, dimensions: [width, height], alpha: false, textMetadata: false }));
})().catch(error => { console.error(error.message); process.exitCode = 1; });
