#include <pspdebug.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <vram.h>

#include <malloc.h>
#include <string.h>
#include <stdio.h>

#include <pspkernel.h>
PSP_MODULE_INFO("pspeanut-gb", 0, 1, 0);

#include "../Peanut-GB/peanut_gb.h"

#define MAX_FILE_NAME_LENGTH 256
#define ROMS_DIRECTORY "./"

#define PSP_SCREEN_WIDTH  480
#define PSP_SCREEN_HEIGHT 272

#define PSP_FRAME_BUFFER_WIDTH 512
#define PSP_FRAME_BUFFER_SIZE  (PSP_FRAME_BUFFER_WIDTH * PSP_SCREEN_HEIGHT)

#define PIXEL_FORMAT GU_PSM_8888
#define PIXEL_SIZE 4

#define RENDER_OFFSET_X 160
#define RENDER_OFFSET_Y (PSP_FRAME_BUFFER_WIDTH * 64)

typedef struct {
    unsigned int width, height;
    unsigned int pW, pH;
    unsigned int size;

    void* data;
} texture;

typedef struct
{
    float x, y, z;
} VertV;

typedef struct
{
    float   u, v;
    float   x, y, z;
} VertTV;

typedef struct
{
    float u, v;
    unsigned int colour;
    float x, y, z;
} TextureVertex;


// Some global variables
void* fbp0 = NULL;
void* fbp1 = NULL;

texture gb_texture;

static unsigned int __attribute__((aligned(16))) list[262144];

struct priv_t
{
	/* Pointer to allocated memory holding GB file. */
	uint8_t *rom;
	/* Pointer to allocated memory holding save file. */
	uint8_t *cart_ram;
};

/**
 * Returns a byte from the ROM file at the given address.
 */
uint8_t gb_rom_read(struct gb_s *gb, const uint_fast32_t addr)
{
	const struct priv_t * const p = (const struct priv_t *) gb->direct.priv;
	return p->rom[addr];
}

/**
 * Returns a byte from the cartridge RAM at the given address.
 */
uint8_t gb_cart_ram_read(struct gb_s *gb, const uint_fast32_t addr)
{
	const struct priv_t * const p = (const struct priv_t *) gb->direct.priv;
	return p->cart_ram[addr];
}

/**
 * Writes a given byte to the cartridge RAM at the given address.
 */
void gb_cart_ram_write(struct gb_s *gb, const uint_fast32_t addr,
		       const uint8_t val)
{
	const struct priv_t * const p = (const struct priv_t *) gb->direct.priv;
	p->cart_ram[addr] = val;
}

/**
 * Returns a pointer to the allocated space containing the ROM. Must be freed.
 */
uint8_t *read_rom_to_ram(const char *file_name)
{
	FILE *rom_file = fopen(file_name, "rb");
	size_t rom_size;
	uint8_t *rom = NULL;

	if(rom_file == NULL)
		return NULL;

	fseek(rom_file, 0, SEEK_END);
	rom_size = ftell(rom_file);
	rewind(rom_file);
	rom = (uint8_t *) malloc(rom_size);

	if(fread(rom, sizeof(uint8_t), rom_size, rom_file) != rom_size)
	{
		free(rom);
		fclose(rom_file);
		return NULL;
	}

	fclose(rom_file);
	return rom;
}

/**
 * Ignore all errors.
 */
void gb_error(struct gb_s *gb, const enum gb_error_e gb_err, const uint16_t val)
{
	const char* gb_err_str[GB_INVALID_MAX] = {
		"UNKNOWN",
		"INVALID OPCODE",
		"INVALID READ",
		"INVALID WRITE",
		"HALT FOREVER"
	};
	struct priv_t *priv = (struct priv_t *) gb->direct.priv;

	fprintf(stderr, "Error %d occurred: %s at %04X\n. Exiting.\n",
			gb_err, gb_err_str[gb_err], val);

	/* Free memory and then exit. */
	free(priv->cart_ram);
	free(priv->rom);
	exit(EXIT_FAILURE);
}

