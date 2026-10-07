// Логотип САФУ для левой ячейки главного экрана -> Core/Src/ui/ui_logo.c
//
// Источник — narfu_logo.svg (символ «logo» из спрайта сайта narfu.ru:
// https://narfu.ru/local/templates/narfu2024/img/sprites/sprite.svg — слово
// «сафу» тёмно-синим #00285E, книга с лучами и дуга — голубым #00AEEF).
// Для 36 px высоты книга упрощена (make_small_svg.py -> narfu_logo_small.svg):
// 4 толстых луча вместо 11 волосяных, дуга утолщена; буквы — как в оригинале.
// SVG растеризуется resvg точно в размер на экране (со сглаживанием), пиксели
// делятся на два слоя по цвету (с долями сглаживания на границах) и пишутся
// картинками LVGL LV_COLOR_FORMAT_A4 (4 бита на пиксель — только прозрачность,
// 16 уровней сглаживания). На экране слои перекрашиваются цветами темы
// (ui_main.c), поэтому логотип читается и на светлом, и на тёмном фоне.
//
// Запуск (нужны Python и Node.js):
//   python tools/ui_sim/logo/make_small_svg.py tools/ui_sim/logo/narfu_logo.svg tools/ui_sim/logo/narfu_logo_small.svg
//   npm i --no-save @resvg/resvg-js
//   node tools/ui_sim/logo/gen_logo.js
'use strict';
const fs = require('fs');
const path = require('path');
const { Resvg } = require('@resvg/resvg-js');

const HEIGHT = 36; // высота логотипа на экране, px: ячейка 46 px, рамка 1 px, поля по 4 px
const BPP = 4;     // LV_COLOR_FORMAT_A4: 16 уровней сглаживания
const DARK = [0x00, 0x28, 0x5e];
const LIGHT = [0x00, 0xae, 0xef];

const here = __dirname;
const svg = fs.readFileSync(path.join(here, 'narfu_logo_small.svg'), 'utf8');
const r = new Resvg(svg, { fitTo: { mode: 'height', value: HEIGHT }, background: 'rgba(0,0,0,0)' });
const img = r.render();
const w = img.width, h = img.height;
const px = img.pixels; // RGBA, цвет домножен на альфу (premultiplied)

// Доля «голубого» по зелёному каналу (у тёмно-синего 0x28, у голубого 0xae)
function lightFrac(g) {
	const t = (g - DARK[1]) / (LIGHT[1] - DARK[1]);
	return Math.min(1, Math.max(0, t));
}

function alphaLayer(pick) {
	const stride = Math.ceil((w * BPP) / 8);
	const maxv = (1 << BPP) - 1;
	const perByte = 8 / BPP;
	const data = Buffer.alloc(stride * h);
	let set = 0;
	for (let y = 0; y < h; y++) {
		for (let x = 0; x < w; x++) {
			const i = (y * w + x) * 4;
			const a = px[i + 3] / 255;
			// resvg отдаёт цвет, домноженный на альфу: вернуть исходный
			const f = a > 0 ? lightFrac(Math.min(255, px[i + 1] / a)) : 0;
			const v = a * (pick === 'light' ? f : 1 - f);
			const q = Math.round(v * maxv);
			if (q) set++;
			// A1/A2/A4: первый пиксель — старшие биты байта
			data[y * stride + Math.floor(x / perByte)] |= q << (8 - BPP - BPP * (x % perByte));
		}
	}
	return { stride, data, set };
}

const dark = alphaLayer('dark');
const light = alphaLayer('light');

function cArray(name, layer) {
	const rows = [];
	for (let y = 0; y < h; y++) {
		const row = [];
		for (let b = 0; b < layer.stride; b++)
			row.push('0x' + layer.data[y * layer.stride + b].toString(16).padStart(2, '0'));
		rows.push('\t' + row.join(', ') + ',');
	}
	return `static const uint8_t ${name}_map[] = {\n${rows.join('\n')}\n};\n\n` +
		`const lv_image_dsc_t ${name} = {\n` +
		`\t.header = {\n` +
		`\t\t.magic = LV_IMAGE_HEADER_MAGIC,\n` +
		`\t\t.cf = LV_COLOR_FORMAT_A${BPP},\n` +
		`\t\t.w = ${w},\n` +
		`\t\t.h = ${h},\n` +
		`\t\t.stride = ${layer.stride},\n` +
		`\t},\n` +
		`\t.data_size = sizeof(${name}_map),\n` +
		`\t.data = ${name}_map,\n` +
		`};\n`;
}

const out = `/*
 * ui_logo.c — логотип САФУ (Северный (Арктический) федеральный университет
 * имени М. В. Ломоносова) для левой ячейки главного экрана, ${w} x ${h} px.
 *
 * Сгенерировано tools/ui_sim/logo/gen_logo.js из narfu_logo.svg — не править
 * вручную. Два слоя LV_COLOR_FORMAT_A${BPP} (только прозрачность): слово «сафу»
 * (в фирменных цветах — тёмно-синее) и книга с лучами и дуга (голубые);
 * цвета задаёт тема (ui_main.c).
 */
#include "ui_internal.h"

${cArray('ui_logo_dark', dark)}
${cArray('ui_logo_light', light)}`;

const dst = path.join(here, '..', '..', '..', 'Core', 'Src', 'ui', 'ui_logo.c');
fs.writeFileSync(dst, out.replace(/\n/g, '\n'));
console.log(`${dst}: ${w}x${h}, слои ${dark.data.length} + ${light.data.length} байт, ` +
	`пикселей ${dark.set} + ${light.set}`);
