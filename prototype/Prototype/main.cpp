#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cassert>
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
		int16_t player_x_change = 0;

		if(key_states[SDL_SCANCODE_LEFT]) {
			player_x_change = 1 << 8;
		}
		if(key_states[SDL_SCANCODE_RIGHT]) {
			player_x_change = -1 << 8;
		}

		if(key_states[SDL_SCANCODE_UP]) {
			player_y += 10;
			object_offset -= 10;
			if(object_offset < 0) {
				object_offset = MaxDepth;
			}
			player_x_change += (curve * 256.0f) / 70.0f;
		}

		if(player_x_change < 0) {
			if(player_x > (-127 << 8) - player_x_change) {
				player_x += player_x_change;
			} else {
				player_x = -127 << 8;
			}
		} else {
			if(player_x < (127 << 8) - player_x_change) {
				player_x += player_x_change;
			} else {
				player_x = 127 << 8;
			}
		}

		if(key_states[SDL_SCANCODE_Q] && curve > -123) {
			curve -= 5.0f;
		}

		if(key_states[SDL_SCANCODE_W] && curve < 122) {
			curve += 5.0f;
		}
	}

private:
	uint8_t player_y = 0;
	int16_t player_x = 0;
	int8_t curve = 0;

	//
	// Road drawing lookup tables.
	//

	// Angle for each screen column.
