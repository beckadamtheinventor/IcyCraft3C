/*
 *--------------------------------------
 * Program Name: Ic3Craft
 * Author: beckadamtheinventor
 * License: GPLv3
 * Description: A Minecraft lookalike for the TI-84+CE graphing calculator
 *--------------------------------------
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tice.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define usb_callback_data_t usb_device_t

#include <graphx.h>
#include <fileioc.h>
#include <keypadc.h>
#include <usbdrvce.h>
#include <srldrvce.h>

const char icy_HexC[16] = "0123456789ABCDEF";

int icy_LoadSRL(void);
void icy_DrawTiles(void);
void drawChunk(int x, int y, uint8_t xo, uint8_t yo);
void icy_DrawHotbar(void);
void icy_DrawCoords(void);
void icy_DrawInventory(uint8_t *inventory);
void icy_RequestChunk(int x, int y, uint8_t z);
uint8_t *icy_FindChunk(uint8_t x,uint8_t y, uint8_t z);
uint8_t *getChunk(int x, int y, uint8_t z);
uint8_t *generateChunk(uint8_t x, uint8_t y, uint8_t z);
void icy_LoadChunkLayer(uint8_t z);
void icy_SaveGame(void);
void icy_WriteNewChunk(uint8_t *chunk);
int icy_Archive(ti_var_t fp);
int icy_USBSetup(void);
int icy_GFXSetup(void);

typedef struct __icy_player_t{
	int x,y,z;
	uint8_t health,hunger,armour;
	uint8_t inventory[72];
}icy_player_t;

icy_player_t icy_Player;

#define flag_ExternalWorld  0x80

uint8_t *icy_ChunkData;
void *icy_ChunkDataEnd;
gfx_sprite_t **icy_Textures;
int icy_Flags = flag_ExternalWorld;

#define icy_MapWidth        14
#define icy_MapHeight       14

#define icy_CoordsDisplayX  (icy_MapWidth<<4)+1
#define icy_CoordsDisplayY  1

#define skyColor            0x1E

const char icy_ChunkTempFile[8] = "__icy__A";
char icy_TextureFile[8] = "ICYtxp00";
char icy_ChunkFile[8] = "ICYmap00";

/* Handle USB events */
static usb_error_t handle_usb_event(usb_event_t event, void *event_data,
								  usb_callback_data_t *callback_data) {
	/* When a device is connected, or when connected to a computer */
	if(event == USB_DEVICE_CONNECTED_EVENT || event == USB_HOST_CONFIGURE_EVENT) {
		if(!*callback_data) {
			/* Set the USB device */
			*callback_data = event_data;
		}
	}

	/* When a device is disconnected */
	if(event == USB_DEVICE_DISCONNECTED_EVENT) {
		*callback_data = NULL;
	}

	return USB_SUCCESS;
}

#define text_bgc 0
#define text_tac 255
#define text_tbc 5
#define text_tcc 0xC0

usb_device_t usb_device;
srl_device_t srl_dev;

usb_error_t usb_error;
srl_error_t srl_error;
ti_var_t fp;

/* A buffer for internal use by the serial library */
uint8_t srl_buf[512];
uint8_t icy_PacketBuffer[512];

void main(void) {

	int i;

	gfx_Begin();
	ti_CloseAll();
	
	if (icy_GFXSetup()){
		goto exit;
	}

	if (icy_USBSetup()==-1){
		goto exit;
	}

	icy_Player.x = 0;
	icy_Player.y = 0;
	icy_Player.z = 64;

	icy_LoadChunkLayer(icy_Player.z);

	do {

		gfx_ZeroScreen();
		gfx_SetColor(skyColor);
		gfx_FillRectangle(0,0,(icy_MapWidth<<4),(icy_MapHeight<<4));
		icy_DrawTiles();
		icy_DrawHotbar();
		icy_DrawCoords();
		gfx_SwapDraw();

		usb_HandleEvents();
		if (srl_Read(&srl_dev,&icy_PacketBuffer,512)){
			if (!memcmp(&icy_PacketBuffer,"IRECV ",6)) {
				if (!memcmp(&icy_PacketBuffer+6,"CHUNK ",6)){
					int x,y;
					uint8_t z;
					uint8_t *chunk;
					x=(int)*icy_PacketBuffer+12;
					y=(int)*icy_PacketBuffer+15;
					z=(uint8_t)*icy_PacketBuffer+18;
					if (chunk=icy_FindChunk(x,y,z)){
						memcpy(chunk+2,&icy_PacketBuffer+21,64);
					} else {
						icy_WriteNewChunk(&icy_PacketBuffer+19);
					}
				}
			}
		}

		kb_Scan();
		if (kb_IsDown(kb_KeyLeft)){
			icy_Player.x--;
		}
		if (kb_IsDown(kb_KeyRight)){
			icy_Player.x++;
		}
		if (kb_IsDown(kb_KeyUp)){
			icy_Player.y--;
		}
		if (kb_IsDown(kb_KeyDown)){
			icy_Player.y++;
		}
	} while(!kb_IsDown(kb_KeyClear));

	icy_SaveGame();
	ti_Delete(icy_ChunkTempFile);

	exit:;
    usb_Cleanup();
	ti_CloseAll();
	gfx_End();
}