/**
 * Draws scanline into framebuffer.
 */
void lcd_draw_line(struct gb_s *gb, const uint8_t pixels[LCD_WIDTH],
		   const uint_fast8_t line)
{
    memcpy((uint8_t*)(gb_texture.data) + (gb_texture.pW * line), pixels, LCD_WIDTH);
}

int string_ends_with(char * string, const char * end) {
    int string_length = strlen(string);
    int end_length = strlen(end);

    for (int i = 0; i < end_length;i++) {
        if (string[i + string_length - end_length] != end[i]) {
            return 0;
        }
    }
    return 1;
}

int is_gameboy_rom(char * file_name) {
    if (string_ends_with(file_name, ".gb") > 0)
        return 1;
    if (string_ends_with(file_name, ".GB") > 0)
        return 1;
    if (string_ends_with(file_name, ".gbc") > 0)
        return 1;
    if (string_ends_with(file_name, ".GBC") > 0)
        return 1;
    return 0;
}

char** get_rom_file_names(int * length) {
    char** result = NULL;
    SceUID directory_file_descriptor;
    SceIoDirent entry;
    int files_found = 0;

    // Count files in directory
    directory_file_descriptor = sceIoDopen(ROMS_DIRECTORY);
    if (directory_file_descriptor > 0) {
        while(sceIoDread(directory_file_descriptor, &entry) > 0) {
            if (is_gameboy_rom(entry.d_name) > 0)
                files_found++;
        }
    } else {
        pspDebugScreenPrintf("Could not find any files \n");
        return NULL;
    }

    *length = files_found;
    result = (char **) malloc(sizeof(char*) * *length);
    directory_file_descriptor = sceIoDopen(ROMS_DIRECTORY);
    files_found = 0;
    while(sceIoDread(directory_file_descriptor, &entry) > 0) {
        if (is_gameboy_rom(entry.d_name) > 0) {
            result[files_found] = (char *) malloc(sizeof(char) * MAX_FILE_NAME_LENGTH);
            memset(result[files_found], '\0', sizeof(char) * MAX_FILE_NAME_LENGTH); // Empty string of bullshit
            strcpy(result[files_found], entry.d_name);
            files_found++;
        }
    }

    *length = files_found;

    return result;
}

void draw_menu(int selection, char** rom_file_names, int rom_file_amount) {
    int i = 0;

    pspDebugScreenClear();

    //Draw top bar
    pspDebugScreenSetTextColor(0xFFFFFF);
    pspDebugScreenPrintf("PSPeanut-GB\n\n");

    if (rom_file_amount == 0){
        pspDebugScreenPrintf("No roms found\n");
    }

    for(i = 0; i < rom_file_amount; i++) {
        if (i == selection){
            pspDebugScreenSetTextColor(0x0000FF);
        } else {
            pspDebugScreenSetTextColor(0x00FFFF);
        }
        pspDebugScreenPrintf("%s\n", rom_file_names[i]);
    }

    pspDebugScreenSetTextColor(0xFFFFFF);
    pspDebugScreenPrintf("\nRom files found: %i\n", rom_file_amount);
}

