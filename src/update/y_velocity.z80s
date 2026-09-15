	; Apply.
	ld a, (player.y + 2)
	ld d, a
	ld hl, (player.y)					; D:HL = player.y

	ld a, (player.yvelocity + 1)
	rla
	sbc a
	ld bc, (player.yvelocity)			; A:BC = velocity, sign extended.

	add hl, bc
	adc d

	ld (player.y), hl
	ld (player.y + 2), a
