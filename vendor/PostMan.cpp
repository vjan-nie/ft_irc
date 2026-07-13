/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   PostMan.cpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: dlesieur <dlesieur@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/03/06 00:00:00 by dlesieur          #+#    #+#             */
/*   Updated: 2026/03/09 03:16:51 by dlesieur         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "PostMan.hpp"
#include <atomic>
#include <iostream>
#include <sstream>
#include <string>

/* Box-drawing: round corners + heavy single lines.
 * Every glyph below is exactly 1 display column wide. */
#define TL  "\xe2\x94\x8f" // ╭
#define TR  "\xe2\x94\x93" 
#define BL  "\xe2\x94\x97"
#define BR  "\xe2\x94\x9b"
#define HH  "\xe2\x94\x81"
#define HV  "\xe2\x94\x83"
#define LH  "\xe2\x94\x80"
#define LV  "\xe2\x94\x82"
#define JLH "\xe2\x94\xa3"
#define JRH "\xe2\x94\xab"
#define JTH "\xe2\x94\xb3"
#define JBH "\xe2\x94\xbb"
#define JXH "\xe2\x95\x8b"
#define JLl "\xe2\x94\x9c"
#define JRl "\xe2\x94\xa4"

/* Atomic: the tests run a live server in a background thread, so new/delete
 * fire concurrently — a plain int loses increments and reports phantom
 * "leaks" of 2-3 allocations. */
std::atomic<int> g_allocations(0);

void* operator new(std::size_t size) {
  void* ptr = std::malloc(size);

  if (!ptr) throw std::bad_alloc();
  g_allocations++;
  return ptr;
}


void operator delete(void* ptr) noexcept
{
  if (ptr) {
    g_allocations--;
    std::free(ptr);
  }
}

void operator delete(void* ptr, std::size_t) noexcept
{
  if (ptr) {
    g_allocations--;
    std::free(ptr);
  }
}

void* operator new[](std::size_t size) {
  void* ptr = std::malloc(size);
  if (!ptr) throw std::bad_alloc();
  g_allocations++;
  return (ptr);
}

void operator delete[](void* ptr) noexcept {
  if (ptr) {
    g_allocations--;
    std::free(ptr);
  }
}

void operator delete[](void* ptr, std::size_t) noexcept {
  if (ptr) {
    g_allocations--;
    std::free(ptr);
  }
}

void  TestReport::snapshotMemory() {
  _allocSnapshot = g_allocations;
}

int   TestReport::leakDelta() const {
  return g_allocations - _allocSnapshot;
}

void  TestReport::assertNoLeaks(const char* label) {
  /* Capture the delta BEFORE anything in this call can allocate (record()
   * and the stream output below both do). */
  const int delta = g_allocations - _allocSnapshot;
  const bool ok = (delta == 0);
  record(label, ok);
  if (!ok) {
    std::cerr << PM_FAIL << PM_BOLD << "  [FAIL] " << PM_RST
              << label << " (leaked " << delta << " allocations)\n";
  } else {
    std::cout << PM_PASS << PM_BOLD << "  [PASS] " << PM_RST << label << "\n";
  }
}

/* === PmCols ============================================================= */

int PmCols::span() const {
  return id + suite + label + stat + 2 * 4 + 3;
}

/* ========================================================================
 * Singleton Instance Management
 * ======================================================================== */

/**
 * @brief Retrieve the global TestReport Singleton instance.
 *
 * Thread-safe singleton pattern using static local variable initialization.
 *
 * @return Reference to the global TestReport instance.
 */
TestReport& TestReport::instance() {
  static TestReport inst;
  return inst;
}

TestReport::TestReport() : _currentSuite("(none)") {}

/**
 * @brief Begin a new test suite section.
 *
 * Subsequent assertions will be grouped under this suite name until
 * beginSuite() is called again.
 *
 * @param name The name of the test suite.
 */
void TestReport::beginSuite(const std::string& name) { _currentSuite = name; }

/**
 * @brief Record a test assertion result.
 *
 * Appends the assertion to the internal buffer, which grows as needed.
 *
 * @param label Description of what the assertion tests.
 * @param passed True if assertion passed, false if failed.
 */
void TestReport::record(const std::string& label, bool passed) {
  PmRow row;
  row.id     = static_cast<int>(_rows.size()) + 1;
  row.suite  = _currentSuite;
  row.label  = label;
  row.passed = passed;
  _rows.push_back(row);
}

int TestReport::rowCount() const { return static_cast<int>(_rows.size()); }

/* ========================================================================
 * UTF-8 String Measurement & Manipulation
 * ======================================================================== */

