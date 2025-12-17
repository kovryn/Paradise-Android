#include <cstring>
#include <string>
#include <dlfcn.h>
#include <pthread.h>
#include <android/log.h>
#include <link.h>
#include "Dobby/dobby.h"
#include "Includes/xorstr.hpp"
#include <stdint.h>
#include <wchar.h>
#include <stdlib.h>
#include <string_view>

int u16len(const char16_t *str)
{
	int len = 0;
	while (str[len])
		len++;
	return len;
}

struct FString
{
	inline static const size_t npos = -1;
	inline static auto GetLen = u16len;
	using Type = char16_t;
	Type *data;
	int32_t length;
	int32_t capacity;

	FString() : data(nullptr), length(0), capacity(0) {}

	FString(const char *src)
	{
		data = nullptr;
		length = 0;
		capacity = 0;
		if (src)
		{
			capacity = length = (int)strlen(src) + 1;
			allocate();
			for (int i = 0; i < length; i++)
				data[i] = (Type)src[i];
		}
	}

	void allocate() { data = (Type *)malloc(capacity * sizeof(Type)); }

	FString(Type *src)
	{
		data = nullptr;
		length = 0;
		capacity = 0;
		if (src)
		{
			capacity = length = (int)GetLen(src) + 1;
			allocate();
			memcpy(data, src, length * sizeof(Type));
		}
	}

	FString(uint32_t size) : capacity(size + 1), length(size + 1)
	{
		allocate();
		if (data)
			data[0] = 0;
	}

	FString substr(size_t offset, size_t count = -1)
	{
		if (offset >= length)
			return FString();

		if (count == -1 || count > length - offset)
			count = length - offset - 1;

		FString result((uint32_t)count);
		if (result.data && data && count > 0)
		{
			memcpy(result.data, (data + offset), count * sizeof(Type));
			result.data[count] = 0;
			result.length = count + 1;
		}
		return result;
	}

	FString(const Type *src, size_t size)
	{
		data = nullptr;
		length = 0;
		capacity = 0;
		if (src && size > 0)
		{
			capacity = length = (uint32_t)size;
			allocate();
			memcpy(data, src, size * sizeof(Type));
		}
	}

	size_t find(Type ch)
	{
		for (uint32_t i = 0; i < length; i++)
		{
			if (data[i] == ch)
				return i;
		}
		return -1;
	}

	size_t find(const Type *pattern)
	{
		int patternLen = GetLen(pattern);
		if (patternLen == 0 || length < patternLen)
			return -1;

		for (uint32_t i = 0; i <= length - patternLen; i++)
		{
			bool matched = true;
			for (int j = 0; j < patternLen; j++)
			{
				if (data[i + j] != pattern[j])
				{
					matched = false;
					break;
				}
			}
			if (matched)
				return i;
		}
		return -1;
	}

	operator Type *() { return data; }
	size_t find_first_of(Type ch) { return find(ch); }

	void release()
	{
		if (data)
		{
			free(data);
			data = nullptr;
		}
		length = 0;
		capacity = 0;
	}

	bool ends_with(const Type *suffix)
	{
		if (!data || !suffix)
			return false;
		auto suffixLen = GetLen(suffix);
		if (suffixLen > length - 1)
			return false;

		auto startPos = (length - 1) - suffixLen;
		for (size_t i = 0; i < suffixLen; i++)
		{
			if (data[startPos + i] != suffix[i])
				return false;
		}
		return true;
	}
};

class URL
{
public:
	using StrType = FString;
	StrType protocol, separator, domain, port, path, query;

	void Construct(StrType url)
	{
		memset(this, 0, sizeof(URL));
		auto protoEnd = url.find(':');
		protocol = url.substr(0, protoEnd);
		auto protoSize = (url[protoEnd + 1] == '/' && url[protoEnd + 2] == '/') ? 3 : 1;
		separator = url.substr(protoEnd, protoSize);
		auto domainPortStr = url.substr(protoEnd + protoSize);
		auto pathIdx = domainPortStr.find_first_of('/');
		auto domainPort = domainPortStr.substr(0, pathIdx);
		auto pathStr = domainPortStr.substr(pathIdx);
		domainPortStr.release();
		auto portIdx = domainPort.find_first_of(':');
		domain = domainPort.substr(0, portIdx);
		if (portIdx != StrType::npos)
			port = domainPort.substr(portIdx);
		domainPort.release();
		auto queryIdx = pathStr.find_first_of('?');
		path = pathStr.substr(0, queryIdx);
		if (queryIdx != StrType::npos)
			query = pathStr.substr(queryIdx);
		pathStr.release();
	}

	URL &SetHost(FString hostUrl)
	{
		auto protoEnd = hostUrl.find(':');
		protocol = hostUrl.substr(0, protoEnd);
		auto protoSize = (hostUrl[protoEnd + 1] == '/' && hostUrl[protoEnd + 2] == '/') ? 3 : 1;
		separator.release();
		separator = hostUrl.substr(protoEnd, protoSize);
		auto domainPortStr = hostUrl.substr(protoEnd + protoSize);
		auto pathIdx = domainPortStr.find_first_of('/');
		auto domainPort = domainPortStr.substr(0, pathIdx);
		domainPortStr.release();
		auto portIdx = domainPort.find_first_of(':');
		domain.release();
		domain = domainPort.substr(0, portIdx);
		if (portIdx != StrType::npos)
		{
			port.release();
			port = domainPort.substr(portIdx);
		}
		domainPort.release();
		return *this;
	}

	StrType GetUrl()
	{
		FString result = FString((protocol.length - 1) + (separator.length - 1) + (domain.length - 1) +
								 (port.data ? port.length - 1 : 0) + (path.length - 1) +
								 (query.data ? query.length - 1 : 0) + 1);
		memcpy((result.data), protocol.data, protocol.length * 2);
		memcpy((result.data + FString::GetLen(result.data)), separator.data, separator.length * 2);
		memcpy((result.data + FString::GetLen(result.data)), domain.data, domain.length * 2);
		if (port.data)
			memcpy((result.data + FString::GetLen(result.data)), port.data, port.length * 2);
		memcpy((result.data + FString::GetLen(result.data)), path.data, path.length * 2);
		if (query.data)
			memcpy((result.data + FString::GetLen(result.data)), query.data, query.length * 2);
		return result;
	}

	operator StrType() { return GetUrl(); }
	void DeallocPathQuery()
	{
		path.release();
		query.release();
	}
	void Dealloc()
	{
		protocol.release();
		separator.release();
		domain.release();
		port.release();
		path.release();
		query.release();
	}
};

#ifdef __clang__
#define __URL_SetHost(url, host) url->SetHost(host)
#else
#define __URL_SetHost(url, host) url->SetHost<host>()
#endif