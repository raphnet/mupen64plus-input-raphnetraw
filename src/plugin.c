/* mupen64plus-input-raphnetraw
 *
 * Copyright (C) 2016 Raphael Assenat
 *
 * An input plugin that lets the game under emulation communicate with
 * the controllers directly using the direct controller communication
 * feature of raphnet V3 adapters[1].
 *
 * [1] http://www.raphnet.net/electronique/gcn64_usb_adapter_gen3/index_en.php
 *
 * Based on the Mupen64plus-input-sdl plugin (original banner below)
 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-input-sdl - plugin.c                                      *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2008-2011 Richard Goedeken                              *
 *   Copyright (C) 2008 Tillin9                                            *
 *   Copyright (C) 2002 Blight                                             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define M64P_PLUGIN_PROTOTYPES 1
#include "config.h"
#include "m64p_common.h"
#include "m64p_config.h"
#include "m64p_plugin.h"
#include "m64p_types.h"
#include "osal_dynamiclib.h"
#include "plugin.h"
#include "version.h"

#include "gcn64.h"
#include "gcn64lib.h"

#ifdef __linux__
#endif /* __linux__ */

#include <errno.h>

/* definitions of pointers to Core config functions */
ptr_ConfigOpenSection      ConfigOpenSection = NULL;
ptr_ConfigDeleteSection    ConfigDeleteSection = NULL;
ptr_ConfigSaveSection      ConfigSaveSection = NULL;
ptr_ConfigListParameters   ConfigListParameters = NULL;
ptr_ConfigSaveFile         ConfigSaveFile = NULL;
ptr_ConfigSetParameter     ConfigSetParameter = NULL;
ptr_ConfigGetParameter     ConfigGetParameter = NULL;
ptr_ConfigGetParameterHelp ConfigGetParameterHelp = NULL;
ptr_ConfigSetDefaultInt    ConfigSetDefaultInt = NULL;
ptr_ConfigSetDefaultFloat  ConfigSetDefaultFloat = NULL;
ptr_ConfigSetDefaultBool   ConfigSetDefaultBool = NULL;
ptr_ConfigSetDefaultString ConfigSetDefaultString = NULL;
ptr_ConfigGetParamInt      ConfigGetParamInt = NULL;
ptr_ConfigGetParamFloat    ConfigGetParamFloat = NULL;
ptr_ConfigGetParamBool     ConfigGetParamBool = NULL;
ptr_ConfigGetParamString   ConfigGetParamString = NULL;

ptr_ConfigGetSharedDataFilepath ConfigGetSharedDataFilepath = NULL;
ptr_ConfigGetUserConfigPath     ConfigGetUserConfigPath = NULL;
ptr_ConfigGetUserDataPath       ConfigGetUserDataPath = NULL;
ptr_ConfigGetUserCachePath      ConfigGetUserCachePath = NULL;

/* static data definitions */
static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;
static int l_PluginInit = 0;
static gcn64_hdl_t gcn64_handle = NULL;

/* Global functions */
void DebugMessage(int level, const char *message, ...)
{
	char msgbuf[1024];
	va_list args;

	if (!l_DebugCallback) {
		printf("No debug callback!\n");
		return;
	}

	va_start(args, message);
	vsprintf(msgbuf, message, args);

	(*l_DebugCallback)(l_DebugCallContext, level, msgbuf);

	va_end(args);
}


