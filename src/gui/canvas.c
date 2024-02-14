#include "canvas.h"

#include <string.h>

#include "display/dispdrv.h"
#include "spectrum.h"
#include "widget/spectrumcolumn.h"

#define SPECTRUM_SIZE   128

typedef union {
	struct {
		uint8_t show[SP_CHAN_END];   // Value to show
		uint8_t prev[SP_CHAN_END];   // Previous value
		uint8_t peak[SP_CHAN_END];   // Peak value
		uint8_t fall[SP_CHAN_END];   // Fall speed
	};
	SpectrumColumn col;
} SpCol;

typedef struct {
	SpCol col[SPECTRUM_SIZE];
} SpDrawData;

typedef struct {
	uint8_t raw[SPECTRUM_SIZE];
} SpData;

static Canvas canvas;
static SpDrawData spDrawData;

static GlcdRect rect;

void canvasInit()
{
	bool rotate = (/*bool)settingsRead(PARAM_DISPLAY_ROTATE,*/ false);
	PalIdx palIdx = (/*PalIdx)settingsRead(PARAM_DISPLAY_PALETTE,*/ PAL_DEFAULT);

	glcdInit(rotate ? GLCD_LANDSCAPE_ROT : GLCD_LANDSCAPE);

	canvas.glcd = glcdGet();

	paletteSetIndex(palIdx);
	canvas.pal = paletteGet();

	rect.x = 0;
	rect.y = 0;
	rect.w = dispdrv.width;
	rect.h = dispdrv.height;
	glcdDrawRect(0, 0, rect.w, rect.h, canvas.pal->bg);

	canvas.glcd->rect = rect;
}

Canvas *canvasGet()
{
	return &canvas;
}

void canvasClear()
{
	GlcdRect rect = canvas.glcd->rect;

	glcdDrawRect(0, 0, rect.w, rect.h, canvas.pal->bg);
	glcdShift(0);

	glcdSetFontColor(canvas.pal->fg);
	glcdSetFontBgColor(canvas.pal->bg);
}

static void fftGet128(FftSample *sp, uint8_t *out, size_t size)
{
	uint8_t db;
	uint8_t *po = out;

	memset(po, 0, size);

	for (int16_t i = 0; i < FFT_SIZE / 2; i++) {
		uint16_t calc = (uint16_t)((sp[i].fr * sp[i].fr + sp[i].fi * sp[i].fi) >> 15);

		db = spGetDb(calc);

		if (*po < db) {
			*po = db;
		}

		if ((i < 48) ||
				((i < 96) && (i & 0x01) == 0x01) ||
				((i < 192) && (i & 0x03) == 0x03) ||
				((i < 384) && (i & 0x07) == 0x07) ||
				((i & 0x0F) == 0x0F)) {
			po++;

			if (--size == 0) {
				break;
			}
		}
	}
}

static void calcGradient(Spectrum *sp, int16_t height, bool mirror, color_t *grad)
{
	color_t colorB = mirror ? canvas.pal->spColG : canvas.pal->spColB;
	color_t colorG = mirror ? canvas.pal->spColB : canvas.pal->spColG;

	color_t rB = (colorB & 0xF800) >> 11;
	color_t gB = (colorB & 0x07E0) >> 5;
	color_t bB = (colorB & 0x001F) >> 0;

	color_t rG = (colorG & 0xF800) >> 11;
	color_t gG = (colorG & 0x07E0) >> 5;
	color_t bG = (colorG & 0x001F) >> 0;

	for (int16_t i = 0; i < height; i++) {
		if (sp->flags & SP_FLAG_GRAD) {
			grad[i] = (color_t)(((rB + (rG - rB) * i / (height - 1)) << 11) |
													 ((gB + (gG - gB) * i / (height - 1)) << 5) |
													 ((bB + (bG - bB) * i / (height - 1)) << 0));
		} else {
			grad[i] = colorB;
		}
	}
}

