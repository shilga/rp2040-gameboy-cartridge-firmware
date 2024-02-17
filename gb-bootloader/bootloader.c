#include <stdint.h>
#include <stdio.h>

#include <gb/cgb.h>
#include <gb/drawing.h>
#include <gb/gb.h>
#include <gbdk/console.h>
#include <gbdk/font.h>

#include "giraffe_4color_data.c"
#include "giraffe_4color_map.c"

#define RIGHT_ARROW_CHAR 3
#define LEFT_ARROW_CHAR 4
#define UNCHECKED_CHAR 5
#define SCREEN_CHAR 6
#define CHECK_CHAR 8
#define NOTE_CHAR 11
#define SPACE_CHAR ' '

#define MENU_GAME_MENU 1
#define MENU_SYSTEM_INFO 2
#define MENU_RGB_TESTER 3
#define MENU_GAME_SETTINGS 4

const palette_color_t backgroundpalette[] = {RGB_WHITE, RGB_YELLOW, RGB_BROWN,
                                             RGB_BLACK};

uint8_t _cursor = 0;
uint8_t _cursorLines = 0;
uint8_t _cursorOffset = 0;

UINT8 buttonPressed(UINT8 input, UINT8 key) {
  if (input & key) {
    waitpadup();

    return TRUE;
  }
  return FALSE;
}

void ClearCursor() {
  gotoxy(1, _cursor + _cursorOffset);
  setchar(SPACE_CHAR);
}

void RenderCursor() {
  gotoxy(1, _cursor + _cursorOffset);
  setchar('>');
}

void moveCursor(UINT8 input) {
  if (buttonPressed(input, J_UP)) {
    if (_cursor > 0) {
      ClearCursor();
      _cursor--;
      RenderCursor();
    }
  }

  if (buttonPressed(input, J_DOWN)) {
    if (_cursor < _cursorLines - 1) {
      ClearCursor();
      _cursor++;
      RenderCursor();
    }
  }
}

struct SharedGameboyData {
  uint16_t git_sha1_l;
  uint16_t git_sha1_h;
  uint8_t git_status;
  char buildType;
  uint8_t versionMajor;
  uint8_t versionMinor;
  uint8_t versionPatch;
  uint8_t number_of_roms;
  uint8_t rom_names;
};

struct SharedGameboyData *_sharedData = (struct SharedGameboyData *)(0xA000);

uint8_t _currentScreen = MENU_GAME_MENU;
uint8_t _screenInitialized = 0;
uint8_t _currentGame = 0;

font_t _font;

void loadNewScreen(uint8_t newScreen) {
  _currentScreen = newScreen;
  _screenInitialized = 0;
  _cursorOffset = 0;
  _cursor = 0;
}

void startGame(uint8_t game, uint8_t mode) {
  (*(UBYTE *)(0xB000)) = mode;
  (*(UBYTE *)(0xB001)) = game;
  DISPLAY_OFF;
  (*(UBYTE *)(0xB002)) = 42;
  while (1) {
    wait_vbl_done();
  }
}