/**
 * @brief Calculate the visible width (terminal columns) of a UTF-8 string.
  std::string out;
 *
 * Correctly handles multi-byte UTF-8 sequences:
 * - ASCII (0x00-0x7F): 1 byte, 1 column
 * - 2-byte (0xC0-0xDF): 2 bytes, 1 column
 * - 3-byte (0xE0-0xEF): 3 bytes, 1 column (e.g., box-drawing)
 * - 4-byte (0xF0-0xF7): 4 bytes, 1 column
 * - Continuation (0x80-0xBF): 1 byte, 0 columns (part of multibyte)
 *
 * @param s The UTF-8 string to measure.
 * @return Visual width in terminal columns.
 */
int TestReport::visWidth(const std::string& s) {
  int w = 0;
  for (std::string::size_type i = 0; i < s.size(); ) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if      (c < 0x80) { ++w; i += 1; }
    else if (c < 0xC0) { i += 1; }
    else if (c < 0xE0) { ++w; i += 2; }
    else if (c < 0xF0) { ++w; i += 3; }
    else               { ++w; i += 4; }
  }
  return w;
}

/**
 * @brief Fit a string to a specific visual width with truncation or padding.
 *
 * - If s is longer than w: truncate to w-1 chars and append '~'
 * - If s is shorter than w: right-pad with spaces
 * - If s fits exactly: return as-is
 *
 * @param s The string to fit.
 * @param w The desired visual width.
 * @return The fitted string (exactly w columns wide).
 */
std::string TestReport::fitTo(const std::string& s, int w) {
  if (w <= 0) return "";
  std::string out;
  int vis = 0;
  for (std::string::size_type i = 0; i < s.size(); ) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    int len = 1;
    if      (c >= 0xF0) len = 4;
    else if (c >= 0xE0) len = 3;
    else if (c >= 0xC0) len = 2;
    if (c >= 0x80 && c < 0xC0) { ++i; continue; }
    if (vis + 1 > w) break;
    bool more = (i + static_cast<std::string::size_type>(len)) < s.size();
    if (vis + 1 == w && more) {
      bool onlySpaces = true;
      for (std::string::size_type j = i + len; j < s.size(); ++j)
        if (s[j] != ' ') { onlySpaces = false; break; }
      if (!onlySpaces) { out += '~'; ++vis; break; }
    }
    out.append(s, i, static_cast<std::string::size_type>(len));
    ++vis;
    i += static_cast<std::string::size_type>(len);
  }
  while (vis < w) { out += ' '; ++vis; }
  return out;
}

/**
 * @brief Repeat a UTF-8 character n times and concatenate into a string.
 *
 * Useful for drawing horizontal lines with box-drawing glyphs.
 *
 * @param u The UTF-8 character to repeat (may be multi-byte).
 * @param n Number of repetitions.
 * @return The concatenated string.
 */
std::string TestReport::repCh(const char* u, int n) {
  std::string o;
  for (int i = 0; i < n; ++i) o += u;
  return o;
}

/* ========================================================================
 * Table Rendering Functions
 * ======================================================================== */

/**
 * @brief Calculate optimal column widths based on data content.
 *
 * Analyzes all rows to determine the minimum width for each column,
 * then applies configurable maximum limits to prevent overly wide columns.
 *
 * Default minimums: id=4, suite=50, label=50, stat=8
 * Maximum limits: suite<=40, label<=44
 *
 * @param rows Array of test records.
 * @param n Number of records in the array.
 * @return PmCols struct with calculated widths.
 */
PmCols TestReport::calcCols(const PmRow* rows, int n) {
  PmCols c;
  c.id = 4; c.suite = 50; c.label = 50; c.stat = 8;
  for (int i = 0; i < n; ++i) {
    std::ostringstream oss; oss << rows[i].id;
    int wi = visWidth(oss.str());
    int ws = visWidth(rows[i].suite);
    int wl = visWidth(rows[i].label);
    if (wi > c.id)    c.id    = wi;
    if (ws > c.suite) c.suite = ws;
    if (wl > c.label) c.label = wl;
  }
  if (c.suite > 40) c.suite = 40;
  if (c.label > 44) c.label = 44;
  return c;
}

/**
 * @brief Draw a horizontal line (rule) in the table with appropriate junctions.
 *
 * Supports different junction styles for different table sections:
 * - **kind 0**: Top row (╭━━━╮) - round corners, full-span
 * - **kind 3**: Bottom row (╰━━━╯) - round corners, full-span
 * - **kind 4**: Merge row (┣━━━┫) - full-span, left/right T junctions
 * - **kind 5**: Merge-bottom (╰━━━╯) - alias for kind 3
 * - **kind 7**: Column cross (┣━╋━┫) - cross junctions (columns above/below)
 * - **kind 8**: Column open (┣━┳━┫) - T-down (columns start below)
 * - **kind 9**: Column close (┣━┻━┫) - T-up (columns end above)
 *
 * @param c Column width configuration.
 * @param kind Junction style code (0, 3, 4, 5, 7, 8, 9).
 */
