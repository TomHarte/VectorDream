This module manages a double-buffered 'span buffer', i.e. a buffer that stores continuous solid horiontal spans of colour.

It can do differential drawing — comparing a new buffer to an old one and outputting only the differences.

It therefore seeks to ameliorate for the SAM's speed vs framebuffer deficit for scenes that:
* can be described well as horizontal strips of colour; and
* that do not change 'too much' from one frame to the next.

It uses 1kb of buffers per display line, for six 32kb pages total if drawing to the entire display. Total memory layout is:
* on `PAGE.VIDEO` is the frame buffer the SAM will read plus the `spans.swap` function and its various component parts;
* From `PAGE.FIRSTSPAN` onwards are the 1kb/line span buffers; and
* as the current project targets a 256kb SAM it has only a single other 32kb page, `PAGE.CODE`, where the rest of the code and data lives.

With only minor exceptions, the high 32kb of the memory map is used for paging span buffers in and out. The low 32kb contains code and, when relevant, the frame buffer. See the cross-page dispatcher for the mechanism by which PAGE.VIDEO and PAGE.CODE call into each other.

Relevant symbols:
* `spans.setup` should be called once at program startup;
* `spans.clear` can be used to populate the current back buffer with a black screen;
* `spans.swap` swaps the front and back span buffers, updating the pixel display; and
* `spans.getaddress` which pages the proper buffer and returns a pointer to the start of a named line.
