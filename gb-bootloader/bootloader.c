#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


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

#define MAX_GAMES_RENDER_NUM 17
#define CHARS_PER_ROW 20

#define MENU_GAME_MENU 1
#define MENU_SYSTEM_INFO 2
#define MENU_RGB_TESTER 3
#define MENU_GAME_SETTINGS 4

const palette_color_t backgroundpalette[] = {RGB_WHITE, RGB_YELLOW, RGB_BROWN,
                                             RGB_BLACK};

uint8_t _cursor = 0;
uint8_t _cursorLines = 0;
uint8_t _cursorOffset = 0;

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

struct SharedGameboyData *__sharedData = (struct SharedGameboyData *)(0xA000);
struct SharedGameboyData *_sharedDataLocalCopy = NULL;

#define _gameCount _sharedDataLocalCopy->number_of_roms

uint8_t _currentScreen = MENU_GAME_MENU;
uint8_t _screenInitialized = 0;
uint8_t _currentGame = 0;

font_t _font;
font_t _fontSelected;

uint8_t buttonPressed(uint8_t input, uint8_t key);
void clearCursor(void);
void renderCursor(void);
void moveCursor(uint8_t input);
void loadNewScreen(uint8_t newScreen);
void startGame(uint8_t game, uint8_t mode);

void copySharedData(void);

char *getRomNameForIndex(uint8_t idx){
  char *pRomNames = _sharedDataLocalCopy->rom_names;
  for (UINT8 i = 0; i < _gameCount; ++i) {
    if (i == idx) return pRomNames;
    pRomNames += strlen(pRomNames)+1;
  }
  return NULL;
}

void renderGamelist(uint8_t first, uint8_t selected){
  gotoxy(0, 0);
  const char *spacer_selected = selected <= 9 ? " " : "";
  const char *spacer_games = _gameCount <= 9 ? " " : "";
  printf("***  Games %s%d/%s%d ***",spacer_selected,selected+1,spacer_games,_gameCount);
  char *pRomNames = _sharedDataLocalCopy->rom_names;

  if (_gameCount != 0) {
    // loop through all game titles and display them
    for (UINT8 i = 0; i < _gameCount; ++i) {
      if (i >= first){
        uint8_t renderIDX = i-first;
        if (renderIDX >= MAX_GAMES_RENDER_NUM) break;
        gotoxy(0, renderIDX+1);
        if (i == selected) {
          font_set(_fontSelected);
        }else{
          font_set(_font);
        }
        printf("%s", pRomNames);
        for (UINT8 z = posx(); z<CHARS_PER_ROW-1; z++){
          /*
            BUG: when the bottom right tile is reached, the screen auto-scrolls up and i have no idea how to disable that :(
            The workaround for now is not to write to that tile by iterating to "z<CHARS_PER_ROW-1" instead of "z<CHARS_PER_ROW"
          */
          putchar(' ');
        }
      }
      pRomNames += strlen(pRomNames)+1;
    }

  } else {
    gotoxy(0, 2);
    printf("No games found");
  }
  font_set(_font);
}

void copySharedData(void){
  _sharedDataLocalCopy = __sharedData;
  uint8_t *startptr = (uint8_t *)__sharedData;
  uint8_t *endptr = (uint8_t*)getRomNameForIndex(_gameCount-1);
  endptr += strlen(endptr)+1;
  size_t sharedDataFilledSize = endptr - startptr;
  _sharedDataLocalCopy = malloc(sharedDataFilledSize);
  memcpy(_sharedDataLocalCopy, __sharedData, sharedDataFilledSize);
}

