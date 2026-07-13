/* ─── PostMan self-test: guards against the T7 row-truncation regression ─── */

#include <gtest/gtest.h>
#include <sstream>
#include "PostMan.hpp"

/*
 * TestReport is a process-wide singleton (see PostMan.hpp), so this test
 * records synthetic rows into the same instance the rest of the suite
 * feeds via OnTestEnd. It only ever records passing rows and never touches
 * _currentSuite, so it cannot affect any other test's pass/fail outcome —
 * it can only add an extra suite section to the cosmetic PostMan table,
 * which is the point: the final table must account for every row.
 */
TEST(PostManTruncationRegression, RecordsAllRowsAboveLegacyCap)
{
	const int kSynthetic = 300; /* comfortably above the removed 256-row cap */
	const int before = TestReport::instance().rowCount();

	for (int i = 0; i < kSynthetic; ++i)
	{
		std::ostringstream label;
		label << "T7_synthetic_" << i;
		TestReport::instance().record(label.str(), true);
	}

	EXPECT_EQ(TestReport::instance().rowCount() - before, kSynthetic);
}
