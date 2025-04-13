#include "win32.h"

std::wstring win32_get_document_folder_path() {
	HKEY key;
	DWORD size;
	std::vector<WCHAR> buffer;
	std::vector<WCHAR> formatted;
	LSTATUS status;
	std::wstring result;

	status = RegOpenKeyExW(
		HKEY_CURRENT_USER,
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders",
		0,
		KEY_READ,
		&key);

	if (status != ERROR_SUCCESS) {
		return result;
	}

	status = RegQueryValueExW(
		key,
		L"Personal",
		0,
		0,
		0,
		&size);

	if (status != ERROR_SUCCESS) {
		RegCloseKey(key);
		return result;
	}

	buffer.resize(size);

	status = RegQueryValueExW(
		key,
		L"Personal",
		0,
		0,
		(LPBYTE)buffer.data(),
		&size);

	if (status != ERROR_SUCCESS) {
		RegCloseKey(key);
		return result;
	}

	size = ExpandEnvironmentStringsW(buffer.data(), nullptr, 0);

	if (size == 0) {
		RegCloseKey(key);
		return result;
	}

	formatted.resize(size);

	size = ExpandEnvironmentStringsW(buffer.data(), formatted.data(), size);

	if (size == 0) {
		RegCloseKey(key);
		return result;
	}

	result = formatted.data();

	return result;
}