//	uint8_t offsets[192];
	uint16_t floor_depths[192];
	uint8_t road_widths[192];
	uint8_t line_widths[192];

	uint8_t one_over_distances[192];
	uint8_t curve_offset[192];

	uint16_t squares[512];

	int top_y = 0;

	static constexpr float DepthUnitConversion = 128.0f;//3.0f * 32.0f;
	static constexpr int16_t MaxDepth = 3751; //20.0f * DepthUnitConversion; // A slightly arbitrary decision as to where objects first appear.

	int16_t object_offset = MaxDepth;
	int16_t distances[192];

	static constexpr int DepthToSineShift = 6;
	uint8_t sine[2 + ((1 + 4915) >> DepthToSineShift)];

	static constexpr float DepthCurvatureDivider = 20.0f;

	static constexpr float x_rotation = -0.3f;
	float height;
	static constexpr float field_of_view = 60.0f;	// In degrees.

	void setup_tables() {
		//
		// Calculate road-drawing tables: widths, 1/zs and curvature multiplier per screen line.
		//
		const auto screen_angle_at = [&](const int y) {
			return atan2((float(y) - 96.0f) / (96.0f * (90.0f / field_of_view)), 1.0f);
		};
		const auto cast_angle_at = [&](const int y) {
			return M_PI_2 - screen_angle_at(y) + x_rotation;
		};
		const auto planar_depth = [&](const float cast_angle) {
			return tan(cast_angle) * height;
		};

		const auto depth_at = [&](const int y) -> float {
			const float screen_angle = screen_angle_at(y);
			const float cast_angle = cast_angle_at(y);
			return height * cos(screen_angle) / cos(cast_angle);
		};

		// Binary search for minimum height.
		static constexpr int TargetWidth = 255;
		float bounds[] = {0.0f, 10.0f};
		while(bounds[1] - bounds[0] > 0.001f) {
			height = (bounds[0] + bounds[1]) * 0.5f;
			const float depth = depth_at(192);
			const int width = int(0.5f + (131.0f / depth));

			if(width > TargetWidth) {
				bounds[0] = (bounds[0] + bounds[1]) * 0.5f;
			} else {
				bounds[1] = (bounds[0] + bounds[1]) * 0.5f;
			}
		}

		float max_depth = std::numeric_limits<float>::min();
		float min_depth = std::numeric_limits<float>::max();
		for(int y = 0; y < 192; y++) {
			const float screen_angle = screen_angle_at(y);

			// tan(angle) = offset / height
			// => offset = height * tan(angle)

			// cos(angle) = height / depth
			// => depth = height / cos(angle)
			// .. and that needs to be multiplied by cos(screen_angle) to get distance from view plane.

			const float cast_angle = cast_angle_at(y);

			if(cast_angle > M_PI_2 - 0.01f) {
				++top_y;
				continue;
			}

			const float depth = height * cos(screen_angle) / cos(cast_angle);
			road_widths[y] = int(0.5f + (131.0f / depth));
			line_widths[y] = int(0.5f + (4.0f / depth));
			curve_offset[y] = sin(depth / DepthCurvatureDivider) * 128.0f;
			one_over_distances[y] = 128.0f / depth;

			if(road_widths[y] < 8) {
				++top_y;
				continue;
			}

			max_depth = std::max(max_depth, depth);
			min_depth = std::min(min_depth, depth);
		}

		printf("Depth range: %0.2f -> %0.2f\n", min_depth, max_depth);

//		const float planar_max = planar_depth(cast_angle_at(top_y));
//		const float planar_min = planar_depth(cast_angle_at(191));
//		const float planar_scale = 65535.0f / (planar_max - planar_min);
		for(int y = top_y; y < 192; y++) {
			const auto floor_depth = planar_depth(cast_angle_at(y));
//			floor_depths[y] = (floor_depth - planar_min) * planar_scale;
//			offsets[y] = uint8_t(floor_depth * DepthUnitConversion);
			distances[y] = floor_depth * DepthUnitConversion;

//			// TODO: unify above scalings, so that objects at least stick to lines.
		}

		for(auto it = std::begin(sine); it != std::end(sine); ++it) {
			const auto depth = (it - std::begin(sine)) << DepthToSineShift;
			const float world_z = float(depth) / 256.0f;
			*it = uint8_t(sin(world_z / DepthCurvatureDivider) * 256.0f);

//				const float world_z = float(wz) / 256.0f;
//				const int16_t centre = 128 + eye_x + curve * sin(world_z / DepthCurvatureDivider);
		}

		//
		// Calculate square table, for fast multiplication.
		//
		for(int c = 0; c < 512; c++) {
			squares[c] = (c * c) / 4;
		}

		printf("top_y: EQU %d\n", top_y);

		const auto dump_table = [&](const char *name, const auto begin, const auto end) {
			printf("%s:", name);

			int c = 0;
			for(auto it = begin; it != end; it++) {
				if(!(c & 15)) {
					if constexpr (sizeof(*begin) == 1) {
						printf("\n\tdb ");
					} else {
						printf("\n\tdw ");
					}
				} else {
					printf(", ");
				}
				++c;

				printf("%d", *it);
			}
			printf("\n");
		};


		std::vector<uint8_t> combo_table;
		for(int y = top_y; y < 192; y++) {
			combo_table.push_back(line_widths[y]);
			combo_table.push_back(road_widths[y]);
			combo_table.push_back(distances[y] & 0xff);
			combo_table.push_back(distances[y] >> 8);
		}
		printf("align 1024\n");
		dump_table("segments", combo_table.begin(), combo_table.end());
		dump_table("curve_offsets", std::begin(curve_offset) + top_y, std::end(curve_offset));

		printf("one_over_z_0: EQU %d\n", int(127.0f / depth_at(top_y)));
		printf("one_over_z_64: EQU %d\n", int(127.0f / depth_at(top_y + 64)));
		printf("max_object_depth: EQU %d\n", int(max_depth * DepthUnitConversion));
	}

	uint16_t mul(const uint8_t a, const uint8_t b) const {
		const uint8_t sub = std::abs(a - b);
		const uint16_t add = a + b;
		return squares[add] - squares[sub];
	}

	uint16_t ufixmul(const uint16_t a, const uint16_t b) const {
		return (a * b) >> 8;
	}

	int16_t sfixmul(const int16_t a, const int16_t b) const {
		return (a * b) >> 8;
	}

	int16_t hfixmul(const uint16_t a, const int16_t b) const {
		return (a * b) >> 8;
	}

	int16_t fixdiv(const int16_t a, const int16_t b) const {
		return (a << 8) / b;
	}

	void populate_spans() {
		int16_t centres[192]{};
		int object_base = 0;

		for(int y = top_y; y < 192; y++) {
			const int16_t centre =
				[&] {
					int16_t result = 0;

					// NB: sign test and adjustment for both player_x and curve can occur
					// **outside the loop** in Z80 world.
					if(player_x > 0) {
						result += mul(player_x >> 7, one_over_distances[y]) >> 1;
					} else {
						result -= mul((-player_x) >> 7, one_over_distances[y]) >> 1;
					}

					if(curve > 0) {
						result += mul(curve, curve_offset[y]) >> 1;
					} else {
						result -= mul(-curve, curve_offset[y]) >> 1;
					}

					centres[y] = 127 + (result >> 6);
					return centres[y];
				} ();

			if(!object_base && distances[y] < object_offset) {
				object_base = y;
			}

			const uint8_t offset = distances[y] + player_y;
			const uint8_t grass_colour = (offset & 128) ? 0xff : 0xee;
			const uint8_t road_colour = 0x33;
			const uint8_t line_colour = 0x44;

			const uint16_t road_width = road_widths[y];
			const uint8_t line_width = line_widths[y];

			const bool has_line = (offset & 64) && (line_width != 0);

			// Special case: road too thin to appear.
			//
			// This case shouldn't still be able to occur due to way I've terminated the
			// floor region via top_y; it now ends some non-zero interval before hitting a true horizontal
			// ray, to avoid issues with road curvature.
			//
//			if(road_width < 1.0f) {
//				overprint(0, 255, y, grass_colour);
//				continue;
//			}

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

		// Linearly interpolate object, if visible.
		if(object_base) {
			const int16_t ratio =
				fixdiv(object_offset - distances[object_base], distances[object_base - 1] - distances[object_base]);
			const int16_t centre =
				[&] () -> int16_t {
					if(ratio == 256) {
						return centres[object_base - 1];
					}
					if(!ratio) {
						return centres[object_base];
					}

					if(centres[object_base - 1] < centres[object_base - 0]) {
						const uint16_t length = centres[object_base - 0] - centres[object_base - 1];
						return centres[object_base - 0] - ufixmul(length, ratio);
					}

					const uint16_t length = centres[object_base - 1] - centres[object_base - 0];
					return centres[object_base - 0] + ufixmul(length, ratio);
				} ();

			const uint16_t scale =
				[&] () -> uint16_t {
					if(ratio == 256) {
						return road_widths[object_base - 1];
					}
					if(!ratio) {
						return road_widths[object_base];
					}

					return
						(
							mul(road_widths[object_base - 1], ratio) +
							mul(road_widths[object_base - 0], 256 - ratio)
						) >> 8;

				} ();

			const int x1 = std::max(centre - scale, 0);
			const int x2 = std::min(centre + scale, 255);
			if(x2 > 0 && x1 < 255) {
				for(int y = object_base - scale; y < object_base; y++) {
					if(y >= 0 && y < 192) {
						overprint(x1, x2, y, 0xdd);
					}
				}
			}
		}

//		printf("%d @ %d\n", object_scale, object_centre);

		// Try to place a single object.

/*		const uint8_t src_y = height * 256.0f;
		const uint16_t src_z = 256.0f * float(object_offset) / DepthUnitConversion;

		const uint8_t sin_x = uint8_t(sin(-x_rotation) * 256.0f);
		const uint8_t cos_x = uint8_t(cos(-x_rotation) * 256.0f);

		const int16_t wy =
			ufixmul(sin_x, src_z) -
			ufixmul(cos_x, src_y);
		const int16_t wz =
			ufixmul(sin_x, src_y) +
			ufixmul(cos_x, src_z);

		if(wz > 80) {
			const int scale = fixdiv(32, wz);
			if(scale > 0) {
				const int16_t fiy = fixdiv(wy, wz);
				const int16_t eye_y = fiy + (fiy >> 1); 				// = fiy * (90.0f / field_of_view)
				const uint8_t base = 96 - (eye_y >> 2) - (eye_y >> 3);	// = 96 - (eye_y * 96)

				const int16_t eye_x = fixdiv(player_x >> 7, wz);
//				const int16_t centre = 128 + eye_x + curve * sin(world_z / DepthCurvatureDivider);

//				const uint8_t x = sine[wz >> DepthToSineShift];
//				const float world_z = float(wz) / 256.0f;
//				const uint8_t s = sin(world_z / DepthCurvatureDivider) * 256.0f;

				const auto index = wz >> DepthToSineShift;
				assert(index + 1 < sizeof(sine));
				const uint8_t s1 = sine[index];
				const uint8_t s2 = sine[index + 1];

				const auto mask = (1 << DepthToSineShift) - 1;
				const auto soffset = wz & mask;
				const uint8_t s = (mul(mask - soffset, s1) + mul(soffset, s2)) >> DepthToSineShift;

//				const float world_z = float(wz) / 256.0f;
				const int16_t centre =
					128 +
					eye_x +
					(curve > 0 ?
						ufixmul(curve, s) :
						-ufixmul(-curve, s)
					);

				// Output as a 2:1 rectangle.
				const int x1 = std::max(centre - scale, 0);
				const int x2 = std::min(centre + scale, 255);
				if(x2 > 0 && x1 < 255) {
					for(int y = int(base - scale); y < int(base); y++) {
						if(y >= 0 && y < 192) {
							overprint(x1, x2, y, 0xdd);
						}
					}
				}
			}
		}*/
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