void main() {
  UBYTE input = 0;
  UINT8 gameCount;

  UINT8 *titleAddress;

  char text[17];

  ENABLE_RAM_MBC1;

  if (_cpu == CGB_TYPE) {
    set_bkg_palette(0, 1, &backgroundpalette[0]);
  }

  font_init();
  _font = font_load(font_ibm);
  font_set(_font);

  set_bkg_data(100, 70, giraffe_4color_data);

  gameCount = _sharedData->number_of_roms;

  loadNewScreen(MENU_GAME_MENU);

  DISPLAY_ON;

  while (1) {
    wait_vbl_done();

    input = joypad();

    if (_currentScreen == MENU_GAME_MENU) {
      if (!_screenInitialized) {
        cls();

        gotoxy(0, 0);
        printf("***    Games     ***");

        text[16] = '\0';
        titleAddress = (UINT8 *)&_sharedData->rom_names;

        if (gameCount != 0) {
          // loop through all game titles and display them
          for (UINT8 i = 0; i < gameCount; ++i) {
            for (UINT8 j = 0; j < 16; ++j) {
              text[j] = *(titleAddress++);
            }

            gotoxy(2, i + 1);
            printf("%s", text);
          }

          _cursorLines = gameCount;
          _cursor = _currentGame;
          _cursorOffset = 1;
          RenderCursor();
        } else {
          gotoxy(2, 2);
          printf("No games found");
        }

        _screenInitialized = 1;
      }

      if (gameCount != 0) {
        moveCursor(input);
        _currentGame = _cursor;

        if (buttonPressed(input, J_A)) {
          startGame(_currentGame, 0xff);
        }

        if (buttonPressed(input, J_START)) {
          loadNewScreen(MENU_GAME_SETTINGS);
        }
      }

      if (buttonPressed(input, J_SELECT)) {
        loadNewScreen(MENU_SYSTEM_INFO);
      }

    } else if (_currentScreen == MENU_GAME_SETTINGS) {
      if (!_screenInitialized) {
        cls();
        gotoxy(0, 0);
        printf("*** Game Options ***");

        titleAddress = (UINT8 *)(&_sharedData->rom_names + (_currentGame * 16));

        for (UINT8 j = 0; j < 16; ++j) {
          text[j] = *(titleAddress++);
        }

        gotoxy(2, 2);
        printf("%s", text);

        gotoxy(0, 4);
        printf("Savegame Hook:");
        gotoxy(2, 5);
        printf("Off");
        gotoxy(2, 6);
        printf("Mode 1 (sel + b)");
        gotoxy(2, 7);
        printf("Mode 2 (sel + dwn)");

        _cursorLines = 3;
        _cursor = 0;
        _cursorOffset = 5;
        RenderCursor();

        _screenInitialized = 1;
      }

      moveCursor(input);

      if (buttonPressed(input, J_B)) {
        loadNewScreen(MENU_GAME_MENU);
      }
      if (buttonPressed(input, J_START)) {
        startGame(_currentGame, _cursor);
      }
    } else if (_currentScreen == MENU_SYSTEM_INFO) {
      if (!_screenInitialized) {
        cls();
        set_bkg_tiles(0, 0, 20, 18, giraffe_4color_map);

        gotoxy(0, 15);
        printf("Croco Cartridge");
        gotoxy(0, 16);
        printf("Ver %hu.", (uint8_t)_sharedData->versionMajor);
        printf("%hu.", (uint8_t)_sharedData->versionMinor);
        printf("%hu ", (uint8_t)_sharedData->versionPatch);
        printf("%c", (char)_sharedData->buildType);
        gotoxy(0, 17);
        printf("rev %X%X", _sharedData->git_sha1_h, _sharedData->git_sha1_l);
        if (_sharedData->git_status) {
          printf(" dirty");
        }

        _screenInitialized = 1;
      }

      if (buttonPressed(input, J_START)) {
        gotoxy(0, 14);
        printf("Started RP2040 BTLD");
        wait_vbl_done();
        disable_interrupts();
        (*(UBYTE *)(0xB011)) = 1;
        wait_vbl_done();
        while (1) {
          wait_vbl_done();
        }
      }
      if (buttonPressed(input, J_RIGHT)) {
        loadNewScreen(MENU_RGB_TESTER);
      }
      if (buttonPressed(input, J_SELECT)) {
        loadNewScreen(MENU_GAME_MENU);
      }
    } else if (_currentScreen == MENU_RGB_TESTER) {
      if (!_screenInitialized) {
        cls();
        gotoxy(0, 0);
        printf("***   Test LED   ***");
        gotoxy(2, 1);
        printf("Off");
        gotoxy(2, 2);
        printf("Red");
        gotoxy(2, 3);
        printf("Green");
        gotoxy(2, 4);
        printf("Blue");
        _cursorLines = 4;
        _cursor = 0;
        _cursorOffset = 1;
        RenderCursor();

        _screenInitialized = 1;
      }

      moveCursor(input);

      if (buttonPressed(input, J_A)) {
        (*(UBYTE *)(0xB010)) = _cursor;
      }
      if (buttonPressed(input, J_LEFT)) {
        (*(UBYTE *)(0xB010)) = 0;
        loadNewScreen(MENU_SYSTEM_INFO);
      }
      if (buttonPressed(input, J_SELECT)) {
        (*(UBYTE *)(0xB010)) = 0;
        loadNewScreen(MENU_GAME_MENU);
      }
    }
  } // endless while
}
