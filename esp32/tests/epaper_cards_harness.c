/*
 * Copyright (c) 2026 limengdu
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "epaper_cards.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static unsigned count_ink(const epaper_cards_canvas_t *c, card_color_t ink) {
    unsigned n = 0;
    for (int y = 0; y < c->height; y++) {
        for (int x = 0; x < c->width; x++) n += epaper_cards_get(c, x, y) == ink;
    }
    return n;
}

static void checks(void) {
    uint8_t guard[800 * 480 / 2 + 32];
    memset(guard, 0xA5, sizeof(guard));
    epaper_cards_canvas_t c;
    assert(!epaper_cards_canvas(&c, guard, 1, 800, 480, 4));
    assert(!epaper_cards_canvas(&c, guard, sizeof(guard), 800, 480, 2));
    for (int bpp = 1; bpp <= 4; bpp += 3) {
        size_t bytes = 800 * 480 * bpp / 8;
        memset(guard, 0xA5, sizeof(guard));
        assert(epaper_cards_canvas(&c, guard + 16, bytes, 800, 480, bpp));
        epaper_cards_clear(&c, CARD_WHITE);
        assert(c.pixels[0] == (bpp == 1 ? 255 : 0x11));
        epaper_cards_pixel(&c, 0, 0, CARD_RED);
        epaper_cards_pixel(&c, 1, 0, CARD_BLUE);
        epaper_cards_pixel(&c, 799, 479, CARD_GREEN);
        assert(c.pixels[0] == (bpp == 1 ? 0x3F : 0x35));
        assert(epaper_cards_get(&c, 799, 479) == (bpp == 1 ? CARD_BLACK : CARD_GREEN));
        epaper_cards_rect(&c, -100, -100, 120, 120, 12, true, CARD_YELLOW);
        assert(epaper_cards_get(&c, 0, 0) == (bpp == 1 ? CARD_BLACK : CARD_YELLOW));
        epaper_cards_rect(&c, 790, 470, 80, 80, 0, false, CARD_BLACK);
        assert(epaper_cards_get(&c, 790, 479) == CARD_BLACK);
        epaper_cards_hline(&c, -4, 479, 20, CARD_BLACK);
        epaper_cards_vline(&c, 799, -4, 20, CARD_BLACK);
        epaper_cards_text(&c, -80, -40, "Clip", 96, CARD_RED);
        epaper_cards_text(&c, 780, 470, "Clip", 96, CARD_RED);
        assert(epaper_cards_icon(&c, -40, -40, "sunny", 112, CARD_BLUE));
        assert(epaper_cards_icon(&c, 790, 470, "sunny", 64, CARD_BLUE));
        uint8_t *comparison=malloc(bytes);assert(comparison);memcpy(comparison,c.pixels,bytes);
        epaper_cards_canvas_t scaled=c;scaled.pixels=comparison;
        assert(epaper_cards_icon(&c,40,130,"home",64,CARD_RED));
        assert(epaper_cards_icon_scaled(&scaled,40,130,"home",64,64,CARD_RED));
        assert(!memcmp(comparison,c.pixels,bytes));free(comparison);
        assert(epaper_cards_icon_scaled(&c,790,470,"sunny",112,40,CARD_GREEN));
        assert(epaper_cards_icon_scaled(&c,INT_MAX,INT_MIN,"home",64,40,CARD_RED));
        assert(!epaper_cards_icon_scaled(&c,0,0,"home",64,0,CARD_BLACK));
        epaper_cards_rect(&c, INT_MIN, INT_MIN, INT_MAX, INT_MAX, 0, true, CARD_RED);
        epaper_cards_hline(&c, INT_MAX, 0, INT_MAX, CARD_RED);
        epaper_cards_vline(&c, 0, INT_MAX, INT_MAX, CARD_RED);
        epaper_cards_invert(&c, INT_MIN, 0, INT_MAX, 480);
        epaper_cards_text(&c, INT_MIN, INT_MIN, "A", 96, CARD_RED);
        for (int i = 0; i < 16; i++) assert(guard[i] == 0xA5 && guard[16 + bytes + i] == 0xA5);
    }
    assert(epaper_cards_canvas(&c, guard + 16, sizeof(guard) - 32, 17, 17, 4));
    assert(c.stride == 9);
    epaper_cards_clear(&c, CARD_WHITE);
    epaper_cards_pixel(&c, 16, 16, CARD_RED);
    assert(epaper_cards_get(&c, 16, 16) == CARD_RED);
    assert(epaper_cards_canvas(&c, guard + 16, sizeof(guard) - 32, 800, 480, 1));
    assert(epaper_cards_measure("", 40) == 0);
    assert(epaper_cards_measure("WW", 40) > epaper_cards_measure("ii", 40));
    assert(epaper_cards_measure("ABC", 40) == epaper_cards_measure("A", 40)
        + epaper_cards_measure("B", 40) + epaper_cards_measure("C", 40));
    assert(epaper_cards_measure("AA\nB", 40) == epaper_cards_measure("AA", 40));
    const int sizes[] = {28, 40, 60, 96};
    const unsigned extra[] = {0xB0, 0x2022, 0x2013, 0x2014, 0x2019, 0x201C, 0x201D, 0x2026};
    for (int i = 0; i < 4; i++) {
        const card_font_t *font = card_font_find(sizes[i]);
        assert(font && font->ascent > 0 && font->line_height > font->ascent);
        for (unsigned k = 32; k < 127; k++) assert(card_font_glyph(font, k)->codepoint == k);
        for (unsigned k = 0; k < sizeof(extra) / sizeof(extra[0]); k++) {
            const card_glyph_t *g = card_font_glyph(font, extra[k]);
            assert(g->codepoint == extra[k] && g->width > 0 && g->height > 0);
        }
    }
    const char *icons[] = {"dentistry", "checkroom", "backpack", "restaurant", "menu_book",
        "pool", "piano", "bed", "medication", "stethoscope", "call", "sunny", "cloud",
        "rainy", "ac_unit", "star", "home", "school", "sports_soccer", "palette", "bathtub",
        "pets", "directions_bus", "shopping_cart", "cleaning_services", "local_laundry_service",
        "music_note", "brush", "egg", "water_drop", "nightlight", "alarm", "favorite",
        "check_circle", "elderly", "child_care", "toys", "book", "edit", "celebration"};
    for (unsigned i = 0; i < sizeof(icons) / sizeof(icons[0]); i++) {
        assert(card_icon_find(icons[i], 64) && card_icon_find(icons[i], 112));
    }
    assert(!card_icon_find("unknown", 64) && !card_icon_find(NULL, 64) && !card_icon_find("home", 1));
    epaper_cards_clear(&c, CARD_WHITE);
    int line_height = card_font_find(28)->line_height;
    int width = epaper_cards_measure("one two", 28);
    epaper_cards_text_result_t r = epaper_cards_text_box(&c, 0, 0, width, 480,
        "one two three", 28, CARD_BLACK, 8);
    assert(r.lines == 2 && !r.truncated);
    r = epaper_cards_text_box(&c, 0, 0, width, 480, "one two three", 28, CARD_BLACK, 1);
    assert(r.lines == 1 && r.truncated);
    uint8_t actual[800 * 480 / 8];
    epaper_cards_clear(&c, CARD_WHITE);
    int ell_width = epaper_cards_measure("A \xE2\x80\xA6", 28);
    r = epaper_cards_text_box(&c, 0, 0, ell_width, 480, "A A A A", 28, CARD_BLACK, 1);
    assert(r.lines == 1 && r.truncated);
    memcpy(actual, c.pixels, sizeof(actual));
    epaper_cards_clear(&c, CARD_WHITE);
    epaper_cards_text(&c, 0, 0, "A\xE2\x80\xA6", 28, CARD_BLACK);
    assert(!memcmp(actual, c.pixels, sizeof(actual)));
    epaper_cards_clear(&c, CARD_WHITE);
    r = epaper_cards_text_box(&c, 0, 0, width, line_height - 1, "one", 28, CARD_BLACK, 4);
    assert(r.lines == 0 && r.truncated && count_ink(&c, CARD_BLACK) == 0);
    r = epaper_cards_text_box(&c, 0, 0, epaper_cards_measure("abc", 28), 480,
        "abcdefghij", 28, CARD_BLACK, 2);
    assert(r.lines == 2 && r.truncated);
    r = epaper_cards_text_box(&c, 0, 0, 800, 480, "a\nb\nc", 28, CARD_BLACK, 2);
    assert(r.lines == 2 && r.truncated);
    epaper_cards_clear(&c, CARD_WHITE);
    epaper_cards_checkbox(&c, 10, 10, 40, false, CARD_BLACK);
    unsigned empty = count_ink(&c, CARD_BLACK);
    epaper_cards_checkbox(&c, 10, 10, 40, true, CARD_BLACK);
    assert(count_ink(&c, CARD_BLACK) > empty);
    epaper_cards_clear(&c, CARD_WHITE);
    epaper_cards_invert(&c, 10, 10, 100, 20);
    assert(count_ink(&c, CARD_BLACK) == 2000);
    epaper_cards_invert(&c, 10, 10, 100, 20);
    assert(count_ink(&c, CARD_BLACK) == 0);
}

static void sample(epaper_cards_canvas_t *c) {
    epaper_cards_clear(c, CARD_WHITE);
    epaper_cards_rect(c, 16, 16, 768, 448, 18, false, CARD_BLACK);
    epaper_cards_icon(c, 40, 30, "home", 64, CARD_BLUE);
    epaper_cards_text(c, 120, 30, "Today at home", 40, CARD_BLACK);
    epaper_cards_hline(c, 40, 98, 720, CARD_BLACK);
    epaper_cards_icon(c, 44, 108, "sunny", 112, CARD_YELLOW);
    epaper_cards_text(c, 180, 105, "24\xC2\xB0", 96, CARD_RED);
    epaper_cards_text(c, 445, 139, "08:30", 60, CARD_BLUE);
    epaper_cards_text_box(c, 48, 238, 700, 80,
        "A bright morning \xE2\x80\x94 time to get ready for school.", 28, CARD_BLACK, 2);
    epaper_cards_checkbox(c, 48, 332, 42, true, CARD_GREEN);
    epaper_cards_text(c, 110, 325, "Backpack ready", 40, CARD_BLACK);
    epaper_cards_icon(c, 588, 312, "backpack", 112, CARD_BLUE);
    epaper_cards_rect(c, 44, 399, 500, 48, 8, true, CARD_BLACK);
    epaper_cards_text(c, 62, 405, "Tap to tell Muse you are ready", 28, CARD_WHITE);
}

static void preview(const char *dir, int bpp) {
    epaper_cards_canvas_t c;
    size_t bytes = 800 * 480 * bpp / 8;
    uint8_t *pixels = malloc(bytes);
    assert(pixels && epaper_cards_canvas(&c, pixels, bytes, 800, 480, bpp));
    sample(&c);
    char path[4096];
    snprintf(path, sizeof(path), "%s/e100%d_sample.%s", dir, bpp == 1 ? 1 : 2, bpp == 1 ? "pgm" : "ppm");
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "%s\n800 480\n255\n", bpp == 1 ? "P5" : "P6");
    static const uint8_t rgb[][3] = {{0,0,0},{255,255,255},{255,255,0},{255,0,0},
                                   {0,0,0},{0,0,255},{0,255,0}};
    for (int y = 0; y < 480; y++) {
        for (int x = 0; x < 800; x++) {
            unsigned color = epaper_cards_get(&c, x, y);
            assert(color < 7 && color != 4);
            if (bpp == 1) fputc(color ? 255 : 0, f);
            else assert(fwrite(rgb[color], 1, 3, f) == 3);
        }
    }
    fclose(f);
    free(pixels);
}

int main(void) {
    checks();
    const char *dir = getenv("CARD_PREVIEW_DIR");
    if (dir) { mkdir(dir, 0755); preview(dir, 1); preview(dir, 4); }
    puts("ok");
    return 0;
}
