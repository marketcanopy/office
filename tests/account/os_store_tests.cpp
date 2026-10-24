/*
 * Market Canopy Office: the real OS credential store and rotation lock.
 * Copyright (C) 2026 Market Canopy LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

// Runs only when MC_OS_STORE_TESTS=1, because it writes to the person's
// keychain, Credential Manager or Secret Service: CI sets it on throwaway
// GitHub-hosted runners. It uses its own service name, never the product's.
// With MC_OS_STORE_EXPECT=unavailable it checks the memory-only fallback
// instead (Linux with no reachable session bus, so no Secret Service).

#include <chrono>
#include <cstdlib>
#include <memory>
#include <thread>

#include "contract.h"
#include "crypto.h"
#include "platform.h"
#include "test_support.h"

using namespace NSMarketCanopy;
using namespace NSTest;

int main()
{
	const char* enabled = std::getenv("MC_OS_STORE_TESTS");
	if (!enabled || std::string(enabled) != "1")
	{
		std::printf("os_store_tests: skipped (set MC_OS_STORE_TESTS=1 on a machine whose store may be written)\n");
		return 0;
	}
	const char* expect = std::getenv("MC_OS_STORE_EXPECT");
	bool expectUnavailable = expect && std::string(expect) == "unavailable";
	const std::string service = "com.marketcanopy.office.test-" + NewUuidV4().substr(0, 8);
	const std::string account = GrantAccount("https://api.marketcanopy.com");
	std::printf("os_store_tests on %s, service %s\n", OsPlatformName(), service.c_str());

	std::unique_ptr<ICredentialStore> store(CreateOsCredentialStore(service));
	std::string value;
	ICredentialStore::EStatus status = store->Read(account, value);
	if (expectUnavailable)
	{
		CHECK(status == ICredentialStore::Unavailable, "no Secret Service reads as unavailable (memory-only session)");
		CHECK(store->Write(account, "x") == ICredentialStore::Unavailable, "and so does a write");
		return Finish("os_store_tests (unavailable)");
	}
	CHECK(status == ICredentialStore::NotFound, "a new service has no entry");

	// The largest value the contract allows, written, overwritten, read, deleted.
	CGrant grant;
	grant.apiOrigin = "https://api.marketcanopy.com";
	grant.userId = "66f0c0ffee0000000000a001";
	grant.deviceSessionId = "66f0c0ffee0000000000d001";
	grant.installationId = "4f7d2c1e-8a8b-4c39-9d7e-2b1f5a0c9e11";
	grant.refreshToken = "dsr_EXAMPLE-not-a-real-refresh-token-0001";
	grant.issuedAt = "2026-10-24T21:40:00.000Z";
	grant.writtenBy = "office";
	std::string first = WriteGrant(grant);
	CHECK(store->Write(account, first) == ICredentialStore::Ok, "write");
	CHECK(store->Read(account, value) == ICredentialStore::Ok && value == first, "read back byte for byte");
	grant.refreshToken = "dsr_" + std::string(kMaxGrantBytes - first.size() + grant.refreshToken.size() - 4, 'a');
	std::string large = WriteGrant(grant);
	CHECK_EQ(large.size(), kMaxGrantBytes, "a value at the 2048-byte limit");
	CHECK(store->Write(account, large) == ICredentialStore::Ok, "overwrite in place");
	CHECK(store->Read(account, value) == ICredentialStore::Ok && value == large, "the overwrite is what reads back");
	CHECK(store->Write(kInstallationAccount, WriteInstallation(grant.installationId)) == ICredentialStore::Ok, "a second account");
	CHECK(store->Read(account, value) == ICredentialStore::Ok && value == large, "accounts are separate entries");
	CHECK(store->Delete(account) == ICredentialStore::Ok, "delete");
	CHECK(store->Read(account, value) == ICredentialStore::NotFound, "gone after delete");
	CHECK(store->Delete(account) == ICredentialStore::NotFound, "deleting again finds nothing");
	CHECK(store->Delete(kInstallationAccount) == ICredentialStore::Ok, "clean up");

	// The rotation lock: owner-only, exclusive across handles, released on Release.
	std::unique_ptr<IStoreLock> lock(CreateOsStoreLock(service));
	std::unique_ptr<IStoreLock> other(CreateOsStoreLock(service));
	CHECK(lock->Acquire(1000), "acquire");
	bool otherGot = true;
	std::thread contender([&]() { otherGot = other->Acquire(300); });
	contender.join();
	CHECK(!otherGot, "a second holder waits, then gives up");
	bool afterRelease = false;
	std::thread waiter([&]() { afterRelease = other->Acquire(3000); });
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	lock->Release();
	waiter.join();
	CHECK(afterRelease, "the waiter gets it once the holder releases");
	other->Release();
	CHECK(lock->Acquire(1000), "acquire again");
	lock->Release();
	return Finish("os_store_tests");
}
