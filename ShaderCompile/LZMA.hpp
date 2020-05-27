#ifndef LZMA_HPP
#define LZMA_HPP

namespace LZMA
{
	static constexpr int LZMA_ID = 'AMZL';
#pragma pack( 1 )
	struct lzma_header_t
	{
		uint32_t	id;
		uint32_t	actualSize; // always little endian
		uint32_t	lzmaSize;   // always little endian
		uint8_t		properties[5];
	};
#pragma pack()
	static_assert( sizeof( lzma_header_t ) == 17 );

	static void* SzAlloc( void*, size_t size )
	{
		return malloc( size );
	}
	static void SzFree( void*, void* address )
	{
		free( address );
	}
	static ISzAlloc g_Alloc = { SzAlloc, SzFree };

	SRes LzmaEncode( const Byte* inBuffer, size_t inSize, Byte* outBuffer, size_t outSize, size_t* outSizeProcessed )
	{
		class CInStreamRam : public ISeqInStream
		{
			const Byte* Data;
			size_t Size;
			size_t Pos;

			SRes DoRead( void* buf, size_t* size )
			{
				size_t inSize = *size;
				size_t remain = Size - Pos;
				if ( inSize > remain )
					inSize = remain;

				for ( size_t i = 0; i < inSize; ++i )
					reinterpret_cast<Byte*>( buf )[i] = Data[Pos + i];

				Pos += inSize;
				*size = inSize;
				return SZ_OK;
			}

			static SRes StaticRead( void* p, void* buf, size_t* size )
			{
				return reinterpret_cast<CInStreamRam*>( p )->DoRead( buf, size );
			}

		public:
			CInStreamRam( const Byte* data, size_t size )
			{
				Data = data;
				Size = size;
				Pos = 0;
				Read = StaticRead;
			}
		};

		class COutStreamRam : public ISeqOutStream
		{
			size_t Size;

			static size_t StaticWrite( void* p, const void* buf, size_t size )
			{
				return reinterpret_cast<COutStreamRam*>( p )->DoWrite( buf, size );
			}

		public:
			Byte* Data;
			size_t Pos;
			bool Overflow;

			COutStreamRam( Byte* data, size_t size )
			{
				Data = data;
				Size = size;
				Pos = 0;
				Overflow = false;
				Write = StaticWrite;
			}

			size_t DoWrite( const void* buf, size_t size )
			{
				size_t i;
				for ( i = 0; i < size && Pos < Size; ++i )
					Data[Pos++] = reinterpret_cast<const Byte*>( buf )[i];
				if ( i != size )
					Overflow = true;
				return i;
			}
		};

		// Based on Encode helper in SDK/LzmaUtil
		*outSizeProcessed = 0;

		const size_t kMinDestSize = 13;
		if ( outSize < kMinDestSize )
			return SZ_ERROR_FAIL;

		CLzmaEncHandle enc;
		SRes res;
		CLzmaEncProps props;

		enc = LzmaEnc_Create( &g_Alloc );
		if ( !enc )
			return SZ_ERROR_FAIL;

		LzmaEncProps_Init( &props );
		res = LzmaEnc_SetProps( enc, &props );

		if ( res != SZ_OK )
			return res;

		COutStreamRam outStream( outBuffer, outSize );

		Byte header[LZMA_PROPS_SIZE + 8];
		size_t headerSize = LZMA_PROPS_SIZE;

		res = LzmaEnc_WriteProperties( enc, header, &headerSize );
		if ( res != SZ_OK )
			return res;

		// Uncompressed size after properties in header
		for ( int i = 0; i < 8; i++ )
			header[headerSize++] = static_cast<Byte>( inSize >> ( 8 * i ) );

		if ( outStream.DoWrite( header, headerSize ) != headerSize )
			res = SZ_ERROR_WRITE;
		else if ( res == SZ_OK )
		{
			CInStreamRam inStream( inBuffer, inSize );
			res = LzmaEnc_Encode( enc, &outStream, &inStream, nullptr, &g_Alloc, &g_Alloc );

			if ( outStream.Overflow )
				res = SZ_ERROR_FAIL;
			else
				*outSizeProcessed = outStream.Pos;
		}

		LzmaEnc_Destroy( enc, &g_Alloc, &g_Alloc );

		return res;
	}

	static uint8_t* Compress( uint8_t* pInput, size_t inputSize, size_t* pOutputSize )
	{
		*pOutputSize = 0;

		// using same work buffer calcs as the SDK 105% + 64K
		size_t outSize = inputSize / 20 * 21 + ( 1 << 16 );
		uint8_t* pOutputBuffer = new uint8_t[outSize];
		if ( !pOutputBuffer )
			return nullptr;

		// compress, skipping past our header
		size_t compressedSize;
		int result = LzmaEncode( pInput, inputSize, pOutputBuffer + sizeof( lzma_header_t ), outSize - sizeof( lzma_header_t ), &compressedSize );
		if ( result != SZ_OK )
		{
			Assert( result == SZ_OK );
			delete[] pOutputBuffer;
			return nullptr;
		}

		// construct our header, strip theirs
		lzma_header_t* pHeader = reinterpret_cast<lzma_header_t*>( pOutputBuffer );
		pHeader->id = LZMA_ID;
		pHeader->actualSize = gsl::narrow<uint32_t>( inputSize );
		pHeader->lzmaSize = gsl::narrow<uint32_t>( compressedSize - 13 );
		memcpy( pHeader->properties, pOutputBuffer + sizeof( lzma_header_t ), LZMA_PROPS_SIZE );

		// shift the compressed data into place
		memmove( pOutputBuffer + sizeof( lzma_header_t ), pOutputBuffer + sizeof( lzma_header_t ) + 13, compressedSize - 13 );

		// final output size is our header plus compressed bits
		*pOutputSize = sizeof( lzma_header_t ) + compressedSize - 13;

		return pOutputBuffer;
	}

	uint8_t* OpportunisticCompress( uint8_t* pInput, size_t inputSize, size_t* pOutputSize )
	{
		uint8_t* pRet = Compress( pInput, inputSize, pOutputSize );
		if ( *pOutputSize >= inputSize )
		{
			// compression got worse or stayed the same
			delete[] pRet;
			return nullptr;
		}

		return pRet;
	}
} // namespace LZMA

#endif // LZMA_HPP