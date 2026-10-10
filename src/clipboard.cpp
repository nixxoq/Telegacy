/*
Copyright © 2026 N3xtery, nixxoq

This file is part of Telegacy.

Telegacy is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Telegacy is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Telegacy. If not, see <https://www.gnu.org/licenses/>.
*/

#include "telegacy.h"
#include "clipboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#define CLIP_MAX_DIRECT_PASTE (20 * 1024 * 1024)
#define CLIP_MAX_DIMENSION 32768
#define TEMP_STATE_ATTACHED 0
#define TEMP_STATE_SENDING 1

struct TrackedTempFile
{
	wchar_t path[MAX_PATH];
	int state;
};

struct ClipboardDIB
{
	HGLOBAL hClipHandle;
	HGLOBAL hAllocHandle;
	const BYTE *pData;
	DWORD totalSize;
	DWORD offsetToBits;
};

static std::vector<TrackedTempFile> g_trackedTempFiles;
static void DeleteTempFileOnDisk(const wchar_t *path);
static CRITICAL_SECTION g_csTemp;
static LONG g_tempCounter = 0;

/* Calculates DIB byte offset to pixel array */
static DWORD DibPixelOffset(const BYTE *pDib, DWORD dibSize)
{
	if (!pDib || dibSize < sizeof(BITMAPINFOHEADER))
		return 0;

	const BITMAPINFOHEADER *bih = (const BITMAPINFOHEADER *)pDib;
	DWORD headerSize = bih->biSize;
	LONG width = bih->biWidth;
	LONG height = bih->biHeight;
	WORD bpp = bih->biBitCount;
	DWORD comp = bih->biCompression;
	DWORD colors = bih->biClrUsed;

	if (headerSize < sizeof(BITMAPINFOHEADER) || headerSize > dibSize ||
		width <= 0 || width > CLIP_MAX_DIMENSION || height == 0 ||
		height < -CLIP_MAX_DIMENSION || height > CLIP_MAX_DIMENSION || bih->biPlanes != 1)
		return 0;

	if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
		return 0;
	if (comp != BI_RGB && comp != BI_BITFIELDS)
		return 0;
	if (comp == BI_BITFIELDS && bpp != 16 && bpp != 32)
		return 0;

	DWORD colorEntries = 0;
	DWORD maskBytes = (comp == BI_BITFIELDS && headerSize == sizeof(BITMAPINFOHEADER)) ? 3 * sizeof(DWORD) : 0;
	if (bpp <= 8)
	{
		DWORD maxColors = 1UL << bpp;
		if (comp != BI_RGB || colors > maxColors)
			return 0;
		colorEntries = colors ? colors : maxColors;
	}
	else if (colors)
	{
		if (colors > 256)
			return 0;
		colorEntries = colors;
	}

	unsigned __int64 offset = (unsigned __int64)headerSize + maskBytes +
							  (unsigned __int64)colorEntries * sizeof(RGBQUAD);
	if (offset > dibSize)
		return 0;

	unsigned __int64 pitch = (((unsigned __int64)width * bpp + 31) & ~((unsigned __int64)31)) >> 3;
	unsigned __int64 pixels = pitch * (unsigned __int64)(height < 0 ? -height : height);
	if (pixels > dibSize - offset)
		return 0;

	return (DWORD)offset;
}

