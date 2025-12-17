#include <cstring>
#include <string>
#include <dlfcn.h>
#include <pthread.h>
#include <android/log.h>
#include <link.h>
#include "Dobby/dobby.h"
#include "Includes/xorstr.hpp"
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>
#include <string_view>
#include "Unreal.h"

FString Backend = _(u"https://xenon-api-v1.fluxfn.org");
auto Realloc = (void *(*)(void *, size_t, int32_t)) nullptr;

static uintptr_t base = 0;

using ProcessRequestFn = bool (*)(void *);
static ProcessRequestFn ProcessRequestOG = nullptr;

struct FCurlHttpRequest
{
	void **VTable;

	FString &GetURL()
	{
		return *(FString *)(uint64_t(this) + 0x70);
	}
};

static const FString redirectedUrls[] = {
	_(u"ol.epicgames.com"),
	_(u"ol.epicgames.net"),
	_(u"on.epicgames.com"),
	_(u"game-social.epicgames.com"),
	_(u"ak.epicgames.com"),
	_(u"epicgames.dev")};

bool ShouldWeRedirect(URL *uri)
{
	for (int i = 0; i < sizeof(redirectedUrls) / sizeof(FString); i++)
	{
		if (uri->domain.ends_with(redirectedUrls[i].data))
			return true;
	}
	return false;
}

bool ProcessRequest(FCurlHttpRequest *Request)
{
	if (!Request)
		return ProcessRequestOG ? ProcessRequestOG(Request) : false;

	auto &urlS = Request->GetURL();

	if (!urlS.data || urlS.length == 0)
		return ProcessRequestOG(Request);

	URL url{};
	url.Construct(urlS);

	bool shouldRedirect = ShouldWeRedirect(&url);

	if (shouldRedirect)
	{
		FString oldPath((uint32_t)(url.path.length));
		FString oldQuery((uint32_t)(url.query.length));

		if (url.path.data && url.path.length > 0)
			memcpy(oldPath.data, url.path.data, url.path.length * sizeof(FString::Type));
		if (url.query.data && url.query.length > 0)
			memcpy(oldQuery.data, url.query.data, url.query.length * sizeof(FString::Type));

		url.DeallocPathQuery();
		url.SetHost(Backend);

		url.path = oldPath;
		url.query = oldQuery;

		FString newUrl = url.GetUrl();

		auto &URL = *(FString *)(uint64_t(Request) + 0x70);

		if (newUrl.data && newUrl.length > 0)
		{
			if (newUrl.length > URL.capacity)
			{
				URL.data = (FString::Type *)Realloc(URL.data, newUrl.length * sizeof(FString::Type), alignof(FString::Type));
				URL.capacity = newUrl.length;
			}
			URL.length = newUrl.length;
			memcpy(URL.data, newUrl.data, newUrl.length * sizeof(FString::Type));
		}

		newUrl.release();
	}

	url.Dealloc();

	return ProcessRequestOG(Request);
}

int findLibUE4(struct dl_phdr_info *info, size_t, void *)
{
	if (info->dlpi_name && strstr(info->dlpi_name, "libUE4.so"))
	{
		base = info->dlpi_addr;
		return 1;
	}
	return 0;
}

void *Main(void *)
{
	dl_iterate_phdr(findLibUE4, nullptr);
	if (base)
	{
		Realloc = (void *(*)(void *, size_t, int32_t))(base + 0xAD23778);
		void *ProcessRequestAddr = (void *)(base + 0x95FB1C4);
		if (ProcessRequestAddr)
		{
			DobbyHook(ProcessRequestAddr, (void *)ProcessRequest, (void **)&ProcessRequestOG);
		}
	}
	return nullptr;
}

__attribute__((constructor)) void lib_paradise()
{
	pthread_t tid;
	pthread_create(&tid, nullptr, Main, nullptr);
	pthread_detach(tid);
}