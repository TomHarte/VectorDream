# VectorDream

A SAM Coupé library for partial display updates of solid vector graphics.

Algorithm pursued:
* an RLE span buffer describes both the current and next displays;
* differencing the span buffers produces the minimal list of pixels needing adjustment.

**Important**: this is currently more of a design document than a description of the code contained herein. Code to follow.

## Span Buffer

There are two span buffers; each reserves 512 bytes to each line of the display — two bytes per output pixel — but is properly construed as a sparsely-packed linked list.

Each entry in the linked list describes an RLE run as: (i) the pixel it ends immediately after; and (ii) the colour of the run.

So e.g. a completely blank line will consist of a single entry at x = 0 of {.end = 255, .colour = 0}.

A line which is colour 0 at the edges and colour 7 in the might be of the form:
* at x = 0: {.end = 63, .colour = 0x00};
* at x = 63: {.end = 192, .colour = 0x77};
* at x = 192: {.end = 255, .colour = 0x00}.

**Code note**: all pseudocode below is written extemporaneously, and needs to be verified. Read this as me sketching out thoughts.

### Inserting

Assuming back-to-front drawing, rational alignment and the two-byte fields are split into two adjacent 256-byte arrays, to insert a single span from `x1` to `x2` in colour `c`:

	; Insert a node at the start of this RLE run.
	func insert:
		hl = start of line

		; Skip anything to left of new span, keep reference to first thing potentially to split.
		while [hl] < x1:
			l = [hl]
		prior = l

		; Find first thing that extends to or beyond right of new span.
		while [hl] < x2:
			l = [hl]

		; Split end span if necessary.
		if [hl] != x2:
			next = [hl]
			colour = [h+1:l]

			l = x2
			[hl] = next
			[h+1:l] = colour

		; Now consider splitting preceding span.
		if prior != x1:
			[h:prior] = x1

		; Insert new span.
		[h:x1] = x2
		[h+1:x1] = colour

### Front-to-back Drawing

Taking 0 as the transparency colour:

    func insert:
		hl = start of line

		; Skip spans that end before x1.
		while [hl] < x1:
			l = [hl]

		; Easy case: span also ends at or after x2.
		if [hl] >= x2:
			; Span is not transparent: the new one doesn't appear at all.
			if [h+1:l] != 0:
				return

			if [hl] == x2:
				if l != x1:
					; This transparent span ends exactly at x2, but starts
					; before it. So leave a transparent region on the left
					; and fill on the right.
					[h:x1] = x2
					[h+1:x1] = c

					[hl] = x1
				else:
					; This transparent span is exactly the size of the one to
					; be inserted! Just change the colour.
					[h+1:l] = c
			else:
				; This is a transparent span that ends after x2.
				; So first divide on the right to leave a transparent
				; region after the current.
				[h:x2] = [hl]
				[h+1:x2] = 0

				if l != x1:
					; This span also starts after x1. So leave a transparent
					; region on the left and fill on the right.
					[h:x1] = [h:x2]
					[h+1:x] = c

					[hl] = x1
				else:
					; This span exactly starts at x1. Since it's already
					; divided on the right, just fill on the left.
					[hl] = x2
					[h+1:l] = c

			return

		; Subdivide current span and fill right-hand size if it doesn't exactly start on x1
		; and is transparent.
		if l != x1 && [h+1:l] == 0:
			[h:x1] = [hl]
			[h+1:x1] = c
			[hl] = x1
			l = x1

		; Check all spans that end before x2 for colour, recolouring if appropriate.
		while [hl] <= x2:
			if [h+1:l] == 0:
				[h+1:l] = c
			l = [hl]

		; If the final span already exactly ends on x2, work is done.
		if [hl] == x2:
			return

		; Subdivide final span if colour allows.
		if [h+1:l] != 0:
			[h:x2] = [hl]
			[h+1:x2] = 0
			[hl] = x2
			[h+1:l] = c

### Differencing

Assuming `hl = buffer representing next display`, `de = buffer representing current display`:

	func output:
		x = 0
		while(l != 255) {
			if [hl] == [de]:
				if [h+1:l] != [d+1:e]:
					draw_span(x, [hl], [h+1:l])
				x = e = l = [hl]
			elif [hl] < [de]:
				if [h+1:l] != [d+1:e]:
					draw_span(x, [hl], [h+1:l])
				x = l = [hl]
			else:
				if [h+1:l] != [d+1:e]:
					draw_span(x, [de], [h+1:l])
				x = e = [de]
		}

## Net Memory Footprint

192 lines * 512 bytes * 2 = 192kb for the buffers.

Plus only a single 24kb buffer for the screen, at the cost of some tearing, as that already effectively gives a double buffer.

So: 216kb total for display buffers, leaving about 40kb for all other logic. So software can be similar in scope to that of a 48kb Spectrum if running on a 256kb SAM.

## Previous Attempts

Circa 2008 I attempted a similar differencing-to-save-drawing approach but used a compact span buffer rather than the sparse one described above; profiling suggested that maintaining the buffer was a substantial cost, primarily shuffling tail bytes on every insertion.

This sparse buffer approach is a belated reaction to that.

The question is going to be whether the approach as described above actually saves time: is the work done to avoid drawing cheaper than just brute-force drawing?