/* Converts HBITMAP to 24-bpp DIB. This exists because MS Paint uses CF_BITMAP directly */
static BOOL AcquireBitmapAsDIB(HBITMAP hBitmap, ClipboardDIB *out, DWORD maxBytes)
{
	BITMAP bm;
	if (!hBitmap || !out || !GetObject(hBitmap, sizeof(bm), &bm) ||
		bm.bmWidth <= 0 || bm.bmHeight <= 0 ||
		bm.bmWidth > CLIP_MAX_DIMENSION || bm.bmHeight > CLIP_MAX_DIMENSION)
		return FALSE;

	DWORD pitch = (((DWORD)bm.bmWidth * 24 + 31) & ~31UL) >> 3;
	unsigned __int64 imageSize64 = (unsigned __int64)pitch * bm.bmHeight;
	if (imageSize64 > maxBytes || imageSize64 + sizeof(BITMAPINFOHEADER) > maxBytes)
		return FALSE;

	DWORD imageSize = (DWORD)imageSize64;
	DWORD totalSize = sizeof(BITMAPINFOHEADER) + imageSize;
	HGLOBAL hMemory = GlobalAlloc(GMEM_MOVEABLE, totalSize);
	if (!hMemory)
		return FALSE;

	BYTE *pData = (BYTE *)GlobalLock(hMemory);
	if (!pData)
	{
		GlobalFree(hMemory);
		return FALSE;
	}

	BITMAPINFOHEADER *bih = (BITMAPINFOHEADER *)pData;
	memset(bih, 0, sizeof(*bih));
	bih->biSize = sizeof(*bih);
	bih->biWidth = bm.bmWidth;
	bih->biHeight = bm.bmHeight;
	bih->biPlanes = 1;
	bih->biBitCount = 24;
	bih->biCompression = BI_RGB;
	bih->biSizeImage = imageSize;

	HDC hdc = GetDC(NULL);
	if (!hdc)
	{
		GlobalUnlock(hMemory);
		GlobalFree(hMemory);
		return FALSE;
	}

	HPALETTE hPalette = (HPALETTE)GetClipboardData(CF_PALETTE);
	HPALETTE hOldPalette = hPalette ? SelectPalette(hdc, hPalette, FALSE) : NULL;
	if (hOldPalette)
		RealizePalette(hdc);

	int lines = GetDIBits(hdc, hBitmap, 0, bm.bmHeight,
						  pData + sizeof(BITMAPINFOHEADER), (BITMAPINFO *)bih, DIB_RGB_COLORS);

	if (hOldPalette)
		SelectPalette(hdc, hOldPalette, FALSE);
	ReleaseDC(NULL, hdc);

	if (lines <= 0)
	{
		GlobalUnlock(hMemory);
		GlobalFree(hMemory);
		return FALSE;
	}

	out->hAllocHandle = hMemory;
	out->pData = pData;
	out->totalSize = totalSize;
	out->offsetToBits = sizeof(BITMAPINFOHEADER);
	return TRUE;
}

static BOOL Clipboard_AcquireDIB(ClipboardDIB *out, DWORD maxBytes)
{
	if (!out)
		return FALSE;
	memset(out, 0, sizeof(*out));

	if (IsClipboardFormatAvailable(CF_DIB))
	{
		HGLOBAL hData = (HGLOBAL)GetClipboardData(CF_DIB);
		if (hData)
		{
			DWORD size = (DWORD)GlobalSize(hData);
			if (size && size <= maxBytes)
			{
				const BYTE *pData = (const BYTE *)GlobalLock(hData);
				if (pData)
				{
					DWORD offset = DibPixelOffset(pData, size);
					if (offset)
					{
						out->hClipHandle = hData;
						out->pData = pData;
						out->totalSize = size;
						out->offsetToBits = offset;
						return TRUE;
					}
					GlobalUnlock(hData);
				}
			}
		}
	}

	if (IsClipboardFormatAvailable(CF_BITMAP))
		return AcquireBitmapAsDIB((HBITMAP)GetClipboardData(CF_BITMAP), out, maxBytes);

	return FALSE;
}

static void Clipboard_ReleaseDIB(ClipboardDIB *dib)
{
	if (dib->hClipHandle)
		GlobalUnlock(dib->hClipHandle);
	if (dib->hAllocHandle)
	{
		GlobalUnlock(dib->hAllocHandle);
		GlobalFree(dib->hAllocHandle);
	}
	memset(dib, 0, sizeof(*dib));
}

static BOOL CreateTempBmpFile(const BYTE *pDib, DWORD dibSize, DWORD offsetToBits, wchar_t *outPath)
{
	if (!pDib || !dibSize || !offsetToBits || !outPath ||
		dibSize > CLIP_MAX_DIRECT_PASTE || offsetToBits > dibSize)
		return FALSE;

	wchar_t tempDir[MAX_PATH] = {0};
	char tempDirA[MAX_PATH];
	DWORD dirLen = GetTempPathA(MAX_PATH, tempDirA);
	if (dirLen && dirLen < MAX_PATH)
		MultiByteToWideChar(CP_ACP, 0, tempDirA, -1, tempDir, MAX_PATH);
	if (!tempDir[0])
		wcscpy(tempDir, appdata_path);

	size_t dirSize = wcslen(tempDir);
	if (!dirSize || dirSize > MAX_PATH - 64)
		return FALSE;
	if (tempDir[dirSize - 1] == L'\\' || tempDir[dirSize - 1] == L'/')
		tempDir[--dirSize] = 0;

	swprintf(outPath, L"%s\\tg_clip_%u_%u_%d.bmp", tempDir,
			 GetCurrentProcessId(), GetTickCount(), InterlockedIncrement(&g_tempCounter));

	BITMAPFILEHEADER bfh;
	memset(&bfh, 0, sizeof(bfh));
	bfh.bfType = 0x4D42;
	bfh.bfSize = sizeof(bfh) + dibSize;
	bfh.bfOffBits = sizeof(bfh) + offsetToBits;

	FILE *file = _wfopen(outPath, L"wb");
	if (!file)
	{
		char pathA[MAX_PATH];
		if (!WideCharToMultiByte(CP_ACP, 0, outPath, -1, pathA, MAX_PATH, NULL, NULL))
			return FALSE;
		file = fopen(pathA, "wb");
	}
	if (!file)
		return FALSE;

	BOOL ok = fwrite(&bfh, sizeof(bfh), 1, file) == 1 && fwrite(pDib, 1, dibSize, file) == dibSize;
	if (fclose(file) != 0)
		ok = FALSE;
	if (!ok)
	{
		DeleteFileW(outPath);
		char pathA[MAX_PATH];
		if (WideCharToMultiByte(CP_ACP, 0, outPath, -1, pathA, MAX_PATH, NULL, NULL))
			DeleteFileA(pathA);
	}
	return ok;
}

