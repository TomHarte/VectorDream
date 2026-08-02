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

		// Seed screen angles.
		setup_tables();
	}

	void update(const KeyStates &key_states) {
		int8_t player_x_change = 0;

		if(key_states[SDL_SCANCODE_LEFT]) {
			player_x_change = 1;
		}
		if(key_states[SDL_SCANCODE_RIGHT] && player_x > -127) {
			player_x_change = -1;
		}

		if(key_states[SDL_SCANCODE_UP]) {
			player_y += 10;
			player_x_change += curve / 70.0f;
		}

		if(player_x_change < 0) {
			if(player_x > -127 - player_x_change) {
				player_x += player_x_change;
			} else {
				player_x = -127;
			}
		} else {
			if(player_x < 127 - player_x_change) {
				player_x += player_x_change;
			} else {
				player_x = 127;
			}
		}

		if(key_states[SDL_SCANCODE_Q]) {
			curve -= 5.0f;
		}

		if(key_states[SDL_SCANCODE_W]) {
			curve += 5.0f;
		}
	}

private:
	uint8_t player_y = 0;
	int8_t player_x = 0;
	int8_t curve = 0;

	//
	// Road drawing lookup tables.
	//

	// Angle for each screen column.
	uint8_t offsets[192];
	uint16_t road_widths[192];
	uint8_t line_widths[192];

	uint8_t one_over_distances[192];
	uint8_t curve_offset[192];

	int top_y = 0;

	void setup_tables() {
		static constexpr float height = 0.475f;
		static constexpr float x_rotation = -0.3f;

		static constexpr float field_of_view = 60.0f;	// In degrees.
		for(int y = 0; y < 192; y++) {
			const float screen_angle = atan2((float(y) - 96.0f) / (96.0f * (90.0f / field_of_view)), 1.0f);

			// tan(angle) = offset / height
			// => offset = height * tan(angle)

			// cos(angle) = height / depth
			// => depth = height / cos(angle)
			// .. and that needs to be multiplied by cos(screen_angle) to get distance from view plane.

			const float cos_screen_angle = cos(screen_angle);
			const float cast_angle = M_PI_2 - screen_angle + x_rotation;

			if(cast_angle > M_PI_2 - 0.01f) {
				++top_y;
				continue;
			}

			offsets[y] = uint8_t(tan(cast_angle) * height * 32.0f * 3.0f);

			const float distance = height * cos_screen_angle / cos(cast_angle);

			road_widths[y] = int(0.5f + (100.0f / distance));
			line_widths[y] = int(0.5f + (4.0f / distance));
			curve_offset[y] = sin(distance / 20.0f) * 128.0f;
			one_over_distances[y] = 128.0f / distance;
		}
	}

	void populate_spans() {
		for(int y = top_y; y < 192; y++) {
			const int16_t centre =
				127 +
				(
					player_x * one_over_distances[y]	// i8 * u8
					+ curve * curve_offset[y]			// i8 * u8
				) / 128;

			const uint8_t offset = offsets[y] + player_y;
			const uint8_t grass_colour = (offset & 128) ? 0xff : 0xee;
			const uint8_t road_colour = 0x33;
			const uint8_t line_colour = 0x44;

			const uint16_t road_width = road_widths[y];
			const uint8_t line_width = line_widths[y];

			const bool has_line = (offset & 64) && (line_width != 0);

			// Special case: road too thin to appear.
			if(road_width < 1.0f) {
				overprint(0, 255, y, grass_colour);
				continue;
			}

			// Grass on left.
			int x = 0;
			if(centre - road_width >= 1.0f) {
				if(centre - road_width >= 255) {
					overprint(0, 255, y, grass_colour);
					continue;
				}

				overprint(0, centre - road_width, y, grass_colour);
				x = centre - road_width;
			}

			// Road on left and line, if any.
			if(has_line) {
				// Road on left.
				if(centre - line_width >= 1.0f) {
					if(centre - line_width > 255) {
						overprint(x, 255, y, road_colour);
						continue;
					}
					overprint(x, centre - line_width, y, road_colour);
					x = centre - line_width;
				}

				if(centre + line_width >= 1.0f) {
					if(centre + line_width > 255) {
						overprint(x, 255, y, line_colour);
						continue;
					}
					overprint(x, centre + line_width, y, line_colour);
					x = centre + line_width;
				}
			}

			// Right hand side of road.
			if(centre + road_width >= 1.0f) {
				if(centre + road_width > 255) {
					overprint(x, 255, y, road_colour);
					continue;
				}
				overprint(x, centre + road_width, y, road_colour);
				x = centre + road_width;
			}

			// Right-hand grass. Must exist if the loop got here.
			overprint(x, 255, y, grass_colour);
		}
	}

	void draw() {
		populate_spans();

		// Enable here to show diffs only.
//		std::fill(std::begin(screen), std::end(screen), 0);

		// Do partial updates.
		for(int y = 0; y < 192; y++) {
			uint16_t h = y << 1;
			uint8_t l = 0;
			uint16_t d = h;
			uint8_t e = l;
			const auto hl = [&] {
				return (h << 8) | l;
			};
			const auto de = [&] {
				return (d << 8) | e;
			};

			uint8_t x = 0;
			while(l != 255) {
				const uint8_t next_l = spans[hl()];
				const uint8_t next_e = previous_spans[de()];
				++h;
				++d;

				if(next_l == next_e) {
					if(spans[hl()] != previous_spans[de()]) {
						draw_span(x, next_l, y, spans[hl()]);
					}
					x = l = e = next_l;
				} else if(next_l < next_e) {
					if(spans[hl()] != previous_spans[de()]) {
						draw_span(x, next_l, y, spans[hl()]);
					}
					x = l = next_l;
				} else {
					if(spans[hl()] != previous_spans[de()]) {
						draw_span(x, next_e, y, spans[hl()]);
					}
					x = e = next_e;
				}

				--h;
				--d;
			}
		}

		// Swap buffers, clear the new front.
		std::swap(previous_spans, spans);
		for(int y = 0; y < 192; y++) {
			spans[y * 512] = 255;
			spans[y * 512 + 256] = 0;
		}
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

	void overprint(const uint8_t start, const uint8_t end, const uint8_t y, const uint8_t colour) {
		uint16_t h = y << 1;
		uint8_t l = 0;
		uint16_t d = h;
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

		// Split end if necessary.
		if(spans[hl()] != end) {
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
