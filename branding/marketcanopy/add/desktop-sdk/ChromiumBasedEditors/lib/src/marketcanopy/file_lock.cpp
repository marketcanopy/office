/*
 * Market Canopy Office: the rotation lock file shared with the desktop app (SPEC 5.4).
 * Copyright (C) 2026 Market Canopy LLC
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License version 3 as
 * published by the Free Software Foundation, together with the additional
 * terms in the LICENSE file of https://github.com/marketcanopy/office.
 *
 * This program is distributed WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * AGPL at https://www.gnu.org/licenses/agpl-3.0.html.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */

// The file is opened for each acquisition and closed on release. Both
// LockFileEx and flock lock per open handle, so two holders inside one
// process exclude each other as two processes do. Acquire and Release may
// run on different threads (the account module acquires on a blocking
// thread and releases on CEF's), so the handle is guarded, and a second
// Acquire on the same object waits for the first holder like any other.

#include "platform.h"

#include <chrono>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace NSMarketCanopy
{
	namespace
	{
		const int kPollMs = 50;

#ifdef _WIN32
		class CWindowsLock : public IStoreLock
		{
		public:
			explicit CWindowsLock(const std::string& service) : m_service(service), m_file(INVALID_HANDLE_VALUE) {}
			~CWindowsLock() override { Release(); }

			bool Acquire(int timeoutMs) override
			{
				std::wstring path;
				if (!LockPath(path))
					return false;
				HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
										  NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				if (file == INVALID_HANDLE_VALUE)
					return false;
				auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
				for (;;)
				{
					{
						std::lock_guard<std::mutex> guard(m_mutex);
						OVERLAPPED overlapped;
						ZeroMemory(&overlapped, sizeof(overlapped));
						if (m_file == INVALID_HANDLE_VALUE)
						{
							if (LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlapped))
							{
								m_file = file;
								return true;
							}
							if (GetLastError() != ERROR_LOCK_VIOLATION)
								break;
						}
					}
					if (std::chrono::steady_clock::now() >= deadline)
						break;
					std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
				}
				CloseHandle(file);
				return false;
			}

			void Release() override
			{
				std::lock_guard<std::mutex> guard(m_mutex);
				if (m_file == INVALID_HANDLE_VALUE)
					return;
				OVERLAPPED overlapped;
				ZeroMemory(&overlapped, sizeof(overlapped));
				UnlockFileEx(m_file, 0, 1, 0, &overlapped);
				CloseHandle(m_file);
				m_file = INVALID_HANDLE_VALUE;
			}

		private:
			std::string m_service;
			std::mutex m_mutex;
			HANDLE m_file;

			// %LOCALAPPDATA%\MarketCanopy\credentials\<service>.lock; the folders inherit
			// %LOCALAPPDATA%'s user-only ACL.
			bool LockPath(std::wstring& path) const
			{
				PWSTR base = NULL;
				if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &base)))
					return false;
				std::wstring folder(base);
				CoTaskMemFree(base);
				folder += L"\\MarketCanopy";
				CreateDirectoryW(folder.c_str(), NULL);
				folder += L"\\credentials";
				CreateDirectoryW(folder.c_str(), NULL);
				DWORD attributes = GetFileAttributesW(folder.c_str());
				if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
					(attributes & FILE_ATTRIBUTE_REPARSE_POINT))
					return false;
				path = folder + L"\\" + std::wstring(m_service.begin(), m_service.end()) + L".lock";
				return true;
			}
		};
#else
		// mkdir -p; the last component is created 0700 and must be a directory this user owns.
		bool MakeOwnerOnlyDirectory(const std::string& path)
		{
			for (size_t i = 1; i <= path.size(); ++i)
			{
				if (i < path.size() && path[i] != '/')
					continue;
				std::string part = path.substr(0, i);
				if (mkdir(part.c_str(), i == path.size() ? 0700 : 0755) != 0 && errno != EEXIST)
					return false;
			}
			struct stat info;
			if (lstat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != getuid())
				return false;
			if ((info.st_mode & 0077) != 0 && chmod(path.c_str(), 0700) != 0)
				return false;
			return true;
		}

		std::string HomeDirectory()
		{
			const char* home = getenv("HOME");
			if (home && *home == '/')
				return home;
			struct passwd* entry = getpwuid(getuid());
			return entry && entry->pw_dir ? entry->pw_dir : "";
		}

		bool LockDirectory(std::string& directory)
		{
#ifdef __APPLE__
			std::string home = HomeDirectory();
			if (home.empty())
				return false;
			directory = home + "/Library/Application Support/MarketCanopy/credentials";
#else
			const char* runtime = getenv("XDG_RUNTIME_DIR");
			if (runtime && *runtime == '/')
				directory = std::string(runtime) + "/marketcanopy";
			else
			{
				std::string home = HomeDirectory();
				if (home.empty())
					return false;
				directory = home + "/.local/state/marketcanopy";
			}
#endif
			return MakeOwnerOnlyDirectory(directory);
		}

		class CPosixLock : public IStoreLock
		{
		public:
			explicit CPosixLock(const std::string& service) : m_service(service), m_fd(-1) {}
			~CPosixLock() override { Release(); }

			bool Acquire(int timeoutMs) override
			{
				std::string directory;
				if (!LockDirectory(directory))
					return false;
				std::string path = directory + "/" + m_service + ".lock";
				int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
				if (fd < 0)
					return false;
				struct stat info;
				if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid() ||
					((info.st_mode & 0077) != 0 && fchmod(fd, 0600) != 0))
				{
					close(fd);
					return false;
				}
				auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
				for (;;)
				{
					{
						std::lock_guard<std::mutex> guard(m_mutex);
						if (m_fd < 0)
						{
							if (flock(fd, LOCK_EX | LOCK_NB) == 0)
							{
								m_fd = fd;
								return true;
							}
							if (errno != EWOULDBLOCK && errno != EINTR)
								break;
						}
					}
					if (std::chrono::steady_clock::now() >= deadline)
						break;
					std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
				}
				close(fd);
				return false;
			}

			void Release() override
			{
				std::lock_guard<std::mutex> guard(m_mutex);
				if (m_fd < 0)
					return;
				flock(m_fd, LOCK_UN);
				close(m_fd);
				m_fd = -1;
			}

		private:
			std::string m_service;
			std::mutex m_mutex;
			int m_fd;
		};
#endif
	}

	IStoreLock* CreateOsStoreLock(const std::string& service)
	{
#ifdef _WIN32
		return new CWindowsLock(service);
#else
		return new CPosixLock(service);
#endif
	}

	const char* OsPlatformName()
	{
#if defined(_WIN32)
		return "windows";
#elif defined(__APPLE__)
		return "macos";
#else
		return "linux";
#endif
	}
}