void Clipboard_Init(void)
{
	InitializeCriticalSection(&g_csTemp);
}

void Clipboard_Shutdown(void)
{
	Clipboard_CleanupAttachedTempFiles();
}

static BOOL Clipboard_PasteHDROP(HWND hWndOwner)
{
	if (!IsClipboardFormatAvailable(CF_HDROP))
		return FALSE;

	BOOL opened = FALSE;
	int retry;
	for (retry = 0; retry < 5; retry++)
	{
		if (OpenClipboard(hWndOwner))
		{
			opened = TRUE;
			break;
		}
		Sleep(10);
	}
	if (!opened)
		return FALSE;

	HDROP hDrop = (HDROP)GetClipboardData(CF_HDROP);
	if (!hDrop)
	{
		CloseClipboard();
		return FALSE;
	}

	UINT count = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);
	if (count == 0)
	{
		CloseClipboard();
		return FALSE;
	}

	std::vector<wchar_t*> newFiles;
	UINT idx;
	for (idx = 0; idx < count; idx++)
	{
		wchar_t path[MAX_PATH];
		path[0] = 0;
		UINT len = DragQueryFileW(hDrop, idx, path, MAX_PATH);
		if (len == 0)
		{
			char pathA[MAX_PATH];
			pathA[0] = 0;
			UINT lenA = DragQueryFileA(hDrop, idx, pathA, MAX_PATH);
			if (lenA > 0)
				MultiByteToWideChar(CP_ACP, 0, pathA, -1, path, MAX_PATH);
		}

		if (path[0] == 0)
			continue;

		DWORD attr = GetFileAttributesW(path);
		if (attr == 0xFFFFFFFF)
		{
			char pathA[MAX_PATH];
			if (WideCharToMultiByte(CP_ACP, 0, path, -1, pathA, MAX_PATH, NULL, NULL))
				attr = GetFileAttributesA(pathA);
		}

		/* Skip directories and nonexistent files */
		if (attr == 0xFFFFFFFF || (attr & FILE_ATTRIBUTE_DIRECTORY))
			continue;

		wchar_t *fileStr = _wcsdup(path);
		if (!fileStr)
			continue;

		try
		{
			newFiles.push_back(fileStr);
		}
		catch (...)
		{
			free(fileStr);
			break;
		}

		if (editing_msg_id)
			break;
	}

	CloseClipboard();

	if (newFiles.empty())
		return FALSE;

	EnterCriticalSection(&csFiles);
	if (editing_msg_id && !files.empty())
	{
		Clipboard_FinishAndRemoveTempFile(files[0]);
		free(files[0]);
		files.clear();
	}

	for (size_t k = 0; k < newFiles.size(); k++)
	{
		try
		{
			files.push_back(newFiles[k]);
		}
		catch (...)
		{
			free(newFiles[k]);
		}
	}
	LeaveCriticalSection(&csFiles);

	SendMessage(hToolbar, TB_CHANGEBITMAP, 4, MAKELPARAM(14, 0));
	open_files_list();
	return TRUE;
}

