#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

// Indexed by SDL_SCANCODE, e.g. SDL_SCANCODE_LEFT.
using KeyStates = std::array<bool, SDL_SCANCODE_COUNT>;

struct SDLApp {
public:
	static constexpr int ImageWidth = 256;
	static constexpr int ImageHeight = 192;
	static constexpr int WindowScale = 3;

	void update(const KeyStates &key_states) {
	}

	void draw(uint8_t *const base, const size_t pitch) {
		draw();

		// Map SAM-style buffer to output.
		static constexpr uint32_t colours[16] = {
			0xff'00'00'00,	0xff'00'00'80,	0xff'00'80'00,	0xff'00'80'80,
			0xff'80'00'00,	0xff'80'00'80,	0xff'80'80'00,	0xff'80'80'80,
			0xff'00'00'00,	0xff'00'00'ff,	0xff'00'ff'00,	0xff'00'ff'ff,
			0xff'ff'00'00,	0xff'ff'00'ff,	0xff'ff'ff'00,	0xff'ff'ff'ff,
		};

		for(int y = 0; y < SDLApp::ImageHeight; y++) {
			uint32_t *const destination = reinterpret_cast<uint32_t *>(base + y * pitch);
			uint8_t *const source = &screen[y * 128];
			int shift = 4;
			for(int x = 0; x < SDLApp::ImageWidth; x++) {
				destination[x] = colours[ (source[x >> 1] >> shift) & 0xf ];
				shift ^= 4;
			}
		}
	}

	SDLApp() {
		// Seed span buffers to mutually empty.
		for(int c = 0; c < 192; c++) {
			for(int i = 0; i < 2; i++) {
				buffers[i][(c * 512) + 0] = 255;
				buffers[i][(c * 512) + 256] = 0;
			}
		}
	}

private:
	void draw() {
		// TODO: something that produces output here.
		overprint(10, 20, 0, 0xff);

		// Do partial updates.
		for(int y = 0; y < 192; y++) {
			uint8_t h = y << 1;
			uint8_t l = 0;
			uint8_t d = h;
			uint8_t e = l;
			const auto hl = [&] {
				return (h << 8) | l;
			};
			const auto de = [&] {
				return (d << 8) | e;
			};

			while(l != 255) {
				const uint8_t next_l = spans[hl()];
				const uint8_t next_e = spans[de()];
				++h;
				++d;

				if(next_l == next_e) {
					if(spans[hl()] != spans[de()]) {
						draw_span(l, next_l, y, spans[hl()]);
					}
					l = e = next_l;
				} else if(next_l < next_e) {
					if(spans[hl()] != spans[de()]) {
						draw_span(l, next_l, y, spans[hl()]);
					}
					l = next_l;
				} else {
					if(spans[hl()] != spans[de()]) {
						draw_span(l, next_e, y, spans[hl()]);
					}
					e = next_e;
				}

				--h;
				--d;
			}
		}

		std::swap(previous_spans, spans);
	}

	void draw_span(uint8_t start, const uint8_t end, const uint8_t y, const uint8_t colour) {
		uint8_t *line = &screen[y * 128];

		if(start & 1) {
			line[start >> 1] = (line[start >> 1] & 0xf0) | (colour & 0x0f);
			++start;
		}

		for(int c = start >> 1; c < end >> 1; c++) {
			line[c] = colour;
		}

		if(end & 1) {
			line[end >> 1] = (line[end >> 1] & 0x0f) | (colour & 0xf0);
		}
	}

	void overprint(uint8_t start, const uint8_t end, const uint8_t y, const uint8_t colour) {
		uint8_t h = y << 1;
		uint8_t l = 0;
		uint8_t d = h;
		uint8_t e = start;
		const auto hl = [&] {
			return (h << 8) | l;
		};
		const auto de = [&] {
			return (d << 8) | e;
		};

		// Find first stored span that overlaps start of new span.
		while(spans[hl()] < start) {
			l = spans[hl()];
		}
		const uint8_t prior = l;

		// Continue search for first span that ends after current.
		while(spans[hl()] < end) {
			l = spans[hl()];
		}

		// Split end necessary.
		if(spans[hl()] > end) {
			const uint8_t next = spans[hl()];
			++h;
			const uint8_t colour = spans[hl()];

			l = end;
			spans[hl()] = colour;
			--h;
			spans[hl()] = next;
		}

		// Split original if necessary.
		if(prior != e) {
			l = prior;
			spans[hl()] = start;
		}

		// Insert new span.
		spans[de()] = end;
		++d;
		spans[de()] = colour;
	}

	// The two span buffers.
	uint8_t buffers[2][512*192];
	uint8_t *previous_spans = buffers[0], *spans = buffers[1];

	// SAM-style framebuffer.
	uint8_t screen[ImageWidth * ImageHeight / 2]{};
};



int main(int argc, char* argv[]) {
	if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return EXIT_FAILURE;
	}

	SDL_Window *const window = SDL_CreateWindow(
		"Test Patch",
		SDLApp::ImageWidth * SDLApp::WindowScale,
		SDLApp::ImageHeight * SDLApp::WindowScale,
		0
	);
	const auto renderer = SDL_CreateRenderer(window, nullptr);
	SDL_SetRenderVSync(renderer, 1);	// Synchronise output to display refresh rate.
	const auto texture = SDL_CreateTexture(
		renderer,
		SDL_PIXELFORMAT_ABGR8888,
		SDL_TEXTUREACCESS_STREAMING,
		SDLApp::ImageWidth,
		SDLApp::ImageHeight
	);

	if(!window || !renderer || !texture) {
		fprintf(stderr, "Window/renderer/texture creation: %s\n", SDL_GetError());
		return EXIT_FAILURE;
	}

	SDLApp app;
	bool quit = false;
	KeyStates key_states{};
	while(!quit) {
		// Check for events.
		SDL_Event e;
		while(SDL_PollEvent(&e)) {
			switch(e.type) {
				case SDL_EVENT_QUIT:
					quit = true;
				break;

				case SDL_EVENT_KEY_DOWN:
				case SDL_EVENT_KEY_UP:
					key_states[e.key.scancode] = e.type == SDL_EVENT_KEY_DOWN;
				break;

				default: break;
			}
		}

		// Update.
		app.update(key_states);

		// Draw.
		uint8_t *pixels;
		int pitch;
		SDL_LockTexture(texture, nullptr, reinterpret_cast<void **>(&pixels), &pitch);

		app.draw(pixels, pitch);

		SDL_UnlockTexture(texture);
		SDL_RenderTexture(renderer, texture, nullptr, nullptr);
		SDL_RenderPresent(renderer);
	}

	SDL_DestroyRenderer(renderer);
	SDL_DestroyTexture(texture);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return EXIT_SUCCESS;
}
