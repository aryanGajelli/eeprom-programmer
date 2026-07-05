PORTB = $6000
PORTA = $6001

DDRB = $6002
DDRA = $6003

E = 0b10000000
RW = 0b01000000
RS = 0b00100000

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

	ldx #0						; idx 0
print:
	lda message,x
	beq loop
	jsr print_char
	inx
	jmp print
	

loop:
	jmp loop


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

print_char:
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
	; Need a dummy read for st7920
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


; DATA Section
message: .asciiz "Hello, World!"

; Reset Vector
	.org $fffc
	.word reset
	.word $eaea