BOOL Clipboard_OnPasteMessageInput(HWND hWndEdit)
{
	HWND hWndOwner = hWndEdit ? hWndEdit : hMain;
	if (Clipboard_PasteHDROP(hWndOwner))
		return TRUE;

	if (!IsClipboardFormatAvailable(CF_DIB) && !IsClipboardFormatAvailable(CF_BITMAP))
		return FALSE;

	BOOL opened = FALSE;
	for (int i = 0; i < 5; i++)
	{
		if (OpenClipboard(hWndEdit ? hWndEdit : hMain))
		if (OpenClipboard(hWndOwner))
		{
			opened = TRUE;
			break;
		}
		Sleep(10);
	}
	if (!opened)
		return FALSE;

	ClipboardDIB dib;
	BOOL acquired = Clipboard_AcquireDIB(&dib, CLIP_MAX_DIRECT_PASTE);
	wchar_t tempPath[MAX_PATH] = {0};
	BOOL created = acquired && CreateTempBmpFile(dib.pData, dib.totalSize, dib.offsetToBits, tempPath);
	if (acquired)
		Clipboard_ReleaseDIB(&dib);
	CloseClipboard();
	if (!created)
		return FALSE;

	wchar_t *filePath = _wcsdup(tempPath);
	if (!filePath)
	{
		DeleteTempFileOnDisk(tempPath);
		return FALSE;
	}

	EnterCriticalSection(&csFiles);
	if (editing_msg_id && !files.empty())
	{
		Clipboard_FinishAndRemoveTempFile(files[0]);
		free(files[0]);
		files.clear();
	}
	if (!Clipboard_TrackTempFile(tempPath))
	{
		LeaveCriticalSection(&csFiles);
		free(filePath);
		DeleteTempFileOnDisk(tempPath);
		return FALSE;
	}
	try
	{
		files.push_back(filePath);
	}
	catch (...)
	{
		Clipboard_FinishAndRemoveTempFile(tempPath);
		LeaveCriticalSection(&csFiles);
		free(filePath);
		return FALSE;
	}
	LeaveCriticalSection(&csFiles);

	SendMessage(hToolbar, TB_CHANGEBITMAP, 4, MAKELPARAM(14, 0));
	open_files_list();
	return TRUE;
}

static int FindTrackedTempFile(const wchar_t *path)
{
	for (size_t i = 0; i < g_trackedTempFiles.size(); i++)
		if (_wcsicmp(g_trackedTempFiles[i].path, path) == 0)
			return (int)i;
	return -1;
}

static void DeleteTempFileOnDisk(const wchar_t *path)
{
	if (!DeleteFileW(path))
	{
		char pathA[MAX_PATH];
		if (WideCharToMultiByte(CP_ACP, 0, path, -1, pathA, MAX_PATH, NULL, NULL))
			DeleteFileA(pathA);
	}
}

BOOL Clipboard_TrackTempFile(const wchar_t *path)
{
	if (!path)
		return FALSE;

	EnterCriticalSection(&g_csTemp);
	int index = FindTrackedTempFile(path);
	if (index >= 0)
		g_trackedTempFiles[index].state = TEMP_STATE_ATTACHED;
	else
	{
		TrackedTempFile item;
		wcsncpy(item.path, path, MAX_PATH - 1);
		item.path[MAX_PATH - 1] = 0;
		item.state = TEMP_STATE_ATTACHED;
		try
		{
			g_trackedTempFiles.push_back(item);
		}
		catch (...)
		{
			LeaveCriticalSection(&g_csTemp);
			return FALSE;
		}
	}
	LeaveCriticalSection(&g_csTemp);
	return TRUE;
}

void Clipboard_MarkTempFileSending(const wchar_t *path)
{
	if (!path)
		return;
	EnterCriticalSection(&g_csTemp);
	int index = FindTrackedTempFile(path);
	if (index >= 0)
		g_trackedTempFiles[index].state = TEMP_STATE_SENDING;
	LeaveCriticalSection(&g_csTemp);
}

void Clipboard_FinishAndRemoveTempFile(const wchar_t *path)
{
	if (!path)
		return;
	EnterCriticalSection(&g_csTemp);
	int index = FindTrackedTempFile(path);
	if (index >= 0)
	{
		DeleteTempFileOnDisk(g_trackedTempFiles[index].path);
		g_trackedTempFiles.erase(g_trackedTempFiles.begin() + index);
	}
	LeaveCriticalSection(&g_csTemp);
}

void Clipboard_CleanupAttachedTempFiles(void)
{
	EnterCriticalSection(&g_csTemp);
	for (size_t i = 0; i < g_trackedTempFiles.size();)
	{
		if (g_trackedTempFiles[i].state == TEMP_STATE_ATTACHED)
		{
			DeleteTempFileOnDisk(g_trackedTempFiles[i].path);
			g_trackedTempFiles.erase(g_trackedTempFiles.begin() + i);
		}
		else
		{
			++i;
		}
	}
	LeaveCriticalSection(&g_csTemp);
}