void TestReport::drawHLine(const PmCols& c, int kind) {
  const int w[] = { c.id, c.suite, c.label, c.stat };
  std::cout << PM_FRAME;
  if (kind == 0) {
    std::cout << TL << repCh(HH, c.span()) << TR;
  } else if (kind == 3 || kind == 5) {
    std::cout << BL << repCh(HH, c.span()) << BR;
  } else if (kind == 7 || kind == 8 || kind == 9) {
    const char* jn = JXH;
    if (kind == 8) jn = JTH;
    if (kind == 9) jn = JBH;
    std::cout << JLH;
    for (int i = 0; i < 4; ++i) {
      std::cout << repCh(HH, w[i] + 2);
      if (i < 3) std::cout << jn;
    }
    std::cout << JRH;
  } else {
    /* kind 4 : full-span, no column junctions */
    std::cout << JLH << repCh(HH, c.span()) << JRH;
  }
  std::cout << PM_RST << "\n";
}

/**
 * @brief Draw the table header row with column titles.
 *
 * Renders four columns: # | Suite | Assertion | Result
 * Uses light blue color for headers.
 *
 * @param c Column width configuration.
 */
void TestReport::drawHeader(const PmCols& c) {
  std::cout
    << PM_FRAME << HV << PM_RST
    << " " << PM_HEAD << PM_BOLD << fitTo("#",         c.id)    << PM_RST << " "
    << PM_FRAME << LV << PM_RST
    << " " << PM_HEAD << PM_BOLD << fitTo("Suite",     c.suite) << PM_RST << " "
    << PM_FRAME << LV << PM_RST
    << " " << PM_HEAD << PM_BOLD << fitTo("Assertion", c.label) << PM_RST << " "
    << PM_FRAME << LV << PM_RST
    << " " << PM_HEAD << PM_BOLD << fitTo("Result",    c.stat)  << PM_RST << " "
    << PM_FRAME << HV << PM_RST << "\n";
}

/**
 * @brief Draw a single data row for an assertion result.
 *
 * Displays:
 * - Assertion number (dim gray)
 * - Suite name (muted purple)
 * - Assertion message (light gray, truncated with ~ if needed)
 * - Result status: "PASS" (green) or "FAIL" (red)
 *
 * @param c Column width configuration.
 * @param r The assertion record to display.
 */
void TestReport::drawRow(const PmCols& c, const PmRow& r) {
  const char* clr = r.passed ? PM_PASS : PM_FAIL;
  std::ostringstream oss; oss << r.id;
  std::string st = r.passed ? "PASS" : "FAIL";
  std::cout
    << PM_FRAME << HV << PM_RST
    << " " << PM_ID    << fitTo(oss.str(), c.id)    << PM_RST << " "
    << PM_FRAME << LV << PM_RST
    << " " << PM_SUITE << fitTo(r.suite,   c.suite) << PM_RST << " "
    << PM_FRAME << LV << PM_RST
    << " " << PM_FG    << fitTo(r.label,   c.label) << PM_RST << " "
    << PM_FRAME << LV << PM_RST
    << " " << clr << PM_BOLD << fitTo(st, c.stat) << PM_RST << " "
    << PM_FRAME << HV << PM_RST << "\n";
}

/**
 * @brief Draw a suite summary line showing pass/fail counts.
 *
 * Format: "Suite Name  X/Y passed  Z FAILED"
 * Shows pass count in green and fail count (if any) in red.
 *
 * @param c Column width configuration.
 * @param s Suite name.
 * @param p Number of passed assertions in this suite.
 * @param f Number of failed assertions in this suite.
 */
void TestReport::drawSuiteSum(const PmCols& c,
                              const std::string& s, int p, int f) {
  int ts = p + f;
  std::ostringstream plain;
  plain << s << "  " << p << "/" << ts << " passed";
  if (f > 0) plain << "  " << f << " FAILED";
  int used = visWidth(plain.str());
  int pad  = c.span() - used - 1;
  if (pad < 0) pad = 0;
  std::ostringstream col;
  col << PM_ITAL << PM_DIM << s << PM_RST
      << "  " << PM_PASS << PM_BOLD << p << "/" << ts
      << " passed" << PM_RST;
  if (f > 0)
    col << "  " << PM_FAIL << PM_BOLD << f << " FAILED" << PM_RST;
  std::cout
    << PM_FRAME << HV << PM_RST
    << " " << col.str() << std::string(pad, ' ')
    << PM_FRAME << HV << PM_RST << "\n";
}

