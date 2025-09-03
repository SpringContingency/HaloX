#include "import_windows_font.h"

#include <Windows.h>
#include <vector>
#include <fstream>
#include <cstdint>
#include <string>
#include <iostream>
#include <regex>

#include <atlbase.h>
#include <winnt.h>
#include <wingdi.h>
#include <unordered_map>


auto fontPath = TEXT(R"(BLENDERHALO.ttf)");
auto fontName = TEXT(R"(Blender Halo)");

struct CHANDLEDELETER { void operator()(HANDLE handle) const { CloseHandle(handle); } };
struct CHDCDeleter { void operator()(HDC hdc) const { DeleteDC(hdc); } };
struct CHGDIOBJDeleter { void operator()(HGDIOBJ hgd) const { DeleteObject(hgd); } };

template<typename T, typename D>
class CAutoHandle {
public:
	CAutoHandle(T h = nullptr, D d = D()) : handle(h), deleter(d) {
	}

	~CAutoHandle() {
		reset();
	}

	void operator=(T h) {
		reset();
		handle = h;
	}

	void reset() {
		if (handle != nullptr) {
			deleter(handle);
			handle = nullptr;
		}
	}

	bool operator==(T h) const {
		return handle == h;
	}

	bool operator!=(T h) const {
		return handle != h;
	}

	operator T() const {
		return handle;
	}

public:
	T handle;
	D deleter;
};

typedef CAutoHandle<HDC, CHDCDeleter> CHDC;
typedef CAutoHandle<HGDIOBJ, CHGDIOBJDeleter> CHGDIOBJ;
typedef CAutoHandle<HFONT, CHGDIOBJDeleter> CHFONT;
typedef CAutoHandle<HBITMAP, CHGDIOBJDeleter> CHBITMAP;
typedef CAutoHandle<HANDLE, CHANDLEDELETER> CHANDLE;

int CALLBACK EnumFontFamProc(
	CONST LOGFONT* lpelf,
	CONST TEXTMETRIC* lpntm,
	DWORD FontType,
	LPARAM lParam
) {
	if (FontType != TRUETYPE_FONTTYPE) {
		return 1;
	}
	auto info = reinterpret_cast<CONST ENUMLOGFONTEXDV*>(lpelf);

	wprintf(TEXT("Font: %s %s %s\n"), 
		info->elfEnumLogfontEx.elfFullName,
		info->elfEnumLogfontEx.elfStyle,
		info->elfEnumLogfontEx.elfScript
	);

	return 1;
}

int enum_font(HDC hDC) {
	LOGFONT lf{
		.lfCharSet = DEFAULT_CHARSET,
	};

	EnumFonts(hDC, NULL, EnumFontFamProc, 0);

	EnumFontFamiliesEx(hDC, &lf, EnumFontFamProc, 0, 0);

	return 0;
}

struct s_backed_font {
	short width;
	short height;
	char data[1];
};

struct s_font {

};

struct s_font_character {
	wchar_t unicode;
	short left;
	short top;
	short right;
	short bottom;
};

struct s_bmp {
	BITMAPFILEHEADER bfh;
	BITMAPINFOHEADER bi;
	RGBQUAD colors[256];
};

