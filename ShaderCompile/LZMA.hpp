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

	static uint8_t* Compress( uint8_t* pInput, size_t inputSize, size_t* pOutputSize )
	{
		Byte* inBuffer = pInput;
		CLzmaEncProps props;
		LzmaEncProps_Init( &props );
		LzmaEncProps_Normalize( &props );

		size_t outSize = inputSize / 20 * 21 + ( 1 << 16 );
		Byte* outBuffer = static_cast<Byte*>( malloc( outSize ) );
		if ( outBuffer == nullptr )
			return nullptr;

		lzma_header_t* header = reinterpret_cast<lzma_header_t*>( outBuffer );
		header->id = LZMA_ID;
		header->actualSize = gsl::narrow<uint32_t>( inputSize );

		{
			size_t outSizeProcessed = outSize - sizeof( lzma_header_t );
			size_t outPropsSize = LZMA_PROPS_SIZE;

			const SRes res = LzmaEncode( outBuffer + sizeof( lzma_header_t ), &outSizeProcessed,
				inBuffer, inputSize, &props, header->properties, &outPropsSize, 0,
				nullptr, &g_Alloc, &g_Alloc );

			if ( res != SZ_OK )
			{
				free( outBuffer );
				return nullptr;
			}

			header->lzmaSize = gsl::narrow<uint32_t>( outSizeProcessed );
			outSize = sizeof( lzma_header_t ) + outSizeProcessed;
		}
		*pOutputSize = outSize;
		return outBuffer;
	}
} // namespace LZMA

#endif // LZMA_HPP