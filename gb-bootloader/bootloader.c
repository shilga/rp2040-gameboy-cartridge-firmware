/* RP2040 GameBoy cartridge
 * Copyright (C) 2023 Sebastian Quilitz
 * Copyright (C) 2024 Tihmstar
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gb/cgb.h>
#include <gb/drawing.h>
#include <gb/gb.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <gbdk/platform.h>

#include "giraffe_4color_data.c"
#include "giraffe_4color_map.c"

#ifdef DMG_TEST_MODE_ON_CBG
#define DEVICE_SUPPORTS_COLOR (0)
#else
#define DEVICE_SUPPORTS_COLOR (_cpu == CGB_TYPE)
#endif // DMG_TEST_MODE_ON_CBG

#define MENU_GAME_MENU 1
#define MENU_SYSTEM_INFO 2
#define MENU_RGB_TESTER 3
#define MENU_GAME_SETTINGS 4
#define MENU_GAME_SETTINGS_SAVEGAMEHOOK 4
#define MENU_GAME_SETTINGS_RTC 5

#define MAX_GAMES_RENDER_NUM 16
#define CHARS_PER_ROW 20

#define ROW_RTC_REAL 12

#define SMEM_ADDR_LED_CONTROL ((UBYTE *)(0xB010))
#define SMEM_ADDR_RP2040_BOOTLOADER ((UBYTE *)(0xB011))
#define SMEM_ADDR_GAME_MODE_SELECTOR ((UBYTE *)(0xB000))
#define SMEM_ADDR_GAME_SELECTOR ((UBYTE *)(0xB001))
#define SMEM_ADDR_GAME_CONTROL ((UBYTE *)(0xB002))

#define SMEM_GAME_START_MAGIC 42

#define DMG_BKG_SELECTED_PALETTE                                               \
  DMG_PALETTE(DMG_DARK_GRAY, DMG_LITE_GRAY, DMG_BLACK, DMG_WHITE);
#define DMG_BKG_NORMAL_PALETTE                                                 \
  DMG_PALETTE(DMG_DARK_GRAY, DMG_LITE_GRAY, DMG_WHITE, DMG_BLACK);
#define DMG_TILE_NORMAL_PALETTE                                                \
  DMG_PALETTE(DMG_WHITE, DMG_DARK_GRAY, DMG_LITE_GRAY, DMG_BLACK);
#define DMG_TILE_SELECTED_PALETTE                                              \
  DMG_PALETTE(DMG_WHITE, DMG_DARK_GRAY, DMG_LITE_GRAY, DMG_WHITE);

#define COLOR_BLACK 3
#define COLOR_WHITE 2

struct SharedGameboyData {
  uint16_t git_sha1_l;
  uint16_t git_sha1_h;
  uint8_t git_status;
  char buildType;
  uint8_t versionMajor;
  uint8_t versionMinor;
  uint8_t versionPatch;
  uint8_t number_of_roms;
  char rom_names[];
};

struct CfgRTCReal {
  uint8_t s;    // seconds 0-59
  uint8_t m;    // minute 0-59
  uint8_t h;    // hour   0-23
  uint8_t d;    // day    0-30
  uint8_t mon;  // month  0-11
  uint8_t year; // year   0-255 (+1970) -> 1970-2225
};

const palette_color_t backgroundpalette[] = {
    RGB_BROWN, RGB_YELLOW, RGB_WHITE, RGB_BLACK,
    RGB_BROWN, RGB_YELLOW, RGB_BLACK, RGB_WHITE,
};

const palette_color_t spritepalette[] = {
    RGB_BROWN, RGB_YELLOW, RGB_GREEN, RGB_BLACK,
    RGB_BROWN, RGB_YELLOW, RGB_GREEN, RGB_WHITE,
};

static uint8_t gCurrentInput = 0;
static uint8_t gForceDrawScreen = 1;
static uint8_t gHighlightOffset = 1;
static uint8_t gCursor = 0;
static uint8_t gPageCursor = 0;
static uint8_t gLastSelectedGame = 0;
static uint8_t gLastSelectedGamePage = 0;

static uint8_t gDMGHighlightLine = 0xFF;

struct SharedGameboyData *s_sharedData = (struct SharedGameboyData *)(0xA000);
#define s_GamesCount s_sharedData->number_of_roms

void sanitizeRTCReal(struct CfgRTCReal *rtc);

void startGame(uint8_t game, uint8_t mode);
void loadRealtimeRTC(struct CfgRTCReal *rtc);
void storeRealtimeRTC(struct CfgRTCReal *rtc);

char *getRomNameForIndex(uint8_t idx) {
  char *pRomNames = s_sharedData->rom_names;
  for (uint8_t i = 0; i < s_GamesCount; ++i) {
    if (i == idx)
      return pRomNames;
    pRomNames += strlen(pRomNames) + 1;
  }
  return NULL;
}

uint8_t buttonPressed(uint8_t key) {
  if (gCurrentInput & key) {
    waitpadup();

    return TRUE;
  }
  return FALSE;
}

void selectTile(uint8_t x, uint8_t y, uint8_t sprite) {
  set_sprite_prop(sprite, y == gDMGHighlightLine ? S_PALETTE : 0);
  set_sprite_tile(sprite, get_bkg_tile_xy(x, y));
  x = (x << 3) + 8;
  y = (y << 3) + 16;
  move_sprite(sprite, x, y);
}

void resetSelection(void) {
  move_sprite(0, 0, 0);
  move_sprite(1, 0, 0);
  move_sprite(2, 0, 0);
  move_sprite(3, 0, 0);
}

void highlightLine(uint8_t idx) {
#define ATTR 1
  static uint8_t attrs[CHARS_PER_ROW] = {
      ATTR, ATTR, ATTR, ATTR, ATTR, ATTR, ATTR, ATTR, ATTR, ATTR,
      ATTR, ATTR, ATTR, ATTR, ATTR, ATTR, ATTR, ATTR, ATTR, ATTR};
  if (DEVICE_SUPPORTS_COLOR) {
    set_bkg_attributes(0, idx, CHARS_PER_ROW, 1, attrs);
  }
  gDMGHighlightLine = idx;
#undef ATTR
}

void resetHighlights(void) {
  static uint8_t attrs[CHARS_PER_ROW * MAX_GAMES_RENDER_NUM] = {0};
  if (DEVICE_SUPPORTS_COLOR) {
    set_bkg_attributes(0, 1, CHARS_PER_ROW, MAX_GAMES_RENDER_NUM, attrs);
  }
  resetSelection();
  gDMGHighlightLine = 0xFF;
}

void renderGamelist(uint8_t first, uint8_t selected) {
  gotoxy(0, 0);
  const char *spacer_selected = selected < 9 ? " " : "";
  const char *spacer_games = s_GamesCount < 9 ? " " : "";
  printf("***  Games %s%d/%s%d ***", spacer_selected, selected + 1,
         spacer_games, s_GamesCount);
  char *pRomNames = s_sharedData->rom_names;

  if (s_GamesCount != 0) {
    // loop through all game titles and display them
    for (UINT8 i = 0; i < s_GamesCount; ++i) {
      if (i >= first) {
        uint8_t renderIDX = i - first;
        if (renderIDX >= MAX_GAMES_RENDER_NUM)
          break;
        gotoxy(0, renderIDX + 1);
        printf("%s", pRomNames);
        for (UINT8 z = posx(); z < CHARS_PER_ROW; z++) {
          putchar(' ');
        }
      }
      pRomNames += strlen(pRomNames) + 1;
    }
  } else {
    gotoxy(0, 2);
    printf("No games found");
  }
}

void moveCursor(uint8_t limit) {
  if (buttonPressed(J_UP)) {
    if (gCursor > 0) {
      gCursor--;
      if (gCursor < gPageCursor) {
        gPageCursor--;
      }
    }
    gForceDrawScreen = 1;

  } else if (buttonPressed(J_DOWN)) {
    if (gCursor < limit - 1) {
      gCursor++;
      if (gCursor > gPageCursor + MAX_GAMES_RENDER_NUM - 1) {
        gPageCursor++;
      }
    }
    gForceDrawScreen = 1;

  } else if (buttonPressed(J_LEFT) && limit > MAX_GAMES_RENDER_NUM) {
    if (gCursor > 0) {
      if (gCursor >= MAX_GAMES_RENDER_NUM)
        gCursor -= MAX_GAMES_RENDER_NUM;
      else
        gCursor = 0;

      if (gPageCursor >= MAX_GAMES_RENDER_NUM)
        gPageCursor -= MAX_GAMES_RENDER_NUM;
      else
        gPageCursor = 0;
    }
    gForceDrawScreen = 1;

  } else if (buttonPressed(J_RIGHT) && limit > MAX_GAMES_RENDER_NUM) {
    if (gCursor < limit - 1) {
      if (gCursor + MAX_GAMES_RENDER_NUM < limit)
        gCursor += MAX_GAMES_RENDER_NUM;
      else
        gCursor = limit - 1;

      if (gPageCursor + (MAX_GAMES_RENDER_NUM << 1) < limit)
        gPageCursor += MAX_GAMES_RENDER_NUM;
      else
        gPageCursor = limit - MAX_GAMES_RENDER_NUM;
    }
    gForceDrawScreen = 1;
  }

  if (gForceDrawScreen && limit != 0) {
    resetHighlights();
    highlightLine(gCursor - gPageCursor + gHighlightOffset);
  }
}

uint8_t drawscreenGameMenu(void) {
  if (buttonPressed(J_SELECT)) {
    return MENU_SYSTEM_INFO;
  }

  if (s_GamesCount) {
    gCursor = gLastSelectedGame;
    gPageCursor = gLastSelectedGamePage;

    if (buttonPressed(J_SELECT)) {
      return MENU_SYSTEM_INFO;

    } else if (buttonPressed(J_START)) {
      return MENU_GAME_SETTINGS;

    } else if (buttonPressed(J_A)) {
      startGame(gCursor, 0xff);
    }

    moveCursor(s_GamesCount);
  }
  gLastSelectedGame = gCursor;
  gLastSelectedGamePage = gPageCursor;

  if (gForceDrawScreen)
    renderGamelist(gPageCursor, gCursor);
  return MENU_GAME_MENU;
}

uint8_t drawscreenSystemInfo(void) {

  if (buttonPressed(J_SELECT)) {
    return MENU_GAME_MENU;

  } else if (buttonPressed(J_START)) {
    gotoxy(0, 14);
    printf("Started RP2040 BTLD");
    wait_vbl_done();
    disable_interrupts();
    *SMEM_ADDR_RP2040_BOOTLOADER = 1;
    wait_vbl_done();
    while (1) {
      wait_vbl_done();
    }

  } else if (buttonPressed(J_RIGHT)) {
    return MENU_RGB_TESTER;
  }

  if (gForceDrawScreen) {
    set_bkg_tiles(0, 0, 20, 18, giraffe_4color_map);
    gotoxy(0, 0);
    printf("***   Sysinfo    ***");
    gotoxy(0, 15);
    printf("Croco Cartridge");
    gotoxy(0, 16);
    printf("Ver %hu.", (uint8_t)s_sharedData->versionMajor);
    printf("%hu.", (uint8_t)s_sharedData->versionMinor);
    printf("%hu ", (uint8_t)s_sharedData->versionPatch);
    printf("%c", (char)s_sharedData->buildType);
    gotoxy(0, 17);
    printf("rev %X%X", s_sharedData->git_sha1_h, s_sharedData->git_sha1_l);
    if (s_sharedData->git_status) {
      printf(" dirty");
    }
  }

  return MENU_SYSTEM_INFO;
}

uint8_t drawscreenRGBTester(void) {
  moveCursor(4);

  if (buttonPressed(J_A)) {
    *SMEM_ADDR_LED_CONTROL = gCursor;

  } else if (buttonPressed(J_SELECT)) {
    *SMEM_ADDR_LED_CONTROL = 0;
    return MENU_GAME_MENU;

  } else if (buttonPressed(J_LEFT)) {
    *SMEM_ADDR_LED_CONTROL = 0;
    return MENU_SYSTEM_INFO;
  }

  if (gForceDrawScreen) {
    gotoxy(0, 0);
    printf("***   Test LED   ***");
    printf("Off\n");
    printf("Red\n");
    printf("Green\n");
    printf("Blue\n");
  }

  return MENU_RGB_TESTER;
}

inline void drawscreenGameSettingsUI(void) {
  gotoxy(0, 0);
  printf("*** Game Options ***");

  gotoxy(0, 2);
  printf("%s", getRomNameForIndex(gLastSelectedGame));

  gotoxy(0, 4);
}

uint8_t drawscreenGameSettingsSavegameHook(void) {
  gHighlightOffset = 5;
  moveCursor(3);

  if (buttonPressed(J_B)) {
    return MENU_GAME_MENU;

  } else if (buttonPressed(J_SELECT)) {
    return MENU_GAME_SETTINGS_RTC;

  } else if (buttonPressed(J_START)) {
    startGame(gLastSelectedGame, gCursor);
  }

  if (gForceDrawScreen) {
    drawscreenGameSettingsUI();
    printf("[Savegame Hook]");
    gotoxy(2, 5);
    printf("Off");
    gotoxy(2, 6);
    printf("Mode 1 (sel + b)");
    gotoxy(2, 7);
    printf("Mode 2 (sel + dwn)");
  }

  return MENU_GAME_SETTINGS_SAVEGAMEHOOK;
}

uint8_t drawscreenGameSettingsRTC(void) {
  static uint8_t selectionX; // addresses fields from right to left
  static struct CfgRTCReal real_rtc;
  static uint8_t *modval = (uint8_t *)&real_rtc;
  uint8_t rtc_needs_commiting = 0;

  if (gForceDrawScreen) {
    loadRealtimeRTC(&real_rtc);
    selectionX = 1;
    modval = (uint8_t *)&real_rtc;
  }

  if (buttonPressed(J_SELECT)) {
    return MENU_GAME_SETTINGS_SAVEGAMEHOOK;

  } else if (buttonPressed(J_B)) {

  } else if (buttonPressed(J_START)) {
    rtc_needs_commiting = 1;
    gForceDrawScreen = 1;
  } else if (buttonPressed(J_A)) {

    rtc_needs_commiting = 1;

    gForceDrawScreen = 1;
  } else if (buttonPressed(J_UP)) {

    modval = (uint8_t *)&real_rtc;

    uint8_t oldval = ++modval[selectionX];
    sanitizeRTCReal(&real_rtc);
    if (modval[selectionX] == oldval - 1)
      modval[selectionX] = 0;

    gForceDrawScreen = 1;
  } else if (buttonPressed(J_DOWN)) {

    modval = (uint8_t *)&real_rtc;

    modval[selectionX]--;
    sanitizeRTCReal(&real_rtc);

    gForceDrawScreen = 1;
  } else if (buttonPressed(J_LEFT)) {

    if (selectionX < 5) {
      selectionX++;
    }

    gForceDrawScreen = 1;
  } else if (buttonPressed(J_RIGHT)) {
    if (selectionX > 1) {
      selectionX--;
    }
    gForceDrawScreen = 1;
  }

  if (rtc_needs_commiting & 1) {
    storeRealtimeRTC(&real_rtc);
  }

  if (gForceDrawScreen) {
    drawscreenGameSettingsUI();
    printf("[RTC config]");

    gotoxy(0, 10);
    printf("Real:");
    gotoxy(1, 11);
    printf("YYYY MM DD HH MM");
    gotoxy(1, 12);
    printf("%d ", real_rtc.year + 1970);
    if (real_rtc.mon < 9)
      putchar('0');
    printf("%d ", real_rtc.mon + 1);
    if (real_rtc.d < 9)
      putchar('0');
    printf("%d ", real_rtc.d + 1);
    if (real_rtc.h < 10)
      putchar('0');
    printf("%d ", real_rtc.h);
    if (real_rtc.m < 10)
      putchar('0');
    printf("%d ", real_rtc.m);

    gotoxy(1, 15);
    if (rtc_needs_commiting) {
      printf("[RTC config saved]");
    } else {
      for (uint8_t i = 0; i < 18; i++)
        putchar(' ');
    }

    resetHighlights();

    selectTile(18 - selectionX * 3, ROW_RTC_REAL, 0);
    selectTile(19 - selectionX * 3, ROW_RTC_REAL, 1);
    if (selectionX == 5) {
      selectTile(1, 12, 2);
      selectTile(2, 12, 3);
    }
  }

  return MENU_GAME_SETTINGS_RTC;
}

void drawscreen(void) {
  static uint8_t curScreen = MENU_GAME_MENU;
  uint8_t nextScreen = 0;

  switch (curScreen) {
  case MENU_GAME_SETTINGS_SAVEGAMEHOOK:
    nextScreen = drawscreenGameSettingsSavegameHook();
    break;

  case MENU_GAME_SETTINGS_RTC:
    nextScreen = drawscreenGameSettingsRTC();
    break;

  case MENU_RGB_TESTER:
    nextScreen = drawscreenRGBTester();
    break;

  case MENU_SYSTEM_INFO:
    nextScreen = drawscreenSystemInfo();
    break;

  case MENU_GAME_MENU:
  default:
    nextScreen = drawscreenGameMenu();
    break;
  }
  if (nextScreen != curScreen) {
    resetHighlights();
    cls();
    curScreen = nextScreen;
    gCursor = 0;
    gPageCursor = 0;
    gHighlightOffset = 1;
    gForceDrawScreen = 1;
  } else {
    gForceDrawScreen = 0;
  }
}

void scanline_isr(void) {
  if (gDMGHighlightLine != 0xFF) {
    if (((LY_REG + 1) >> 3) == gDMGHighlightLine) {
      BGP_REG = DMG_BKG_SELECTED_PALETTE;
      LYC_REG = ((gDMGHighlightLine + 1) << 3) - 1;
      return;
    } else {
      LYC_REG = (gDMGHighlightLine << 3) - 1;
    }
  }
  rBGP = DMG_BKG_NORMAL_PALETTE;
}

void main(void) {
  ENABLE_RAM_MBC1;

  if (DEVICE_SUPPORTS_COLOR) {
    set_bkg_palette(0, 2, &backgroundpalette[0]);
    set_sprite_palette(0, 2, &spritepalette[0]);
  } else {
    CRITICAL {
      STAT_REG = STATF_LYC;
      LYC_REG = 0;
      add_LCD(scanline_isr);
    }
    set_interrupts(IE_REG | LCD_IFLAG);

    OBP0_REG = DMG_TILE_NORMAL_PALETTE;
    OBP1_REG = DMG_TILE_SELECTED_PALETTE;
  }

  font_init();
  font_color(COLOR_BLACK, COLOR_WHITE);
  font_t curFont = font_load(font_ibm);
  font_set(curFont);

  mode(M_TEXT_OUT | M_NO_SCROLL);

  {
    uint8_t fontData[102 * 16];
    get_bkg_data(0, 102, fontData);
    set_sprite_data(0, 102, fontData);
  }
  SHOW_SPRITES;

  DISPLAY_ON;

  while (1) {
    vsync();

    gCurrentInput = joypad();

    drawscreen();
  } // endless while
}

void sanitizeRTCReal(struct CfgRTCReal *rtc) {
  if (rtc->mon > 11)
    rtc->mon = 11;

  uint8_t maxDay;
  if (rtc->mon == 1) {
    uint16_t year = 1970 + rtc->year;
    if ((year & 3) == 0) {
      // divisible by 4
      if ((year % 100) == 0) {
        if ((year % 400) == 0) {
          // leap year
          maxDay = 28;
        } else {
          // not leap year
          maxDay = 27;
        }
      } else {
        // leap year
        maxDay = 28;
      }
    } else {
      // not leap year
      maxDay = 27;
    }
  } else if (((rtc->mon < 7) && (rtc->mon & 1)) ||
             ((rtc->mon >= 7) && (rtc->mon & 1) == 0)) {
    maxDay = 29;
  } else {
    maxDay = 30;
  }
  if (rtc->d > maxDay)
    rtc->d = maxDay;
  if (rtc->h > 23)
    rtc->h = 23;
  if (rtc->m > 59)
    rtc->m = 59;
}

void startGame(uint8_t game, uint8_t mode) {
  *SMEM_ADDR_GAME_MODE_SELECTOR = mode;
  *SMEM_ADDR_GAME_SELECTOR = game;
  DISPLAY_OFF;
  *SMEM_ADDR_GAME_CONTROL = SMEM_GAME_START_MAGIC;
  while (1) {
    vsync();
  }
}

void loadRealtimeRTC(struct CfgRTCReal *rtc) {
#warning TODO: read this from rp2040
  rtc->m = 56;
  rtc->h = 17;
  rtc->d = 7;
  rtc->mon = 2;
  rtc->year = 54;
  sanitizeRTCReal(rtc);
}

void storeRealtimeRTC(struct CfgRTCReal *rtc) {
#warning TODO: write this from rp2040
  (void)rtc;
}
