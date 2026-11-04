/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Double-click to open live preview, then tap the shutter or double-click again. */
void watcher_camera_preview_toggle(void);
bool watcher_camera_preview_active(void);
/* Captures one frame for the Home Link camera.capture command. */
bool watcher_camera_capture(char **jpeg_base64, const char **error);
esp_err_t watcher_camera_prepare(void);
#if CONFIG_MUSE_HOME_EYES
/* Captures a grayscale square thumbnail using the ROM JPEG decoder at 1/8.
 * 使用 ROM JPEG 解码器以八分之一比例拍摄灰度方形缩略图。 */
bool watcher_camera_thumbnail(uint8_t *pixels, size_t side, const char **error);
#endif

#ifdef __cplusplus
}
#endif