int main(void)
{
    int selection = 0;
    int selection_done = 0;
    int exit = 0;
    SceCtrlLatch pad;
    char** rom_file_names;
    int rom_file_amount;
    static struct gb_s gb;
    static struct priv_t priv;
    enum gb_init_error_e ret;

    pspDebugScreenInit();

    // Setup controls
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    rom_file_names = get_rom_file_names(&rom_file_amount);
    
    draw_menu(selection, rom_file_names, rom_file_amount);

    while(!selection_done) {
        sceCtrlReadLatch(&pad);
        if (pad.uiMake & PSP_CTRL_START) {
            selection_done = 1;
            exit = 1;
        }
        if (pad.uiMake & PSP_CTRL_CROSS) {
            selection_done = 1;
        }
        if (pad.uiMake & PSP_CTRL_DOWN) {
            selection++;
            if (selection >= rom_file_amount)
                selection = 0;
            draw_menu(selection, rom_file_names, rom_file_amount);
        }
        if (pad.uiMake & PSP_CTRL_UP) {
            selection--;
            if (selection < 0)
                selection = rom_file_amount - 1;
            draw_menu(selection, rom_file_names, rom_file_amount);
        }
    }

    if (exit == 0) {
        pspDebugScreenClear();
        pspDebugScreenPrintf("Loading %s...\n", rom_file_names[selection]);

        if((priv.rom = read_rom_to_ram(rom_file_names[selection])) == NULL) {
            pspDebugScreenPrintf("Failed, press X to exit.\n");
            while(!exit) {
                sceCtrlReadLatch(&pad);
                if (pad.uiMake & PSP_CTRL_CROSS) {
                    return 1;
                }
            }
        }
        pspDebugScreenPrintf("Success!\n");

        for(int i = 0; i < rom_file_amount; i++) {
            free(rom_file_names[i]);
        }
        free(rom_file_names);


        ret = gb_init(&gb, &gb_rom_read, &gb_cart_ram_read,
		      &gb_cart_ram_write, &gb_error, &priv);
        if(ret != GB_INIT_NO_ERROR)
        {
            pspDebugScreenPrintf("Error: %d\nPress x to exit.", ret);
            while(!exit) {
                sceCtrlReadLatch(&pad);
                if (pad.uiMake & PSP_CTRL_CROSS) {
                    return 1;
                }
            }
        }

        priv.cart_ram = (uint8_t *) malloc(gb_get_save_size(&gb));

        gb_init_lcd(&gb, &lcd_draw_line);

        fbp0 = getStaticVramBuffer(PSP_FRAME_BUFFER_WIDTH, PSP_SCREEN_HEIGHT, GU_PSM_8888);
        fbp1 = getStaticVramBuffer(PSP_FRAME_BUFFER_WIDTH, PSP_SCREEN_HEIGHT, GU_PSM_8888);

        gb_texture.width = LCD_WIDTH;
        gb_texture.height = LCD_HEIGHT;
        gb_texture.pH = 256;
        gb_texture.pW = 256;
        gb_texture.size = gb_texture.pH * gb_texture.pW * PIXEL_SIZE;
        gb_texture.data = getStaticVramTexture(gb_texture.pW, gb_texture.pH, GU_PSM_T8);
        TextureVertex tverts[4] = {
            {0.0f, 0.0f, 0xFFFFFFFF, (PSP_SCREEN_WIDTH - LCD_WIDTH) / 2.0f, (PSP_SCREEN_HEIGHT - LCD_HEIGHT) / 2.0f, 0.0f},
            {(float) gb_texture.width, (float) gb_texture.height, 0xFFFFFFFF, (float) gb_texture.width + ((PSP_SCREEN_WIDTH - LCD_WIDTH) / 2.0f), (float) gb_texture.height + ((PSP_SCREEN_HEIGHT - LCD_HEIGHT) / 2.0f), 0.0f},
        };

        memset(gb_texture.data, 0xFF00FF00, gb_texture.pW * gb_texture.pH);

        // This needs to be 32 entries, but we only need 4
        uint32_t __attribute__((aligned(16))) palette[32];
        palette[0] = 0xFFFFFFFF;
        palette[1] = 0xFFA5A5A5;
        palette[2] = 0xFF525252;
        palette[3] = 0xFF000000;

        sceGuInit();

        /* setup GU */
        sceGuStart(GU_DIRECT, list);
        sceGuDrawBuffer(PIXEL_FORMAT, fbp0, PSP_FRAME_BUFFER_WIDTH);
        sceGuDispBuffer(PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT, fbp1, PSP_FRAME_BUFFER_WIDTH);
        sceGuDepthBuffer(fbp0, 0); // Set the depth buffer to the same space as the framebuffer

        sceGuOffset(2048 - (PSP_SCREEN_WIDTH >> 1), 2048 - (PSP_SCREEN_HEIGHT >> 1));
        sceGuViewport(2048, 2048, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT);

        sceGuDisable(GU_DEPTH_TEST);

        /* Scissoring */
        sceGuScissor(0, 0, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT);
        sceGuEnable(GU_SCISSOR_TEST);

        sceGuClear(GU_COLOR_BUFFER_BIT);
        sceGuFinish();
        sceGuDisplay(GU_TRUE);

        while(!exit) {
            sceCtrlReadLatch(&pad);
            sceGuStart(GU_DIRECT, list);

            gb_run_frame(&gb);
            sceKernelDcacheWritebackRange(gb_texture.data, gb_texture.size);

            sceGuClutMode(GU_PSM_8888, 0, 3, 0);
	        sceGuClutLoad(1, palette);
            sceGuTexMode(GU_PSM_T8, 0, 0, GU_FALSE);
            sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
            sceGuTexImage(0, gb_texture.pW, gb_texture.pH, gb_texture.pW, gb_texture.data);

            sceGuEnable(GU_TEXTURE_2D);            
            sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_TEXTURE_32BITF| GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, tverts);
            sceGuDisable(GU_TEXTURE_2D);

            sceGuFinish();
            sceGuSync(0, 0);
            sceGuSwapBuffers();

            // Exit button is triangle
            if (pad.uiMake & PSP_CTRL_TRIANGLE) {
                exit = 1;
            }

            // Button releases
            if (pad.uiBreak & PSP_CTRL_CROSS) {
                gb.direct.joypad |= JOYPAD_A;
            }
            if (pad.uiBreak & PSP_CTRL_CIRCLE) {
                gb.direct.joypad |= JOYPAD_B;
            }
            if (pad.uiBreak & PSP_CTRL_SQUARE) {
                gb.direct.joypad |= JOYPAD_B;
            }
            if (pad.uiBreak & PSP_CTRL_START) {
                gb.direct.joypad |= JOYPAD_START;
            }
            if (pad.uiBreak & PSP_CTRL_SELECT) {
                gb.direct.joypad |= JOYPAD_SELECT;
            }
            if (pad.uiBreak & PSP_CTRL_UP) {
                gb.direct.joypad |= JOYPAD_UP;
            }
            if (pad.uiBreak & PSP_CTRL_DOWN) {
                gb.direct.joypad |= JOYPAD_DOWN;
            }
            if (pad.uiBreak & PSP_CTRL_LEFT) {
                gb.direct.joypad |= JOYPAD_LEFT;
            }
            if (pad.uiBreak & PSP_CTRL_RIGHT) {
                gb.direct.joypad |= JOYPAD_RIGHT;
            }

            // Button pressed
            if (pad.uiMake & PSP_CTRL_CROSS) {
                gb.direct.joypad &= ~JOYPAD_A;
            }
            if (pad.uiMake & PSP_CTRL_CIRCLE) {
                gb.direct.joypad &= ~JOYPAD_B;
            }
            if (pad.uiMake & PSP_CTRL_SQUARE) {
                gb.direct.joypad &= ~JOYPAD_B;
            }
            if (pad.uiMake & PSP_CTRL_START) {
                gb.direct.joypad &= ~JOYPAD_START;
            }
            if (pad.uiMake & PSP_CTRL_SELECT) {
                gb.direct.joypad &= ~JOYPAD_SELECT;
            }
            if (pad.uiMake & PSP_CTRL_UP) {
                gb.direct.joypad &= ~JOYPAD_UP;
            }
            if (pad.uiMake & PSP_CTRL_DOWN) {
                gb.direct.joypad &= ~JOYPAD_DOWN;
            }
            if (pad.uiMake & PSP_CTRL_LEFT) {
                gb.direct.joypad &= ~JOYPAD_LEFT;
            }
            if (pad.uiMake & PSP_CTRL_RIGHT) {
                gb.direct.joypad &= ~JOYPAD_RIGHT;
            }
        }
    }

    sceGuDisplay(GU_FALSE);
    sceGuTerm();
    vfree(fbp0);
    vfree(fbp1);
    vfree(gb_texture.data);

	free(priv.cart_ram);
	free(priv.rom);

    return 0;
}