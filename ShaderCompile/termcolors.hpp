#ifndef TERMCOLORS_HPP
#define TERMCOLORS_HPP

namespace clr
{
	using namespace termcolor;
	static const auto red = _internal::ansi_color( color( 222, 12, 17 ) );
	static const auto green = _internal::ansi_color( color( 33, 201, 41 ) );
	static const auto green2 = _internal::ansi_color( color( 12, 222, 154 ) );
	static const auto blue = _internal::ansi_color( color( 14, 70, 220 ) );
	static const auto pinkish = _internal::ansi_color( color( 254, 90, 90 ) );
} // namespace clr

#endif // TERMCOLORS_HPP