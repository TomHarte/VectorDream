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

### Inserting

Assuming back-to-front drawing, rational alignment and the two-byte fields are split into two adjacent 256-byte arrays, to insert a single span from `x1` to `x2` in colour `c`:

	; Insert a node at the start of this RLE run.
	func insert:
		hl = start of line

		while true:
			; The start of the new span is already a node; insert one after to complete the span,
			; then update it.
			if l == x1:
				call fix_tail
				[hl] = x2
				[h+1:l] = c
				return

			next = [hl]

			; The start of the new span should be within this span; search from here to complete
			; the span then insert a start node.
			if next > x1:
				prior = hl
				call fix_tail
				hl = prior
				[hl] = x1
				l = x1
				[hl] = x2
				[h+1:l] = c
				return;

			l = next

	; Ensure a node exists at the end of this RLE span.
	func fix_tail:
		while true:
			next = [hl]

			if next == x2:
				return

			if next > x2:
				[h:x2] = next
				[h:x2] = [h+1:l]
				return

			l = next

(Written extemporaneously, to be verified).

### Differencing

Assuming `hl = buffer representing next display`, `de = buffer representing current display`:

	func output:
		while(l != 255) {
			if [hl] == [de]:
				if [h+1:l] != [d+1:e]:
					draw_span(l, [hl], [h+1:l])
				e = l = [hl]
				continue

			if [hl] < [de]:
				if [h+1:l] != [d+1:e]:
					draw_span(l, [hl], [h+1:l])
				l = [hl]
			else:
				if [h+1:l] != [d+1:e]:
					draw_span(l, [de], [h+1:l])
				e = [de]
		}

(Also written extemporaneously, to be verified).

## Net Cost

192 lines * 512 bytes * 2 = 192kb for the buffers.

Plus only a single 24kb buffer for the screen, at the cost of some tearing, as that already effectively gives a double buffer.

So: 216kb total for display buffers, leaving about 40kb for all other logic. So software can be similar in scope to that of a 48kb Spectrum if running on a 256kb SAM.

## Previous Attempts

Circa 2008 I attempted a similar differencing-to-save-drawing approach but used a compact span buffer rather than the sparse one described above; profiling suggested that maintaining the buffer was a substantial cost, primarily shuffling tail bytes on every insertion.

This sparse buffer approach is a belated reaction to that.

The question is going to be whether the approach as described above actually saves time: is the work done to avoid drawing cheaper than just brute-force drawing?