//!
//! termcolor
//! ~~~~~~~~~
//!
//! termcolor is a header-only c++ library for printing colored messages
//! to the terminal. Written just for fun with a help of the Force.
//!
//! :copyright: (c) 2013 by Ihor Kalnytskyi
//! :license: BSD, see LICENSE for details
//!

#ifndef TERMCOLOR_HPP_
#define TERMCOLOR_HPP_

// the following snippet of code detects the current OS and
// defines the appropriate macro that is used to wrap some
// platform specific things
#if defined(_WIN32) || defined(_WIN64)
#   define TERMCOLOR_OS_WINDOWS
#elif defined(__APPLE__)
#   define TERMCOLOR_OS_MACOS
#elif defined(__unix__) || defined(__unix)
#   define TERMCOLOR_OS_LINUX
#else
#   error unsupported platform
#endif


// This headers provides the `isatty()`/`fileno()` functions,
// which are used for testing whether a standart stream refers
// to the terminal. As for Windows, we also need WinApi funcs
// for changing colors attributes of the terminal.
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
#   include <unistd.h>
#elif defined(TERMCOLOR_OS_WINDOWS)
#   include <io.h>
#   include <windows.h>
#endif


#include <iostream>
#include <cstdio>


namespace termcolor
{
	struct __color_index_8bit
	{
		std::uint8_t index;
		bool foreground;
	};

	struct __color_rgb_24bit
	{
		std::uint8_t red, green, blue;
		bool foreground;
	};

    // Forward declaration of the `_internal` namespace.
    // All comments are below.
    namespace _internal
    {
        // An index to be used to access a private storage of I/O streams. See
        // colorize / nocolorize I/O manipulators for details.
        static int colorize_index = std::ios_base::xalloc();

        inline FILE* get_standard_stream(const std::ostream& stream);
        inline bool is_colorized(std::ostream& stream);
        inline bool is_atty(const std::ostream& stream);

        struct ansi_color
        {
            char buffer[24];

            ansi_color(const __color_index_8bit& color)
            {
                sprintf_s(buffer, "\033[%c8;5;%dm",
                    (color.foreground ? '3':'4'), color.index);
            }

            ansi_color(const __color_rgb_24bit& rgb)
            {
                sprintf_s(buffer, "\033[%c8;2;%d;%d;%dm",
                    (rgb.foreground ? '3':'4'), rgb.red, rgb.green, rgb.blue);
            }

            operator const char*() const { return buffer; }
        };
    }

    inline
    std::ostream& colorize(std::ostream& stream)
    {
        stream.iword(_internal::colorize_index) = 1L;
        return stream;
    }

    inline
    std::ostream& nocolorize(std::ostream& stream)
    {
        stream.iword(_internal::colorize_index) = 0L;
        return stream;
    }

    inline
    std::ostream& reset(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[00m";
        }
        return stream;
    }


    inline
    std::ostream& bold(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[1m";
        }
        return stream;
    }


    inline
    std::ostream& dark(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[2m";
        }
        return stream;
    }


    inline
    std::ostream& underline(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[4m";
        }
        return stream;
    }


    inline
    std::ostream& blink(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[5m";
        }
        return stream;
    }


    inline
    std::ostream& reverse(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[7m";
        }
        return stream;
    }


    inline
    std::ostream& concealed(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[8m";
        }
        return stream;
    }


    inline
    std::ostream& grey(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[30m";
        }
        return stream;
    }

    inline
    std::ostream& red(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[31m";
        }
        return stream;
    }

    inline
    std::ostream& green(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[32m";
        }
        return stream;
    }

    inline
    std::ostream& yellow(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[33m";
        }
        return stream;
    }

    inline
    std::ostream& blue(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[34m";
        }
        return stream;
    }

    inline
    std::ostream& magenta(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[35m";
        }
        return stream;
    }

    inline
    std::ostream& cyan(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[36m";
        }
        return stream;
    }

    inline
    std::ostream& white(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[37m";
        }
        return stream;
    }

    inline
    std::ostream& on_grey(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[40m";
        }
        return stream;
    }

    inline
    std::ostream& on_red(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[41m";
        }
        return stream;
    }

    inline
    std::ostream& on_green(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[42m";
        }
        return stream;
    }

    inline
    std::ostream& on_yellow(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[43m";
        }
        return stream;
    }

    inline
    std::ostream& on_blue(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[44m";
        }
        return stream;
    }

    inline
    std::ostream& on_magenta(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[45m";
        }
        return stream;
    }

    inline
    std::ostream& on_cyan(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[46m";
        }
        return stream;
    }

    inline
    std::ostream& on_white(std::ostream& stream)
    {
        if (_internal::is_colorized(stream))
        {
            stream << "\033[47m";
        }

        return stream;
    }

    inline
    __color_index_8bit color(uint8_t index)
    {
        return { index, /* .foreground = */ true };
    }

    inline
    __color_rgb_24bit color(uint8_t red, uint8_t green, uint8_t blue)
    {
        return { red, green, blue, /* .foreground = */ true };
    }

    inline
    __color_index_8bit on_color(uint8_t index)
    {
        return { index, /* .foregound = */ false };
    }

    inline
    __color_rgb_24bit on_color(uint8_t red, uint8_t green, uint8_t blue)
    {
        return { red, green, blue, /* .foreground = */ false };
    }

    inline
    std::ostream& operator<< (std::ostream& stream, const __color_index_8bit& color)
    {
        if (_internal::is_colorized(stream))
        {
            stream << _internal::ansi_color(color);
        }
        return stream;
    }

    inline
    std::ostream& operator<< (std::ostream& stream, const __color_rgb_24bit& color)
    {
        if (_internal::is_colorized(stream))
        {
            stream << _internal::ansi_color(color);
        }
        return stream;
    }

    //! Since C++ hasn't a way to hide something in the header from
    //! the outer access, I have to introduce this namespace which
    //! is used for internal purpose and should't be access from
    //! the user code.
    namespace _internal
    {
        //! Since C++ hasn't a true way to extract stream handler
        //! from the a given `std::ostream` object, I have to write
        //! this kind of hack.
        inline
        FILE* get_standard_stream(const std::ostream& stream)
        {
            if (&stream == &std::cout)
                return stdout;
            else if ((&stream == &std::cerr) || (&stream == &std::clog))
                return stderr;

            return 0;
        }

        // Say whether a given stream should be colorized or not. It's always
        // true for ATTY streams and may be true for streams marked with
        // colorize flag.
        inline
        bool is_colorized(std::ostream& stream)
        {
            return is_atty(stream) || static_cast<bool>(stream.iword(colorize_index));
        }

        //! Test whether a given `std::ostream` object refers to
        //! a terminal.
        inline
        bool is_atty(const std::ostream& stream)
        {
            FILE* std_stream = get_standard_stream(stream);

            // Unfortunately, fileno() ends with segmentation fault
            // if invalid file descriptor is passed. So we need to
            // handle this case gracefully and assume it's not a tty
            // if standard stream is not detected, and 0 is returned.
            if (!std_stream)
                return false;

        #if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
            return ::isatty(fileno(std_stream));
        #elif defined(TERMCOLOR_OS_WINDOWS)
            return ::_isatty(_fileno(std_stream));
        #endif
        }
    } // namespace _internal

} // namespace termcolor


#undef TERMCOLOR_OS_WINDOWS
#undef TERMCOLOR_OS_MACOS
#undef TERMCOLOR_OS_LINUX

#endif // TERMCOLOR_HPP_
