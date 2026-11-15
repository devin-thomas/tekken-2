"""Patch 0010's audio bridge: the slow-host profile and the unchanged defaults.

Builds the bridge header and its C tests from the pinned framework with patch
0010 applied in a temporary copy (the submodule's own tree is not touched),
then runs them:

- test_audio_slow_host.c simulates the guest pushing a frame of SPU output per
  guest frame against 1024-frame device callbacks. The historical defaults
  must stay gap-free at 59.94 Hz (desktop) and must gap at 56 Hz and on the
  Galaxy S21 Ultra's logged 53-58 Hz pattern (the reported dropouts); the
  slow-host profile must play all of them without a gap or a dropped frame.
- test_audio_recovery.c (amended 2026-11-14 after the Galaxy's evening
  session) runs the host's pacer, frame skip and hitches against the bridge:
  no gap while frame skip keeps the guest clock running, and after a slow
  stretch no overflow and the fill held near its 180 ms ceiling, where the
  first profile reached ~300 ms and dropped frames one push at a time.
- test_audio_sink_resume.c is the framework's existing bridge test, run
  against the patched header to show its behaviour is unchanged.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FRAMEWORK = ROOT / "psxrecomp"
PATCH = ROOT / "patches/0010-slow-host-audio-and-frame-catchup.patch"
FILES = ["runtime/src/recomp_audio_drc.h",
         "runtime/tests/test_audio_slow_host.c",
         "runtime/tests/test_audio_recovery.c",
         "runtime/tests/test_audio_sink_resume.c"]


def pristine(rel):
    """The file at the submodule's pinned commit (None if it is new)."""
    out = subprocess.run(["git", "-C", str(FRAMEWORK), "show", f"HEAD:{rel}"],
                         capture_output=True)
    return out.stdout if out.returncode == 0 else None


@unittest.skipUnless(shutil.which("cc") and (FRAMEWORK / ".git").exists(),
                     "needs cc and the psxrecomp submodule")
class SlowHostAudioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="t2-audio-"))
        subprocess.run(["git", "init", "-q", str(cls.tmp)], check=True)
        for rel in FILES:
            data = pristine(rel)
            if data is not None:
                (cls.tmp / rel).parent.mkdir(parents=True, exist_ok=True)
                (cls.tmp / rel).write_bytes(data)
        subprocess.run(["git", "-C", str(cls.tmp), "apply",
                        *[f"--include={rel}" for rel in FILES], str(PATCH)],
                       check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_c(self, name):
        exe = self.tmp / name
        subprocess.run(["cc", "-O2", "-std=gnu99", "-I", str(self.tmp / "runtime/src"),
                        str(self.tmp / f"runtime/tests/{name}.c"), "-lm", "-o", str(exe)],
                       check=True, capture_output=True)
        out = subprocess.run([str(exe)], capture_output=True, text=True, timeout=300)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        return out.stdout

    def test_slow_host_profile(self):
        out = self.run_c("test_audio_slow_host")
        self.assertIn("PASS slow-host audio bridge", out)

    def test_recovery_after_slow_stretch(self):
        out = self.run_c("test_audio_recovery")
        self.assertIn("PASS audio recovery", out)

    def test_existing_bridge_test_unchanged(self):
        out = self.run_c("test_audio_sink_resume")
        self.assertIn("PASS resume preserves PCM", out)

    def test_defaults_unchanged(self):
        header = (self.tmp / "runtime/src/recomp_audio_drc.h").read_text()
        defaults = header[header.index("void rab_config_defaults(rab_config *c) {"):
                          header.index("void rab_config_slow_host(rab_config *c, int callback_frames) {")]
        for line in ("c->target_ms      = 180.0;", "c->max_correction = 0.005;",
                     "c->kp             = 0.02;", "c->slew_pp_per_s  = 0.75;",
                     "c->rate_track_s   = 0.0;", "c->reprime_ms     = 0.0;"):
            self.assertIn(line, defaults)


if __name__ == "__main__":
    unittest.main()
