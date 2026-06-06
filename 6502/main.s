PORTB = $6000
PORTA = $6001

DDRB = $6002
DDRA = $6003

E = 0b10000000
RW = 0b01000000
RS = 0b00100000

	.org $8000

reset:
	ldx #0xff					; Reset stack to 0x01ff
	txs

	lda #0b11111111				; Set all pins of PORTB as outputs
	sta DDRB                    

	lda #(RS |RW | E)			; Set E/RW/RS of PORTA as outputs
	sta DDRA


	lda #0b00110000				; Function set: 8-bit mode, basic instruction set
	sta PORTB
	jsr lcd_instruction

	lda #0b00110100				; Function set: 8-bit mode, extended instruction set
	sta PORTB
	jsr lcd_instruction

	lda #0b00001100				; Display ON, Curson OFF, Blink OFF
	sta PORTB
	jsr lcd_instruction

	lda #0b00000001				; Clear display
	sta PORTB
	jsr lcd_instruction

	lda #0b00000110				; Increment address, no display shift
	sta PORTB
	jsr lcd_instruction

	lda #"H"
	sta PORTB
	jsr print_char

	lda #"e"
	sta PORTB
	jsr print_char

	lda #"l"
	sta PORTB
	jsr print_char

	lda #"l"
	sta PORTB
	jsr print_char

	lda #"o"
	sta PORTB
	jsr print_char

	lda #" "
	sta PORTB
	jsr print_char

	lda #"W"
	sta PORTB
	jsr print_char

	lda #"o"
	sta PORTB
	jsr print_char

	lda #"r"
	sta PORTB
	jsr print_char

	lda #"l"
	sta PORTB
	jsr print_char

	lda #"d"
	sta PORTB
	jsr print_char

	lda #"!"
	sta PORTB
	jsr print_char

loop:
	jmp loop


lcd_instruction:
	lda #0						; Clear E/RW/RS
	sta PORTA
	lda #E						; Set E high to latch the command
	sta PORTA
	lda #0						; Clear E/RW/RS
	sta PORTA

	rts

print_char:
	lda #RS						; Clear E/RW, RS high to indicate data
	sta PORTA
	lda #(RS | E)				; Set E high to latch the command, RS high to indicate data
	sta PORTA
	lda #RS						; Clear E/RW, RS high to indicate data
	sta PORTA

	rts

	.org $fffc
	.word reset
	.word $eaea