/* Mupen64Plus plugin functions */
EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                   void (*DebugCallback)(void *, int, const char *))
{
    ptr_CoreGetAPIVersions CoreAPIVersionFunc;

    int ConfigAPIVersion, DebugAPIVersion, VidextAPIVersion;

    if (l_PluginInit)
        return M64ERR_ALREADY_INIT;

	l_DebugCallback = DebugCallback;
	l_DebugCallContext = Context;

    /* attach and call the CoreGetAPIVersions function, check Config API version for compatibility */
    CoreAPIVersionFunc = (ptr_CoreGetAPIVersions) osal_dynlib_getproc(CoreLibHandle, "CoreGetAPIVersions");
    if (CoreAPIVersionFunc == NULL)
    {
        DebugMessage(M64MSG_ERROR, "Core emulator broken; no CoreAPIVersionFunc() function found.");
        return M64ERR_INCOMPATIBLE;
    }
    
    (*CoreAPIVersionFunc)(&ConfigAPIVersion, &DebugAPIVersion, &VidextAPIVersion, NULL);
    if ((ConfigAPIVersion & 0xffff0000) != (CONFIG_API_VERSION & 0xffff0000) || ConfigAPIVersion < CONFIG_API_VERSION)
    {
        DebugMessage(M64MSG_ERROR, "Emulator core Config API (v%i.%i.%i) incompatible with plugin (v%i.%i.%i)",
                VERSION_PRINTF_SPLIT(ConfigAPIVersion), VERSION_PRINTF_SPLIT(CONFIG_API_VERSION));
        return M64ERR_INCOMPATIBLE;
    }

    /* Get the core config function pointers from the library handle */
    ConfigOpenSection = (ptr_ConfigOpenSection) osal_dynlib_getproc(CoreLibHandle, "ConfigOpenSection");
    ConfigDeleteSection = (ptr_ConfigDeleteSection) osal_dynlib_getproc(CoreLibHandle, "ConfigDeleteSection");
    ConfigSaveFile = (ptr_ConfigSaveFile) osal_dynlib_getproc(CoreLibHandle, "ConfigSaveFile");
    ConfigSaveSection = (ptr_ConfigSaveSection) osal_dynlib_getproc(CoreLibHandle, "ConfigSaveSection");
    ConfigListParameters = (ptr_ConfigListParameters) osal_dynlib_getproc(CoreLibHandle, "ConfigListParameters");
    ConfigSetParameter = (ptr_ConfigSetParameter) osal_dynlib_getproc(CoreLibHandle, "ConfigSetParameter");
    ConfigGetParameter = (ptr_ConfigGetParameter) osal_dynlib_getproc(CoreLibHandle, "ConfigGetParameter");
    ConfigSetDefaultInt = (ptr_ConfigSetDefaultInt) osal_dynlib_getproc(CoreLibHandle, "ConfigSetDefaultInt");
    ConfigSetDefaultFloat = (ptr_ConfigSetDefaultFloat) osal_dynlib_getproc(CoreLibHandle, "ConfigSetDefaultFloat");
    ConfigSetDefaultBool = (ptr_ConfigSetDefaultBool) osal_dynlib_getproc(CoreLibHandle, "ConfigSetDefaultBool");
    ConfigSetDefaultString = (ptr_ConfigSetDefaultString) osal_dynlib_getproc(CoreLibHandle, "ConfigSetDefaultString");
    ConfigGetParamInt = (ptr_ConfigGetParamInt) osal_dynlib_getproc(CoreLibHandle, "ConfigGetParamInt");
    ConfigGetParamFloat = (ptr_ConfigGetParamFloat) osal_dynlib_getproc(CoreLibHandle, "ConfigGetParamFloat");
    ConfigGetParamBool = (ptr_ConfigGetParamBool) osal_dynlib_getproc(CoreLibHandle, "ConfigGetParamBool");
    ConfigGetParamString = (ptr_ConfigGetParamString) osal_dynlib_getproc(CoreLibHandle, "ConfigGetParamString");

    ConfigGetSharedDataFilepath = (ptr_ConfigGetSharedDataFilepath) osal_dynlib_getproc(CoreLibHandle, "ConfigGetSharedDataFilepath");
    ConfigGetUserConfigPath = (ptr_ConfigGetUserConfigPath) osal_dynlib_getproc(CoreLibHandle, "ConfigGetUserConfigPath");
    ConfigGetUserDataPath = (ptr_ConfigGetUserDataPath) osal_dynlib_getproc(CoreLibHandle, "ConfigGetUserDataPath");
    ConfigGetUserCachePath = (ptr_ConfigGetUserCachePath) osal_dynlib_getproc(CoreLibHandle, "ConfigGetUserCachePath");

    if (!ConfigOpenSection || !ConfigDeleteSection || !ConfigSaveFile || !ConfigSaveSection || !ConfigSetParameter || !ConfigGetParameter ||
        !ConfigSetDefaultInt || !ConfigSetDefaultFloat || !ConfigSetDefaultBool || !ConfigSetDefaultString ||
        !ConfigGetParamInt   || !ConfigGetParamFloat   || !ConfigGetParamBool   || !ConfigGetParamString ||
        !ConfigGetSharedDataFilepath || !ConfigGetUserConfigPath || !ConfigGetUserDataPath || !ConfigGetUserCachePath)
    {
        DebugMessage(M64MSG_ERROR, "Couldn't connect to Core configuration functions");
        return M64ERR_INCOMPATIBLE;
    }

	gcn64_init(1);

    l_PluginInit = 1;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    if (!l_PluginInit)
        return M64ERR_NOT_INIT;

	/* reset some local variables */
	l_DebugCallback = NULL;
	l_DebugCallContext = NULL;

	if (gcn64_handle) {
		gcn64_closeDevice(gcn64_handle);
	}

	gcn64_shutdown();

    l_PluginInit = 0;

    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    /* set version info */
    if (PluginType != NULL)
        *PluginType = M64PLUGIN_INPUT;

    if (PluginVersion != NULL)
        *PluginVersion = PLUGIN_VERSION;

    if (APIVersion != NULL)
        *APIVersion = INPUT_PLUGIN_API_VERSION;

    if (PluginNamePtr != NULL)
        *PluginNamePtr = PLUGIN_NAME;

    if (Capabilities != NULL)
    {
        *Capabilities = 0;
    }

    return M64ERR_SUCCESS;
}

