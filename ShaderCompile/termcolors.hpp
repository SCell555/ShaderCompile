#ifndef TERMCOLORS_HPP
#define TERMCOLORS_HPP

namespace clr
{
	using namespace termcolor;
	static inline constexpr auto red = _internal::ansi_color( color( 222, 12, 17 ) );
	static inline constexpr auto green = _internal::ansi_color( color( 33, 201, 41 ) );
	static inline constexpr auto green2 = _internal::ansi_color( color( 12, 222, 154 ) );
	static inline constexpr auto blue = _internal::ansi_color( color( 14, 70, 220 ) );
	static inline constexpr auto pinkish = _internal::ansi_color( color( 254, 90, 90 ) );
} // namespace clr

#endif // TERMCOLORS_HPP