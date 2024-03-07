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

#pragma mark defines

#define MENU_GAME_MENU 1
#define MENU_SYSTEM_INFO 2
#define MENU_RGB_TESTER 3
#define MENU_GAME_SETTINGS 4

#define MAX_GAMES_RENDER_NUM 17
#define CHARS_PER_ROW 20

#define SMEM_ADDR_LED_CONTROL         ((UBYTE *)(0xB010))
#define SMEM_ADDR_RP2040_BOOTLOADER   ((UBYTE *)(0xB011))
#define SMEM_ADDR_GAME_MODE_SELECTOR  ((UBYTE *)(0xB000))
#define SMEM_ADDR_GAME_SELECTOR       ((UBYTE *)(0xB001))
#define SMEM_ADDR_GAME_CONTROL        ((UBYTE *)(0xB002))

#define SMEM_GAME_START_MAGIC 42

#pragma mark types
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

#pragma mark constants
const palette_color_t backgroundpalette[] = {
  RGB_WHITE, RGB_YELLOW, RGB_BROWN, RGB_BLACK,
  RGB_BLACK, RGB_YELLOW, RGB_BROWN, RGB_WHITE,
};

#pragma mark globals
static uint8_t gForceDrawScreen = 1;
static uint8_t gHighlightOffset = 1;
static uint8_t gCursor = 0;
static uint8_t gPageCursor = 0;
static uint8_t gLastSelectedGame = 0;
static uint8_t gLastSelectedGamePage = 0;


#pragma mark shared mem globals
struct SharedGameboyData *s_SharedData = (struct SharedGameboyData *)(0xA000);
struct SharedGameboyData *_sharedDataLocalCopy = NULL;
#define s_GamesCount _sharedDataLocalCopy->number_of_roms

#pragma mark declarations
void startGame(uint8_t game, uint8_t mode);


#pragma mark functions

char *getRomNameForIndex(uint8_t idx){
  char *pRomNames = _sharedDataLocalCopy->rom_names;
  for (UINT8 i = 0; i < s_GamesCount; ++i) {
    if (i == idx) return pRomNames;
    pRomNames += strlen(pRomNames)+1;
  }
  return NULL;
}

void copySharedData(void){
  _sharedDataLocalCopy = s_SharedData;
  uint8_t *startptr = (uint8_t *)s_SharedData;
  uint8_t *endptr = (uint8_t*)getRomNameForIndex(s_GamesCount-1);
  endptr += strlen(endptr)+1;
  size_t sharedDataFilledSize = endptr - startptr;
  _sharedDataLocalCopy = malloc(sharedDataFilledSize);
  memcpy(_sharedDataLocalCopy, s_SharedData, sharedDataFilledSize);
}

#pragma mark control functions
uint8_t buttonPressed(uint8_t input, uint8_t key) {
  if (input & key) {
    waitpadup();

    return TRUE;
  }
  return FALSE;
}

#pragma mark screen functions
void resetAttributes(void){
  static uint8_t attrs[CHARS_PER_ROW*MAX_GAMES_RENDER_NUM] = {};
  set_bkg_attributes(0, 1, CHARS_PER_ROW, MAX_GAMES_RENDER_NUM, attrs);
}

void highlightLine(uint8_t idx){
#define ATTR 1
  static uint8_t attrs[CHARS_PER_ROW] = {ATTR,ATTR,ATTR,ATTR,ATTR,ATTR,ATTR,ATTR,ATTR,ATTR,ATTR,ATTR,ATTR,ATTR,ATTR,ATTR,ATTR,ATTR,ATTR,ATTR};
  set_bkg_attributes(0, idx+gHighlightOffset, CHARS_PER_ROW, 1, attrs);
#undef ATTR
}

