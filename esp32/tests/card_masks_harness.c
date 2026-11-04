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

#include "card_fonts.h"
#include "card_icons.h"
#include "card_mask.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *const names[] = {"dentistry", "checkroom", "backpack", "restaurant", "menu_book",
    "pool", "piano", "bed", "medication", "stethoscope", "call", "sunny", "cloud", "rainy",
    "ac_unit", "star", "home", "school", "sports_soccer", "palette", "bathtub", "pets",
    "directions_bus", "shopping_cart", "cleaning_services", "local_laundry_service", "music_note",
    "brush", "egg", "water_drop", "nightlight", "alarm", "favorite", "check_circle", "elderly",
    "child_care", "toys", "book", "edit", "celebration"};

static void vectors(void) {
    const uint8_t raw[] = {0x81,0x42,0x81,0x42,0x00,0x42,0x00,0x42};
    const uint8_t encoded[] = {0x01,0x81,0x42,0x81,0x00,0x81,0x82};
    card_mask_decoder_t d;
    for(int kind=0;kind<2;kind++) {
        assert(card_mask_begin(&d,kind?encoded:raw,kind?sizeof(encoded):CARD_MASK_RAW|sizeof(raw),16,4));
        for(int y=0;y<4;y++){const uint8_t *row=card_mask_next(&d);assert(row&&!memcmp(row,raw+y*2,2));}
        assert(!card_mask_next(&d));
    }
    assert(!card_mask_begin(&d,NULL,1,8,1));assert(!card_mask_next(&d));
    assert(!card_mask_begin(&d,raw,0,8,1));assert(!card_mask_begin(&d,raw,8,129,1));
    assert(!card_mask_begin(&d,raw,CARD_MASK_RAW|7,16,4));
    const uint8_t short_literal[]={1,0x12},too_long[]={0x88},trailing[]={0x80,0x80};
    assert(card_mask_begin(&d,short_literal,sizeof(short_literal),16,1));assert(!card_mask_next(&d)&&!card_mask_next(&d));
    assert(card_mask_begin(&d,too_long,sizeof(too_long),8,1));assert(!card_mask_next(&d));
    assert(card_mask_begin(&d,trailing,sizeof(trailing),8,1));assert(!card_mask_next(&d));
    const uint8_t zero128[]={0xff};
    assert(card_mask_begin(&d,zero128,sizeof(zero128),128,8));
    for(int y=0;y<8;y++){const uint8_t *row=card_mask_next(&d);assert(row);for(int x=0;x<16;x++)assert(!row[x]);}
    assert(!card_mask_next(&d));
}
static void dump(const uint8_t *data,uint16_t count,unsigned width,unsigned height) {
    if(!width&&!height)return;
    card_mask_decoder_t d;assert(card_mask_begin(&d,data,count,width,height));
    for(unsigned y=0;y<height;y++){const uint8_t *row=card_mask_next(&d);assert(row);assert(fwrite(row,1,d.stride,stdout)==d.stride);}
    assert(!card_mask_next(&d));
}
int main(int argc,char **argv) {
    assert(argc==2);vectors();
    if(!strcmp(argv[1],"fonts")) {
        const int sizes[]={28,40,60,96};
        for(unsigned i=0;i<4;i++){const card_font_t *font=card_font_find(sizes[i]);
#if CONFIG_HOMEHUB_CARE_BOARD
            if(sizes[i]==96){assert(!font);continue;}
#endif
            assert(font);
            for(unsigned g=0;g<font->count;g++){const card_glyph_t *glyph=&font->glyphs[g];dump(font->bitmap+glyph->offset,glyph->encoded_bytes,glyph->width,glyph->height);}}
    } else {
        assert(!strcmp(argv[1],"icons"));
        for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++)for(int size=64;size<=112;size+=48){
            const card_icon_t *icon=card_icon_find(names[i],size);assert(icon);dump(icon->bitmap,icon->encoded_bytes,icon->width,icon->height);}
    }
    return 0;
}
