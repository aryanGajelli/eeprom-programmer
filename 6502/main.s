; Memory
PORTB = $6000
PORTA = $6001

DDRB = $6002
DDRA = $6003

; Constants
E = 0b10000000
RW = 0b01000000
RS = 0b00100000

CLK_SPEED = 15000000

; Variables
sleepHi = $00
sleepLo = $01

lineHi = $02
lineLo = $03

; CODE Section
	.org $8000

reset:
	ldx #0xff					; Reset stack to 0x01ff
	txs

	lda #0b11111111				; Set all pins of PORTB as outputs
	sta DDRB                    

	lda #(RS |RW | E)			; Set E/RW/RS of PORTA as outputs
	sta DDRA
	
	lda #0b00001100				; Display ON, Curson OFF, Blink OFF
	jsr lcd_instruction

	lda #0b00110100				; Function Set, 8-bit mode, extended instruction set
	jsr lcd_instruction

	lda #0b00110110		  		; Function Set, 8-bit mode, extended instruction set, graphics ON
	jsr lcd_instruction

	; first 8 pixels are set, rest are 0
	lda #0xff
	sta lineHi
	stz lineLo

	ldx #0

move:
	jsr clr_gdram
	ldy #0

write_block:
	tya
	ora #0b10000000				; Set GDRAM vertical AC to row number
	jsr lcd_instruction

	txa
	; divide x by 8 to get the column number (0-15) for the GDRAM horizontal AC
	lsr
	lsr
	lsr
	and #0b00000111				; Mask out all bits except lower 3 bits
	ora #0b10000000				; Set GDRAM horizontal AC to 0x00
	jsr lcd_instruction

	lda lineHi
	jsr lcd_write

	lda lineLo
	jsr lcd_write

	
	iny
	cpy #8
	bne write_block				; If at last row stop

	; rotate right the 16-bit zero-page value in lineHi/lineLo by 1 bit
	; lineHi bit 0 -> carry -> lineLo bit 7
	lda lineLo
	lsr
	ror lineHi
	ror lineLo

	inx

	; Sleep so display is visible
	phx
	ldx #100
	jsr sleep_ms				
	plx

	jmp move					; Start over with next block of 8 rows


loop:
	jmp loop


clr_gdram:
	; https://www.instructables.com/The-Secrets-of-an-Inexpensive-Ubiquitous-Chinese-L/
	pha
	phy
	phx
	; Write data to GDRAM at row 0, column 0 -> 16*8-1 (128 pixels)

	ldy #0
write_row:
	tya
	ora #0b10000000				; Set GDRAM vertical AC to row number
	jsr lcd_instruction

	lda #0b10000000				; Set GDRAM horizontal AC to 0x00
	jsr lcd_instruction

	ldx #0
write_8:
	lda #0				
	jsr lcd_write
	inx
	cpx #32						; If at last col stop
	bne write_8

	iny
	cpy #32
	bne write_row				; If at last row stop
	
	lda #0				
	jsr lcd_write
	lda #0				
	jsr lcd_write

	plx
	ply
	pla
	rts

lcd_instruction:
	jsr lcd_wait
	sta PORTB					; Output command to PORTB
	lda #0						; Clear E/RW/RS
	sta PORTA
	lda #E						; Set E high to latch the command
	sta PORTA
	lda #0						; Clear E/RW/RS
	sta PORTA

	rts

lcd_write:
	jsr lcd_wait
	sta PORTB					; Output character to PORTB
	lda #RS						; Clear E/RW, RS high to indicate data
	sta PORTA
	lda #(RS | E)				; Set E high to latch the command, RS high to indicate data
	sta PORTA
	lda #RS						; Clear E/RW, RS high to indicate data
	sta PORTA
	rts

lcd_wait:
	pha
	lda #0b00000000				; Input from PORTB
	sta DDRB					; PORTB to input

	lda #RW						; RW high to read
	sta PORTA
	lda #(RW | E)				; RW high to read, E high to latch
	sta PORTA
busy_read:
	lda PORTB					; Read busy flag from PORTB
	and #0b10000000				; Mask out all bits except bit 7 (busy flag)
	bne busy_read				; If busy, loop until it's clear (branch if not 0 (bit 7 set))

	; !!! Need to clear RW after E goes low to avoid timing violation where RW/RS need to be stable 10ns after E goes low !!!
	; see page 36 of https://www.waveshare.com/datasheet/LCD_en_PDF/ST7920.pdf
	lda #RW						; RW high to read 
	sta PORTA
	lda #0						; Clear E/RW/RS
	sta PORTA

	lda #0b11111111				; PORTB to output
	sta DDRB					; Set PORTB as input
	pla
	rts


ONE_MS = (CLK_SPEED/1000)-13387		; Calibrated preload for about 1ms per count at 15 MHz
; Put the amount of time to sleep in the x register
sleep_ms:
	; Calibrated for the current DEC/LDA/BNE loop timing at 15 MHz
	pha

sleep_x_ms:
	lda #ONE_MS>>8
	sta sleepHi
	lda #ONE_MS&0xff
	sta sleepLo
	
sleep_1ms:
	dec sleepLo
	lda sleepLo
	bne sleep_1ms
	dec sleepHi
	lda sleepHi
	bne sleep_1ms

	dex
	bne sleep_x_ms

	pla
	rts


; DATA Section
message: .asciiz "Hello, World!"

; Reset Vector
	.org $fffc
	.word reset
	.word $eaea
