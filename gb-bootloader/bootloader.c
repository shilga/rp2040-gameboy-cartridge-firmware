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

const palette_color_t backgroundpalette[] = {RGB_WHITE, RGB_YELLOW, RGB_BROWN,
                                             RGB_BLACK};

UINT8 _cursor = 0;
UINT8 _cursorLines = 0;

UINT8 buttonPressed(UINT8 input, UINT8 key) {
  if (input & key) {
    waitpadup();

    return TRUE;
  }
  return FALSE;
}

void ClearCursor() {
  gotoxy(1, _cursor + 1);
  setchar(SPACE_CHAR);
}

void RenderCursor() {
  gotoxy(1, _cursor + 1);
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

void main() {
  font_t font;
  UINT8 renderMenu = 1;
  UBYTE input = 0;
  UINT8 gameCount;

  UINT8 *titleAddress;

  char text[17];

  ENABLE_RAM_MBC1;

  if (_cpu == CGB_TYPE) {
    set_bkg_palette(0, 1, &backgroundpalette[0]);
  }

  font_init();
  font = font_load(font_ibm);

  set_bkg_data(100, 70, giraffe_4color_data);

  DISPLAY_ON;

  gameCount = _sharedData->number_of_roms;

  while (1) {

    if (renderMenu == 1) {
      font_set(font);

      cls();

      gotoxy(0, 0);
      printf("Games");

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
        _cursor = 0;
        RenderCursor();
      } else {
        gotoxy(2, 2);
        printf("No games found");
      }

      renderMenu = 0;
    }

    if (gameCount != 0) {
      input = joypad();

      moveCursor(input);

      if (buttonPressed(input, J_A)) {
        (*(UBYTE *)(0xB000)) = _cursor;
        DISPLAY_OFF;
        while (1) {
          wait_vbl_done();
        }
      }
    }

    if (buttonPressed(input, J_SELECT)) {
      renderMenu = 2;
    }

    if (renderMenu == 2) {
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

      while (renderMenu == 2) {
        input = joypad();

        if (buttonPressed(input, J_START)) {
          gotoxy(0, 14);
          printf("Started RP2040 BTLD");
          wait_vbl_done();
          disable_interrupts();
          (*(UBYTE *)(0xB010)) = 1;
          wait_vbl_done();
          while (1) {
            wait_vbl_done();
          }
        }
        if (buttonPressed(input, J_RIGHT)) {
          renderMenu = 3;
        }
        if (buttonPressed(input, J_SELECT)) {
          renderMenu = 1;
        }
        wait_vbl_done();
      }
    }

    if (renderMenu == 3) {
      cls();
      gotoxy(0, 0);
      printf("Test LED:");
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
      RenderCursor();

      while (renderMenu == 3) {
        input = joypad();

        moveCursor(input);

        if (buttonPressed(input, J_A)) {
          (*(UBYTE *)(0xB001)) = _cursor;
        }
        if (buttonPressed(input, J_LEFT)) {
          renderMenu = 2;
        }
        if (buttonPressed(input, J_SELECT)) {
          renderMenu = 1;
        }

        wait_vbl_done();
      }
      (*(UBYTE *)(0xB001)) = 0;
    }

    wait_vbl_done();
  }
}
