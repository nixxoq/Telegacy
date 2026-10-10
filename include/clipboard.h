/*
Copyright © 2026 N3xtery, nixxoq

This file is part of Telegacy.

Telegacy is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Telegacy is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Telegacy. If not, see <https://www.gnu.org/licenses/>. 
*/

#ifndef TL_CLIPBOARD_H
#define TL_CLIPBOARD_H

#ifndef _WINDOWS_
#ifndef WINVER
#define WINVER 0x0500
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0400
#endif
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

	void Clipboard_Init(void);
	void Clipboard_Shutdown(void);
	BOOL Clipboard_OnPasteMessageInput(HWND hWndEdit);
	BOOL Clipboard_TrackTempFile(const wchar_t *filePath);
	void Clipboard_MarkTempFileSending(const wchar_t *filePath);
	void Clipboard_FinishAndRemoveTempFile(const wchar_t *filePath);
	void Clipboard_CleanupAttachedTempFiles(void);

#ifdef __cplusplus
}
#endif

#endif
