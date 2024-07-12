#include <pspdebug.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <pspdisplay.h>
#include <pspgu.h>
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

// Some global variables
uint32_t *doublebuffer = NULL;
uint32_t *backbuffer = NULL;
uint32_t *frontbuffer = NULL;


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
	const uint32_t palette[] = { 0xFFFFFFFF, 0xFFA5A5A5, 0xFF525252, 0xFF000000 };
    uint32_t result[LCD_WIDTH];
    unsigned int line_size = PIXEL_SIZE * LCD_WIDTH;

	for(unsigned int x = 0; x < LCD_WIDTH; x++) {
		result[x] = palette[pixels[x] & 3];
    }

    uint32_t* memory_address = frontbuffer + (PSP_FRAME_BUFFER_WIDTH * line) + RENDER_OFFSET_Y + RENDER_OFFSET_X;
    memcpy(memory_address, result, line_size);
    sceKernelDcacheWritebackRange(memory_address, line_size);
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
        
        doublebuffer = (uint32_t *) vramalloc(PSP_FRAME_BUFFER_SIZE * PIXEL_SIZE * 2);
        backbuffer = doublebuffer;
        frontbuffer = doublebuffer + (PSP_FRAME_BUFFER_SIZE * PIXEL_SIZE);

        sceGuInit();

        /* setup GU */
        sceGuStart(GU_DIRECT, list);
        sceGuDrawBuffer(PIXEL_FORMAT, vrelptr(frontbuffer), PSP_FRAME_BUFFER_WIDTH);
        sceGuDispBuffer(PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT, vrelptr(backbuffer), PSP_FRAME_BUFFER_WIDTH);
        sceGuDepthBuffer(vrelptr(backbuffer), 0); // Set the depth buffer to the same space as the framebuffer

        sceGuOffset(2048 - (PSP_SCREEN_WIDTH >> 1), 2048 - (PSP_SCREEN_HEIGHT >> 1));
        sceGuViewport(2048, 2048, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT);

        sceGuDisable(GU_DEPTH_TEST);

        /* Scissoring */
        sceGuScissor(0, 0, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT);
        sceGuEnable(GU_SCISSOR_TEST);

        // Clear the screen
        sceGuClearColor(0);
        sceGuClear(GU_COLOR_BUFFER_BIT);

        sceKernelDcacheWritebackAll();

        while(!exit) {
            gb_run_frame(&gb);
            // sceGuDrawBufferList(PIXEL_FORMAT, vrelptr(frontbuffer), PSP_FRAME_BUFFER_WIDTH);

            sceGuFinish();
            sceGuSync(0, 0);
            // sceDisplayWaitVblankStart();
            backbuffer = frontbuffer;
            frontbuffer = (uint32_t *) vabsptr(sceGuSwapBuffers());
            sceGuStart(GU_DIRECT, list);

            sceCtrlReadLatch(&pad);
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
    vfree(backbuffer);
    vfree(frontbuffer);

	free(priv.cart_ram);
	free(priv.rom);

    return 0;
}