/******************************************************************
  Function: InitiateControllers
  Purpose:  This function initialises how each of the controllers
            should be handled.
  input:    - The handle to the main window.
            - A controller structure that needs to be filled for
              the emulator to know how to handle each controller.
  output:   none
*******************************************************************/
EXPORT void CALL InitiateControllers(CONTROL_INFO ControlInfo)
{
    int i;
	struct gcn64_list_ctx * lctx;
	struct gcn64_info inf;

	lctx = gcn64_allocListCtx();
	if (!lctx) {
		DebugMessage(M64MSG_ERROR, "Could not allocate gcn64 list context\n");
		return;
	}

	/* This uses the first device we are able to open. TODO : Configurable per serial */
	while (gcn64_listDevices(&inf, lctx)) {
		gcn64_handle = gcn64_openDevice(&inf);
		if (!gcn64_handle) {
			DebugMessage(M64MSG_ERROR, "Could not open gcn64 device\n");
		}

		gcn64lib_suspendPolling(gcn64_handle, 1);

		DebugMessage(M64MSG_INFO, "Using USB device 0x%04x:0x%04x serial '%ls' name '%ls'",
						inf.usb_vid, inf.usb_pid, inf.str_serial, inf.str_prodname);

		break;
	}
	gcn64_freeListCtx(lctx);

	if (gcn64_handle) {
		for (i=0; i<4; i++) {
			ControlInfo.Controls[i].RawData = 1;

			/* Setting this is currently required or we
			 * won't be called at all.
			 *
			 * Look at pif.c update_pif_write() to see why.
			 */
			ControlInfo.Controls[i].Present = 1;
		}
	}

    DebugMessage(M64MSG_INFO, "%s version %i.%i.%i initialized.", PLUGIN_NAME, VERSION_PRINTF_SPLIT(PLUGIN_VERSION));
}

#ifdef PLUGINDBG
static void debug_raw_commands(unsigned char *command)
{
	int tx_len = command[0];
	int rx_len = command[1];
	int i;

	printf("tx[%d] = {", tx_len);
	for (i=0; i<tx_len; i++) {
		printf("0x%02x ", command[2+i]);
	}
	printf("}, expecting %d byte%c\n", rx_len, rx_len > 1 ? 's':' ');

}
#endif