void main(void) {
  copySharedData();
  uint8_t input = 0;

  ENABLE_RAM_MBC1;

  if (_cpu == CGB_TYPE) {
    set_bkg_palette(0, 1, &backgroundpalette[0]);
  }

  set_bkg_data(100, 70, giraffe_4color_data);

  font_init();  
  _font = font_load(font_ibm);

  font_color(WHITE, BLACK);
  _fontSelected = font_load(font_ibm);
    
  font_set(_font);

  loadNewScreen(MENU_GAME_MENU);

  DISPLAY_ON;

  mode(get_mode() | M_NO_SCROLL);

  int16_t pageCursor = 0;
  int16_t cursor = 0;

  while (1) {
    vsync();

    input = joypad();

    if (_currentScreen == MENU_GAME_MENU) {
      if (!_screenInitialized) {
        cls();
        renderGamelist(pageCursor,cursor);
        _screenInitialized = 1;
      }

      if (_gameCount != 0) {
        if (buttonPressed(input, J_UP)) {
          if (cursor > 0) {
            cursor--;
            if (cursor < pageCursor){
              pageCursor--;
            }
            renderGamelist(pageCursor,cursor);
          }
        }

        if (buttonPressed(input, J_DOWN)) {
          if (cursor < _gameCount-1) {
            cursor++;
            if (cursor > pageCursor+MAX_GAMES_RENDER_NUM-1){
              pageCursor++;
            }
            renderGamelist(pageCursor,cursor);
          }
        }

        if (buttonPressed(input, J_LEFT)) {
          if (cursor > 0) {
            cursor -= MAX_GAMES_RENDER_NUM;
            pageCursor -= MAX_GAMES_RENDER_NUM;
            if (cursor < 0) cursor = 0;
            if (pageCursor < 0) pageCursor = 0;
            renderGamelist(pageCursor,cursor);
          }
        }

        if (buttonPressed(input, J_RIGHT)) {
          if (cursor < _gameCount-1) {
            cursor += MAX_GAMES_RENDER_NUM;
            pageCursor += MAX_GAMES_RENDER_NUM;
            if (cursor >= _gameCount-1) cursor = _gameCount-1 ;
            if (pageCursor + MAX_GAMES_RENDER_NUM >= _gameCount) pageCursor = _gameCount - MAX_GAMES_RENDER_NUM;
            renderGamelist(pageCursor,cursor);
          }
        }

        if (buttonPressed(input, J_A)) {
          startGame(cursor, 0xff);
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

        gotoxy(2, 2);
        printf("%s", getRomNameForIndex(_currentGame));

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
        renderCursor();

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
        printf("Ver %hu.", (uint8_t)_sharedDataLocalCopy->versionMajor);
        printf("%hu.", (uint8_t)_sharedDataLocalCopy->versionMinor);
        printf("%hu ", (uint8_t)_sharedDataLocalCopy->versionPatch);
        printf("%c", (char)_sharedDataLocalCopy->buildType);
        gotoxy(0, 17);
        printf("rev %X%X", _sharedDataLocalCopy->git_sha1_h, _sharedDataLocalCopy->git_sha1_l);
        if (_sharedDataLocalCopy->git_status) {
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
        renderCursor();

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

uint8_t buttonPressed(uint8_t input, uint8_t key) {
  if (input & key) {
    waitpadup();

    return TRUE;
  }
  return FALSE;
}

void clearCursor(void) {
  gotoxy(1, _cursor + _cursorOffset);
  setchar(SPACE_CHAR);
}

void renderCursor(void) {
  gotoxy(1, _cursor + _cursorOffset);
  setchar('>');
}

void moveCursor(uint8_t input) {
  if (buttonPressed(input, J_UP)) {
    if (_cursor > 0) {
      clearCursor();
      _cursor--;
      renderCursor();
    }
  }

  if (buttonPressed(input, J_DOWN)) {
    if (_cursor < _cursorLines - 1) {
      clearCursor();
      _cursor++;
      renderCursor();
    }
  }
}

void loadNewScreen(uint8_t newScreen) {
  _currentScreen = newScreen;
  _screenInitialized = 0;
  _cursorOffset = 0;
  _cursor = 0;
}

void startGame(uint8_t game, uint8_t mode) {
  (*(uint8_t *)(0xB000)) = mode;
  (*(uint8_t *)(0xB001)) = game;
  DISPLAY_OFF;
  (*(uint8_t *)(0xB002)) = 42;
  while (1) {
    vsync();
  }
}
