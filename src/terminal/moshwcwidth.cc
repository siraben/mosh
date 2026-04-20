/*
    Mosh: the mobile shell
    Copyright 2012 Keith Winstein

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    In addition, as a special exception, the copyright holders give
    permission to link the code of portions of this program with the
    OpenSSL library under certain conditions as described in each
    individual source file, and distribute linked combinations including
    the two.

    You must obey the GNU General Public License in all respects for all
    of the code used other than OpenSSL. If you modify file(s) with this
    exception, you may extend this exception to your version of the
    file(s), but you are not obligated to do so. If you do not wish to do
    so, delete this exception statement from your version. If you delete
    this exception statement from all source files in the program, then
    also delete it here.
*/

/*
 * Wrapper around utf8proc_charwidth() for mosh.
 *
 * This replaces the system wcwidth() which is often outdated and returns
 * -1 for valid printable Unicode characters. For example, macOS wcwidth()
 * returns -1 for U+23F5 (BLACK MEDIUM RIGHT-POINTING TRIANGLE), causing
 * mosh to silently drop the character.
 */

#include <utf8proc.h>

#include "src/terminal/moshwcwidth.h"

/* mosh assumes 32-bit wchar_t throughout the terminal layer; codepoints
   above the BMP must round-trip without truncation. Catch any port to
   a platform with 16-bit wchar_t (e.g. native Windows) at compile time. */
static_assert( sizeof( wchar_t ) >= sizeof( utf8proc_int32_t ),
               "mosh requires 32-bit wchar_t for full Unicode support" );

int mosh_wcwidth( wchar_t ucs )
{
  return utf8proc_charwidth( static_cast<utf8proc_int32_t>( ucs ) );
}