static void calcSpCol(int16_t chan, int16_t scale, uint8_t col, SpectrumColumn *spCol,
											SpData *spData)
{
	int16_t raw;

	SpCol *spDrawCol = &spDrawData.col[col];

	if (chan == SP_CHAN_BOTH) {
		uint8_t rawL = spData[SP_CHAN_LEFT].raw[col];
		uint8_t rawR = spData[SP_CHAN_RIGHT].raw[col];
		if (rawL > rawR) {
			raw = rawL;
		} else {
			raw = rawR;
		}
		*spCol = spDrawCol->col;
	} else {
		raw = spData[chan].raw[col];
		spCol->showW = spDrawCol->show[chan];
		spCol->prevW = spDrawCol->prev[chan];
		spCol->peakW = spDrawCol->peak[chan];
		spCol->fallW = spDrawCol->fall[chan];
	}

	raw = (scale * raw) >> 8; // / N_DB = 256

	spCol->prevW = spCol->showW;
	if (raw < spCol->showW) {
		if (spCol->showW >= spCol->fallW) {
			spCol->showW -= spCol->fallW;
			spCol->fallW++;
		} else {
			spCol->showW = 0;
		}
	}

	if (raw > spCol->showW) {
		spCol->showW = raw;
		spCol->fallW = 1;
	}

	if (spCol->peakW <= raw) {
		spCol->peakW = raw + 1;
	} else {
		if (spCol->peakW && spCol->peakW > spCol->showW + 1) {
			spCol->peakW--;
		}
	}

	if (chan == SP_CHAN_BOTH) {
		spDrawCol->col = *spCol;
	} else {
		spDrawCol->show[chan] = (uint8_t)spCol->showW;
		spDrawCol->prev[chan] = (uint8_t)spCol->prevW;
		spDrawCol->peak[chan] = (uint8_t)spCol->peakW;
		spDrawCol->fall[chan] = (uint8_t)spCol->fallW;
	}
}

static void drawSpectrum(bool clear, bool mirror, SpChan chan, GlcdRect *rect)
{
	SpData spData[SP_CHAN_END];

	if (clear) {
		memset(&spDrawData, 0, sizeof (SpDrawData));
		memset(spData, 0, sizeof(spData));
	} else {
		if (chan == SP_CHAN_LEFT || chan == SP_CHAN_BOTH) {
			spGetADC(SP_CHAN_LEFT, spData[SP_CHAN_LEFT].raw, SPECTRUM_SIZE, fftGet128);
		}
		if (chan == SP_CHAN_RIGHT || chan == SP_CHAN_BOTH) {
			spGetADC(SP_CHAN_RIGHT, spData[SP_CHAN_RIGHT].raw, SPECTRUM_SIZE, fftGet128);
		}
	}

	const int16_t step = (rect->w  + 1) / SPECTRUM_SIZE + 1;    // Step of columns
	const int16_t colW = step - (step / 2);                     // Column width
	const int16_t num = (rect->w + colW - 1) / step;            // Number of columns

	const int16_t width = (num - 1) * step + colW;              // Width of spectrum
	const int16_t height = rect->h;                             // Height of spectrum

	const int16_t oft = (rect->w - width) / 2;                  // Spectrum offset for symmetry

	const int16_t y = rect->y;

	Spectrum *sp = spGet();

	color_t grad[512];
	calcGradient(sp, height, mirror, grad);

	for (uint8_t col = 0; col < num; col++) {
		int16_t x = oft + col * step;

		SpectrumColumn spCol;
		calcSpCol(chan, height, col, &spCol, spData);
		if (!(sp->flags & SP_FLAG_PEAKS)) {
			spCol.peakW = 0;
		}
		GlcdRect rect = {x, y, colW, height};
		spectrumColumnDraw(clear, &spCol, &rect, mirror, grad);
	}
}

static bool checkSpectrumReady(void)
{
//	if (swTimGet(SW_TIM_SP_CONVERT) == 0) {
//		swTimSet(SW_TIM_SP_CONVERT, 25);
		return true;
//	}

//	return false;
}

static void drawSpectrumMode(bool clear, GlcdRect rect)
{
	if (!checkSpectrumReady()) {
		return;
	}

	Spectrum *sp = spGet();

	switch (sp->mode) {
	case SP_MODE_STEREO:
	case SP_MODE_MIRROR:
	case SP_MODE_INVERTED:
	case SP_MODE_ANTIMIRROR:
		rect.h = rect.h / 2;
		drawSpectrum(clear, sp->mode == SP_MODE_ANTIMIRROR || sp->mode == SP_MODE_INVERTED,
								 SP_CHAN_LEFT, &rect);
		rect.y += rect.h;
		drawSpectrum(clear, sp->mode == SP_MODE_MIRROR || sp->mode == SP_MODE_INVERTED,
								 SP_CHAN_RIGHT, &rect);
		break;
	default:
		drawSpectrum(clear, false, SP_CHAN_BOTH, &rect);
		break;
	}
}

void canvasShowSpectrum(bool clear)
{
	Spectrum *sp = spGet();

	switch (sp->mode) {
	case SP_MODE_STEREO:
	case SP_MODE_MIRROR:
	case SP_MODE_INVERTED:
	case SP_MODE_ANTIMIRROR:
	case SP_MODE_MIXED:
		drawSpectrumMode(clear, glcdGet()->rect);
		break;
	case SP_MODE_WATERFALL:
//		drawWaterfall(clear);
		break;
	default:
		break;
	}
}