void renderGamelist(uint8_t first, uint8_t selected){
  gotoxy(0, 0);
  const char *spacer_selected = selected <= 9 ? " " : "";
  const char *spacer_games = s_GamesCount <= 9 ? " " : "";
  printf("***  Games %s%d/%s%d ***",spacer_selected,selected+1,spacer_games,s_GamesCount);
  char *pRomNames = _sharedDataLocalCopy->rom_names;

  if (s_GamesCount != 0) {
    // loop through all game titles and display them
    for (UINT8 i = 0; i < s_GamesCount; ++i) {
      if (i >= first){
        uint8_t renderIDX = i-first;
        if (renderIDX >= MAX_GAMES_RENDER_NUM) break;
        gotoxy(0, renderIDX+1);
        printf("%s", pRomNames);
        for (UINT8 z = posx(); z<CHARS_PER_ROW; z++){
          putchar(' ');
        }
      }
      pRomNames += strlen(pRomNames)+1;
    }
  } else {
    gotoxy(0, 2);
    printf("No games found");
  }
}

void moveCursor(uint8_t input, uint8_t limit){
  if (buttonPressed(input, J_UP)) {
    if (gCursor > 0) {
      gCursor--;
      if (gCursor < gPageCursor){
        gPageCursor--;
      }
    }
    gForceDrawScreen = 1;

  }else if (buttonPressed(input, J_DOWN)) {
    if (gCursor < limit-1) {
      gCursor++;
      if (gCursor > gPageCursor+MAX_GAMES_RENDER_NUM-1){
        gPageCursor++;
      }
    }
    gForceDrawScreen = 1;

  }else if (buttonPressed(input, J_LEFT) && limit > MAX_GAMES_RENDER_NUM) {
    if (gCursor > 0) {
      if (gCursor >= MAX_GAMES_RENDER_NUM) gCursor -= MAX_GAMES_RENDER_NUM;
      else gCursor = 0;

      if (gPageCursor >= MAX_GAMES_RENDER_NUM) gPageCursor -= MAX_GAMES_RENDER_NUM;
      else gPageCursor = 0;
    }
    gForceDrawScreen = 1;

  }else if (buttonPressed(input, J_RIGHT) && limit > MAX_GAMES_RENDER_NUM) {
    if (gCursor < limit-1) {
      if (gCursor + MAX_GAMES_RENDER_NUM < limit) gCursor += MAX_GAMES_RENDER_NUM;
      else gCursor = limit-1;

      if (gPageCursor + (MAX_GAMES_RENDER_NUM<<1) < limit) gPageCursor += MAX_GAMES_RENDER_NUM;
      else gPageCursor = limit - MAX_GAMES_RENDER_NUM;
    }
    gForceDrawScreen = 1;
  }

  if (gForceDrawScreen && limit != 0){
    /*
      TODO: fix highlighting for DMG_TYPE
    */
    resetAttributes();
    highlightLine(gCursor-gPageCursor);
  }  
}

uint8_t drawscreenGameMenu(uint8_t input){
  uint8_t nextScreen = MENU_GAME_MENU;
  if (s_GamesCount){
    gCursor = gLastSelectedGame;
    gPageCursor = gLastSelectedGamePage;
    moveCursor(input, s_GamesCount);

    if (buttonPressed(input, J_SELECT)) {
      nextScreen = MENU_SYSTEM_INFO;

    }else if (buttonPressed(input, J_START)) {
      nextScreen = MENU_GAME_SETTINGS;

    }else if (buttonPressed(input, J_A)) {
      nextScreen = MENU_GAME_SETTINGS;
      startGame(gCursor, 0xff);

    }
  }
  gLastSelectedGame = gCursor;
  gLastSelectedGamePage = gPageCursor;

  if (gForceDrawScreen) renderGamelist(gPageCursor,gCursor);
  return nextScreen;
}

uint8_t drawscreenSystemInfo(uint8_t input){
  uint8_t nextScreen = MENU_SYSTEM_INFO;

  if (buttonPressed(input, J_SELECT)) {
    nextScreen = MENU_GAME_MENU;

  }else if (buttonPressed(input, J_START)) {
    gotoxy(0, 14);
    printf("Started RP2040 BTLD");
    wait_vbl_done();
    disable_interrupts();
    *SMEM_ADDR_RP2040_BOOTLOADER = 1;
    wait_vbl_done();
    while (1) {
      wait_vbl_done();
    }

  }else if (buttonPressed(input, J_RIGHT)) {
    nextScreen = MENU_RGB_TESTER;

  }

  if (gForceDrawScreen){
    set_bkg_tiles(0, 0, 20, 18, giraffe_4color_map);
    gotoxy(0,0);
    printf("***   Sysinfo    ***");
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
  }

  return nextScreen;
}

