#pragma once

#include <Common/Core/Core.hpp>

namespace Common::Memory
{
	struct Buffer
	{
		void* Data;
		std::size_t Size;

		Buffer()
			:Data(nullptr), Size(0)
		{

		}

		Buffer(void* data, std::size_t size)
			: Data(data), Size(size)
		{

		}

		void Allocate(std::size_t size)
		{
			delete[] Data;
			Data = nullptr;

			if (size == 0)
				return;

			Data = new std::byte[size];
			Size = size;
		}

		void Release()
		{
			if (Data)
			{
				delete[] Data;
				Data = nullptr;

				Size = 0;
			}
		}

		void ZeroInitialize()
		{
			if (Data)
				memset(Data, 0, Size);
		}

		template <typename T>
		T& Read(std::size_t offset = 0)
		{
			return *(T*)((const char*)Data + offset);
		}

		inline void Write(void* data, std::size_t size, std::size_t offset = 0)
		{
			DESERT_VERIFY(size + offset <= Size, "Buffer overflow!");
			memcpy((std::byte*)Data + offset, data, size);
		}

		static Buffer Copy(const void* data, std::size_t size)
		{
			Buffer buffer;
			buffer.Allocate(size);

			memcpy(buffer.Data, data, size);
			return buffer;
		}

		template <typename T>
		T* As() const
		{
			return (T*)(Data);
		}

		operator bool() const {
			return Data;
		}

		std::byte& operator[](int index)
		{
			return ((std::byte*)Data)[index];
		}

		std::byte operator[](int index) const
		{
			return ((std::byte*)Data)[index];
		}

		inline std::size_t GetSize() { return Size; }

		template <typename T>
		bool operator==(T& rhs)
		{
			return Data == rhs.Data;
		}
	};
}