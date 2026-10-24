import os
import subprocess
import tempfile
import unittest

from officebuild import patches, util

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


class Additions(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.source = os.path.join(self.tmp.name, "source")
        self.work = os.path.join(self.tmp.name, "work")
        add = os.path.join(self.source, "branding", "marketcanopy", "add", "desktop-sdk", "lib", "src", "new")
        os.makedirs(add)
        with open(os.path.join(add, "module.cpp"), "w", encoding="utf-8") as handle:
            handle.write("// new\n")
        component = os.path.join(self.work, "desktop-sdk")
        os.makedirs(os.path.join(component, "lib", "src"))
        with open(os.path.join(component, "lib", "src", "upstream.cpp"), "w", encoding="utf-8") as handle:
            handle.write("// upstream\n")
        subprocess.run(["git", "init", "-q"], cwd=component, check=True)
        subprocess.run(["git", "add", "."], cwd=component, check=True)

    def tearDown(self):
        self.tmp.cleanup()

    def test_new_files_are_copied_into_the_component(self):
        self.assertEqual(patches.additions(self.source), ["desktop-sdk/lib/src/new/module.cpp"])
        self.assertEqual(patches.apply_additions(self.source, self.work), 1)
        with open(
            os.path.join(self.work, "desktop-sdk", "lib", "src", "new", "module.cpp"), encoding="utf-8"
        ) as handle:
            self.assertEqual(handle.read(), "// new\n")

    def test_copying_again_is_allowed(self):
        patches.apply_additions(self.source, self.work)
        self.assertEqual(patches.apply_additions(self.source, self.work), 1)

    def test_an_upstream_file_is_refused(self):
        clash = os.path.join(
            self.source, "branding", "marketcanopy", "add", "desktop-sdk", "lib", "src", "upstream.cpp"
        )
        with open(clash, "w", encoding="utf-8") as handle:
            handle.write("// not ours\n")
        with self.assertRaises(util.BuildError):
            patches.apply_additions(self.source, self.work)

    def test_dry_run_copies_nothing(self):
        patches.apply_additions(self.source, self.work, dry_run=True)
        self.assertFalse(os.path.exists(os.path.join(self.work, "desktop-sdk", "lib", "src", "new")))


if __name__ == "__main__":
    unittest.main()