uint8_t drawscreenRGBTester(uint8_t input){
  uint8_t nextScreen = MENU_RGB_TESTER;

  moveCursor(input,4);
  
  if (buttonPressed(input, J_A)) {
    *SMEM_ADDR_LED_CONTROL = gCursor;

  } else if (buttonPressed(input, J_SELECT)) {
    nextScreen = MENU_GAME_MENU;
    *SMEM_ADDR_LED_CONTROL = 0;

  }else if (buttonPressed(input, J_LEFT)) {
    nextScreen = MENU_SYSTEM_INFO;
    *SMEM_ADDR_LED_CONTROL = 0;

  }

  if (gForceDrawScreen){
    gotoxy(0, 0);
    printf("***   Test LED   ***");
    printf("Off\n");
    printf("Red\n");
    printf("Green\n");
    printf("Blue\n");
  }

  return nextScreen;
}

uint8_t drawscreenGameSettings(uint8_t input){
  uint8_t nextScreen = MENU_GAME_SETTINGS;
  gHighlightOffset = 5;
  moveCursor(input, 3);

  if (buttonPressed(input, J_B)) {
    nextScreen = MENU_GAME_MENU;

  }else if (buttonPressed(input, J_START)) {
    startGame(gLastSelectedGame, gCursor);

  }

  if (gForceDrawScreen){
    gotoxy(0, 0);
    printf("*** Game Options ***");

    gotoxy(0, 2);
    printf("%s", getRomNameForIndex(gLastSelectedGame));

    gotoxy(0, 4);
    printf("Savegame Hook:");
    gotoxy(2, 5);
    printf("Off");
    gotoxy(2, 6);
    printf("Mode 1 (sel + b)");
    gotoxy(2, 7);
    printf("Mode 2 (sel + dwn)");
  }

  return nextScreen;
}

void drawscreen(uint8_t input){
  static uint8_t curScreen = MENU_GAME_MENU;
  uint8_t nextScreen = 0;

  switch (curScreen){
    case MENU_GAME_SETTINGS:
      nextScreen = drawscreenGameSettings(input);
      break;

    case MENU_RGB_TESTER:
      nextScreen = drawscreenRGBTester(input);
      break;

    case MENU_SYSTEM_INFO:
      nextScreen = drawscreenSystemInfo(input);
      break;

    case MENU_GAME_MENU:
    default:
      nextScreen = drawscreenGameMenu(input);
      break;
  }
  if (nextScreen != curScreen){
    resetAttributes();
    cls();
    curScreen = nextScreen;
    gCursor = 0;
    gPageCursor = 0;
    gHighlightOffset = 1;
    gForceDrawScreen = 1;
  }else{
    gForceDrawScreen = 0;
  }
}

#pragma mark main
void main(void) {
  ENABLE_RAM_MBC1;

  copySharedData();

  if (_cpu == CGB_TYPE) {
    set_bkg_palette(0, 2, &backgroundpalette[0]);
  }

  set_bkg_data(100, 70, giraffe_4color_data);

  font_init();  
  font_t curFont = font_load(font_ibm);
  font_set(curFont);

  mode(M_TEXT_OUT | M_NO_SCROLL);

  DISPLAY_ON;

  while (1) {
    vsync();

    uint8_t input = joypad();

    drawscreen(input);
  } // endless while
}

#pragma mark rp2040 communication functions
void startGame(uint8_t game, uint8_t mode) {
  *SMEM_ADDR_GAME_MODE_SELECTOR = mode;
  *SMEM_ADDR_GAME_SELECTOR = game;
  DISPLAY_OFF;
  *SMEM_ADDR_GAME_CONTROL = SMEM_GAME_START_MAGIC;
  while (1) {
    vsync();
  }
}