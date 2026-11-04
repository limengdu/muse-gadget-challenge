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

#include <assert.h>
#include <stdio.h>
#include "../components/muse/boards/watcher_camera.c"

static unsigned captured, released, scale;
static int prepare_error, decode_error, capture_error;
static unsigned frame_width = 416;
static const uint8_t jpeg[] = {0xff, 0xd8, 0xff, 0xd9};
static int board_lock(int ms) { (void)ms; return 0; }
static void board_unlock(void) {}
static const board_t board = { board_lock, board_unlock };
const board_t *muse_board = &board;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t h, int ms) { (void)h; (void)ms; return 1; }
int xSemaphoreGive(SemaphoreHandle_t h) { (void)h; return 1; }
const char *esp_err_to_name(int err) { (void)err; return "error"; }
void vTaskDeleteWithCaps(void *h) { (void)h; }
int xTaskCreateWithCaps(void (*f)(void *), const char *n, unsigned bytes, void *a, unsigned p, void *h, unsigned c)
{ (void)f; (void)n; (void)bytes; (void)a; (void)p; (void)h; (void)c; return pdPASS; }
int mbedtls_base64_encode(unsigned char *o, size_t cap, size_t *n, const unsigned char *src, size_t len)
{ (void)src; (void)len; assert(cap); *n = 0; *o = 0; return 0; }
void muse_state_set_caption(const char *fmt, ...) { (void)fmt; }
void muse_ui_image_draw(int x, int y, int w, int h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
void muse_ui_image_hide(void) {}
void muse_ui_camera_hint(bool active) { (void)active; }
void muse_ui_show_face(void) {}
esp_err_t camera_capture(camera_frame_t *f)
{ if (capture_error) return capture_error; captured++; f->jpeg = jpeg; f->len = sizeof(jpeg); return ESP_OK; }
void camera_release(camera_frame_t *f) { assert(f->jpeg == jpeg); released++; }
esp_err_t camera_stream_start(camera_frame_cb_t cb, void *ctx) { (void)cb; (void)ctx; return ESP_OK; }
void camera_stream_stop(void) {}
int jd_prepare(JDEC *jd, UINT (*read)(JDEC *, BYTE *, UINT), void *pool, UINT n, void *ctx)
{
    assert(pool && n == CAMERA_JPEG_POOL);
    jd->device = ctx; jd->width = frame_width; jd->height = 416;
    BYTE bytes[16]; assert(read(jd, bytes, 16) == sizeof(jpeg)); assert(read(jd, NULL, 16) == 0);
    return prepare_error;
}
int jd_decomp(JDEC *jd, UINT (*draw)(JDEC *, void *, JRECT *), BYTE ratio)
{
    scale = ratio;
    if (decode_error) return decode_error;
    uint8_t rgb[52 * 52 * 3];
    for (unsigned y = 0; y < 52; y++) for (unsigned x = 0; x < 52; x++) {
        unsigned at = (y * 52 + x) * 3;
        rgb[at] = rgb[at + 1] = rgb[at + 2] = x < 26 ? 40 : 160;
    }
    JRECT rect = { .left = 0, .right = 51, .top = 0, .bottom = 51 };
    assert(draw(jd, rgb, &rect) == 1);
    return JDR_OK;
}

int main(void)
{
    uint8_t pixels[32 * 32]; const char *error;
    assert(watcher_camera_thumbnail(pixels, 32, &error)); assert(!error && scale == 3);
    assert(captured == 1 && released == 1);
    assert(pixels[0] == 40 && pixels[31] == 160 && pixels[31 * 32] == 40);
    atomic_store(&s_busy, true);
    assert(!watcher_camera_thumbnail(pixels, 32, &error)); assert(captured == 1);
    atomic_store(&s_busy, false);
    s_preview_active = true; s_last = (uint8_t *)jpeg; s_last_len = sizeof(jpeg);
    assert(watcher_camera_thumbnail(pixels, 32, &error)); assert(captured == 1 && released == 1);
    s_preview_active = false; s_last = NULL;
    prepare_error = 1; assert(!watcher_camera_thumbnail(pixels, 32, &error)); assert(released == 2);
    prepare_error = 0; decode_error = 1; assert(!watcher_camera_thumbnail(pixels, 32, &error));
    decode_error = 0; frame_width = 2000; assert(!watcher_camera_thumbnail(pixels, 32, &error));
    frame_width = 416; capture_error = ESP_ERR_TIMEOUT;
    assert(!watcher_camera_thumbnail(pixels, 32, &error)); assert(!atomic_load(&s_busy));
    assert(!watcher_camera_thumbnail(pixels, 33, &error));
    puts("Home Eyes camera adapter: OK"); return 0;
}
