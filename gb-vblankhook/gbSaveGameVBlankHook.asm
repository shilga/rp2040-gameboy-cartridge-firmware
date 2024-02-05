INCLUDE "hardware.inc"

SECTION "addresses", ROM0[0]
; store the jump adresses here so the cartridge can find them
dw vblank_handler
dw trigger_saving

SECTION "VBlank", ROM0[$40]
    push af

    ; select the joypad buttons
    ld a,$10
    ldh [rP1],a
    jp vblank_handler

SECTION "hook", ROM0[$50]
vblank_handler:
; wait for joypad lines to settle
    push hl
    pop hl
    push hl
    pop hl

    ; read joypad and check for start or select being pressed
    ldh a,[rP1]
    cpl
    and $0c
    call nz,read_from_joypad

    ; reset joypad
    ld a,$30
    ldh [rP1],a

    pop af

    jp $40 ; jump to original vblank handler

read_from_joypad:
    push bc
    push hl

    ; read buttons again and store them in higher nibble of b
    ldh a,[rP1]
    cpl
    and $0f
    swap a
    ld b,a

    ; read the directional pads and xor them together with b in a
    ld a,$20                    
    ld [rP1],a
    ld a,[rP1]
    ld a,[rP1]
    cpl
    and $0f
    xor a, b ; the higher nibble of b contains the buttons

    call check_for_save_trigger

    pop hl
    pop bc

    ret


check_for_save_trigger:
    ; we now that select is being held, or we would not have ended up here

    ; check for b
    ; bit 5,a
    ; jp nz, trigger_saving
    
    ; check for down
    bit 3,a
    jp nz, trigger_saving

do_nothing:
    ret


SECTION "trigger_save", ROM0[$100]
trigger_saving:
    ; the cartridge will recognize that we jumped here and start
    ; storing the saveram to flash. We just need to idle here a bit

    ; prepare to read from buttons again
    ld a,$10
    ldh [rP1],a

    ; wait for beginning of vblank and switch off screen
    ld hl,rLCDC
screen_off_loop:
    ld a,[rLY]
    cp $90
    jr nz, screen_off_loop
    res 7,[hl]

    ; wait until the cartridge has written the savegame to flash
    ; the cartridge will actually change the byte at the rom location
    ; after the storing ist done
wait_for_game_save:
    ld a,[$1FF]
    cp $AA
    jr nz, wait_for_game_save

wait_for_buttons_release:
    ; read joypad and check for start or select being pressed
    ldh a,[rP1]
    cpl
    and $0c
    jr nz, wait_for_buttons_release

    ; turn the screen back on
    ld hl,rLCDC
    set 7,[hl]

    ; wait for vblank - so game always gets full vblank time
wait_vbl:
    ld a,[rLY]
    cp $90
    jr nz,wait_vbl

    ret