/**
 * @brief Draw the title section of the report.
 *
 * Renders the top banner with summary information:
 * - "TEST REPORT" title
 * - "X assertions  Y passed  Z failed" summary line
 * - Column junction line (col-open style)
 *
 * @param c Column width configuration.
 * @param p Total passed assertions.
 * @param f Total failed assertions.
 * @param t Total number of assertions.
 */
void TestReport::drawTitle(const PmCols& c, int p, int f, int t) {
  int sp = c.span();
  std::cout << PM_FRAME << TL << repCh(HH, sp) << TR << PM_RST << "\n";
  std::string tit = "TEST REPORT";
  int tp = sp - visWidth(tit) - 1;
  if (tp < 0) tp = 0;
  std::cout
    << PM_FRAME << HV << PM_RST
    << " " << PM_TITLE << PM_BOLD << tit << PM_RST
    << std::string(tp, ' ')
    << PM_FRAME << HV << PM_RST << "\n";
  std::ostringstream ss;
  ss << t << " assertions   " << p << " passed   " << f << " failed";
  int sw = visWidth(ss.str());
  int stp = sp - sw - 1;
  if (stp < 0) stp = 0;
  std::cout
    << PM_FRAME << HV << PM_RST
    << " " << PM_DIM << ss.str() << PM_RST
    << std::string(stp, ' ')
    << PM_FRAME << HV << PM_RST << "\n";
  drawHLine(c, 8);
}

/**
 * @brief Draw the final verdict section of the report.
 *
 * Renders the bottom banner with final verdict:
 * - Divider line (full-span)
 * - Verdict message ("All X assertions passed" or "Y assertion(s) FAILED")
 * - Bottom border (round corners)
 *
 * Green on success, red on failure.
 *
 * @param c Column width configuration.
 * @param p Total passed assertions.
 * @param f Total failed assertions.
 */
void TestReport::drawVerdict(const PmCols& c, int p, int f) {
  int sp = c.span();
  drawHLine(c, 4);
  std::string msg;
  const char* clr;
  if (f == 0) {
    std::ostringstream oss; oss << p;
    msg = "All " + oss.str() + " assertions passed.";
    clr = PM_PASS;
  } else {
    std::ostringstream oss; oss << f;
    msg = oss.str() + " assertion(s) FAILED.";
    clr = PM_FAIL;
  }
  int vp = sp - visWidth(msg) - 1;
  if (vp < 0) vp = 0;
  std::cout
    << PM_FRAME << HV << PM_RST
    << " " << clr << PM_BOLD << msg << PM_RST
    << std::string(vp, ' ')
    << PM_FRAME << HV << PM_RST << "\n";
  drawHLine(c, 5);
}

/* ========================================================================
 * Main Print Function
 * ======================================================================== */

/**
 * @brief Print the complete formatted test report to stdout.
 *
 * Generates the full test summary table with:
 * 1. Title section (assertion count summary)
 * 2. Column headers
 * 3. All recorded assertions grouped by suite
 * 4. Suite summaries (pass/fail counts per suite)
 * 5. Final verdict (all passed or X failed)
 *
 * The report uses UTF-8 box-drawing characters and ANSI 256-color codes.
 *
 * @note Call this after all test assertions have been recorded.
 */
void TestReport::print() const {
  const int n = static_cast<int>(_rows.size());
  int passed = 0, failed = 0;
  for (int i = 0; i < n; ++i)
    _rows[i].passed ? ++passed : ++failed;
  PmCols cols = calcCols(_rows.data(), n);
  std::cout << "\n\n";
  drawTitle(cols, passed, failed, n);
  drawHeader(cols);
  drawHLine(cols, 7);

  std::string lastSuite;
  int sp = 0, sf = 0;
  for (int i = 0; i < n; ++i) {
    if (_rows[i].suite != lastSuite) {
      if (i != 0) {
        drawHLine(cols, 9);
        drawSuiteSum(cols, lastSuite, sp, sf);
        drawHLine(cols, 8);
        sp = 0; sf = 0;
      }
      lastSuite = _rows[i].suite;
    }
    _rows[i].passed ? ++sp : ++sf;
    drawRow(cols, _rows[i]);
  }
  if (n > 0) {
    drawHLine(cols, 9);
    drawSuiteSum(cols, lastSuite, sp, sf);
  }
  drawVerdict(cols, passed, failed);
  std::cout << "\n";
}