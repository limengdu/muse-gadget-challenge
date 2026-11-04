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

#pragma once
#include "muse_eink_priv.h"
namespace muse_eink {
void notes_setup(App &app);
bool notes_available(void);
void notes_prepare(void);
void notes_screen(Screen &screen);
// Returns -1 for the face, 1 for Settings, or 0 after changing lists.
// 切换清单后返回 0；返回 -1 表示进入表情页，1 表示进入设置页。
int notes_swipe(int dx);
void notes_scroll(int dy);
void notes_home(void);
}
