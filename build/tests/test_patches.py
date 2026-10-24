import unittest

from officebuild import patches

PATCH = """From 1 Mon Sep 17 00:00:00 2001
Subject: [PATCH] example

---
diff --git a/a.txt b/a.txt
--- a/a.txt
+++ b/a.txt
@@ -1,2 +1,2 @@
-old
+new
 same
diff --git a/b.txt b/b.txt
new file mode 100644
--- /dev/null
+++ b/b.txt
@@ -0,0 +1,3 @@
+one
+two
+three
"""


class Budget(unittest.TestCase):
    def test_new_files_do_not_count_against_the_budget(self):
        self.assertEqual(patches.changed_upstream_lines(PATCH), 2)

    def test_budget_line_reports_over_budget(self):
        many = [patches.Patch("core", "p", 100) for _ in range(16)]
        self.assertIn("OVER", patches.budget_line(many))
        self.assertIn("within", patches.budget_line(many[:3]))


if __name__ == "__main__":
    unittest.main()