void icy_DrawTiles(void){
	int x,y;

	x = (icy_Player.x-(icy_MapWidth>>1))>>3;
	y = (icy_Player.y-(icy_MapHeight>>1))>>3;
	drawChunk(x,y,0,0);
	drawChunk(x+1,y,8,0);
	drawChunk(x,y+1,0,8);
	drawChunk(x+1,y+1,8,8);
}
void drawChunk(int x, int y, uint8_t xo, uint8_t yo){
	unsigned int draw_x;
	uint8_t *chunk;
	uint8_t tile_x,tile_y,draw_y,tile;

	draw_x = ((icy_Player.x&7 - 7)<<3) + xo;
	draw_y = ((icy_Player.y&7 - 7)<<3) + yo;
	chunk = getChunk(x,y,icy_Player.z);
	for (tile_y=0;tile_y<8;tile_y++){
		for (tile_x=0;tile_x<8;tile_x++){
			tile=chunk[tile_x&7 + (tile_y&7)<<3];
			if (tile && tile<192 && draw_x<(icy_MapWidth<<4) && draw_y<(icy_MapHeight<<4)){
				gfx_ScaledSprite_NoClip(icy_Textures[tile],draw_x,draw_y,2,2);
			}
			draw_x+=16;
		}
		draw_y+=16;
		draw_x = 0;
	}
}
void icy_DrawHotbar(void){
	
}
void icy_DrawCoords(void){
	gfx_SetTextFGColor(text_tac);
	gfx_PrintStringXY("x:",icy_CoordsDisplayX,icy_CoordsDisplayY);
	gfx_PrintInt(icy_Player.x,7);
	gfx_PrintStringXY("y:",icy_CoordsDisplayX,icy_CoordsDisplayY+9);
	gfx_PrintInt(icy_Player.y,7);
	gfx_PrintStringXY("z:",icy_CoordsDisplayX,icy_CoordsDisplayY+18);
	gfx_PrintInt(icy_Player.z,3);
}
void icy_DrawInventory(uint8_t *inventory){
	
}
uint8_t *getChunk(int x, int y, uint8_t z){
	uint8_t *chunk;
	if (chunk=icy_FindChunk(x>>3,y>>3,z)){
		return chunk;
	} else {
		if (icy_Flags & flag_ExternalWorld){
			icy_RequestChunk(x>>3,y>>3,z);
		}
		return generateChunk(x>>3,y>>3,z);
	}
}
uint8_t *generateChunk(uint8_t x, uint8_t y, uint8_t z){
	return (uint8_t*)0;
}

void icy_RequestChunk(int x, int y, uint8_t z){
	uint8_t *req = "IREQ CHUNK xxxyyyz";
	memcpy(req+11,&x,3);
	memcpy(req+14,&y,3);
	req[17] = z;
	srl_Write(&srl_dev,req,18);
}
uint8_t *icy_FindChunk(uint8_t x,uint8_t y,uint8_t z){
	uint8_t *ptr;

	if (z!=icy_Player.z || !icy_ChunkData){
		icy_LoadChunkLayer(z);
		icy_Player.z = z;
	}

	ptr=icy_ChunkData;
	while (ptr < icy_ChunkDataEnd) {
		if (ptr[0]==x && ptr[1]==y){
			return ptr+2;
		}
		ptr+=66;
	};
	return (uint8_t*)0;
}