int save_bitmap(HDC hDC, HBITMAP hBitmap, const TCHAR* fileName, LONG height) {
	BITMAP bitmap;

	if (!GetObject(hBitmap, sizeof(BITMAP), &bitmap)) {
		return 1;
	}

	bitmap.bmHeight = height;

	WORD cClrBits = bitmap.bmPlanes * bitmap.bmBitsPixel; // 32

	DWORD biSizeImage = ((bitmap.bmWidth * cClrBits + 31) & ~31) / 8 * bitmap.bmHeight;

	BITMAPINFO info = {
		.bmiHeader = {
			.biSize = sizeof(BITMAPINFOHEADER),
			.biWidth = bitmap.bmWidth,
			.biHeight = -bitmap.bmHeight,
			.biPlanes = bitmap.bmPlanes,
			.biBitCount = bitmap.bmBitsPixel,
			.biCompression = BI_RGB,
			.biSizeImage = biSizeImage,
			.biClrUsed = 0,
			.biClrImportant = 0
		}
	};

	DWORD biSize = sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * info.bmiHeader.biClrUsed;

	BITMAPFILEHEADER fileHeader = {
		.bfType = 0x4d42,
		.bfSize = (DWORD)sizeof(BITMAPFILEHEADER) + biSize + info.bmiHeader.biSizeImage,
		.bfOffBits = (DWORD)sizeof(BITMAPFILEHEADER) + biSize
	};

	CHANDLE hFile = CreateFile(
		fileName,
		GENERIC_WRITE,
		0,
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (hFile == nullptr) {
		return 2;
	}

	if (!WriteFile(hFile, &fileHeader, sizeof(fileHeader), NULL, NULL) ||
		!WriteFile(hFile, &info, biSize, NULL, NULL) ||
		!WriteFile(hFile, bitmap.bmBits, info.bmiHeader.biSizeImage, NULL, NULL)
	) {
		printf("Failed to write bitmap %d", GetLastError());
		return 3;
	}

	return 0;
}

HFONT create_font(const TCHAR* face_name) {
	static int LogicalPixelsY;

	if (LogicalPixelsY == 0) {
		LogicalPixelsY = GetDeviceCaps(GetDC(NULL), LOGPIXELSY);
		if (LogicalPixelsY == 0) {
			LogicalPixelsY = 72;
		}
	}

	auto nHeight = -MulDiv(16, LogicalPixelsY, 72);

	ENUMLOGFONTEXDV elf{
		.elfEnumLogfontEx = {
			.elfLogFont = {
				.lfHeight = nHeight,
				.lfWeight = FW_NORMAL,
				.lfCharSet = DEFAULT_CHARSET
			}
		},
		.elfDesignVector = {
			.dvReserved = STAMP_DESIGNVECTOR
		}
	};

	auto lfFaceName = elf.elfEnumLogfontEx.elfLogFont.lfFaceName;

	wsprintf(lfFaceName, TEXT("%s"), face_name);

	return CreateFontIndirectEx(&elf);
}

int configure_text_formatting(HDC hDC) {
	if (
		SetTextColor(hDC, RGB(255, 255, 255)) == CLR_INVALID || 
		SetBkColor(hDC, RGB(0, 0, 0)) == CLR_INVALID || 
		SetBkMode(hDC, TRANSPARENT) == 0 ||
		SetTextAlign(hDC, TA_TOP) == GDI_ERROR ||
		SetMapMode(hDC, MM_TEXT) == 0
	) {
		return 1;
	}

	return 0;
}

std::unique_ptr<GLYPHSET> get_character_range(HDC hDC) {
	auto glyphSetSize = GetFontUnicodeRanges(hDC, nullptr);

	if (glyphSetSize == 0) {
		return nullptr;
	}

	std::unique_ptr<GLYPHSET> result(reinterpret_cast<GLYPHSET*>(new char[glyphSetSize]));

	auto writtenSize = GetFontUnicodeRanges(hDC, result.get());

	if (writtenSize != glyphSetSize) {
		return nullptr;
	}

	return std::move(result);
}

enum {
	k_max_char_in_line = 16,
	k_char_padding = 2,
	k_line_padding = 2,
};

std::unique_ptr<s_backed_font> render_font(HDC hDC, HFONT hFont) {
	TEXTMETRIC tm;
	CHBITMAP hBitmap;

	if (!SelectObject(hDC, hFont)) {
		return nullptr;
	}

	if (configure_text_formatting(hDC)) {
		return nullptr;
	}

	auto glyphSet = get_character_range(hDC);

	if (glyphSet == nullptr) {
		return nullptr;
	}

	if (!GetTextMetrics(hDC, &tm)) {
		return nullptr;
	}

	LONG nWidth = (tm.tmMaxCharWidth + k_char_padding) * k_max_char_in_line;
	LONG nLine = glyphSet->cGlyphsSupported / k_max_char_in_line + 1;
	LONG nHeight = (tm.tmHeight + k_line_padding) * nLine;

	// create render bitmap
	BITMAPINFO bmi{
	.bmiHeader = {
		.biSize = sizeof(BITMAPINFOHEADER),
		.biWidth = nWidth,
		.biHeight = -nHeight,
		.biPlanes = 1,
		.biBitCount = 32,
	}
	};

	hBitmap = CreateDIBSection(NULL, &bmi, 0, NULL, NULL, 0);

	if (hBitmap == nullptr) {
		return nullptr;
	}

	if (!SelectObject(hDC, hBitmap)) {
		return nullptr;
	}

	if (!GdiFlush()) {
		return nullptr;
	}

	POINT cursor{};
	std::unordered_map<wchar_t, s_font_character> character_map;

	for (int i = 0; i < glyphSet->cRanges; ++i) {
		auto range = glyphSet->ranges + i;

		for (int j = 0; j < range->cGlyphs; ++j) {
			ABC abc;
			RECT box;
			SIZE size;
			int options;
			const int s = range->wcLow + j;
			WCHAR str[2] { (WCHAR)s, 0 };

			if (!GetTextExtentPoint32W(hDC, str, 1, &size)) {
				printf("Skipping: %hX\n", str[0]);
				continue;
			}

			if (!GetCharABCWidthsW(hDC, str[0], str[0], &abc)) {
				int width_buffer[2];

				abc.abcA = 0;
				abc.abcC = 0;

				if (GetCharWidth32W(hDC, str[0], str[0], width_buffer)) {
					abc.abcB = width_buffer[0];
				} else {
					abc.abcB = size.cx;
				}
			}

			// render character
			box.right = cursor.x + abc.abcB;

			if (box.right > nWidth) {
				cursor.x = 0;
				cursor.y = cursor.y + tm.tmHeight + k_line_padding;
				box.right = abc.abcB;
			}

			box.left = box.right - abc.abcB;

			if (abc.abcA < 0) {
				box.left += abc.abcA;
			}

			box.top = cursor.y;
			box.bottom = box.top + tm.tmHeight;

			if (!ExtTextOutW(hDC, box.left, box.top, ETO_OPAQUE, NULL, str, 1, nullptr)) {
				printf("Failed to render: %hX\n", str[0]);
				continue;
			}

			cursor.x += abc.abcB + k_char_padding;

			s_font_character character{
				.unicode = str[0],
				.left = static_cast<SHORT>(box.left),
				.top = static_cast<SHORT>(box.top),
				.right = static_cast<SHORT>(box.right),
				.bottom = static_cast<SHORT>(box.bottom),
			};

			//character_map.emplace(str[0], character);
		}
	}

	if (save_bitmap(hDC, hBitmap, TEXT(R"(font.bmp)"), cursor.y + tm.tmHeight)) {
		printf("Failed to save bitmap\n");
		return nullptr;
	}

	return 0;
}

static int import_font(const TCHAR* font, const TCHAR* name) {
	CHDC hDC;
	CHFONT hFont;
	CHBITMAP hBitmap;

	hDC = CreateCompatibleDC(NULL);

	if (hDC == nullptr) {
		return 1;
	}

	if (AddFontResourceEx(font, FR_PRIVATE, NULL) == 0) {
		return 2;
	}

	//enum_font(hDC);

	hFont = create_font(name);

	if (hFont == nullptr) {
		return 3;
	}
	
	auto backed_font = render_font(hDC, hFont);

	return 0;
}

static int import_windows_font(int argc, const char** argv) {
	if (argc < 3) {
		printf("Usage: import_windows_font <FONT> <NAME>\n");
		return 1;
	}

	auto font = argv[1];
	auto name = argv[2];

	if (font == nullptr || name == nullptr) {
		printf("Invalid arguments\n");
		return 2;
	}

	auto wFont = (WCHAR*) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WCHAR) * MAX_PATH);
	auto wName = (WCHAR*) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WCHAR) * MAX_PATH);

	MultiByteToWideChar(CP_UTF8, 0, font, -1, wFont, MAX_PATH);
	MultiByteToWideChar(CP_UTF8, 0, name, -1, wName, MAX_PATH);

	if (import_font(wFont, wName)) {
		printf("Failed to import font %s\n", font);

		HeapFree(GetProcessHeap(), 0, wFont);
		HeapFree(GetProcessHeap(), 0, wName);
		return 3;
	}

	HeapFree(GetProcessHeap(), 0, wFont);
	HeapFree(GetProcessHeap(), 0, wName);
	printf("Font %s imported successfully as %s\n", font, name);
	return 0;

}

s_command g_command_import_windows_font{
	.name = "import_windows_font",
	.description = "<FONT> <NAME>",
	.proc = import_windows_font,
};