/******************************************************************
  Function: ReadController
  Purpose:  To process the raw data in the pif ram that is about to
            be read.
  input:    - Controller Number (0 to 3) and -1 signalling end of
              processing the pif ram.
            - Pointer of data to be processed.
  output:   none
  note:     This function is only needed if the DLL is allowing raw
            data.
*******************************************************************/
EXPORT void CALL ReadController(int Control, unsigned char *Command)
{
	if (Control != -1 && Command) {

		// Command structure
		// based on LaC's n64 hardware dox... v0.8 (unofficial release)
		//
		// Byte 0: nBytes to transmit
		// Byte 1: nBytes to receive
		// Byte 2: TX data start (first byte is the command)
		// Byte 2+[tx_len]: RX data start
		//
		// Request info (status):
		//
		// tx[1] = {0x00 }, expecting 3 bytes
		//
		// Get button status:
		//
		// tx[1] = {0x01 }, expecting 4 bytes
		//
		// Rumble commands: (Super smash bros)
		//
		// tx[35] = {0x03 0x80 0x01 0xfe 0xfe 0xfe 0xfe 0xfe 0xfe 0xfe 0xfe 0xfe
		// 			0xfe 0xfe 0xfe 0xfe 0xfe 0xfe 0xfe 0xfe 0xfe 0xfe 0xfe 0xfe 0xfe
		// 			0xfe 0xfe 0xfe 0xfe 0xfe 0xfe 0xfe 0xfe 0xfe 0xfe }, expecting 1 byte
		//
		// tx[3] = {0x02 0x80 0x01 }, expecting 33 bytes
		//
		// Memory card commands: (Mario Kart 64)
		//
		// tx[3] = {0x02 0x00 0x35 }, expecting 33 bytes
		// tx[35] = {0x03 0x02 0x19 0x00 0x71 0xbf 0x95 0xf5 0xfb 0xd7 0xc1 0x00 0x00
		// 			0x00 0x03 0x00 0x03 0x00 0x03 0x00 0x03 0x00 0x03 0x00 0x03 0x00
		// 			0x03 0x00 0x03 0x00 0x03 0x00 0x03 0x00 0x03 }, expecting 1 byte
		if (gcn64_handle) {
			int tx_len = Command[0];
			int rx_len = Command[1];
			int res;

#ifdef PLUGINDBG
			if (Control == 0) {
				debug_raw_commands(Command);
			}
#endif

			res = gcn64lib_rawSiCommand(gcn64_handle,
									Control,		// Channel
									Command + 2,	// TX data
									tx_len,		// TX data len
									Command + 2 + tx_len,	// RX data
									rx_len		// TX data len
								);
			if (res <= 0) {
				//
				// 0x00 - no error, operation successful.
				// 0x80 - error, device not present for specified command.
				// 0x40 - error, unable to send/recieve the number bytes for command type.
				//
				// Diagrams 1.2-1.4 shows that lower bits are preserved (i.e. The length
				// byte is not completely replaced).
				Command[1] &= 0xC0;
				Command[1] |= 0x80;
			}
		}
	}
}

/* Unused by this plugin, but required. */
EXPORT void CALL ControllerCommand(int Control, unsigned char *Command) { }
EXPORT void CALL GetKeys( int Control, BUTTONS *Keys ) { }

EXPORT void CALL RomClosed(void)
{
	if (gcn64_handle) {
		gcn64lib_suspendPolling(gcn64_handle, 0);
	}
}

EXPORT int CALL RomOpen(void)
{
	if (gcn64_handle) {
		gcn64lib_suspendPolling(gcn64_handle, 1);
	}
	return 1;
}

/******************************************************************
  Function: SDL_KeyDown
  Purpose:  To pass the SDL_KeyDown message from the emulator to the
            plugin.
  input:    keymod and keysym of the SDL_KEYDOWN message.
  output:   none
*******************************************************************/
EXPORT void CALL SDL_KeyDown(int keymod, int keysym)
{
}

/******************************************************************
  Function: SDL_KeyUp
  Purpose:  To pass the SDL_KeyUp message from the emulator to the
            plugin.
  input:    keymod and keysym of the SDL_KEYUP message.
  output:   none
*******************************************************************/
EXPORT void CALL SDL_KeyUp(int keymod, int keysym)
{
}
