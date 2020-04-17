#ifndef MOVINGAVERAGE_HPP
#define MOVINGAVERAGE_HPP


template <typename StorageType, uint32_t TBufferSize>
class CUtlMovingAverage
{
public:
	CUtlMovingAverage()
		: m_buffer{ 0 }
		, m_nValuesPushed( 0 )
		, m_nIndex( 0 )
		, m_total( 0 )
	{
	}

	void Reset()
	{
		m_nValuesPushed = 0;
		m_nIndex = 0;
		m_total = 0;
		memset( m_buffer, 0, sizeof( m_buffer ) );
	}

	[[nodiscard]] StorageType GetAverage() const
	{
		const uint32_t n = std::min( TBufferSize, m_nIndex );
		return gsl::narrow_cast<StorageType>( n ? ( m_total / static_cast<double>( n ) ) : 0 );
	}

	void PushValue( StorageType v )
	{
		uint32_t nIndex = m_nValuesPushed % TBufferSize;
		m_nValuesPushed = nIndex + 1;
		m_nIndex = std::max( m_nIndex, m_nValuesPushed );

		m_total -= m_buffer[nIndex];
		m_total += v;

		m_buffer[nIndex] = v;
	}

private:
	StorageType m_buffer[TBufferSize];
	uint32_t m_nValuesPushed;
	uint32_t m_nIndex;

	StorageType m_total;
};


#endif // MOVINGAVERAGE_HPP