void icy_LoadChunkLayer(uint8_t z){
	ti_var_t fp;
	void *ptr;
	int len;

	icy_ChunkFile[6] = icy_HexC[z>>4];
	icy_ChunkFile[7] = icy_HexC[z&0xF];

	if (fp = ti_Open(icy_ChunkFile,"r")){
		ptr = ti_GetDataPtr(fp);
		len = ti_GetSize(fp);
		ti_Close(fp);
	} else {
		return;
	}

	fp = ti_Open(icy_ChunkTempFile,"w");
	if (len) ti_Write(ptr,len,1,fp);
	ti_Rewind(fp);
	icy_ChunkData = ti_GetDataPtr(fp) + 1;
	icy_ChunkDataEnd = icy_ChunkData + ti_GetSize(fp) - 1;
	ti_Close(fp);
}

void icy_SaveGame(void){
	void *ptr;
	int len;
	if (icy_Flags & flag_ExternalWorld){
		char *req = "IREQ SAVE";
		srl_Write(&srl_dev,req,10);
	} else {
		ti_var_t fp;
		if (fp=ti_Open(icy_ChunkTempFile,"r")){
			ptr=ti_GetDataPtr(fp);
			len=ti_GetSize(fp);
			ti_Close(fp);
			fp=ti_Open(icy_ChunkFile,"w");
			ti_Write(ptr,len,1,fp);
			icy_Archive(fp);
			ti_Close(fp);
		}
	}
}

int icy_LoadSRL(void){
	/* Wait for a USB device to be connected */
	while(!usb_device) {
		kb_Scan();

		/* Exit if clear is pressed */
		if(kb_IsDown(kb_KeyClear)) {
			return 1;
		}
		/* Handle any USB events that have occured */
		usb_HandleEvents();
	}

	/* Initialize the serial library with the USB device */
	if (srl_error = srl_Init(&srl_dev, usb_device, srl_buf, sizeof(srl_buf), SRL_INTERFACE_ANY)){
		return 1;
	}
	return 0;
}
void icy_WriteNewChunk(uint8_t *chunk){
	ti_var_t fp;

	if (!(fp=ti_Open(icy_ChunkTempFile,"a+"))){
		fp = ti_Open(icy_ChunkTempFile,"w");
		ti_PutC(0,fp);
	}
	ti_Write(chunk,66,1,fp);
	ti_Close(fp);
}
int icy_Archive(ti_var_t fp){
	if (ti_ArchiveHasRoom(ti_GetSize(fp))){
		ti_SetArchiveStatus(1,fp);
		return 0;
	} else {
		usb_Cleanup();
		gfx_End();
		ti_SetArchiveStatus(1,fp);
		gfx_Begin();
		ti_CloseAll();
		return icy_USBSetup();
	}
}
int icy_GFXSetup(void){
	gfx_SetDraw(1);
	gfx_SetTextTransparentColor(text_bgc);
	gfx_SetTextBGColor(text_bgc);
	if (fp=ti_Open(icy_TextureFile,"r")){
		void *ptr;
		int i;
		icy_Textures = (gfx_sprite_t**)malloc(768);
		ptr=ti_GetDataPtr(fp);
		for (i=0;i<256;i++){
			icy_Textures[i]=(gfx_sprite_t*)ptr;
			ptr+=66;
		}
		ti_Close(fp);
	} else {
		return 1;
	}
	return 0;
}

int icy_USBSetup(void){
	usb_device = NULL;

	/* Initialize the USB driver with our event handler and the serial device descriptors */
	usb_error = usb_Init(handle_usb_event, &usb_device, srl_GetCDCStandardDescriptors(), USB_DEFAULT_INIT_FLAGS);
	if(usb_error) return -1;
	
	if (icy_Flags & flag_ExternalWorld) {
		gfx_ZeroScreen();
		gfx_SetTextFGColor(text_tac);

		gfx_PrintStringXY("Waiting for USB connection to PC",1,1);

		gfx_SwapDraw();

		if (icy_LoadSRL()){
			icy_Flags ^= flag_ExternalWorld;
			gfx_Blit(0);
			gfx_PrintStringXY("Defaulting to local worlds.",1,10);
			gfx_SwapDraw();
			while (!kb_AnyKey());
			return 1;
		}
	}
	return 0;
}
