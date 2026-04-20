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
 * Unit tests for grapheme-cluster handling in Terminal::Emulator::print().
 *
 * These exercise the cluster paths (combining marks, regional indicator
 * pairs, ZWJ emoji, VS16 widening) by driving Print actions directly into
 * the emulator and inspecting the resulting framebuffer cells.
 */

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "src/terminal/parser.h"
#include "src/terminal/parseraction.h"
#include "src/terminal/terminal.h"
#include "src/terminal/terminalframebuffer.h"

namespace {

int g_fail_count = 0;

#define CHECK_EQ( actual, expected )                                                                                   \
  do {                                                                                                                 \
    const std::string a = ( actual );                                                                                  \
    const std::string e = ( expected );                                                                                \
    if ( a != e ) {                                                                                                    \
      g_fail_count++;                                                                                                  \
      std::fprintf( stderr, "FAIL %s:%d: %s\n  expected: \"%s\"\n  actual:   \"%s\"\n", __FILE__, __LINE__, #actual,   \
                    e.c_str(), a.c_str() );                                                                            \
    }                                                                                                                  \
  } while ( 0 )

#define CHECK( cond )                                                                                                  \
  do {                                                                                                                 \
    if ( !( cond ) ) {                                                                                                 \
      g_fail_count++;                                                                                                  \
      std::fprintf( stderr, "FAIL %s:%d: !(%s)\n", __FILE__, __LINE__, #cond );                                        \
    }                                                                                                                  \
  } while ( 0 )

void emit_cp( Terminal::Emulator& e, wchar_t cp )
{
  Parser::Print p;
  p.ch = cp;
  p.char_present = true;
  /* Print::act_on_terminal is friended into Emulator; calling it
     directly avoids needing to expose Emulator::print(). */
  p.act_on_terminal( &e );
}

/* Drive a UTF-8 byte stream through the full parser, dispatching each
   resulting action to the emulator. This is the realistic path and the
   only way to test escape sequences (e.g. cursor positioning) without
   reaching into private state. */
void feed_bytes( Terminal::Emulator& e, Parser::UTF8Parser& parser, const char* bytes )
{
  Parser::Actions actions;
  for ( const char* p = bytes; *p; p++ ) {
    parser.input( *p, actions );
    for ( const auto& a : actions ) {
      a->act_on_terminal( &e );
    }
    actions.clear();
  }
}

std::string cell_contents( const Terminal::Framebuffer& fb, int row, int col )
{
  std::string out;
  fb.get_cell( row, col )->print_grapheme( out );
  return out;
}

bool cell_is_wide( const Terminal::Framebuffer& fb, int row, int col )
{
  return fb.get_cell( row, col )->get_wide();
}

/* Build the expected UTF-8 string for a cell containing one or more
   codepoints, using the same conversion the emulator uses internally
   so the comparison is locale-correct. */
std::string utf8_of( const wchar_t* codepoints, size_t n )
{
  std::string s;
  for ( size_t i = 0; i < n; i++ ) {
    Terminal::Cell::append_to_str( s, codepoints[i] );
  }
  return s;
}

/* --- tests --- */

void test_plain_ascii()
{
  Terminal::Emulator e( 80, 24 );
  emit_cp( e, L'h' );
  emit_cp( e, L'i' );
  CHECK_EQ( cell_contents( e.get_fb(), 0, 0 ), "h" );
  CHECK_EQ( cell_contents( e.get_fb(), 0, 1 ), "i" );
  CHECK( !cell_is_wide( e.get_fb(), 0, 0 ) );
}

void test_combining_mark()
{
  /* 'a' + COMBINING ACUTE ACCENT (U+0301) → single cell containing both. */
  Terminal::Emulator e( 80, 24 );
  emit_cp( e, L'a' );
  emit_cp( e, 0x0301 );
  const wchar_t expected[] = { L'a', 0x0301 };
  CHECK_EQ( cell_contents( e.get_fb(), 0, 0 ), utf8_of( expected, 2 ) );
}

void test_two_combining_marks()
{
  Terminal::Emulator e( 80, 24 );
  emit_cp( e, L'a' );
  emit_cp( e, 0x0301 );
  emit_cp( e, 0x0308 );
  const wchar_t expected[] = { L'a', 0x0301, 0x0308 };
  CHECK_EQ( cell_contents( e.get_fb(), 0, 0 ), utf8_of( expected, 3 ) );
}

void test_combining_at_start_of_line_uses_fallback()
{
  /* A combining mark at the start of an empty cell should attach to a
     no-break space "fallback". This exercises the case-0 path that
     opens a fresh cluster when no base char has been printed. */
  Terminal::Emulator e( 80, 24 );
  emit_cp( e, 0x0334 ); /* COMBINING TILDE OVERLAY */
  CHECK( e.get_fb().get_cell( 0, 0 )->get_fallback() );
  /* The grapheme should render as NBSP + combining mark. */
  std::string out = cell_contents( e.get_fb(), 0, 0 );
  CHECK( out.find( "\xC2\xA0" ) != std::string::npos );
}

void test_regional_indicator_pair_widens()
{
  /* 🇺 (U+1F1FA) + 🇸 (U+1F1F8) → one wide cell containing both RIs. */
  Terminal::Emulator e( 80, 24 );
  emit_cp( e, 0x1F1FA );
  emit_cp( e, 0x1F1F8 );
  const wchar_t expected[] = { 0x1F1FA, 0x1F1F8 };
  CHECK_EQ( cell_contents( e.get_fb(), 0, 0 ), utf8_of( expected, 2 ) );
  CHECK( cell_is_wide( e.get_fb(), 0, 0 ) );
  /* Next print must land at column 2, not the placeholder right half. */
  emit_cp( e, L'X' );
  CHECK_EQ( cell_contents( e.get_fb(), 0, 2 ), "X" );
}

void test_two_flag_emoji_in_a_row()
{
  /* 🇺🇸 followed by 🇨🇳 — two separate clusters, each widening. */
  Terminal::Emulator e( 80, 24 );
  emit_cp( e, 0x1F1FA );
  emit_cp( e, 0x1F1F8 );
  emit_cp( e, 0x1F1E8 );
  emit_cp( e, 0x1F1F3 );
  const wchar_t us[] = { 0x1F1FA, 0x1F1F8 };
  const wchar_t cn[] = { 0x1F1E8, 0x1F1F3 };
  CHECK_EQ( cell_contents( e.get_fb(), 0, 0 ), utf8_of( us, 2 ) );
  CHECK_EQ( cell_contents( e.get_fb(), 0, 2 ), utf8_of( cn, 2 ) );
  CHECK( cell_is_wide( e.get_fb(), 0, 0 ) );
  CHECK( cell_is_wide( e.get_fb(), 0, 2 ) );
}

void test_zwj_emoji_sequence()
{
  /* 👨 + ZWJ + 👩 → single cluster. */
  Terminal::Emulator e( 80, 24 );
  emit_cp( e, 0x1F468 );
  emit_cp( e, 0x200D );
  emit_cp( e, 0x1F469 );
  const wchar_t expected[] = { 0x1F468, 0x200D, 0x1F469 };
  CHECK_EQ( cell_contents( e.get_fb(), 0, 0 ), utf8_of( expected, 3 ) );
  CHECK( cell_is_wide( e.get_fb(), 0, 0 ) );
}

void test_family_of_four_zwj_sequence()
{
  Terminal::Emulator e( 80, 24 );
  const wchar_t cps[] = {
    0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467, 0x200D, 0x1F466,
  };
  for ( size_t i = 0; i < sizeof( cps ) / sizeof( cps[0] ); i++ ) {
    emit_cp( e, cps[i] );
  }
  CHECK_EQ( cell_contents( e.get_fb(), 0, 0 ), utf8_of( cps, sizeof( cps ) / sizeof( cps[0] ) ) );
  CHECK( cell_is_wide( e.get_fb(), 0, 0 ) );
}

void test_vs16_widens_text_emoji()
{
  /* HEAVY BLACK HEART (U+2764, narrow text default) + VS16 (U+FE0F)
     → emoji presentation, widened to two columns. */
  Terminal::Emulator e( 80, 24 );
  emit_cp( e, 0x2764 );
  emit_cp( e, 0xFE0F );
  const wchar_t expected[] = { 0x2764, 0xFE0F };
  CHECK_EQ( cell_contents( e.get_fb(), 0, 0 ), utf8_of( expected, 2 ) );
  CHECK( cell_is_wide( e.get_fb(), 0, 0 ) );
  emit_cp( e, L'X' );
  CHECK_EQ( cell_contents( e.get_fb(), 0, 2 ), "X" );
}

void test_macos_unprintable_no_longer_dropped()
{
  /* U+23F5 BLACK MEDIUM RIGHT-POINTING TRIANGLE — system wcwidth() on
     macOS returns -1, dropping it. utf8proc gives it width 1. */
  Terminal::Emulator e( 80, 24 );
  emit_cp( e, 0x23F5 );
  const wchar_t expected[] = { 0x23F5 };
  CHECK_EQ( cell_contents( e.get_fb(), 0, 0 ), utf8_of( expected, 1 ) );
}

void test_explicit_cursor_motion_breaks_cluster()
{
  /* After CUP, a fresh cluster must start. Drive the move via the
     parser so we don't depend on private framebuffer accessors. */
  Terminal::Emulator e( 80, 24 );
  Parser::UTF8Parser parser;
  emit_cp( e, 0x1F1FA );                 /* RI U */
  feed_bytes( e, parser, "\033[1;11H" ); /* CUP row 1, col 11 → row 0, col 10 */
  emit_cp( e, 0x1F1F8 );                 /* RI S — must NOT continue the prior cluster */
  const wchar_t s[] = { 0x1F1F8 };
  CHECK_EQ( cell_contents( e.get_fb(), 0, 10 ), utf8_of( s, 1 ) );
  CHECK( !cell_is_wide( e.get_fb(), 0, 10 ) );
}

void test_third_ri_after_pair_starts_new_cluster()
{
  /* 🇺🇸 then 🇨 alone — the third RI must NOT continue the first pair. */
  Terminal::Emulator e( 80, 24 );
  emit_cp( e, 0x1F1FA );
  emit_cp( e, 0x1F1F8 );
  emit_cp( e, 0x1F1E8 );
  const wchar_t us[] = { 0x1F1FA, 0x1F1F8 };
  const wchar_t c[] = { 0x1F1E8 };
  CHECK_EQ( cell_contents( e.get_fb(), 0, 0 ), utf8_of( us, 2 ) );
  CHECK_EQ( cell_contents( e.get_fb(), 0, 2 ), utf8_of( c, 1 ) );
  CHECK( cell_is_wide( e.get_fb(), 0, 0 ) );
  CHECK( !cell_is_wide( e.get_fb(), 0, 2 ) );
}

void test_line_feed_breaks_cluster()
{
  /* RI on row 0, then LF, then RI on row 1: must not widen. */
  Terminal::Emulator e( 80, 24 );
  Parser::UTF8Parser parser;
  emit_cp( e, 0x1F1FA );
  feed_bytes( e, parser, "\r\n" );
  emit_cp( e, 0x1F1F8 );
  const wchar_t u[] = { 0x1F1FA };
  const wchar_t s[] = { 0x1F1F8 };
  CHECK_EQ( cell_contents( e.get_fb(), 0, 0 ), utf8_of( u, 1 ) );
  CHECK_EQ( cell_contents( e.get_fb(), 1, 0 ), utf8_of( s, 1 ) );
  CHECK( !cell_is_wide( e.get_fb(), 0, 0 ) );
  CHECK( !cell_is_wide( e.get_fb(), 1, 0 ) );
}

} /* namespace */

int main( void )
{
  /* Cell::append uses wcrtomb, so we need a UTF-8 locale. */
  if ( !std::setlocale( LC_CTYPE, "C.UTF-8" ) && !std::setlocale( LC_CTYPE, "en_US.UTF-8" )
       && !std::setlocale( LC_CTYPE, "" ) ) {
    std::fprintf( stderr, "could not set a UTF-8 locale; skipping\n" );
    return 77;
  }

  test_plain_ascii();
  test_combining_mark();
  test_two_combining_marks();
  test_combining_at_start_of_line_uses_fallback();
  test_regional_indicator_pair_widens();
  test_two_flag_emoji_in_a_row();
  test_zwj_emoji_sequence();
  test_family_of_four_zwj_sequence();
  test_vs16_widens_text_emoji();
  test_macos_unprintable_no_longer_dropped();
  test_explicit_cursor_motion_breaks_cluster();
  test_third_ri_after_pair_starts_new_cluster();
  test_line_feed_breaks_cluster();

  if ( g_fail_count != 0 ) {
    std::fprintf( stderr, "%d test(s) failed\n", g_fail_count );
    return 1;
  }
  std::fprintf( stdout, "all grapheme tests passed\n" );
  return 0;
}
