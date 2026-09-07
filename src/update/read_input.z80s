	; Seed D, E, H, L to 0xff. Bits from other locations will be shifted into them.
	; If any is not 0xff at the end then that indicates:
	;
	;	D != 0xff => accelerate.
	;	E != 0xff => brake.
	;	H != 0xff >= left.
	;	L != 0xff >= right.
	ld d, 0xff
	ld e, d
	ld h, d
	ld l, d

	;
	; Read joystick input via port $EFFE.
	;
	;	b0 or b1 = accelerate [0 or 9],
	;	b2 = brake [8],
	;	b3 = right [7],
	;	b3 = left [6].
	ld bc, 0xeffe
	in a, (c)
	rra
	rl d
	rra
	rl d
	rra
	rl e
	rra
	rl l
	rra
	rl h

	;
	; Read cursor keys via port $FFFE.
	;
	;	b1 = accelerate [up],
	;	b2 = brake [down],
	;	b3 = left [left],
	;	b3 = right [right]
	ld b, 0xff
	in a, (c)
	rra
	rra
	rl d
	rra
	rl e
	rra
	rl h
	rra
	rl l

	;
	; Read port $DFFE for O and P.
	;
	;	b0 = right [o],
	;	b1 = left [p]
	ld b, 0xdf
	in a, (c)
	rra
	rl l
	rra
	rl h

	;
	; Read port $FBFE for Q at b0.
	;
	ld b, 0xfb
	in a, (c)
	rra
	rl d

	;
	; Read port $FDFE for A at b0.
	;
	ld b, 0xfd
	in a, (c)
	rra
	rl e

	;
	; Read port $7FFE to accept space at b0 as an alternate accelerate.
	;
	ld b, 0x7f
	in a, (c)
	rra
	rl d

	;
	; Write current inputs, summarising them into b0 of each byte.
	;
@summarise:
	ld a, d
	cp 0xff
	rla
	ld (@-accelerate), a

	ld a, e
	cp 0xff
	rla
	ld (@-brake), a

	ld a, h
	cp 0xff
	rla
	ld (@-left), a

	ld a, l
	cp 0xff
	rla
	ld (@-right), a
